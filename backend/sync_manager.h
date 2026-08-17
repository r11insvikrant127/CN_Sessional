#ifndef SYNC_MANAGER_H
#define SYNC_MANAGER_H

#include "config.h"

/*
 * Cross-process Reader-Writer synchronization.
 *
 * The actual synchronization primitives are POSIX named semaphores.
 * Shared memory stores counters that must be visible to all CGI processes.
 */

typedef struct {
    int magic;
    int active_readers;
    int active_writers;
    int waiting_readers;
    int waiting_writers;
} rw_shared_state;

/*
 * Initialize/open the inter-process synchronization resources.
 */
int sync_manager_init(void);

/*
 * Reader lock.
 */
int acquire_read_lock(const char *client_id);
void release_read_lock(void);

/*
 * Writer lock.
 */
int acquire_write_lock(const char *client_id);
void release_write_lock(void);

/*
 * Update database with current shared counters.
 */
void update_active_counts_in_db(void);

/*
 * Obtain a consistent snapshot of synchronization state.
 */
int get_sync_state(
    int *active_readers,
    int *active_writers,
    int *waiting_readers,
    int *waiting_writers
);

#endif
