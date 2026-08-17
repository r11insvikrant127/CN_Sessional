#define _POSIX_C_SOURCE 200809L

#include "sync_manager.h"
#include "database.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>

/*
 * Named semaphores shared by all CGI processes.
 */
static sem_t *resource_sem = SEM_FAILED;
static sem_t *read_count_sem = SEM_FAILED;
static sem_t *queue_sem = SEM_FAILED;
static sem_t *init_sem = SEM_FAILED;

/*
 * Shared-memory state shared by all CGI processes.
 */
static rw_shared_state *shared_state = NULL;

static int shm_fd = -1;


/*
 * Open/create shared synchronization resources.
 */
int sync_manager_init(void)
{
    /*
     * Initialization semaphore ensures that two CGI processes
     * cannot initialize the shared-memory state simultaneously.
     */
    init_sem = sem_open(
        RW_INIT_SEM,
        O_CREAT,
        0660,
        1
    );

    if (init_sem == SEM_FAILED) {
        perror("sem_open init semaphore");
        return ERROR_LOCK;
    }

    if (sem_wait(init_sem) != 0) {
        perror("sem_wait init semaphore");
        return ERROR_LOCK;
    }

    /*
     * Resource semaphore:
     *
     * 1 = database resource available
     * 0 = resource currently held
     */
    resource_sem = sem_open(
        RW_RESOURCE_SEM,
        O_CREAT,
        0660,
        1
    );

    if (resource_sem == SEM_FAILED) {
        perror("sem_open resource semaphore");
        sem_post(init_sem);
        return ERROR_LOCK;
    }

    /*
     * Protects shared reader counter.
     */
    read_count_sem = sem_open(
        RW_COUNT_SEM,
        O_CREAT,
        0660,
        1
    );

    if (read_count_sem == SEM_FAILED) {
        perror("sem_open reader-count semaphore");
        sem_post(init_sem);
        return ERROR_LOCK;
    }

	//queue_sem
	queue_sem = sem_open(
	    RW_QUEUE_SEM,
	    O_CREAT,
	    0660,
	    1
	);

	if (queue_sem == SEM_FAILED) {
	    perror("sem_open queue semaphore");
	    sem_post(init_sem);
	    return ERROR_LOCK;
	}

    /*
     * Shared memory containing active reader/writer state.
     */
    shm_fd = shm_open(
        RW_SHM_NAME,
        O_CREAT | O_RDWR,
        0660
    );

    if (shm_fd == -1) {
        perror("shm_open");
        sem_post(init_sem);
        return ERROR_LOCK;
    }

    if (ftruncate(shm_fd, sizeof(rw_shared_state)) != 0) {
        perror("ftruncate shared memory");
        sem_post(init_sem);
        return ERROR_LOCK;
    }

    shared_state = mmap(
        NULL,
        sizeof(rw_shared_state),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        shm_fd,
        0
    );

    if (shared_state == MAP_FAILED) {
        perror("mmap shared memory");
        shared_state = NULL;
        sem_post(init_sem);
        return ERROR_LOCK;
    }

    /*
     * Initialize shared memory only once.
     */
    if (shared_state->magic != RW_SHM_MAGIC) {

	    shared_state->active_readers = 0;
	    shared_state->active_writers = 0;
	    shared_state->waiting_readers = 0;
	    shared_state->waiting_writers = 0;

	    shared_state->magic = RW_SHM_MAGIC;
	}

    sem_post(init_sem);

    return SUCCESS;
}


/*
 * Make sure synchronization resources are available.
 */
static int ensure_sync_initialized(void)
{
    if (resource_sem != SEM_FAILED &&
        read_count_sem != SEM_FAILED &&
        queue_sem != SEM_FAILED &&
        shared_state != NULL) {
        return SUCCESS;
    }

    return sync_manager_init();
}

/*
 * Update database using the counters stored in shared memory.
 */
void update_active_counts_in_db(void)
{
    int readers;
    int writers;

    if (shared_state == NULL) {
        return;
    }

    if (sem_wait(read_count_sem) != 0) {
        return;
    }

    readers = shared_state->active_readers;
    writers = shared_state->active_writers;

    sem_post(read_count_sem);

    update_active_counts(readers, writers);
}

int get_sync_state(
    int *active_readers,
    int *active_writers,
    int *waiting_readers,
    int *waiting_writers
)
{
    if (active_readers == NULL ||
        active_writers == NULL ||
        waiting_readers == NULL ||
        waiting_writers == NULL) {
        return ERROR_LOCK;
    }

    if (ensure_sync_initialized() != SUCCESS) {
        return ERROR_LOCK;
    }

    if (sem_wait(read_count_sem) != 0) {
        return ERROR_LOCK;
    }

    *active_readers = shared_state->active_readers;
    *active_writers = shared_state->active_writers;
    *waiting_readers = shared_state->waiting_readers;
    *waiting_writers = shared_state->waiting_writers;

    sem_post(read_count_sem);

    return SUCCESS;
}

/*
 * Acquire Reader lock.
 *
 * Multiple readers may enter simultaneously.
 *
 * The first reader acquires resource_sem.
 * The last reader releases resource_sem.
 */
