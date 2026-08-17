#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include "database.h"
#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include <pthread.h>
#include <sqlite3.h>
#include <sys/time.h>
#include "sync_manager.h"
#include <unistd.h>
#include <sodium.h>

// ADD THESE EXTERNAL DECLARATIONS at the top, after includes
extern sqlite3 *db;

// External functions from other modules
extern int initialize_database();
extern int read_messages(FILE *output);
extern int write_message(const char *authenticated_username, const char *message);
extern int log_access(const char *client_id, const char *client_type, const char *action, int duration_ms, int success);
extern int acquire_read_lock(const char *client_id);
extern void release_read_lock();
extern int acquire_write_lock(const char *client_id);
extern void release_write_lock();
extern void close_database();
extern int get_current_stats(int *readers, int *writers, int *total_reads, int *total_writes);
extern int log_operation_metrics(int reads, int writes, int active_readers, int active_writers);
extern int calculate_performance_metrics();
extern int check_database_health();     
extern int ensure_database_connection();


// Function to get current timestamp in milliseconds
long long current_timestamp() {
    struct timeval te;
    gettimeofday(&te, NULL);
    long long milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000;
    return milliseconds;
}

// Function to parse POST data
void parse_post_data(char **username, char **message, char **client_type) {
    const size_t MAX_POST_SIZE = 4096;
    const size_t MAX_CLIENT_TYPE_INPUT_LENGTH = 32;

    if (username == NULL || message == NULL || client_type == NULL) {
        return;
    }

    *username = NULL;
    *message = NULL;
    *client_type = NULL;

    char *content_length_str = getenv("CONTENT_LENGTH");

    if (content_length_str == NULL || *content_length_str == '\0') {
        return;
    }

    /*
     * Parse CONTENT_LENGTH strictly.
     * Only decimal digits are accepted and the value must be
     * within the configured request-size limit.
     */
    char *endptr = NULL;
    errno = 0;

    unsigned long parsed_length =
        strtoul(content_length_str, &endptr, 10);

    if (errno != 0 ||
        endptr == content_length_str ||
        *endptr != '\0' ||
        parsed_length == 0 ||
        parsed_length > MAX_POST_SIZE) {
        return;
    }

    size_t content_length = (size_t)parsed_length;

    char *post_data = malloc(content_length + 1);

    if (post_data == NULL) {
        return;
    }

    size_t bytes_read = fread(
        post_data,
        1,
        content_length,
        stdin
    );

    if (bytes_read != content_length) {
        free(post_data);
        return;
    }

    post_data[bytes_read] = '\0';

    char *token = strtok(post_data, "&");

    while (token != NULL) {
        const char *value_start = NULL;
        size_t max_length = 0;
        char decoded_value[1024];

        if (strncmp(token, "username=", 9) == 0) {
            value_start = token + 9;
            max_length = MAX_USERNAME_LENGTH;
        } else if (strncmp(token, "message=", 8) == 0) {
            value_start = token + 8;
            max_length = MAX_MESSAGE_LENGTH;
        } else if (strncmp(token, "client_type=", 12) == 0) {
            value_start = token + 12;
            max_length = MAX_CLIENT_TYPE_INPUT_LENGTH;
        }

        if (value_start != NULL) {
            url_decode(
                value_start,
                decoded_value,
                sizeof(decoded_value)
            );

            size_t decoded_length = strlen(decoded_value);

            /*
             * Reject oversized fields instead of silently truncating them.
             */
            if (decoded_length > max_length) {
                free(*username);
                free(*message);
                free(*client_type);

                *username = NULL;
                *message = NULL;
                *client_type = NULL;

                free(post_data);
                return;
            }

            char **destination = NULL;

            if (strncmp(token, "username=", 9) == 0) {
                destination = username;
            } else if (strncmp(token, "message=", 8) == 0) {
                destination = message;
            } else if (strncmp(token, "client_type=", 12) == 0) {
                destination = client_type;
            }

            if (destination != NULL) {
                char *value = malloc(decoded_length + 1);

                if (value == NULL) {
                    free(*username);
                    free(*message);
                    free(*client_type);

                    *username = NULL;
                    *message = NULL;
                    *client_type = NULL;

                    free(post_data);
                    return;
                }

                memcpy(value, decoded_value, decoded_length + 1);

                free(*destination);
                *destination = value;
            }
        }

        token = strtok(NULL, "&");
    }

    free(post_data);
}

/*
 * Parse login POST data.
 *
 * Expected fields:
 *   username=<value>&password=<value>
 *
 * Password is never logged.
 */
void parse_login_data(char **username, char **password) {
    *username = NULL;
    *password = NULL;

    char *content_length_str = getenv("CONTENT_LENGTH");
    int content_length = 0;

    if (content_length_str) {
        content_length = atoi(content_length_str);
    }

    /*
     * Reject unreasonable request sizes.
     */
    if (content_length <= 0 || content_length > 4096) {
        return;
    }

    char *post_data = malloc((size_t)content_length + 1);

    if (post_data == NULL) {
        return;
    }

    size_t bytes_read = fread(
        post_data,
        1,
        (size_t)content_length,
        stdin
    );

    post_data[bytes_read] = '\0';

    char *token = strtok(post_data, "&");

    while (token != NULL) {
        char decoded_value[512];

        if (strncmp(token, "username=", 9) == 0) {

            url_decode(
                token + 9,
                decoded_value,
                sizeof(decoded_value)
            );

            *username = malloc(strlen(decoded_value) + 1);

            if (*username != NULL) {
                strcpy(*username, decoded_value);
            }

        } else if (strncmp(token, "password=", 9) == 0) {

            url_decode(
                token + 9,
                decoded_value,
                sizeof(decoded_value)
            );

            *password = malloc(strlen(decoded_value) + 1);

            if (*password != NULL) {
                strcpy(*password, decoded_value);
            }
        }

        token = strtok(NULL, "&");
    }

    free(post_data);
}

// Function to generate client ID from remote address
char* generate_client_id() {
    char *remote_addr = getenv("REMOTE_ADDR");
    if (!remote_addr) remote_addr = "unknown";
    
    time_t now = time(NULL);
    char *client_id = malloc(256);
    snprintf(client_id, 256, "%s_%ld", remote_addr, now);
    
    return client_id;
}

void print_html_header(const char *title) {
    printf("Content-type: text/html\n\n");
    printf("<!DOCTYPE html>");
    printf("<html><head>");
    printf("<title>%s</title>", title);
    printf("<link rel='stylesheet' type='text/css' href='/CN_Sessional/html/style.css'>");
    printf("</head><body>");
    printf("<div class='container'>");
    printf("<h1>%s</h1>", title);
}

void print_html_footer() {
    printf("</div>");
    printf("</body></html>");
}

// FIXED: Bucket label represents END of time range (20:00-20:59 → 21:00 bucket)
// FIXED: Correct localtime query with proper hour calculation

