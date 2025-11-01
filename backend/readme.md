# 💬 Backend System — Concurrent Chat Application

This backend system implements a **concurrent chat application** with:
- Reader–Writer synchronization  
- Persistent SQLite database storage  
- Real-time monitoring and analytics  

---

## 🏗️ Architecture Overview

### ⚙️ Core Components

#### 1. Synchronization Manager (`sync_manager.h` / `sync_manager.c`)
- Implements Reader–Writer locks using **pthread mutexes**  
- Manages concurrent access to shared resources  
- Tracks active readers and writers in real time  

#### 2. Database Layer (`database.h` / `database.c`)
- SQLite-based data persistence  
- Thread-safe database operations with mutex protection  
- Connection pooling and health monitoring  
- Performance metrics and historical data collection  

#### 3. CGI Server (`server.c`)
- Handles **HTTP requests via CGI**  
- Supports multiple endpoints  
- Provides **real-time statistics and monitoring**  
- Exposes a **JSON API** for frontend integration  

---

## 🔑 Key Functions

### 📁 `server.c`

#### 1. Request Handler Functions

| Function | Description |
|-----------|--------------|
| `main()` | Main CGI entry point with routing and database initialization. |
| `handle_reader()` | Processes reader requests and displays messages. |
| `handle_writer()` | Handles writer message submissions with synchronization. |
| `handle_status_json()` | Provides real-time statistics via JSON API. |
| `handle_historical_data()` | Returns historical operation data for charts. |
| `handle_concurrency_stats()` | Provides concurrency distribution data. |
| `handle_daily_load()` | Returns daily system load patterns. |
| `handle_performance_metrics()` | Provides performance metrics for analytics. |
| `handle_real_stats()` | Displays detailed statistics HTML page. |
| `handle_real_dashboard()` | Shows live dashboard with real-time monitoring. |
| `handle_live_active_clients()` | Provides real-time active client data for charts. |

---

#### 2. Utility Functions

| Function | Description |
|-----------|--------------|
| `current_timestamp()` | Gets the current timestamp in milliseconds. |
| `parse_post_data()` | Extracts and decodes form data from POST requests. |
| `generate_client_id()` | Creates unique identifiers for client tracking. |
| `print_html_header()` | Outputs standardized HTML header. |
| `print_html_footer()` | Outputs standardized HTML footer. |

---

#### 3. Debug Functions

| Function | Description |
|-----------|--------------|
| `debug_operation_history()` | Outputs debug information for operation history. |
| `debug_operation_history_timezone()` | Debugs timezone handling and data consistency. |
| `debug_daily_load()` | Analyzes and prints daily load distribution for testing. |

---

### 📁 `database.c`

#### 1. Database Management

| Function | Description |
|-----------|--------------|
| `initialize_database()` | Creates tables, enables WAL mode, and sets up indexes. |
| `close_database()` | Safely closes database connection. |
| `check_database_health()` | Verifies database connectivity and validity. |
| `ensure_database_connection()` | Implements reconnection logic with retries. |

---

#### 2. Core Operations

| Function | Description |
|-----------|--------------|
| `read_messages(FILE *output)` | Retrieves and displays chat messages in HTML. |
| `write_message(const char *username, const char *message)` | Stores new messages (with URL decoding). |
| `log_access(const char *client_id, ...)` | Records all system access for auditing. |
| `update_stats()` | Updates system statistics counters. |
| `get_current_stats()` | Retrieves current system statistics for real-time updates. |

---

#### 3. Analytics & Monitoring

| Function | Description |
|-----------|--------------|
| `update_active_counts(int readers, int writers)` | Tracks real-time reader/writer counts. |
| `log_operation_metrics()` | Logs operation metrics for historical tracking. |
| `calculate_performance_metrics()` | Computes overall performance and stability metrics. |

---

#### 4. Helper Functions

| Function | Description |
|-----------|--------------|
| `url_decode()` | Decodes URL-encoded strings safely. |

---

### 📁 `sync_manager.c`

#### 1. Synchronization Functions

| Function | Description |
|-----------|--------------|
| `acquire_read_lock(const char *client_id)` | Implements reader lock acquisition using mutex. |
| `release_read_lock()` | Safely releases reader locks and updates counters. |
| `acquire_write_lock(const char *client_id)` | Grants exclusive write access using mutex. |
| `release_write_lock()` | Releases write lock and resumes waiting operations. |
| `update_active_counts_in_db()` | Synchronizes in-memory active counts with the database. |

---

## ✅ Summary

This backend provides:
- 🔒 **Safe concurrency** with Reader–Writer synchronization  
- 💾 **SQLite persistence** with real-time updates  
- 📊 **Operational analytics** for readers/writers and throughput  
- 🌐 **CGI-based web endpoints** for dashboards and APIs  

---

**Author:** Indranil Das  
**Language:** C (CGI, SQLite, pthreads)  
**License:** MIT  
**Platform:** Linux (Apache CGI Environment)

