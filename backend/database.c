#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <pthread.h>
#include <time.h>
#include "config.h"
#include "sync_manager.h"
#include <ctype.h>
#include <unistd.h> 

// Global database connection with mutex for thread safety
sqlite3 *db = NULL;
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

// FIXED: Database initialization with indexes for better performance
int initialize_database() {
    // Force local timezone to IST for consistent timestamps
    setenv("TZ", "Asia/Kolkata", 1);
    tzset();

// ADD THIS: Debug output to see database location
    fprintf(stderr, "DEBUG: Opening database at: %s\n", DATABASE_FILE);

    pthread_mutex_lock(&db_mutex);
    
    int rc = sqlite3_open(DATABASE_FILE, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    /*
	 * Configure SQLite connection-level protections.
	 *
	 * foreign_keys:
	 *   Enforces declared FOREIGN KEY constraints for this connection.
	 *
	 * journal_mode=WAL:
	 *   Improves concurrent reader/writer behavior.
	 *
	 * synchronous=NORMAL:
	 *   Provides a reasonable durability/performance balance with WAL.
	 */
	char *pragma_error = NULL;

	/* Foreign-key enforcement is security/correctness critical. */
	rc = sqlite3_exec(
	    db,
	    "PRAGMA foreign_keys = ON;",
	    NULL,
	    NULL,
	    &pragma_error
	);

	if (rc != SQLITE_OK) {
	    fprintf(
		stderr,
		"Failed to enable SQLite foreign-key enforcement: %s\n",
		pragma_error ? pragma_error : sqlite3_errmsg(db)
	    );

	    sqlite3_free(pragma_error);
    sqlite3_close(db);
    db = NULL;

    pthread_mutex_unlock(&db_mutex);
    return ERROR_DB;
}

sqlite3_free(pragma_error);
pragma_error = NULL;

/*
 * Verify that foreign-key enforcement is actually enabled
 * on this application connection.
 */
sqlite3_stmt *fk_stmt = NULL;

rc = sqlite3_prepare_v2(
    db,
    "PRAGMA foreign_keys;",
    -1,
    &fk_stmt,
    NULL
);

if (rc != SQLITE_OK ||
    sqlite3_step(fk_stmt) != SQLITE_ROW ||
    sqlite3_column_int(fk_stmt, 0) != 1) {

    fprintf(
        stderr,
        "SQLite foreign-key enforcement verification failed\n"
    );

    if (fk_stmt != NULL) {
        sqlite3_finalize(fk_stmt);
    }

    sqlite3_close(db);
    db = NULL;

    pthread_mutex_unlock(&db_mutex);
    return ERROR_DB;
}

sqlite3_finalize(fk_stmt);

/*
 * WAL mode improves concurrent reader/writer behavior.
 */
rc = sqlite3_exec(
    db,
    "PRAGMA journal_mode = WAL;",
    NULL,
    NULL,
    &pragma_error
);

if (rc != SQLITE_OK) {
    fprintf(
        stderr,
        "Warning: failed to enable SQLite WAL mode: %s\n",
        pragma_error ? pragma_error : sqlite3_errmsg(db)
    );
}

sqlite3_free(pragma_error);
pragma_error = NULL;

/*
 * NORMAL synchronous mode is appropriate with WAL.
 */
rc = sqlite3_exec(
    db,
    "PRAGMA synchronous = NORMAL;",
    NULL,
    NULL,
    &pragma_error
);

if (rc != SQLITE_OK) {
    fprintf(
        stderr,
        "Warning: failed to configure SQLite synchronous mode: %s\n",
        pragma_error ? pragma_error : sqlite3_errmsg(db)
    );
}

sqlite3_free(pragma_error);



    // Wait for a short period instead of immediately failing on
	// transient SQLite lock contention between CGI processes.
	sqlite3_busy_timeout(db, DB_TIMEOUT_MS);
    
// Create tables if they don't exist
const char *create_tables = 
    "CREATE TABLE IF NOT EXISTS messages ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "username TEXT NOT NULL,"
    "message TEXT NOT NULL,"
    "timestamp DATETIME DEFAULT (datetime('now', 'localtime'))"  // CHANGED
    ");"
    
    "CREATE TABLE IF NOT EXISTS access_logs ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "client_id TEXT NOT NULL,"
    "client_type TEXT NOT NULL,"
    "action TEXT NOT NULL,"
    "timestamp DATETIME DEFAULT (datetime('now', 'localtime')),"  // CHANGED
    "duration_ms INTEGER,"
    "success BOOLEAN"
    ");"
    
    "CREATE TABLE IF NOT EXISTS system_stats ("
    "id INTEGER PRIMARY KEY CHECK (id = 1),"
    "total_reads INTEGER DEFAULT 0,"
    "total_writes INTEGER DEFAULT 0,"
    "current_readers INTEGER DEFAULT 0,"
    "current_writers INTEGER DEFAULT 0,"
    "last_performance_calc DATETIME DEFAULT (datetime('now', 'localtime'))"  // CHANGED
    ");"
    
    "CREATE TABLE IF NOT EXISTS operation_history ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "timestamp DATETIME DEFAULT (datetime('now', 'localtime')),"  // CHANGED
    "reads INTEGER DEFAULT 0,"
    "writes INTEGER DEFAULT 0,"
    "active_readers INTEGER DEFAULT 0,"
    "active_writers INTEGER DEFAULT 0"
    ");"
    
    "CREATE TABLE IF NOT EXISTS performance_metrics ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "metric_type TEXT NOT NULL,"
    "reader_score REAL DEFAULT 0,"
	"writer_score REAL DEFAULT 0,"
    "recorded_at DATETIME DEFAULT (datetime('now', 'localtime'))"  // CHANGED
    ");"
    
    
    "CREATE TABLE IF NOT EXISTS users ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "username TEXT NOT NULL UNIQUE,"
    "password_hash TEXT NOT NULL,"
    "role TEXT NOT NULL CHECK(role IN ('reader', 'writer', 'admin')),"
    "created_at DATETIME DEFAULT (datetime('now', 'localtime'))"
    ");"

    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id TEXT NOT NULL UNIQUE,"
    "user_id INTEGER NOT NULL,"
    "created_at DATETIME DEFAULT (datetime('now', 'localtime')),"
    "expires_at DATETIME NOT NULL,"
    "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE"
    ");"

    "CREATE TABLE IF NOT EXISTS auth_logs ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "user_id INTEGER,"
    "username TEXT,"
    "action TEXT NOT NULL,"
    "success INTEGER NOT NULL,"
    "timestamp DATETIME DEFAULT (datetime('now', 'localtime')),"
    "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE SET NULL"
    ");"


    "INSERT OR IGNORE INTO system_stats (id, total_reads, total_writes, current_readers, current_writers) VALUES (1, 0, 0, 0, 0);";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(db, create_tables, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    // FIXED: Add indexes with non-fatal error handling
    const char *create_indexes =
        "CREATE INDEX IF NOT EXISTS idx_operation_history_timestamp ON operation_history(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_access_logs_timestamp ON access_logs(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_access_logs_client_type ON access_logs(client_type);"
        "CREATE INDEX IF NOT EXISTS idx_access_logs_success ON access_logs(success);"
        "CREATE INDEX IF NOT EXISTS idx_performance_metrics_recorded ON performance_metrics(recorded_at);"
        "CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);"
        "CREATE INDEX IF NOT EXISTS idx_sessions_session_id ON sessions(session_id);"
        "CREATE INDEX IF NOT EXISTS idx_sessions_expires_at ON sessions(expires_at);"
        "CREATE INDEX IF NOT EXISTS idx_auth_logs_timestamp ON auth_logs(timestamp);";
    
    rc = sqlite3_exec(db, create_indexes, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        // FIXED: Make index errors non-fatal but logged
        fprintf(stderr, "Warning: Index creation failed (non-critical): %s\n", err_msg);
        sqlite3_free(err_msg);
        // Continue execution - indexes are for performance only
    } else {
        fprintf(stderr, "Database indexes created/verified successfully\n");
    }
    
    pthread_mutex_unlock(&db_mutex);
    return SUCCESS;
}


// FIXED: Add database connection health check
int check_database_health() {
    pthread_mutex_lock(&db_mutex);
    
    if (db == NULL) {
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    const char *health_sql = "SELECT 1 FROM system_stats WHERE id = 1;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, health_sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Database health check failed: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    int health_ok = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
    return health_ok ? SUCCESS : ERROR_DB;
}

// Func to close database
void close_database() {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

// FIXED: Add database reconnection logic
int ensure_database_connection() {
    static int reconnect_attempts = 0;
    const int max_reconnect_attempts = 3;
    
    if (check_database_health() == SUCCESS) {
        reconnect_attempts = 0;
        return SUCCESS;
    }
    
    // Try to reconnect
    while (reconnect_attempts < max_reconnect_attempts) {
        fprintf(stderr, "Attempting database reconnection (%d/%d)...\n", 
                reconnect_attempts + 1, max_reconnect_attempts);
        
        close_database();
        sleep(1 << reconnect_attempts); // Exponential backoff: 1s, 2s, 4s
        
        if (initialize_database() == SUCCESS && check_database_health() == SUCCESS) {
            fprintf(stderr, "Database reconnection successful\n");
            reconnect_attempts = 0;
            return SUCCESS;
        }
        
        reconnect_attempts++;
    }
    
    fprintf(stderr, "Database reconnection failed after %d attempts\n", max_reconnect_attempts);
    return ERROR_DB;
}

// ADD THIS FUNCTION to database.c (place it before log_operation_metrics)
// FIXED: Update active counts with proper local timestamp
int update_active_counts(int readers, int writers) {
    // DEBUG
    FILE *f = fopen("/tmp/db_debug.log", "a");
    if (f) {
        fprintf(f, "DEBUG: update_active_counts() called - readers=%d, writers=%d\n", readers, writers);
        fclose(f);
    }
    
    pthread_mutex_lock(&db_mutex);
    
    // 1. Update system_stats table
    char sql[256];
    snprintf(sql, sizeof(sql),
        "UPDATE system_stats SET current_readers = %d, current_writers = %d WHERE id = 1;",
        readers, writers);
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error updating active counts: %s\n", err_msg);
        sqlite3_free(err_msg);
        // Continue execution - don't return yet
    }
    
    // 2. ALSO insert into operation_history with EXPLICIT local timestamp
    const char *log_sql = "INSERT INTO operation_history (timestamp, reads, writes, active_readers, active_writers) VALUES (datetime('now', 'localtime'), 0, 0, ?, ?);";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, log_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, readers);
        sqlite3_bind_int(stmt, 2, writers);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        // DEBUG
        FILE *f = fopen("/tmp/db_debug.log", "a");
        if (f) {
            fprintf(f, "DEBUG: Successfully logged to operation_history with local timestamp\n");
            fclose(f);
        }
    } else {
        fprintf(stderr, "Failed to prepare operation_history statement\n");
    }
    
    pthread_mutex_unlock(&db_mutex);
    
    return SUCCESS;
}

// ADD THIS FUNCTION - Log operation metrics for historical data
// FIXED: Log operation metrics with proper local timestamp
int log_operation_metrics(int reads, int writes, int active_readers, int active_writers) {
    fprintf(stderr, "DEBUG: log_operation_metrics() called with reads=%d, writes=%d, active_r=%d, active_w=%d\n",
            reads, writes, active_readers, active_writers);
    pthread_mutex_lock(&db_mutex);
    
    // Use explicit localtime in the INSERT
    const char *sql = "INSERT INTO operation_history (timestamp, reads, writes, active_readers, active_writers) VALUES (datetime('now', 'localtime'), ?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    sqlite3_bind_int(stmt, 1, reads);
    sqlite3_bind_int(stmt, 2, writes);
    sqlite3_bind_int(stmt, 3, active_readers);
    sqlite3_bind_int(stmt, 4, active_writers);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "SQL error inserting operation metrics: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    fprintf(stderr, "DEBUG: Successfully inserted into operation_history with local timestamp\n");

    // Clean old data - keep only last 30 days instead of 1000 records
    const char *cleanup_sql = "DELETE FROM operation_history WHERE timestamp < datetime('now', '-30 days', 'localtime');";
    sqlite3_exec(db, cleanup_sql, NULL, NULL, NULL);
    
    pthread_mutex_unlock(&db_mutex);
    return SUCCESS;
}

// FIXED: Calculate performance metrics from real data with proper logic
int calculate_performance_metrics()
{
    pthread_mutex_lock(&db_mutex);

    /*
     * Real performance measurements over the last 60 seconds.
     *
     * No arbitrary scoring constants are used.
     *
     * speed        = average successful operation duration (ms)
     * reliability  = successful operations / total operations (%)
     * concurrency  = maximum observed active clients
     * scalability  = successful operations per second
     * consistency  = mean absolute deviation of successful latency (ms)
     * availability = successful operations / total operations (%)
     */

    const char *metrics_sql =
        "INSERT INTO performance_metrics "
        "(metric_type, reader_score, writer_score) "

        /* SPEED: real average successful operation latency */
        "SELECT 'speed', "
        "COALESCE((SELECT AVG(duration_ms) "
        " FROM access_logs "
        " WHERE client_type = 'reader' "
        " AND success = 1 "
        " AND timestamp >= datetime('now', '-60 seconds')), 0), "
        "COALESCE((SELECT AVG(duration_ms) "
        " FROM access_logs "
        " WHERE client_type = 'writer' "
        " AND success = 1 "
        " AND timestamp >= datetime('now', '-60 seconds')), 0) "

        "UNION ALL "

        /* RELIABILITY: successful operations / total operations */
        "SELECT 'reliability', "
        "COALESCE((SELECT "
        " SUM(CASE WHEN success = 1 THEN 1 ELSE 0 END) * 100.0 "
        " / NULLIF(COUNT(*), 0) "
        " FROM access_logs "
        " WHERE client_type = 'reader' "
        " AND timestamp >= datetime('now', '-60 seconds')), 0), "
        "COALESCE((SELECT "
        " SUM(CASE WHEN success = 1 THEN 1 ELSE 0 END) * 100.0 "
        " / NULLIF(COUNT(*), 0) "
        " FROM access_logs "
        " WHERE client_type = 'writer' "
        " AND timestamp >= datetime('now', '-60 seconds')), 0) "

        "UNION ALL "

        /* CONCURRENCY: maximum actually observed active clients */
        "SELECT 'concurrency', "
        "COALESCE((SELECT MAX(active_readers) "
        " FROM operation_history "
        " WHERE timestamp >= datetime('now', '-60 seconds')), 0), "
        "COALESCE((SELECT MAX(active_writers) "
        " FROM operation_history "
        " WHERE timestamp >= datetime('now', '-60 seconds')), 0) "

        "UNION ALL "

        /*
         * SCALABILITY:
         * Successful operations per second over the measurement window.
         *
         * This is deliberately a measured rate rather than an invented
         * 0-100 score.
         */
        "SELECT 'scalability', "
        "COALESCE((SELECT "
        " SUM(CASE WHEN success = 1 THEN 1 ELSE 0 END) / 60.0 "
        " FROM access_logs "
        " WHERE client_type = 'reader' "
        " AND timestamp >= datetime('now', '-60 seconds')), 0), "
        "COALESCE((SELECT "
        " SUM(CASE WHEN success = 1 THEN 1 ELSE 0 END) / 60.0 "
        " FROM access_logs "
        " WHERE client_type = 'writer' "
        " AND timestamp >= datetime('now', '-60 seconds')), 0) "

        "UNION ALL "

        /*
         * CONSISTENCY:
         * Mean absolute deviation of successful operation duration.
         *
         * Lower value = more consistent latency.
         *
         * No SQLite sqrt/math extension is required.
         */
        "SELECT 'consistency', "
        "COALESCE((SELECT AVG(ABS(a.duration_ms - "
        "    (SELECT AVG(b.duration_ms) "
        "     FROM access_logs b "
        "     WHERE b.client_type = 'reader' "
        "     AND b.success = 1 "
        "     AND b.timestamp >= datetime('now', '-60 seconds')))) "
        " FROM access_logs a "
        " WHERE a.client_type = 'reader' "
        " AND a.success = 1 "
        " AND a.timestamp >= datetime('now', '-60 seconds')), 0), "
        "COALESCE((SELECT AVG(ABS(a.duration_ms - "
        "    (SELECT AVG(b.duration_ms) "
        "     FROM access_logs b "
        "     WHERE b.client_type = 'writer' "
        "     AND b.success = 1 "
        "     AND b.timestamp >= datetime('now', '-60 seconds')))) "
        " FROM access_logs a "
        " WHERE a.client_type = 'writer' "
        " AND a.success = 1 "
        " AND a.timestamp >= datetime('now', '-60 seconds')), 0) "

        "UNION ALL "

        /*
         * AVAILABILITY:
         * Successful operations / all operations over the last 60 seconds.
         */
        "SELECT 'availability', "
        "COALESCE((SELECT "
        " SUM(CASE WHEN success = 1 THEN 1 ELSE 0 END) * 100.0 "
        " / NULLIF(COUNT(*), 0) "
        " FROM access_logs "
        " WHERE client_type = 'reader' "
        " AND timestamp >= datetime('now', '-60 seconds')), 0), "
        "COALESCE((SELECT "
        " SUM(CASE WHEN success = 1 THEN 1 ELSE 0 END) * 100.0 "
        " / NULLIF(COUNT(*), 0) "
        " FROM access_logs "
        " WHERE client_type = 'writer' "
        " AND timestamp >= datetime('now', '-60 seconds')), 0);";

    char *err_msg = NULL;

    int rc = sqlite3_exec(
        db,
        metrics_sql,
        NULL,
        NULL,
        &err_msg
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "SQL error calculating performance metrics: %s\n",
            err_msg ? err_msg : sqlite3_errmsg(db)
        );

        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }

    /*
     * Keep only the last 7 days of metric snapshots.
     */
    const char *cleanup_sql =
        "DELETE FROM performance_metrics "
        "WHERE recorded_at < datetime('now', '-7 days');";

    sqlite3_exec(
        db,
        cleanup_sql,
        NULL,
        NULL,
        NULL
    );

    pthread_mutex_unlock(&db_mutex);

    return SUCCESS;
}