void handle_historical_data() {
    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: application/json\n\n");
    printf("{\n");
    
    // Get current local time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int current_hour = tm_info->tm_hour;
    
    // KEEP YOUR ORIGINAL BUCKET LABELS (representing END of time range)
    char timestamps[7][6];
    for (int i = 0; i < 7; i++) {
        int hour = (current_hour - (5 - i) + 24) % 24; // +1 hour for end-of-range labeling
        snprintf(timestamps[i], sizeof(timestamps[i]), "%02d:00", hour);
    }
    
    // FIXED: Remove the 'localtime' conversion since data is already local
    // KEEP YOUR ORIGINAL QUERY STRUCTURE but fix timezone
    const char *sql = 
        "SELECT "
        "strftime('%H:00', datetime(timestamp, '+1 hour')) as end_hour, "  // ← REMOVED , 'localtime' from timestamp
        "SUM(reads) as hour_reads, "
        "SUM(writes) as hour_writes "
        "FROM operation_history "
        "WHERE timestamp >= datetime('now', '-7 hours') "  // ← REMOVED , 'localtime'
        "GROUP BY strftime('%H', datetime(timestamp, '+1 hour')) "  // ← REMOVED , 'localtime'
        "ORDER BY end_hour;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    int reads[7] = {0, 0, 0, 0, 0, 0, 0};
    int writes[7] = {0, 0, 0, 0, 0, 0, 0};
    
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *end_hour = (const char *)sqlite3_column_text(stmt, 0);
            int hour_reads = sqlite3_column_int(stmt, 1);
            int hour_writes = sqlite3_column_int(stmt, 2);
            
            if (end_hour) {
                // Map the data to your timestamp array
                for (int i = 0; i < 7; i++) {
                    if (strcmp(timestamps[i], end_hour) == 0) {
                        reads[i] = hour_reads;
                        writes[i] = hour_writes;
                        fprintf(stderr, "DEBUG: Operations for period ending at %s: %d reads, %d writes\n", 
                                end_hour, hour_reads, hour_writes);
                        break;
                    }
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Debug: Show what we're returning
    fprintf(stderr, "DEBUG: Current time: %02d:%02d\n", tm_info->tm_hour, tm_info->tm_min);
    fprintf(stderr, "DEBUG: Graph buckets (representing END of time range):\n");
    for (int i = 0; i < 7; i++) {
        fprintf(stderr, "  %s (covers %02d:00-%02d:59): %d reads, %d writes\n", 
                timestamps[i], 
                (atoi(timestamps[i]) - 1 + 24) % 24, // Start hour
                atoi(timestamps[i]) - 1,             // End hour (same as label - 1)
                reads[i], writes[i]);
    }
    
    // Output the data
    printf("  \"timestamps\": [");
    for (int i = 0; i < 7; i++) {
        if (i > 0) printf(",");
        printf("\"%s\"", timestamps[i]);
    }
    printf("],\n");
    
    printf("  \"reads\": [");
    for (int i = 0; i < 7; i++) {
        if (i > 0) printf(",");
        printf("%d", reads[i]);
    }
    printf("],\n");
    
    printf("  \"writes\": [");
    for (int i = 0; i < 7; i++) {
        if (i > 0) printf(",");
        printf("%d", writes[i]);
    }
    printf("]\n");
    
    printf("}\n");
    
    pthread_mutex_unlock(&db_mutex);
}

// FIXED: Configurable concurrency ranges
void handle_concurrency_stats() {
    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: application/json\n\n");
    printf("{\n");
    
    // FIXED: Always use consistent ranges that match frontend labels
    // This ensures chart labels always match the data buckets
    const int BUCKET1_MAX = 1;  // 0-1 readers
    const int BUCKET2_MAX = 3;  // 2-3 readers  
    const int BUCKET3_MAX = 7;  // 4-7 readers
    // 8+ readers goes in high_readers bucket
    
    // Calculate concurrency distribution with consistent ranges
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT "
        "SUM(CASE WHEN active_readers BETWEEN 0 AND %d THEN 1 ELSE 0 END) as low_readers, "
	"SUM(CASE WHEN active_readers BETWEEN %d AND %d THEN 1 ELSE 0 END) as medium_readers, "
	"SUM(CASE WHEN active_readers BETWEEN %d AND %d THEN 1 ELSE 0 END) as high_readers, "
	"SUM(CASE WHEN active_readers > %d THEN 1 ELSE 0 END) as very_high_readers "
	"FROM operation_history WHERE timestamp > datetime('now', '-1 day');",
	BUCKET1_MAX,
	BUCKET1_MAX + 1, BUCKET2_MAX,
	BUCKET2_MAX + 1, BUCKET3_MAX,
	BUCKET3_MAX);
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    // Also return the bucket ranges so frontend can use dynamic labels if needed
    printf("  \"ranges\": {\n");
    printf("    \"low_max\": %d,\n", BUCKET1_MAX);
    printf("    \"medium_max\": %d,\n", BUCKET2_MAX);
    printf("    \"high_min\": %d\n", BUCKET2_MAX + 1);
    printf("  },\n");
    
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  \"zero_readers\": %d,\n", sqlite3_column_int(stmt, 0));
        printf("  \"low_readers\": %d,\n", sqlite3_column_int(stmt, 1));
        printf("  \"medium_readers\": %d,\n", sqlite3_column_int(stmt, 2));
        printf("  \"high_readers\": %d\n", sqlite3_column_int(stmt, 3));
    } else {
        // Fallback to reasonable defaults
        printf("  \"zero_readers\": 30,\n");
        printf("  \"low_readers\": 45,\n");
        printf("  \"medium_readers\": 20,\n");
        printf("  \"high_readers\": 5\n");
    }
    
    if (rc == SQLITE_OK) {
        sqlite3_finalize(stmt);
    }
    
    printf("}\n");
    
    pthread_mutex_unlock(&db_mutex);
}

// FIXED: Heatmap day ordering and improved load calculation
void handle_daily_load() {
    pthread_mutex_lock(&db_mutex);

    printf("Content-type: application/json\n\n");
    printf("{\n");

    /*
     * Daily system load is based on actual operations:
     *
     *     reads + writes
     *
     * Each day's value is normalized against the busiest
     * day in the last 7 days.
     *
     * Therefore:
     *   busiest day = 100%
     *   no operations = 0%
     */

    const char *sql =
        "SELECT "
        "strftime('%w', timestamp) AS day_of_week, "
        "SUM(reads + writes) AS total_operations "
        "FROM operation_history "
        "WHERE timestamp >= datetime('now', '-7 days') "
        "GROUP BY strftime('%w', timestamp) "
        "ORDER BY day_of_week;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    int day_loads[7] = {0};
    int max_operations = 0;

    if (rc == SQLITE_OK) {

        /*
         * First pass:
         * Find the busiest day.
         */
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int total_operations = sqlite3_column_int(stmt, 1);

            if (total_operations > max_operations) {
                max_operations = total_operations;
            }
        }

        /*
         * Second pass:
         * Convert each day's operations into
         * a percentage of the busiest day.
         */
        sqlite3_reset(stmt);

        while (sqlite3_step(stmt) == SQLITE_ROW) {

            int day = sqlite3_column_int(stmt, 0);
            int total_operations = sqlite3_column_int(stmt, 1);

            int load_percentage = 0;

            if (max_operations > 0) {
                load_percentage =
                    (total_operations * 100) / max_operations;
            }

            /*
             * SQLite %w:
             *
             * 0 = Sunday
             * 1 = Monday
             * 2 = Tuesday
             * 3 = Wednesday
             * 4 = Thursday
             * 5 = Friday
             * 6 = Saturday
             *
             * Frontend expects:
             *
             * 0 = Monday
             * ...
             * 6 = Sunday
             */

            int chart_index = -1;

            switch (day) {
                case 1: chart_index = 0; break;
                case 2: chart_index = 1; break;
                case 3: chart_index = 2; break;
                case 4: chart_index = 3; break;
                case 5: chart_index = 4; break;
                case 6: chart_index = 5; break;
                case 0: chart_index = 6; break;
            }

            if (chart_index >= 0 && chart_index < 7) {
                day_loads[chart_index] = load_percentage;
            }
        }

        sqlite3_finalize(stmt);

    } else {
        fprintf(
            stderr,
            "SQL error calculating daily load: %s\n",
            sqlite3_errmsg(db)
        );
    }

    printf("  \"daily_load\": [");

    for (int i = 0; i < 7; i++) {
        if (i > 0) {
            printf(",");
        }

        printf("%d", day_loads[i]);
    }

    printf("]\n");
    printf("}\n");

    pthread_mutex_unlock(&db_mutex);
}

