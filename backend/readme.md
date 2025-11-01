Backend System - Concurrent Chat Application
This backend system implements a concurrent chat application with reader-writer synchronization, database persistence, and real-time monitoring capabilities.

🏗️ Architecture Overview
Core Components
1. Synchronization Manager (sync_manager.h/c)
  a. Implements reader-writer locks using pthread mutexes
  b. Manages concurrent access to shared resources
  c. Tracks active readers and writers

2. Database Layer (database.h/c)
  a. SQLite-based data persistence
  b. Thread-safe database operations
  c. Connection pooling and health monitoring
  d. Performance metrics collection

3. CGI Server (server.c)
  a. HTTP request handling via CGI
  b. Multiple endpoint support
  c. Real-time statistics and monitoring
  d. JSON API for frontend integration


Key Functions 

📁 server.c 

1. Request Handler Functions 

  a. main() - Main CGI entry point with routing and database initialization 
  b. handle_reader() - Processes reader requests and displays messages 
  c. handle_writer() - Handles writer message submissions with synchronization 
  d. handle_status_json() - Provides real-time statistics via JSON API 
  e. handle_historical_data() - Returns historical operation data for charts 
  f. handle_concurrency_stats() - Provides concurrency distribution data 
  g. handle_daily_load() - Returns daily system load patterns 
  h. handle_performance_metrics() - Provides performance metrics for analytics 
  i. handle_real_stats() - Displays detailed statistics HTML page 
  j. handle_real_dashboard() - Shows live dashboard with real-time monitoring 
  g. handle_live_active_clients() - Provides real-time active client data for charts 

2. Utility Functions 

  a. current_timestamp() - Gets current timestamp in milliseconds 
  b. parse_post_data() - Extracts and decodes form data from POST requests 
  c. generate_client_id() - Creates unique identifiers for client tracking 
  d. print_html_header() - Outputs standardized HTML header 
  e. print_html_footer() - Outputs standardized HTML footer 

3. Debug Functions 

  a. debug_operation_history() - Debug output for operation history 
  b. debug_operation_history_timezone() - Debug timezone analysis 
  c. debug_daily_load() - Debug daily load data analysis 

 

📁 database.c 

1. Database Management 

  a. initialize_database() - Creates tables, enables WAL mode, sets up indexes 
  b. close_database() - Safely closes database connection 
  c. check_database_health() - Verifies database connectivity 
  d. ensure_database_connection() - Implements reconnection logic with retries 

2. Core Operations 

  a. read_messages(FILE *output) - Retrieves and displays chat messages in HTML 
  b. write_message(const char *username, const char *message) - Stores new messages with URL decoding 
  c. log_access(const char *client_id, ...) - Records all system access for auditing 
  d. update_stats() - Updates system statistics counters 
  e. get_current_stats() - Retrieves current system statistics for real-time updates 

3. Analytics & Monitoring 

  a. update_active_counts(int readers, int writers) - Tracks real-time reader/writer counts 
  b. log_operation_metrics() - Logs operation metrics for historical data 
  c. calculate_performance_metrics() - Computes system performance scores 

4. Helper Functions 

  a. url_decode() - Decodes URL-encoded strings 

 

📁 sync_manager.c 

1. Synchronization Functions 

  a. acquire_read_lock(const char *client_id) - Implements reader lock acquisition with mutex 
  b. release_read_lock() - Safely releases reader locks and updates counters 
  c. acquire_write_lock(const char *client_id) - Provides exclusive write access using mutex 
  d. release_write_lock() - Releases write lock and allows waiting operations 
  e. update_active_counts_in_db() - Synchronizes memory state with database. 