// FIXED: Use parameterized queries for log_access
int log_access(
    const char *client_id,
    const char *client_type,
    const char *action,
    int duration_ms,
    int wait_time_ms,
    int success
) {
    pthread_mutex_lock(&db_mutex);

    const char *sql =
        "INSERT INTO access_logs "
        "(client_id, client_type, action, duration_ms, wait_time_ms, success) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n",
                sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }

    sqlite3_bind_text(stmt, 1, client_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, client_type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, action, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, duration_ms);
    sqlite3_bind_int(stmt, 5, wait_time_ms);
    sqlite3_bind_int(stmt, 6, success);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "SQL error logging access: %s\n",
                sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }

    pthread_mutex_unlock(&db_mutex);
    return SUCCESS;
}

// ADD THIS FUNCTION - Get current stats from database
int get_current_stats(int *readers, int *writers, int *total_reads, int *total_writes) {
    pthread_mutex_lock(&db_mutex);
    
    const char *sql = "SELECT current_readers, current_writers, total_reads, total_writes FROM system_stats WHERE id = 1;";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *readers = sqlite3_column_int(stmt, 0);
        *writers = sqlite3_column_int(stmt, 1);
        *total_reads = sqlite3_column_int(stmt, 2);
        *total_writes = sqlite3_column_int(stmt, 3);
    } else {
        *readers = *writers = *total_reads = *total_writes = 0;
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return SUCCESS;
}

