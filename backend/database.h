#ifndef DATABASE_H
#define DATABASE_H

#include <stdio.h>
#include <sqlite3.h>
#include <pthread.h>  // ADD THIS LINE
#include "config.h"

// External database connection
extern sqlite3 *db;
extern pthread_mutex_t db_mutex;

// Database initialization and management
int initialize_database();
void close_database();
int check_database_health();
int ensure_database_connection();

// Core database operations  
int read_messages(FILE *output);
int write_message(const char *username, const char *message);
int log_access(const char *client_id, const char *client_type, const char *action, int duration_ms, int wait_time_ms, int success);
int update_stats(int readers_delta, int writers_delta, int reads_delta, int writes_delta);

// Statistics and monitoring functions
int get_current_stats(int *readers, int *writers, int *total_reads, int *total_writes);
int log_operation_metrics(int reads, int writes, int active_readers, int active_writers);
int calculate_performance_metrics();

// NEW FUNCTION: Thread-safe active counts update
int update_active_counts(int readers, int writers);

// Helper functions
void url_decode(const char *src, char *dest, size_t dest_size);
void html_escape(const char *src, char *dest, size_t dest_size);

#endif