int acquire_read_lock(const char *client_id)
{
    (void)client_id;

    if (ensure_sync_initialized() != SUCCESS) {
        return ERROR_LOCK;
    }

    /*
     * Record that this reader is waiting.
     */
    if (sem_wait(read_count_sem) != 0) {
        return ERROR_LOCK;
    }

    shared_state->waiting_readers++;

    sem_post(read_count_sem);

    /*
     * Fairness gate.
     */
    if (sem_wait(queue_sem) != 0) {
        if (sem_wait(read_count_sem) == 0) {
            if (shared_state->waiting_readers > 0) {
                shared_state->waiting_readers--;
            }
            sem_post(read_count_sem);
        }

        return ERROR_LOCK;
    }

    /*
     * Reader is no longer waiting at the fairness gate.
     */
    if (sem_wait(read_count_sem) != 0) {
        sem_post(queue_sem);
        return ERROR_LOCK;
    }

    if (shared_state->waiting_readers > 0) {
        shared_state->waiting_readers--;
    }

    /*
     * First reader acquires the shared resource.
     */
    if (shared_state->active_readers == 0) {
        if (sem_wait(resource_sem) != 0) {
            sem_post(read_count_sem);
            sem_post(queue_sem);
            return ERROR_LOCK;
        }
    }

    shared_state->active_readers++;

    /*
     * Take a snapshot while protected.
     */
    int readers = shared_state->active_readers;
    int writers = shared_state->active_writers;

    /*
     * Release synchronization protection BEFORE doing DB I/O.
     */
    sem_post(read_count_sem);

    /*
     * Update database outside the synchronization critical section.
     */
    update_active_counts(readers, writers);

    /*
     * Allow the next waiting client to proceed.
     */
    sem_post(queue_sem);

    return SUCCESS;
}


/*
 * Release Reader lock.
 */
void release_read_lock(void)
{
    int readers;
    int writers;
    int release_resource = 0;

    if (ensure_sync_initialized() != SUCCESS) {
        return;
    }

    if (sem_wait(read_count_sem) != 0) {
        return;
    }

    if (shared_state->active_readers > 0) {
        shared_state->active_readers--;
    }

    /*
     * Last reader releases the shared resource.
     */
    if (shared_state->active_readers == 0) {
        release_resource = 1;
    }

    /*
     * Snapshot state while protected.
     */
    readers = shared_state->active_readers;
    writers = shared_state->active_writers;

    sem_post(read_count_sem);

    /*
     * Release resource outside read_count_sem.
     */
    if (release_resource) {
        sem_post(resource_sem);
    }

    /*
     * Database I/O happens outside synchronization critical section.
     */
    update_active_counts(readers, writers);
}


/*
 * Acquire Writer lock.
 *
 * Only one writer can hold resource_sem.
 */
int acquire_write_lock(const char *client_id)
{
    (void)client_id;

    if (ensure_sync_initialized() != SUCCESS) {
        return ERROR_LOCK;
    }

    /*
     * Record waiting writer.
     */
    if (sem_wait(read_count_sem) != 0) {
        return ERROR_LOCK;
    }

    shared_state->waiting_writers++;

    sem_post(read_count_sem);

    /*
     * Enter fairness queue.
     */
    if (sem_wait(queue_sem) != 0) {
        if (sem_wait(read_count_sem) == 0) {
            if (shared_state->waiting_writers > 0) {
                shared_state->waiting_writers--;
            }
            sem_post(read_count_sem);
        }

        return ERROR_LOCK;
    }

    /*
     * Wait for the shared resource.
     */
    if (sem_wait(resource_sem) != 0) {
        sem_post(queue_sem);

        if (sem_wait(read_count_sem) == 0) {
            if (shared_state->waiting_writers > 0) {
                shared_state->waiting_writers--;
            }
            sem_post(read_count_sem);
        }

        return ERROR_LOCK;
    }

    /*
     * Writer is no longer waiting.
     */
    if (sem_wait(read_count_sem) != 0) {
        sem_post(resource_sem);
        sem_post(queue_sem);
        return ERROR_LOCK;
    }

    if (shared_state->waiting_writers > 0) {
        shared_state->waiting_writers--;
    }

    shared_state->active_writers = 1;

    /*
     * Snapshot state while protected.
     */
    int readers = shared_state->active_readers;
    int writers = shared_state->active_writers;

    sem_post(read_count_sem);

    /*
     * Database I/O outside synchronization critical section.
     */
    update_active_counts(readers, writers);

    /*
     * Writer keeps resource_sem, but allows the next client
     * to enter the fairness queue.
     */
    sem_post(queue_sem);

    return SUCCESS;
}


/*
 * Release Writer lock.
 */
void release_write_lock(void)
{
    int readers;
    int writers;

    if (ensure_sync_initialized() != SUCCESS) {
        return;
    }

    if (sem_wait(read_count_sem) != 0) {
        return;
    }

    shared_state->active_writers = 0;

    /*
     * Snapshot state while protected.
     */
    readers = shared_state->active_readers;
    writers = shared_state->active_writers;

    sem_post(read_count_sem);

    /*
     * Database I/O outside synchronization critical section.
     */
    update_active_counts(readers, writers);

    /*
     * Finally release the actual shared resource.
     */
    sem_post(resource_sem);
}