// Helper function for URL decoding
void url_decode(const char *src, char *dest, size_t dest_size) {
    size_t i = 0, j = 0;
    
    while (src[i] != '\0' && j < dest_size - 1) {
        if (src[i] == '+') {
            dest[j++] = ' ';
            i++;
        } else if (src[i] == '%' && src[i+1] != '\0' && src[i+2] != '\0') {
            // Decode URL-encoded characters
            char hex[3] = {src[i+1], src[i+2], '\0'};
            char *endptr;
            long int hex_val = strtol(hex, &endptr, 16);
            
            if (*endptr == '\0') {
                dest[j++] = (char)hex_val;
                i += 3;
            } else {
                // Invalid encoding, copy as-is
                dest[j++] = src[i++];
            }
        } else {
            dest[j++] = src[i++];
        }
    }
    dest[j] = '\0';
}

// FIXED: Add time-based performance calculation instead of operation count
int update_stats(int readers_delta, int writers_delta, int reads_delta, int writes_delta) {
    pthread_mutex_lock(&db_mutex);
    
    char sql[MAX_SQL_LENGTH];
    snprintf(sql, sizeof(sql),
        "UPDATE system_stats SET "
        "current_readers = current_readers + %d, "
        "current_writers = current_writers + %d, "
        "total_reads = total_reads + %d, "
        "total_writes = total_writes + %d "
        "WHERE id = 1;",
        readers_delta, writers_delta, reads_delta, writes_delta);
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error updating stats: %s\n", err_msg);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    pthread_mutex_unlock(&db_mutex);
    
    // Log operation metrics for historical data
    int current_readers, current_writers, total_reads, total_writes;
    get_current_stats(&current_readers, &current_writers, &total_reads, &total_writes);
    log_operation_metrics(reads_delta, writes_delta, current_readers, current_writers);
    
    // Update performance metrics based on time instead of operation count
    calculate_performance_metrics(); // Now uses time-based check internally
    
    return SUCCESS;
}


