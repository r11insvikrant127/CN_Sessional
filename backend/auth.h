#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>

/*
 * Authentication result codes
 */
#define AUTH_SUCCESS 0
#define AUTH_INVALID_CREDENTIALS 1
#define AUTH_DATABASE_ERROR 2
#define AUTH_SESSION_ERROR 3
#define AUTH_INVALID_SESSION 4
#define AUTH_UNAUTHORIZED 5
#define AUTH_INPUT_ERROR 6

/*
 * Session configuration
 */
#define SESSION_ID_HEX_LENGTH 64
#define SESSION_LIFETIME_MINUTES 30

/*
 * Authenticate a user using username and plaintext password.
 *
 * On success:
 *   - user_id receives the database user ID
 *   - role receives "reader", "writer", or "admin"
 *
 * Returns:
 *   AUTH_SUCCESS
 *   AUTH_INVALID_CREDENTIALS
 *   AUTH_DATABASE_ERROR
 */
int authenticate_user(
    const char *username,
    const char *password,
    int *user_id,
    char *role,
    size_t role_size
);

/*
 * Create a new authenticated session for a user.
 *
 * session_id receives a 64-character hexadecimal token.
 *
 * Returns:
 *   AUTH_SUCCESS
 *   AUTH_DATABASE_ERROR
 *   AUTH_SESSION_ERROR
 */
int create_session(
    int user_id,
    char *session_id,
    size_t session_id_size
);

/*
 * Validate an existing session.
 *
 * On success:
 *   - user_id receives the associated user ID
 *   - role receives the user's current role from the database
 *
 * Returns:
 *   AUTH_SUCCESS
 *   AUTH_INVALID_SESSION
 *   AUTH_DATABASE_ERROR
 */
int validate_session(
    const char *session_id,
    int *user_id,
    char *username,
    size_t username_size,
    char *role,
    size_t role_size
);
/*
 * Delete a session during logout.
 */
int destroy_session(const char *session_id);

/*
 * Remove expired sessions from the sessions table.
 */
int cleanup_expired_sessions(void);

/*
 * Record authentication/security events.
 */
int log_auth_event(
    int user_id,
    const char *username,
    const char *action,
    int success
);

/*
 * Generate a cryptographically secure session ID.
 */
int generate_session_id(
    char *session_id,
    size_t session_id_size
);

/*
 * Extract session_id from an HTTP Cookie header.
 *
 * Example:
 *   Cookie: session_id=abc123; other=value
 *
 * session_id receives the extracted token.
 */
int extract_session_id_from_cookie(
    const char *cookie_header,
    char *session_id,
    size_t session_id_size
);

/*
 * Check whether a role is allowed to modify the database.
 *
 * Only writer and admin are authorized.
 */
int is_writer_or_admin(const char *role);

#endif