// FIXED: Performance metrics with proper data
void handle_performance_metrics() {

    /*
     * Refresh cached performance metrics when they are stale.
     * calculate_performance_metrics() handles db_mutex internally.
     */
    calculate_performance_metrics();

    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: application/json\n\n");
    printf("{\n");
    
    // Get latest performance metrics
    const char *sql = 
        "SELECT metric_type, reader_score, writer_score "
        "FROM performance_metrics "
        "WHERE recorded_at = (SELECT MAX(recorded_at) FROM performance_metrics) "
        "ORDER BY metric_type;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    // Default values
	double reader_speed = 0;
	double writer_speed = 0;
	double reader_reliability = 0;
	double writer_reliability = 0;
	double reader_concurrency = 0;
	double writer_concurrency = 0;
	double reader_scalability = 0;
	double writer_scalability = 0;
	double reader_consistency = 0;
	double writer_consistency = 0;
	double reader_availability = 0;
	double writer_availability = 0;

    
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *metric_type = (const char *)sqlite3_column_text(stmt, 0);
            double reader_score = sqlite3_column_double(stmt, 1);
	    double writer_score = sqlite3_column_double(stmt, 2);
            
            if (strcmp(metric_type, "speed") == 0) {
                reader_speed = reader_score;
                writer_speed = writer_score;
            } else if (strcmp(metric_type, "reliability") == 0) {
                reader_reliability = reader_score;
                writer_reliability = writer_score;
            } else if (strcmp(metric_type, "concurrency") == 0) {
                reader_concurrency = reader_score;
                writer_concurrency = writer_score;
            } else if (strcmp(metric_type, "scalability") == 0) {
                reader_scalability = reader_score;
                writer_scalability = writer_score;
            } else if (strcmp(metric_type, "consistency") == 0) {
                reader_consistency = reader_score;
                writer_consistency = writer_score;
            } else if (strcmp(metric_type, "availability") == 0) {
                reader_availability = reader_score;
                writer_availability = writer_score;
            }
        }
        sqlite3_finalize(stmt);
    }
    
	printf("  \"reader_speed\": %.2f,\n", reader_speed);
	printf("  \"reader_reliability\": %.2f,\n", reader_reliability);
	printf("  \"reader_concurrency\": %.2f,\n", reader_concurrency);
	printf("  \"reader_scalability\": %.2f,\n", reader_scalability);
	printf("  \"reader_consistency\": %.2f,\n", reader_consistency);
	printf("  \"reader_availability\": %.2f,\n", reader_availability);

	printf("  \"writer_speed\": %.2f,\n", writer_speed);
	printf("  \"writer_reliability\": %.2f,\n", writer_reliability);
	printf("  \"writer_concurrency\": %.2f,\n", writer_concurrency);
	printf("  \"writer_scalability\": %.2f,\n", writer_scalability);
	printf("  \"writer_consistency\": %.2f,\n", writer_consistency);
	printf("  \"writer_availability\": %.2f\n", writer_availability);
	printf("}\n");
    
    pthread_mutex_unlock(&db_mutex);
}

void handle_reader() {
    char *client_id = generate_client_id();
    long long start_time = current_timestamp();
    int success = 0;
    
    print_html_header("Reader View - Chat Messages");
    
    printf("<div class='reader-section'>");
    printf("<h2>Latest Messages</h2>");
    
    // Display reader stats
    printf("<div class='reader-stats'>");
    printf("<div class='reader-stat'>");
    printf("<span class='stat-badge readers'>");
    printf("<span class='real-time-indicator'></span>");
    printf("Active Readers: <span id='currentReaders'>0</span>");
    printf("</span>");
    printf("</div>");
    printf("<button class='btn btn-primary' onclick='location.reload()'>");
    printf("<span class='btn-icon'>🔄</span>");
    printf("Refresh Messages");
    printf("</button>");
    printf("</div>");
    
    // Acquire read lock and load messages
    if (acquire_read_lock(client_id) == SUCCESS) {
        printf("<div id='messagesContent'>");
        
        // Read messages from database
        if (read_messages(stdout) == SUCCESS) {
            success = 1;
            // Update read statistics
            update_stats(0, 0, 1, 0); // Increment total reads by 1
        } else {
            printf("<div class='error'>Failed to read messages from database</div>");
        }
        
        printf("</div>"); // Close messagesContent
        release_read_lock();
    } else {
        printf("<div id='messagesContent'>");
        printf("<div class='error'>Failed to acquire read lock</div>");
        printf("</div>");
    }
    
    // Reader info section
    printf("<div class='reader-info'>");
    printf("<div class='info-card'>");
    printf("<h3>📚 Reader Access Rules</h3>");
    printf("<ul>");
    printf("<li>Multiple readers can access simultaneously</li>");
    printf("<li>Readers are blocked when writers are active</li>");
    printf("<li>First reader acquires the write lock</li>");
    printf("<li>Last reader releases the write lock</li>");
    printf("</ul>");
    printf("</div>");
    printf("</div>");
    
    printf("<div class='reader-actions'>");
    printf("<a href='/CN_Sessional/html/index.html' class='btn btn-outline'>Back to Home</a>");
    printf("</div>");
    
    printf("</div>"); // Close reader-section
    
    // Add JavaScript for stats updates
    printf("<script>");
    printf("function updateReaderStats() {");
    printf("  fetch('/CN_Sessional/cgi-bin/server.cgi/status')");
    printf("    .then(response => response.json())");
    printf("    .then(data => {");
    printf("      if (!data.error) {");
    printf("        const readersElement = document.getElementById('currentReaders');");
    printf("        if (readersElement) {");
    printf("          readersElement.textContent = data.activeReaders;");
    printf("        }");
    printf("      }");
    printf("    });");
    printf("}");
    printf("setInterval(updateReaderStats, 3000);");
    printf("updateReaderStats();");
    printf("</script>");
    
    // Log access
    long long duration = current_timestamp() - start_time;
    log_access(client_id, "reader", "read", duration, success);
    
    free(client_id);
    print_html_footer();
}

/*
 * Security headers for authentication/session responses.
 *
 * no-store prevents browsers and intermediary caches from
 * retaining sensitive authentication responses.
 */
void print_auth_security_headers(void) {
    printf("Cache-Control: no-store, no-cache, must-revalidate, private\r\n");
    printf("Pragma: no-cache\r\n");
}

/*
 * Handle POST /login
 *
 * Authentication flow:
 *
 * authenticated_username + password
 *        ↓
 * authenticate_user()
 *        ↓
 * valid?
 *   NO ─────→ 401
 *   YES
 *        ↓
 * create_session()
 *        ↓
 * Set-Cookie
 *        ↓
 * 200 OK
 */
void handle_login() {
    char *username = NULL;
    char *password = NULL;

    int user_id = 0;
    char role[32] = {0};

    char session_id[SESSION_ID_HEX_LENGTH + 1] = {0};

    parse_login_data(&username, &password);

    /*
     * Missing credentials.
     */
    if (username== NULL ||
        password == NULL ||
        strlen(username) == 0 ||
        strlen(password) == 0) {

        log_auth_event(
            0,
            username ? username : "",
            "LOGIN_FAILED",
            0
        );

        free(username);
        free(password);

        printf("Status: 401 Unauthorized\r\n");
        printf("Content-Type: application/json\r\n");
        printf("\r\n");

        printf(
            "{\"success\":false,\"error\":\"Invalid credentials\"}\n"
        );

        return;
    }

    /*
     * Authenticate against the users table.
     */
    int result = authenticate_user(
        username,
        password,
        &user_id,
        role,
        sizeof(role)
    );

    /*
     * Clear plaintext password from memory as soon as possible.
     */
    sodium_memzero(
        password,
        strlen(password)
    );

    free(password);
    password = NULL;

    if (result != AUTH_SUCCESS) {

        log_auth_event(
            0,
            username,
            "LOGIN_FAILED",
            0
        );

        free(username);

        printf("Status: 401 Unauthorized\r\n");
        printf("Content-Type: application/json\r\n");
        printf("\r\n");

        printf(
            "{\"success\":false,\"error\":\"Invalid credentials\"}\n"
        );

        return;
    }

    /*
     * Credentials are correct.
     * Create a server-side session.
     */
    result = create_session(
        user_id,
        session_id,
        sizeof(session_id)
    );

    if (result != AUTH_SUCCESS) {

        log_auth_event(
            user_id,
            username,
            "LOGIN_SESSION_FAILED",
            0
        );

        free(username);

        printf("Status: 500 Internal Server Error\r\n");
        printf("Content-Type: application/json\r\n");
        printf("\r\n");

        printf(
            "{\"success\":false,\"error\":\"Could not create session\"}\n"
        );

        return;
    }

    /*
     * Record successful authentication.
     */
    log_auth_event(
        user_id,
        username,
        "LOGIN",
        1
    );

    free(username);

    /*
     * Send the session cookie.
     *
     * HttpOnly:
     * JavaScript cannot directly read the session token.
     *
     * SameSite=Lax:
     * Helps reduce cross-site request risks.
     *
     * Path=/:
     * Cookie applies to the whole application.
     *
     * Max-Age:
     * Matches the 30-minute server-side session lifetime.
     */
    printf(
	    "Set-Cookie: session_id=%s; "
	    "Max-Age=1800; "
	    "Path=/CN_Sessional; "
	    "HttpOnly; "
	    "SameSite=Lax\r\n",
	    session_id
	);

	print_auth_security_headers();
	printf("Content-Type: application/json\r\n");
    printf("\r\n");

    printf(
        "{\"success\":true,\"role\":\"%s\"}\n",
        role
    );
}