/*
 * Escape untrusted text before inserting it into HTML.
 * Prevents stored/reflected XSS attacks.
 */
void html_escape(const char *src, char *dest, size_t dest_size) {
    size_t i = 0;
    size_t j = 0;

    if (src == NULL || dest == NULL || dest_size == 0) {
        return;
    }

    while (src[i] != '\0' && j < dest_size - 1) {
        const char *replacement = NULL;

        switch (src[i]) {
            case '&':
                replacement = "&amp;";
                break;

            case '<':
                replacement = "&lt;";
                break;

            case '>':
                replacement = "&gt;";
                break;

            case '"':
                replacement = "&quot;";
                break;

            case '\'':
                replacement = "&#39;";
                break;

            default:
                dest[j++] = src[i++];
                continue;
        }

        size_t len = strlen(replacement);

        if (j + len >= dest_size) {
            break;
        }

        memcpy(dest + j, replacement, len);
        j += len;
        i++;
    }

    dest[j] = '\0';
}

// FIXED: Ensure valid timestamps
int read_messages(FILE *output) {
    pthread_mutex_lock(&db_mutex);

    // Get messages with proper timestamp formatting and validation
    const char *sql = "SELECT username, message, datetime(timestamp, 'localtime') as local_ts FROM messages WHERE timestamp IS NOT NULL ORDER BY timestamp DESC LIMIT 20;";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    fprintf(output, "<div class='messages-container'>");
    fprintf(output, "<div class='messages-header'>");
    fprintf(output, "<h3>💬 Chat Messages</h3>");
    fprintf(output, "<div class='messages-count' id='messagesCount'>0 messages</div>");
    fprintf(output, "</div>");
    fprintf(output, "<div class='messages-list'>");
    
    int message_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *username = (const char *)sqlite3_column_text(stmt, 0);
        const char *message = (const char *)sqlite3_column_text(stmt, 1);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 2);
        
        if (username && message) {
            char display_time[20];
            if (timestamp && strlen(timestamp) >= 19) {
                // Validate timestamp format and show only the time part
                snprintf(display_time, sizeof(display_time), "%.8s", timestamp + 11);
            } else {
                // Use current time as fallback with proper validation
                time_t now = time(NULL);
                if (now != (time_t)-1) {
                    struct tm *tm_info = localtime(&now);
                    if (tm_info) {
                        strftime(display_time, sizeof(display_time), "%H:%M:%S", tm_info);
                    } else {
                        strcpy(display_time, "Unknown");
                    }
                } else {
                    strcpy(display_time, "Unknown");
                }
            }
            
            char escaped_username[512];
	    char escaped_message[1024];

	    html_escape(username, escaped_username, sizeof(escaped_username));
	    html_escape(message, escaped_message, sizeof(escaped_message));
            // Display message card
            fprintf(output, "<div class='message-card'>");
            fprintf(output, "<div class='message-header'>");
            fprintf(output, "<div class='user-info'>");
            fprintf(output, "<div class='user-avatar'>%c</div>", username[0] ? toupper(username[0]) : 'U');
            fprintf(output, "<div class='user-details'>");
            fprintf(output, "<div class='username'>%s</div>", escaped_username);
            fprintf(output, "<div class='message-time'>%s</div>", display_time);
            fprintf(output, "</div>");
            fprintf(output, "</div>");
            fprintf(output, "</div>");
            fprintf(output, "<div class='message-content'>%s</div>", escaped_message);
            fprintf(output, "</div>");
            
            message_count++;
        }
    }
    
    if (message_count == 0) {
        fprintf(output, "<div class='empty-state'>");
        fprintf(output, "<div class='empty-icon'>💬</div>");
        fprintf(output, "<h3>No messages yet</h3>");
        fprintf(output, "<p>Be the first to start the conversation!</p>");
        fprintf(output, "</div>");
    }
    
    fprintf(output, "</div>"); // Close messages-list
    fprintf(output, "</div>"); // Close messages-container
    
    // Update messages count
    fprintf(output, "<script>");
    fprintf(output, "if (document.getElementById('messagesCount')) {");
    fprintf(output, "  document.getElementById('messagesCount').textContent = '%d message%s';", 
            message_count, message_count == 1 ? "" : "s");
    fprintf(output, "}");
    fprintf(output, "</script>");
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    
    return SUCCESS;
}

