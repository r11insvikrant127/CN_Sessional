#include "auth.h"
#include "database.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <sodium.h>

/*
 * Authenticate a user.
 *
 * The password received from the client is NEVER stored.
 * The stored password_hash is verified using libsodium's
 * Argon2id-based password hashing implementation.
 */
int authenticate_user(
    const char *username,
    const char *password,
    int *user_id,
    char *role,
    size_t role_size
) {
    if (username == NULL ||
        password == NULL ||
        user_id == NULL ||
        role == NULL ||
        role_size == 0) {
        return AUTH_INPUT_ERROR;
    }

    pthread_mutex_lock(&db_mutex);

    if (db == NULL) {
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    const char *sql =
        "SELECT id, password_hash, role "
        "FROM users "
        "WHERE username = ?;";

    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "Authentication SQL prepare failed: %s\n",
            sqlite3_errmsg(db)
        );

        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    sqlite3_bind_text(
        stmt,
        1,
        username,
        -1,
        SQLITE_TRANSIENT
    );

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);

        /*
         * Do not reveal whether the username exists.
         */
        return AUTH_INVALID_CREDENTIALS;
    }

    *user_id = sqlite3_column_int(stmt, 0);

    const char *stored_hash =
        (const char *)sqlite3_column_text(stmt, 1);

    const char *stored_role =
        (const char *)sqlite3_column_text(stmt, 2);

    if (stored_hash == NULL || stored_role == NULL) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    /*
     * Verify password using Argon2id.
     */
    int password_valid =
        crypto_pwhash_str_verify(
            stored_hash,
            password,
            strlen(password)
        );

    if (password_valid != 0) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);

        return AUTH_INVALID_CREDENTIALS;
    }

    /*
     * Copy role safely.
     */
    snprintf(
        role,
        role_size,
        "%s",
        stored_role
    );

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return AUTH_SUCCESS;
}


/*
 * Generate a cryptographically secure 256-bit session token.
 *
 * 32 random bytes = 256 bits.
 * Each byte is represented by two hexadecimal characters.
 *
 * Result:
 * 64 hexadecimal characters + '\0'
 */
int generate_session_id(
    char *session_id,
    size_t session_id_size
) {
    if (session_id == NULL ||
        session_id_size < SESSION_ID_HEX_LENGTH + 1) {
        return AUTH_INPUT_ERROR;
    }

    unsigned char random_bytes[32];

    /*
     * Generate cryptographically secure random bytes.
     */
    randombytes_buf(random_bytes, sizeof(random_bytes));

    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        snprintf(
            session_id + (i * 2),
            3,
            "%02x",
            random_bytes[i]
        );
    }

    session_id[SESSION_ID_HEX_LENGTH] = '\0';

    return AUTH_SUCCESS;
}


/*
 * Create a session in SQLite.
 */
int create_session(
    int user_id,
    char *session_id,
    size_t session_id_size
) {
    if (user_id <= 0 ||
        session_id == NULL ||
        session_id_size < SESSION_ID_HEX_LENGTH + 1) {
        return AUTH_INPUT_ERROR;
    }

    int result = generate_session_id(
        session_id,
        session_id_size
    );

    if (result != AUTH_SUCCESS) {
        return result;
    }

    pthread_mutex_lock(&db_mutex);

    if (db == NULL) {
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    /*
     * Use SQLite's local time consistently with the existing
     * database schema.
     */
    const char *sql =
        "INSERT INTO sessions "
        "(session_id, user_id, created_at, expires_at) "
        "VALUES (?, ?, datetime('now', 'localtime'), "
        "datetime('now', 'localtime', '+30 minutes'));";

    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "Session SQL prepare failed: %s\n",
            sqlite3_errmsg(db)
        );

        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    sqlite3_bind_text(
        stmt,
        1,
        session_id,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(
        stmt,
        2,
        user_id
    );

    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    if (rc != SQLITE_DONE) {
        fprintf(
            stderr,
            "Session creation failed: %s\n",
            sqlite3_errmsg(db)
        );

        return AUTH_SESSION_ERROR;
    }

    return AUTH_SUCCESS;
}


/*
 * Validate a session and retrieve the user's current role.
 *
 * IMPORTANT:
 *
 * The browser only supplies the session ID.
 *
 * The role is retrieved from:
 *
 * session_id
 *     ↓
 * sessions.user_id
 *     ↓
 * users.role
 *
 * Therefore a browser cannot simply submit:
 *
 * role=admin
 *
 * and become an administrator.
 */
int validate_session(
    const char *session_id,
    int *user_id,
    char *username,
    size_t username_size,
    char *role,
    size_t role_size
) {
    if (session_id == NULL ||
    user_id == NULL ||
    username == NULL ||
    username_size == 0 ||
    role == NULL ||
    role_size == 0 ||
    strlen(session_id) != SESSION_ID_HEX_LENGTH){
        return AUTH_INVALID_SESSION;
    }

    pthread_mutex_lock(&db_mutex);

    if (db == NULL) {
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    const char *sql =
	    "SELECT users.id, users.username, users.role "
	    "FROM sessions "
	    "JOIN users ON users.id = sessions.user_id "
	    "WHERE sessions.session_id = ? "
	    "AND sessions.expires_at > datetime('now', 'localtime');";

    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "Session validation SQL prepare failed: %s\n",
            sqlite3_errmsg(db)
        );

        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    sqlite3_bind_text(
        stmt,
        1,
        session_id,
        -1,
        SQLITE_TRANSIENT
    );

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);

        return AUTH_INVALID_SESSION;
    }

    *user_id = sqlite3_column_int(stmt, 0);

	const char *stored_username =
	    (const char *)sqlite3_column_text(stmt, 1);

	const char *stored_role =
	    (const char *)sqlite3_column_text(stmt, 2);

	if (stored_username == NULL || stored_role == NULL) {
	    sqlite3_finalize(stmt);
	    pthread_mutex_unlock(&db_mutex);
	    return AUTH_DATABASE_ERROR;
	}

	snprintf(
	    username,
	    username_size,
	    "%s",
	    stored_username
	);

	snprintf(
	    role,
	    role_size,
	    "%s",
	    stored_role
	);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return AUTH_SUCCESS;
}