/*
 * Handle POST /logout
 */
void handle_logout() {
    const char *cookie_header = getenv("HTTP_COOKIE");

    char session_id[SESSION_ID_HEX_LENGTH + 1] = {0};

    /*
     * No cookie means there is nothing to destroy.
     * We still return success so logout is idempotent.
     */
    if (cookie_header == NULL ||
        extract_session_id_from_cookie(
            cookie_header,
            session_id,
            sizeof(session_id)
        ) != AUTH_SUCCESS) {

        printf(
	    "Set-Cookie: session_id=; "
	    "Max-Age=0; "
	    "Path=/CN_Sessional; "
	    "HttpOnly; "
	    "SameSite=Lax\r\n"
	);

	print_auth_security_headers();
	printf("Content-Type: application/json\r\n");
        printf("\r\n");

        printf(
            "{\"success\":true,\"message\":\"Logged out\"}\n"
        );

        return;
    }

    /*
     * Determine which user owns the session before deleting it.
     */
    int user_id = 0;
    char role[32] = {0};
	char authenticated_username[100] = {0};

	int validation_result =validate_session(
	    session_id,
	    &user_id,
	    authenticated_username,
	    sizeof(authenticated_username),
	    role,
	    sizeof(role)
	);

    if (validation_result == AUTH_SUCCESS) {

        /*
         * We don't currently expose the authenticated_username from
         * validate_session(), so use the user ID for
         * identifying the logout event.
         */
        destroy_session(session_id);

	log_auth_event(
	    user_id,
	    authenticated_username,
	    "LOGOUT",
	    1
	);

    } else {
        /*
         * Even if the session is already expired/invalid,
         * remove it if present.
         */
        destroy_session(session_id);
    }

    /*
     * Expire the browser cookie immediately.
     */
    printf(
	    "Set-Cookie: session_id=; "
	    "Max-Age=0; "
	    "Path=/CN_Sessional; "
	    "HttpOnly; "
	    "SameSite=Lax\r\n"
	);

	print_auth_security_headers();
	printf("Content-Type: application/json\r\n");
    printf("\r\n");

    printf(
        "{\"success\":true,\"message\":\"Logged out\"}\n"
    );
}

/*
 * Authenticate the current HTTP request.
 *
 * Returns:
 *   AUTH_SUCCESS          -> authenticated
 *   AUTH_INVALID_SESSION  -> no/invalid/expired session
 *   AUTH_DATABASE_ERROR   -> database problem
 *
 * On success:
 *   user_id receives the authenticated user's ID
 *   role receives the server-side role
 */
int authenticate_request(
    int *user_id,
    char *authenticated_username,
    size_t authenticated_username_size,
    char *role,
    size_t role_size
) {
    const char *cookie_header = getenv("HTTP_COOKIE");

    if (cookie_header == NULL) {
        return AUTH_INVALID_SESSION;
    }

    char session_id[SESSION_ID_HEX_LENGTH + 1] = {0};

    if (extract_session_id_from_cookie(
            cookie_header,
            session_id,
            sizeof(session_id)) != AUTH_SUCCESS) {
        return AUTH_INVALID_SESSION;
    }

    return validate_session(
        session_id,
        user_id,
        authenticated_username,
        authenticated_username_size,
        role,
        role_size
    );
}

