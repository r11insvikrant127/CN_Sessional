#ifndef SYNC_MANAGER_H
#define SYNC_MANAGER_H

#include <pthread.h>
#include "config.h"

// Synchronization primitives
extern pthread_mutex_t read_count_mutex;
extern pthread_mutex_t write_mutex;
extern int active_readers;
extern int active_writers;

// Reader-writer lock functions
int acquire_read_lock(const char *client_id);
void release_read_lock();
int acquire_write_lock(const char *client_id);
void release_write_lock();

// Active counts management
void update_active_counts_in_db();

#endif