/*
 * Destroy a session.
 */
int destroy_session(const char *session_id) {
    if (session_id == NULL ||
        strlen(session_id) != SESSION_ID_HEX_LENGTH) {
        return AUTH_INVALID_SESSION;
    }

    pthread_mutex_lock(&db_mutex);

    if (db == NULL) {
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    const char *sql =
        "DELETE FROM sessions "
        "WHERE session_id = ?;";

    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "Logout SQL prepare failed: %s\n",
            sqlite3_errmsg(db)
        );

        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    sqlite3_bind_text(
        stmt,
        1,
        session_id,
        -1,
        SQLITE_TRANSIENT
    );

    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    if (rc != SQLITE_DONE) {
        return AUTH_DATABASE_ERROR;
    }

    return AUTH_SUCCESS;
}



/*
 * Remove expired sessions from the database.
 *
 * Expired sessions are already rejected by validate_session().
 * This cleanup additionally removes stale rows so the sessions
 * table does not grow indefinitely.
 */
int cleanup_expired_sessions(void) {
    pthread_mutex_lock(&db_mutex);

    if (db == NULL) {
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    const char *sql =
        "DELETE FROM sessions "
        "WHERE expires_at <= datetime('now', 'localtime');";

    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "Expired-session cleanup SQL prepare failed: %s\n",
            sqlite3_errmsg(db)
        );

        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(
            stderr,
            "Expired-session cleanup failed: %s\n",
            sqlite3_errmsg(db)
        );

        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    int deleted = sqlite3_changes(db);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    if (deleted > 0) {
        fprintf(
            stderr,
            "Expired-session cleanup: removed %d session(s)\n",
            deleted
        );
    }

    return AUTH_SUCCESS;
}


/*
 * Record authentication/security events.
 */
int log_auth_event(
    int user_id,
    const char *username,
    const char *action,
    int success
) {
    if (username == NULL || action == NULL) {
        return AUTH_INPUT_ERROR;
    }

    pthread_mutex_lock(&db_mutex);

    if (db == NULL) {
        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    const char *sql =
        "INSERT INTO auth_logs "
        "(user_id, username, action, success) "
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "Auth log SQL prepare failed: %s\n",
            sqlite3_errmsg(db)
        );

        pthread_mutex_unlock(&db_mutex);
        return AUTH_DATABASE_ERROR;
    }

    /*
     * For failed logins the user ID may be unknown.
     * SQLite NULL is used in that case.
     */
    if (user_id > 0) {
        sqlite3_bind_int(stmt, 1, user_id);
    } else {
        sqlite3_bind_null(stmt, 1);
    }

    sqlite3_bind_text(
        stmt,
        2,
        username,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        3,
        action,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(
        stmt,
        4,
        success ? 1 : 0
    );

    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return (rc == SQLITE_DONE)
        ? AUTH_SUCCESS
        : AUTH_DATABASE_ERROR;
}


/*
 * Extract session_id from a Cookie header.
 *
 * Example:
 *
 * Cookie: session_id=abcdef123...; other=value
 */
int extract_session_id_from_cookie(
    const char *cookie_header,
    char *session_id,
    size_t session_id_size
) {
    if (cookie_header == NULL ||
        session_id == NULL ||
        session_id_size < SESSION_ID_HEX_LENGTH + 1) {
        return AUTH_INPUT_ERROR;
    }

    const char *name = "session_id=";

    const char *start = strstr(
        cookie_header,
        name
    );

    if (start == NULL) {
        return AUTH_INVALID_SESSION;
    }

    start += strlen(name);

    size_t i = 0;

    while (
        start[i] != '\0' &&
        start[i] != ';' &&
        !isspace((unsigned char)start[i]) &&
        i < SESSION_ID_HEX_LENGTH
    ) {
        session_id[i] = start[i];
        i++;
    }

    session_id[i] = '\0';

    if (i != SESSION_ID_HEX_LENGTH) {
        session_id[0] = '\0';
        return AUTH_INVALID_SESSION;
    }

    /*
     * Session IDs generated by us contain only hexadecimal
     * characters. Reject anything else.
     */
    for (i = 0; i < SESSION_ID_HEX_LENGTH; i++) {
        if (!isxdigit((unsigned char)session_id[i])) {
            session_id[0] = '\0';
            return AUTH_INVALID_SESSION;
        }
    }

    return AUTH_SUCCESS;
}


/*
 * Authorization helper.
 */
int is_writer_or_admin(const char *role) {
    if (role == NULL) {
        return 0;
    }

    return (
        strcmp(role, "writer") == 0 ||
        strcmp(role, "admin") == 0
    );
}