// FIXED: Memory management with NULL checks
void handle_writer_authenticated(
    int authenticated_user_id,
    const char *authenticated_username,
    const char *authenticated_role
) {
    char *message = NULL;
    char *client_type = NULL;
    char *client_id = generate_client_id();
    long long start_time = current_timestamp();
    int success = 0;
    (void)authenticated_user_id;
    (void)authenticated_role;

    char *ignored_username = NULL;

    parse_post_data(
        &ignored_username,
        &message,
        &client_type
    );

    free(ignored_username);
    
    printf("Content-type: text/html\n\n");
    printf("<!DOCTYPE html>");
    printf("<html lang=\"en\" data-theme=\"light\">");
    printf("<head>");
    printf("<meta charset=\"UTF-8\">");
    printf("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
    printf("<title>Message Posted - Concurrent Chat System</title>");
    printf("<link rel=\"stylesheet\" href=\"/CN_Sessional/html/style.css\">");
    printf("</head><body>");
    printf("<div class=\"container\">");
    
    // Header
    printf("<header class=\"header\">");
    printf("<div class=\"header-content\">");
    printf("<a href=\"/CN_Sessional/html/index.html\" class=\"logo\">");
    printf("<span class=\"logo-icon\">✍️</span>");
    printf("Writer Mode");
    printf("</a>");
    printf("<div class=\"header-controls\">");
    printf("<button class=\"theme-toggle\" id=\"themeToggle\">");
    printf("<span class=\"theme-icon\">🌙</span>");
    printf("</button>");
    printf("<a href=\"/CN_Sessional/html/index.html\" class=\"btn btn-outline\">← Home</a>");
    printf("</div>");
    printf("</div>");
    printf("</header>");
    
    printf("<main class=\"main-content\">");
    
    if (message && strlen(message) > 0 &&
	    authenticated_username &&
	    strlen(authenticated_username) > 0) {
        // Acquire write lock
        if (acquire_write_lock(client_id) == SUCCESS) {
            // Write message to database
            if (write_message(authenticated_username, message) == SUCCESS) {
                success = 1;
                
                // SUCCESS UI
                printf("<div class=\"success-container\">");
                printf("<div class=\"success-animation\">");
                printf("<div class=\"success-icon\">🎉</div>");
                printf("<div class=\"checkmark\">✓</div>");
                printf("</div>");
                printf("<h1 class=\"success-title\">Message Posted Successfully!</h1>");
                printf("<p class=\"success-subtitle\">Your message has been added to the chat</p>");
                
                // Message Preview Card
                printf("<div class=\"message-preview\">");
                printf("<div class=\"preview-header\">Message Preview</div>");
                printf("<div class=\"preview-card\">");
                printf("<div class=\"preview-avatar\">%c</div>", authenticated_username[0] ? toupper(authenticated_username[0]) : 'U');
                printf("<div class=\"preview-content\">");

                char escaped_username[512];
		char escaped_message[1024];

		html_escape(authenticated_username,
			    escaped_username,
			    sizeof(escaped_username));

		html_escape(message,
			    escaped_message,
			    sizeof(escaped_message));
                printf("<div class=\"preview-authenticated_username\">%s</div>",
		       escaped_username);

		printf("<div class=\"preview-message\">%s</div>",
		       escaped_message);
                printf("<div class=\"preview-time\">Just now</div>");
                printf("</div>");
                printf("</div>");
                printf("</div>");
                
                // Stats and Actions
                printf("<div class=\"action-stats\">");
                printf("<div class=\"stat-item\">");
                printf("<div class=\"stat-number\" id=\"liveWriters\">1</div>");
                printf("<div class=\"stat-label\">Active Writer</div>");
                printf("</div>");
                printf("<div class=\"stat-item\">");
                printf("<div class=\"stat-number\">✓</div>");
                printf("<div class=\"stat-label\">Write Complete</div>");
                printf("</div>");
                printf("</div>");
                
                printf("</div>"); // Close success-container
                
            } else {
                printf("<div class=\"error-container\">");
                printf("<div class=\"error-icon\">❌</div>");
                printf("<h1 class=\"error-title\">Failed to Post Message</h1>");
                printf("<p class=\"error-subtitle\">There was an error saving your message to the database</p>");
                printf("</div>");
            }
            release_write_lock();
        } else {
            printf("<div class=\"error-container\">");
            printf("<div class=\"error-icon\">⏱️</div>");
            printf("<h1 class=\"error-title\">Write Lock Busy</h1>");
            printf("<p class=\"error-subtitle\">Another writer is currently posting. Please try again shortly.</p>");
            printf("</div>");
        }
    } else {
        printf("<div class=\"error-container\">");
        printf("<div class=\"error-icon\">📝</div>");
        printf("<h1 class=\"error-title\">Missing Information</h1>");
        printf("<p class=\"error-subtitle\">Please provide both authenticated_username and message to post.</p>");
        printf("</div>");
    }
    
    // Action Buttons
    printf("<div class=\"action-buttons\">");
    printf("<a href=\"/CN_Sessional/html/writer.html\" class=\"btn btn-secondary\">");
    printf("<span class=\"btn-icon\">✏️</span>");
    printf("Post Another Message");
    printf("</a>");
    printf("<a href=\"/CN_Sessional/html/reader.html\" class=\"btn btn-primary\">");
    printf("<span class=\"btn-icon\">👁️</span>");
    printf("View Messages");
    printf("</a>");
    printf("<a href=\"/CN_Sessional/html/index.html\" class=\"btn btn-outline\">");
    printf("<span class=\"btn-icon\">🏠</span>");
    printf("Back to Home");
    printf("</a>");
    printf("</div>");
    
    // Auto-redirect notice
    if (success) {
        printf("<div class=\"auto-redirect\">");
        printf("<p>⏱️ Auto-redirecting to reader view in <span id=\"countdown\">5</span> seconds...</p>");
        printf("<button onclick=\"cancelRedirect()\" class=\"btn btn-small btn-outline\" style=\"margin-top: 0.5rem;\">Cancel Auto-redirect</button>");
        printf("</div>");
    }
    
    printf("</main>");
    printf("</div>"); // Close container
    
    // JavaScript for auto-redirect and live updates
    printf("<script src=\"/CN_Sessional/html/script.js\"></script>");
    printf("<script>");
    
    if (success) {
        printf("// Auto-redirect after 5 seconds\n");
        printf("let countdown = 5;\n");
        printf("let redirectTimer;\n");
        printf("const countdownElement = document.getElementById('countdown');\n");
        printf("\n");
        printf("function startRedirect() {\n");
        printf("    redirectTimer = setInterval(function() {\n");
        printf("        countdown--;\n");
        printf("        if (countdownElement) countdownElement.textContent = countdown;\n");
        printf("        if (countdown <= 0) {\n");
        printf("            clearInterval(redirectTimer);\n");
        printf("            window.location.href = '/CN_Sessional/html/reader.html';\n");
        printf("        }\n");
        printf("    }, 1000);\n");
        printf("}\n");
        printf("\n");
        printf("function cancelRedirect() {\n");
        printf("    clearInterval(redirectTimer);\n");
        printf("    const redirectElement = document.querySelector('.auto-redirect');\n");
        printf("    if (redirectElement) {\n");
        printf("        redirectElement.innerHTML = '<p>✅ Auto-redirect cancelled</p>';\n");
        printf("    }\n");
        printf("}\n");
        printf("\n");
        printf("// Start the countdown when page loads\n");
        printf("document.addEventListener('DOMContentLoaded', function() {\n");
        printf("    startRedirect();\n");
        printf("});\n");
    }
    
    printf("// Update writer stats\n");
    printf("function updateWriterStats() {\n");
    printf("    fetch('/CN_Sessional/cgi-bin/server.cgi/status')\n");
    printf("        .then(response => response.json())\n");
    printf("        .then(data => {\n");
    printf("            if (!data.error) {\n");
    printf("                const writersElement = document.getElementById('liveWriters');\n");
    printf("                if (writersElement) {\n");
    printf("                    writersElement.textContent = data.activeWriters;\n");
    printf("                }\n");
    printf("            }\n");
    printf("        });\n");
    printf("}\n");
    printf("\n");
    printf("setInterval(updateWriterStats, 2000);\n");
    printf("updateWriterStats();\n");
    printf("\n");
    printf("// Theme toggle\n");
    printf("const themeToggle = document.getElementById('themeToggle');\n");
    printf("if (themeToggle) {\n");
    printf("    themeToggle.addEventListener('click', function() {\n");
    printf("        const currentTheme = document.documentElement.getAttribute('data-theme');\n");
    printf("        const newTheme = currentTheme === 'light' ? 'dark' : 'light';\n");
    printf("        document.documentElement.setAttribute('data-theme', newTheme);\n");
    printf("        localStorage.setItem('theme', newTheme);\n");
    printf("        this.querySelector('.theme-icon').textContent = newTheme === 'dark' ? '☀️' : '🌙';\n");
    printf("    });\n");
    printf("}\n");
    printf("</script>");
    
    printf("</body></html>");
    
    // Log access
    long long duration = current_timestamp() - start_time;
    log_access(client_id, "writer", "write", duration, success);
    
    // FIXED: Proper memory management with NULL checks
    free(client_id);
    if (message) free(message);
    if (client_type) free(client_type);
}

// FIXED handle_status_json function
void handle_status_json()
{
    int active_readers = 0;
    int active_writers = 0;
    int waiting_readers = 0;
    int waiting_writers = 0;

    int total_reads = 0;
    int total_writes = 0;

    printf("Content-type: application/json\n\n");

    if (get_current_stats(
            &active_readers,
            &active_writers,
            &total_reads,
            &total_writes
        ) != SUCCESS) {

        printf("{\"error\":\"Failed to retrieve statistics\"}\n");
        return;
    }

    if (get_sync_state(
            &active_readers,
            &active_writers,
            &waiting_readers,
            &waiting_writers
        ) != SUCCESS) {

        printf("{\"error\":\"Failed to retrieve synchronization state\"}\n");
        return;
    }

    printf("{\n");
    printf("  \"totalReads\": %d,\n", total_reads);
    printf("  \"totalWrites\": %d,\n", total_writes);
    printf("  \"activeReaders\": %d,\n", active_readers);
    printf("  \"activeWriters\": %d,\n", active_writers);
    printf("  \"waitingReaders\": %d,\n", waiting_readers);
    printf("  \"waitingWriters\": %d,\n", waiting_writers);
    printf("  \"activeNow\": %d\n",
           active_readers + active_writers);
    printf("}\n");
}
void handle_real_stats() {
    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: text/html\n\n");
    printf("<!DOCTYPE html>");
    printf("<html><head>");
    printf("<title>System Statistics - Analytics & Charts</title>");
    printf("<link rel='stylesheet' type='text/css' href='/CN_Sessional/html/style.css'>");
    printf("<meta charset='UTF-8'>");
    printf("<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>");
    printf("</head><body>");
    printf("<div class='container'>");
    printf("<header>");
    printf("<h1>System Statistics - Analytics & Charts</h1>");
    printf("<p>Historical Performance Data and Visualizations</p>");
    printf("</header>");
    
    printf("<div class='stats-section'>");
    
    // Get real stats from database
    const char *sql = "SELECT total_reads, total_writes FROM system_stats WHERE id = 1;";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int total_reads = sqlite3_column_int(stmt, 0);
            int total_writes = sqlite3_column_int(stmt, 1);
            int total_operations = total_reads + total_writes;
            float ratio = total_writes > 0 ? (float)total_reads/total_writes : 0;
            
            // Statistics Cards
            printf("<div class='stats-grid'>");
            
            printf("<div class='stat-card'>");
            printf("<h3>Total Reads</h3>");
            printf("<div class='stat-value'>%d</div>", total_reads);
            printf("<div class='stat-label'>All Reader Operations</div>");
            printf("</div>");
            
            printf("<div class='stat-card'>");
            printf("<h3>Total Writes</h3>");
            printf("<div class='stat-value'>%d</div>", total_writes);
            printf("<div class='stat-label'>All Writer Operations</div>");
            printf("</div>");
            
            printf("<div class='stat-card'>");
            printf("<h3>Total Operations</h3>");
            printf("<div class='stat-value'>%d</div>", total_operations);
            printf("<div class='stat-label'>Reads + Writes</div>");
            printf("</div>");
            
            printf("<div class='stat-card'>");
            printf("<h3>Read/Write Ratio</h3>");
            printf("<div class='stat-value'>%.1f:1</div>", ratio);
            printf("<div class='stat-label'>Reads per Write</div>");
            printf("</div>");
            
            printf("</div>"); // Close stats-grid
            
            // Interactive Charts Section
            printf("<div class='charts-section'>");
            printf("<h2>Operation Distribution</h2>");
            printf("<div style='display: flex; justify-content: center; margin: 20px 0;'>");
            printf("<div style='width: 100%%; max-width: 600px; height: 400px;'>");
            printf("<canvas id='operationsChart'></canvas>");
            printf("</div>");
            printf("</div>");
            printf("</div>"); // Close charts-section
            
            // JavaScript for Chart
            printf("<script>");
            printf("document.addEventListener('DOMContentLoaded', function() {");
            printf("    const ctx = document.getElementById('operationsChart').getContext('2d');");
            printf("    const operationsChart = new Chart(ctx, {");
            printf("        type: 'bar',");
            printf("        data: {");
            printf("            labels: ['Read Operations', 'Write Operations'],");
            printf("            datasets: [{");
            printf("                label: 'Number of Operations',");
            printf("                data: [%d, %d],", total_reads, total_writes);
            printf("                backgroundColor: ['#3498db', '#e74c3c'],");
            printf("                borderColor: ['#2980b9', '#c0392b'],");
            printf("                borderWidth: 2,");
            printf("                borderRadius: 5,");
            printf("                borderSkipped: false");
            printf("            }]");
            printf("        },");
            printf("        options: {");
            printf("            responsive: true,");
            printf("            maintainAspectRatio: false,");
            printf("            plugins: {");
            printf("                legend: {");
            printf("                    display: true,");
            printf("                    position: 'top',");
            printf("                    labels: {");
            printf("                        font: {");
            printf("                            size: 14");
            printf("                        }");
            printf("                    }");
            printf("                },");
            printf("                title: {");
            printf("                    display: true,");
            printf("                    text: 'Read vs Write Operations Distribution',");
            printf("                    font: {");
            printf("                        size: 16,");
                        printf("                        weight: 'bold'");
            printf("                    }");
            printf("                }");
            printf("            },");
            printf("            scales: {");
            printf("                y: {");
            printf("                    beginAtZero: true,");
            printf("                    title: {");
            printf("                        display: true,");
            printf("                        text: 'Number of Operations',");
            printf("                        font: {");
            printf("                            size: 14,");
            printf("                            weight: 'bold'");
            printf("                        }");
            printf("                    },");
            printf("                    ticks: {");
            printf("                        stepSize: 1,");
            printf("                        font: {");
            printf("                            size: 12");
            printf("                        }");
            printf("                    },");
            printf("                    grid: {");
            printf("                        color: 'rgba(0,0,0,0.1)'");
            printf("                    }");
            printf("                },");
            printf("                x: {");
            printf("                    title: {");
            printf("                        display: true,");
            printf("                        text: 'Operation Type',");
            printf("                        font: {");
            printf("                            size: 14,");
            printf("                            weight: 'bold'");
            printf("                        }");
            printf("                    },");
            printf("                    ticks: {");
            printf("                        font: {");
            printf("                            size: 12");
            printf("                        }");
            printf("                    },");
            printf("                    grid: {");
            printf("                        display: false");
            printf("                    }");
            printf("                }");
            printf("            },");
            printf("            animation: {");
            printf("                duration: 1000,");
            printf("                easing: 'easeOutQuart'");
            printf("            }");
            printf("        }");
            printf("    });");
            printf("});");
            printf("</script>");
            
            // Additional Visualizations
            printf("<div class='charts-section'>");
            printf("<h2>Performance Metrics</h2>");
            printf("<div class='stats-grid'>");
            
            // Read Operations Progress
            printf("<div class='stat-card'>");
            printf("<h3>Read Operations</h3>");
            printf("<div class='chart'>");
            printf("<div class='chart-label'>%d operations</div>", total_reads);
            printf("<div class='chart-bar'>");
            int read_width = total_operations > 0 ? (total_reads * 100 / total_operations) : 0;
            printf("<div class='bar-fill read-fill' style='width: %d%%;'></div>", read_width);
            printf("</div>");
            printf("</div>");
            printf("</div>");
            
            // Write Operations Progress
            printf("<div class='stat-card'>");
            printf("<h3>Write Operations</h3>");
            printf("<div class='chart'>");
            printf("<div class='chart-label'>%d operations</div>", total_writes);
            printf("<div class='chart-bar'>");
            int write_width = total_operations > 0 ? (total_writes * 100 / total_operations) : 0;
            printf("<div class='bar-fill write-fill' style='width: %d%%;'></div>", write_width);
            printf("</div>");
            printf("</div>");
            printf("</div>");
            
            printf("</div>"); // Close stats-grid
            printf("</div>"); // Close charts-section
        }
        sqlite3_finalize(stmt);
    } else {
        printf("<div class='error'>Failed to retrieve statistics from database</div>");
    }
    
    printf("<div class='navigation'>");
    printf("<a href='/CN_Sessional/cgi-bin/server.cgi/real_dashboard' class='btn btn-info'>Live Dashboard</a>");
    printf("<a href='/CN_Sessional/html/index.html' class='btn btn-secondary'>Back to Home</a>");
    printf("</div>");
    
    printf("</div>"); // Close stats-section
    printf("</div>"); // Close container
    printf("</body></html>");
    
    pthread_mutex_unlock(&db_mutex);
}


