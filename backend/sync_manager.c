#include "sync_manager.h"
#include <stdio.h>
#include <unistd.h>

pthread_mutex_t read_count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
int active_readers = 0;
int active_writers = 0;

int update_active_counts(int readers, int writers);

// ADD external declarations for new database functions
extern int log_operation_metrics(int reads, int writes, int active_readers, int active_writers);
extern int calculate_performance_metrics();


void update_active_counts_in_db() {
    // DEBUG: Write to file to confirm function is called
    FILE *f = fopen("/tmp/sync_debug.log", "a");
    if (f) {
        fprintf(f, "DEBUG: update_active_counts_in_db() called - readers=%d, writers=%d\n", 
                active_readers, active_writers);
        fclose(f);
    }
    
    // Call the database function
    update_active_counts(active_readers, active_writers);
}

int acquire_read_lock(const char *client_id) {
    pthread_mutex_lock(&read_count_mutex);
    
    active_readers++;
    if (active_readers == 1) {
        pthread_mutex_lock(&write_mutex);
    }
    
    update_active_counts_in_db();
    
    pthread_mutex_unlock(&read_count_mutex);
    return SUCCESS;
}

void release_read_lock() {
    pthread_mutex_lock(&read_count_mutex);
    
    active_readers--;
    if (active_readers == 0) {
        pthread_mutex_unlock(&write_mutex);
    }
    
    update_active_counts_in_db();
    
    pthread_mutex_unlock(&read_count_mutex);
}

int acquire_write_lock(const char *client_id) {
    pthread_mutex_lock(&write_mutex);
    
    active_writers = 1;
    update_active_counts_in_db();
    
    return SUCCESS;
}

void release_write_lock() {
    active_writers = 0;
    update_active_counts_in_db();
    
    pthread_mutex_unlock(&write_mutex);
}