// FIXED: URL decoding variable names
int write_message(const char *username, const char *message) {
    pthread_mutex_lock(&db_mutex);

    // Improved URL decoding with correct variable names
    char decoded_username[256];
    char decoded_message[512];
    size_t i, j;
    
    // Decode username
    for (i = 0, j = 0; username[i] != '\0' && j < sizeof(decoded_username)-1; i++, j++) {
        if (username[i] == '+') {
            decoded_username[j] = ' ';
        } else if (username[i] == '%' && username[i+1] == '2' && username[i+2] == 'C') {
            decoded_username[j] = ',';
            i += 2;
        } else if (username[i] == '%' && username[i+1] == '2' && username[i+2] == '1') {
            decoded_username[j] = '!';
            i += 2;
        } else if (username[i] == '%' && username[i+1] == '3' && username[i+2] == 'F') {
            decoded_username[j] = '?';
            i += 2;
        } else if (username[i] == '%' && username[i+1] == '2' && username[i+2] == '7') {
            decoded_username[j] = '\'';
            i += 2;
        } else {
            decoded_username[j] = username[i];
        }
    }
    decoded_username[j] = '\0';
    
    // FIXED: Use message variable instead of username in decoding loop
    for (i = 0, j = 0; message[i] != '\0' && j < sizeof(decoded_message)-1; i++, j++) {
        if (message[i] == '+') {
            decoded_message[j] = ' ';
        } else if (message[i] == '%' && message[i+1] == '2' && message[i+2] == 'C') {
            decoded_message[j] = ',';
            i += 2;
        } else if (message[i] == '%' && message[i+1] == '2' && message[i+2] == '1') {
            decoded_message[j] = '!';
            i += 2;
        } else if (message[i] == '%' && message[i+1] == '3' && message[i+2] == 'F') {
            decoded_message[j] = '?';
            i += 2;
        } else if (message[i] == '%' && message[i+1] == '2' && message[i+2] == '7') {
            decoded_message[j] = '\'';
            i += 2;
        } else {
            decoded_message[j] = message[i];
        }
    }
    decoded_message[j] = '\0';

    // Use parameterized query to prevent SQL injection
    const char *sql = "INSERT INTO messages (username, message, timestamp) VALUES (?, ?, datetime('now'));";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }
    
    sqlite3_bind_text(stmt, 1, decoded_username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, decoded_message, -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return ERROR_DB;
    }

    pthread_mutex_unlock(&db_mutex);
    
    // Update statistics - increment total writes
    update_stats(0, 0, 0, 1);
    
    return SUCCESS;
}