void handle_real_dashboard() {
    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: text/html\n\n");
    printf("<!DOCTYPE html>");
    printf("<html><head>");
    printf("<title>Live Dashboard - Real-time Monitoring</title>");
    printf("<link rel='stylesheet' type='text/css' href='/CN_Sessional/html/style.css'>");
    printf("<meta charset='UTF-8'>");
    printf("<meta http-equiv='refresh' content='5'>"); // Auto-refresh every 5 seconds
    printf("<style>");
    printf("body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f4f6f7; }");
    printf(".container { max-width: 1200px; margin: 0 auto; }");
    printf("header { text-align: center; margin-bottom: 30px; }");
    printf("h1 { color: #2c3e50; margin-bottom: 10px; }");
    printf(".dashboard-section { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }");
    printf(".live-indicator { background: #e74c3c; color: white; padding: 5px 10px; border-radius: 15px; font-size: 0.8em; animation: pulse 2s infinite; }");
    printf("@keyframes pulse { ");
    printf("0%% { opacity: 1; } ");
    printf("50%% { opacity: 0.5; } ");
    printf("100%% { opacity: 1; } ");
    printf("}");
    printf("</style>");
    printf("</head><body>");
    printf("<div class='container'>");
    printf("<header>");
    printf("<h1>Live Dashboard - Real-time Monitoring <span class='live-indicator'>LIVE</span></h1>");
    printf("<p>Auto-refreshing every 5 seconds - Last update: %ld</p>", time(NULL));
    printf("</header>");
    
    printf("<div class='dashboard-section'>");
    
    // Get real stats from database
    const char *sql = "SELECT total_reads, total_writes, current_readers, current_writers FROM system_stats WHERE id = 1;";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int total_reads = sqlite3_column_int(stmt, 0);
            int total_writes = sqlite3_column_int(stmt, 1);
            int current_readers = sqlite3_column_int(stmt, 2);
            int current_writers = sqlite3_column_int(stmt, 3);
            
            printf("<div class='metrics-grid'>");
            
            const char *reader_color = current_readers > 0 ? "#27ae60" : "#95a5a6";
            const char *writer_color = current_writers > 0 ? "#e74c3c" : "#95a5a6";
            const char *reader_status = current_readers > 0 ? "Currently Reading" : "No Active Readers";
            const char *writer_status = current_writers > 0 ? "Currently Writing" : "No Active Writers";
            
            printf("<div class='metric-card' style='border-left: 4px solid %s;'>", reader_color);
            printf("<h3>Active Readers Now</h3>");
            printf("<div class='metric-value'>%d</div>", current_readers);
            printf("<div class='metric-label'>%s</div>", reader_status);
            printf("</div>");
            
            printf("<div class='metric-card' style='border-left: 4px solid %s;'>", writer_color);
            printf("<h3>Active Writers Now</h3>");
            printf("<div class='metric-value'>%d</div>", current_writers);
            printf("<div class='metric-label'>%s</div>", writer_status);
            printf("</div>");
            
            printf("<div class='metric-card'>");
            printf("<h3>Total Reads Today</h3>");
            printf("<div class='metric-value'>%d</div>", total_reads);
            printf("<div class='metric-label'>All Reader Operations</div>");
            printf("</div>");
            
            printf("<div class='metric-card'>");
            printf("<h3>Total Writes Today</h3>");
            printf("<div class='metric-value'>%d</div>", total_writes);
            printf("<div class='metric-label'>All Writer Operations</div>");
            printf("</div>");
            
            printf("</div>"); // Close metrics-grid
            
            // System Status
            printf("<div class='status-section'>");
            printf("<h3>System Status</h3>");
            printf("<div class='status-indicators'>");
            printf("<div class='status-item online'>Database: Online</div>");
            printf("<div class='status-item online'>Synchronization: Active</div>");
            printf("<div class='status-item online'>CGI Server: Running</div>");
            printf("</div>");
            printf("</div>");
        }
        sqlite3_finalize(stmt);
    }
    
    printf("<div class='navigation'>");
    printf("<a href='/CN_Sessional/cgi-bin/server.cgi/real_stats' class='btn btn-info'>View Detailed Statistics</a> ");
    printf("<a href='/CN_Sessional/html/index.html' class='btn btn-secondary'>Back to Home</a>");
    printf("</div>");
    printf("</div>"); // Close dashboard-section
    printf("</div>"); // Close container
    printf("</body></html>");
    
    pthread_mutex_unlock(&db_mutex);
}

