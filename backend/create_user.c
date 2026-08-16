#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <sodium.h>

#include "config.h"

#define MAX_USERNAME 50
#define MAX_PASSWORD 256

int main(void) {
    char username[MAX_USERNAME + 1];
    char password[MAX_PASSWORD + 1];
    char password_hash[crypto_pwhash_STRBYTES];

    printf("Username: ");

    if (fgets(username, sizeof(username), stdin) == NULL) {
        fprintf(stderr, "Failed to read username.\n");
        return 1;
    }

    username[strcspn(username, "\n")] = '\0';

    if (username[0] == '\0') {
        fprintf(stderr, "Username cannot be empty.\n");
        return 1;
    }

    printf("Password: ");

    if (fgets(password, sizeof(password), stdin) == NULL) {
        fprintf(stderr, "Failed to read password.\n");
        return 1;
    }

    password[strcspn(password, "\n")] = '\0';

    if (password[0] == '\0') {
        fprintf(stderr, "Password cannot be empty.\n");
        return 1;
    }

    printf("Role (reader/writer/admin): ");

    char role[16];

    if (fgets(role, sizeof(role), stdin) == NULL) {
        fprintf(stderr, "Failed to read role.\n");
        return 1;
    }

    role[strcspn(role, "\n")] = '\0';

    if (strcmp(role, "reader") != 0 &&
        strcmp(role, "writer") != 0 &&
        strcmp(role, "admin") != 0) {
        fprintf(stderr, "Invalid role.\n");
        return 1;
    }

    if (sodium_init() < 0) {
        fprintf(stderr, "libsodium initialization failed.\n");
        return 1;
    }

    /*
     * Generate an Argon2id password hash.
     *
     * The plaintext password is never stored.
     */
    if (crypto_pwhash_str(
            password_hash,
            password,
            strlen(password),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE
        ) != 0) {

        fprintf(stderr, "Password hashing failed.\n");
        return 1;
    }

    sqlite3 *db = NULL;

    int rc = sqlite3_open(
        DATABASE_FILE,
        &db
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "Cannot open database: %s\n",
            sqlite3_errmsg(db)
        );

        if (db != NULL) {
            sqlite3_close(db);
        }

        return 1;
    }

    const char *sql =
        "INSERT INTO users "
        "(username, password_hash, role) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt *stmt = NULL;

    rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK) {
        fprintf(
            stderr,
            "SQL prepare failed: %s\n",
            sqlite3_errmsg(db)
        );

        sqlite3_close(db);
        return 1;
    }

    sqlite3_bind_text(
        stmt,
        1,
        username,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        2,
        password_hash,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        3,
        role,
        -1,
        SQLITE_TRANSIENT
    );

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(
            stderr,
            "Failed to create user: %s\n",
            sqlite3_errmsg(db)
        );

        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /*
     * Clear plaintext password from memory as far as possible.
     */
    sodium_memzero(password, sizeof(password));

    printf(
        "User '%s' created successfully with role '%s'.\n",
        username,
        role
    );

    return 0;
}
