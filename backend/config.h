#ifndef CONFIG_H
#define CONFIG_H

// Database configuration - CHANGED FROM /tmp TO PERMANENT LOCATION
#define DATABASE_FILE "/var/www/CN_Sessional/data/cn_chat.db"
#define MAX_SQL_LENGTH 1024

// Synchronization configuration
#define MAX_CONCURRENT_READERS 10
#define MAX_CONCURRENT_WRITERS 3
#define LOCK_TIMEOUT_SECONDS 10
#define DB_TIMEOUT_MS 5000

// Server configuration
#define MAX_BUFFER_SIZE 4096
#define MAX_CLIENTS 100

// Message limits
#define MAX_MESSAGE_LENGTH 256
#define MAX_USERNAME_LENGTH 50

// Error codes
#define SUCCESS 0
#define ERROR_DB -1
#define ERROR_LOCK -2
#define ERROR_TIMEOUT -3

#endif