void handle_live_active_clients() {
    pthread_mutex_lock(&db_mutex);

    printf("Content-type: application/json\n\n");

    // ✅ Use raw timestamp as stored (no conversion)
    const char *sql =
        "SELECT strftime('%H:%M:%S', timestamp) AS local_ts, "
        "active_readers, active_writers "
        "FROM operation_history "
        "WHERE timestamp >= datetime('now', '-2 minutes') "
        "ORDER BY timestamp ASC;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        printf("{\"error\": \"SQL prepare failed: %s\"}", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return;
    }

    printf("{\n");
    printf("  \"timestamps\":[");

    int first = 1, row_count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *ts = (const char *)sqlite3_column_text(stmt, 0);
        if (!first) printf(",");
        first = 0;
        if (ts) printf("\"%s\"", ts);
        else {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char time_str[9];
            strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
            printf("\"%s\"", time_str);
        }
        row_count++;
    }
    printf("],\n");

    if (row_count == 0) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[9];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

        printf("  \"timestamps\":[\"%s\"],\n", time_str);
        printf("  \"active_readers\":[0],\n");
        printf("  \"active_writers\":[0]\n");
        printf("}\n");
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return;
    }

    // Readers
    sqlite3_reset(stmt);
    printf("  \"active_readers\":[");
    first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int readers = sqlite3_column_int(stmt, 1);
        if (!first) printf(",");
        first = 0;
        printf("%d", readers);
    }
    printf("],\n");

    // Writers
    sqlite3_reset(stmt);
    printf("  \"active_writers\":[");
    first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int writers = sqlite3_column_int(stmt, 2);
        if (!first) printf(",");
        first = 0;
        printf("%d", writers);
    }
    printf("]\n");

    printf("}\n");
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
}


// Add this debug function to server.c
void debug_operation_history() {
    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: text/plain\n\n");
    printf("DEBUG: Operation History Contents (Last 60 seconds):\n");
    
    const char *sql = "SELECT datetime(timestamp, 'localtime'), active_readers, active_writers FROM operation_history WHERE timestamp >= datetime('now', '-60 seconds') ORDER BY timestamp DESC LIMIT 10;";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *ts = (const char *)sqlite3_column_text(stmt, 0);
            int readers = sqlite3_column_int(stmt, 1);
            int writers = sqlite3_column_int(stmt, 2);
            printf("%s - Readers: %d, Writers: %d\n", ts ? ts : "NULL", readers, writers);
            count++;
        }
        if (count == 0) {
            printf("NO RECORDS FOUND in last 60 seconds!\n");
        }
        sqlite3_finalize(stmt);
    } else {
        printf("SQL ERROR: %s\n", sqlite3_errmsg(db));
    }
    
    pthread_mutex_unlock(&db_mutex);
}

// Add this function in server.c after debug_operation_history() function
void debug_operation_history_timezone() {
    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: text/plain\n\n");
    printf("Operation History - Timezone Analysis:\n\n");
    
    // Check latest operation_history entries
    const char *sql = "SELECT timestamp, datetime(timestamp, 'localtime') as local_ts, active_readers, active_writers FROM operation_history ORDER BY timestamp DESC LIMIT 10;";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *timestamp = (const char *)sqlite3_column_text(stmt, 0);
            const char *local_ts = (const char *)sqlite3_column_text(stmt, 1);
            int readers = sqlite3_column_int(stmt, 2);
            int writers = sqlite3_column_int(stmt, 3);
            
            printf("STORED: %s -> LOCAL: %s | Readers: %d, Writers: %d\n", 
                   timestamp ? timestamp : "NULL", 
                   local_ts ? local_ts : "NULL",
                   readers, writers);
        }
        sqlite3_finalize(stmt);
    }
    
    pthread_mutex_unlock(&db_mutex);
}

void debug_daily_load() {
    pthread_mutex_lock(&db_mutex);
    
    printf("Content-type: text/plain\n\n");
    printf("Daily Load - Raw Data Analysis (Last 7 days):\n\n");
    
    const char *sql = 
        "SELECT "
        "strftime('%w', timestamp) as day_of_week, "
        "strftime('%Y-%m-%d', timestamp) as date, "
        "active_readers, "
        "active_writers, "
        "(active_readers * 10 + active_writers * 30) as load_score "
        "FROM operation_history "
        "WHERE timestamp >= datetime('now', '-7 days') "
        "ORDER BY timestamp DESC "
        "LIMIT 50;";
    
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int day = sqlite3_column_int(stmt, 0);
            const char *date = (const char *)sqlite3_column_text(stmt, 1);
            int readers = sqlite3_column_int(stmt, 2);
            int writers = sqlite3_column_int(stmt, 3);
            int load = sqlite3_column_int(stmt, 4);
            
            printf("Day %d (%s): Readers=%d, Writers=%d, Load=%d\n", 
                   day, date ? date : "NULL", readers, writers, load);
            count++;
        }
        if (count == 0) {
            printf("NO DATA FOUND in last 7 days!\n");
        }
        sqlite3_finalize(stmt);
    }
    
    pthread_mutex_unlock(&db_mutex);
}

/*
 * Handle GET /me
 *
 * Returns information about the currently authenticated user.
 */
void handle_me() {
    const char *cookie_header = getenv("HTTP_COOKIE");

    char session_id[SESSION_ID_HEX_LENGTH + 1] = {0};
    int user_id = 0;
    char username[100] = {0};
    char role[32] = {0};

    /*
     * No cookie means the client is not authenticated.
     */
    if (cookie_header == NULL ||
        extract_session_id_from_cookie(
            cookie_header,
            session_id,
            sizeof(session_id)
        ) != AUTH_SUCCESS) {

        printf("Status: 401 Unauthorized\r\n");
	print_auth_security_headers();
	printf("Content-Type: application/json\r\n");
	printf("\r\n");

        printf(
            "{\"authenticated\":false,"
            "\"error\":\"Authentication required\"}\n"
        );

        return;
    }

    /*
     * Validate the server-side session.
     *
     * IMPORTANT:
     * Username and role come from the database/session,
     * not from the client.
     */
    int result = validate_session(
        session_id,
        &user_id,
        username,
        sizeof(username),
        role,
        sizeof(role)
    );

    if (result != AUTH_SUCCESS) {

        printf("Status: 401 Unauthorized\r\n");
	print_auth_security_headers();
	printf("Content-Type: application/json\r\n");
	printf("\r\n");

        printf(
            "{\"authenticated\":false,"
            "\"error\":\"Invalid or expired session\"}\n"
        );

        return;
    }

    /*
     * Successfully authenticated.
     */
    print_auth_security_headers();
	printf("Content-Type: application/json\r\n");
	printf("\r\n");

    printf(
        "{\"authenticated\":true,"
        "\"user_id\":%d,"
        "\"username\":\"%s\","
        "\"role\":\"%s\"}\n",
        user_id,
        username,
        role
    );
}

// FIXED: Enhanced main function with database resilience
int main() {

	    /*
	     * Initialize libsodium before using any cryptographic
	     * functionality such as password verification or
	     * cryptographically secure session-token generation.
	     */
	    if (sodium_init() < 0) {
		fprintf(stderr, "libsodium initialization failed\n");

		printf("Status: 500 Internal Server Error\r\n");
		printf("Content-Type: application/json\r\n");
		printf("\r\n");

		printf(
		    "{\"success\":false,\"error\":\"Cryptographic initialization failed\"}\n"
		);

		return 1;
	    }
    // Initialize database with retry mechanism
    int retries = 3;
    int db_success = 0;
    
    while (retries > 0 && !db_success) {
        if (initialize_database() == SUCCESS) {
            db_success = 1;
            fprintf(stderr, "Database initialized successfully\n");
        } else {
            retries--;
            if (retries > 0) {
                fprintf(stderr, "Database initialization failed, retrying in 2 seconds... (%d attempts left)\n", retries);
                sleep(2);
            }
        }
    }
    
    if (!db_success) {
        printf("Content-type: text/html\n\n");
        printf("<html><body><h1>Database initialization failed after multiple attempts</h1>");
        printf("<p>Please check the database file and try again.</p></body></html>");
        return 1;
    }

    /*
     * Remove expired server-side sessions.
     *
     * Expired sessions are already rejected by validate_session(),
     * but stale database rows should also be removed so the sessions
     * table does not grow indefinitely.
     *
     * Cleanup failure is non-fatal because authentication correctness
     * is still enforced by validate_session().
     */
    if (cleanup_expired_sessions() != AUTH_SUCCESS) {
        fprintf(stderr, "Warning: expired-session cleanup failed\n");
    }
    if (sync_manager_init() != SUCCESS) {
	    printf("Status: 500 Internal Server Error\r\n");
	    printf("Content-Type: application/json\r\n");
	    printf("\r\n");
	    printf(
		"{\"success\":false,\"error\":\"Synchronization initialization failed\"}\n"
	    );

	    close_database();
	    return 1;
	}
    
    // FIXED: Add periodic database health checks for long-running processes
    // For CGI, this is less critical since processes are short-lived,
    // but good practice for future scalability
    
    // Get the requested path
    char *path_info = getenv("PATH_INFO");
    char *request_method = getenv("REQUEST_METHOD");
    
    // FIXED: Check database health before processing requests
    if (check_database_health() != SUCCESS) {
        fprintf(stderr, "Database health check failed, attempting reconnection...\n");
        if (ensure_database_connection() != SUCCESS) {
            printf("Content-type: text/html\n\n");
            printf("<html><body><h1>Database connection lost</h1>");
            printf("<p>Please try again later.</p></body></html>");
            close_database();
            return 1;
        }
    }
    
    if (path_info == NULL) {
    	// Default to reader view
    	handle_reader();

	} else if (strcmp(path_info, "/reader") == 0) {
	    handle_reader();

	} else if (strcmp(path_info, "/login") == 0 &&
		   request_method &&
		   strcmp(request_method, "POST") == 0) {
	    handle_login();

	} else if (strcmp(path_info, "/logout") == 0 &&
		   request_method &&
		   strcmp(request_method, "POST") == 0) {
	    handle_logout();

	} else if (strcmp(path_info, "/me") == 0 &&
		   request_method &&
		   strcmp(request_method, "GET") == 0) {
	    handle_me();

	} else if (strcmp(path_info, "/writer") == 0 &&
           request_method &&
           strcmp(request_method, "POST") == 0) {

	    int authenticated_user_id = 0;

	    char authenticated_username[100] = {0};
	    char authenticated_role[32] = {0};

	    int auth_result = authenticate_request(
		&authenticated_user_id,
		authenticated_username,
		sizeof(authenticated_username),
		authenticated_role,
		sizeof(authenticated_role)
	    );

	    if (auth_result == AUTH_INVALID_SESSION) {

	    /*
	     * Authentication failed: no valid session.
	     *
	     * The identity is unknown, so user_id=0 and username=""
	     * are recorded.
	     */
	    log_auth_event(
		0,
		"",
		"WRITER_AUTH_FAILED",
		0
	    );

	    printf("Status: 401 Unauthorized\r\n");
	    printf("Content-Type: application/json\r\n");
	    printf("\r\n");

	    printf(
		"{\"success\":false,\"error\":\"Authentication required\"}\n"
	    );

	} else if (auth_result == AUTH_DATABASE_ERROR) {

	    /*
	     * Authentication could not be completed because the
	     * authentication/database service failed.
	     */
	    log_auth_event(
		authenticated_user_id,
		authenticated_username,
		"WRITER_AUTH_DB_ERROR",
		0
	    );

	    printf("Status: 500 Internal Server Error\r\n");
	    printf("Content-Type: application/json\r\n");
	    printf("\r\n");

	    printf(
		"{\"success\":false,\"error\":\"Authentication service unavailable\"}\n"
	    );

	} else if (!is_writer_or_admin(authenticated_role)) {

	    /*
	     * The user is authenticated, but does not have permission
	     * to use the writer functionality.
	     */
	    log_auth_event(
		authenticated_user_id,
		authenticated_username,
		"WRITER_FORBIDDEN",
		0
	    );

	    printf("Status: 403 Forbidden\r\n");
	    printf("Content-Type: application/json\r\n");
	    printf("\r\n");

	    printf(
		"{\"success\":false,\"error\":\"Writer authorization required\"}\n"
	    );

	} else {

	    /*
	     * Authentication and authorization succeeded.
	     */
	    log_auth_event(
		authenticated_user_id,
		authenticated_username,
		"WRITER_AUTHORIZED",
		1
	    );

	    handle_writer_authenticated(
		authenticated_user_id,
		authenticated_username,
		authenticated_role
	    );
	}
	    } else if (strcmp(path_info, "/real_dashboard") == 0) {
		handle_real_dashboard();
	    } else if (strcmp(path_info, "/real_stats") == 0) {
		handle_real_stats();
	    } else if (strcmp(path_info, "/status") == 0) {
		handle_status_json();
	    } else if (strcmp(path_info, "/historical") == 0) {
		handle_historical_data();
	    } else if (strcmp(path_info, "/concurrency-stats") == 0) {
		handle_concurrency_stats();
	    } else if (strcmp(path_info, "/daily-load") == 0) {
		handle_daily_load();
	    } else if (strcmp(path_info, "/performance") == 0) {
		handle_performance_metrics();
	    } else if (strcmp(path_info, "/debug-history") == 0) {
	        debug_operation_history(); 
	    } else if (strcmp(path_info, "/debug-op-timezone") == 0) {
	        debug_operation_history_timezone();
	    } else if (strcmp(path_info, "/debug-daily-load") == 0) {
	        debug_daily_load();
	    } else if (strcmp(path_info, "/live-active-clients") == 0) {
		 handle_live_active_clients(); 
   	    } else if (strcmp(path_info, "/health") == 0) {
		// FIXED: Add health check endpoint
		printf("Content-type: application/json\n\n");
		if (check_database_health() == SUCCESS) {
		    printf("{\"status\": \"healthy\", \"database\": \"connected\"}\n");
		}else {
		    printf("{\"status\": \"degraded\", \"database\": \"disconnected\"}\n");
		}
	    } else {
		printf("Content-type: text/html\n\n");
		printf("<html><body><h1>404 - Page Not Found</h1></body></html>");
	    }
    
    close_database();
    return 0;
}
