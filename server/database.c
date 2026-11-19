/* Read, write, and authenticate accounts in the text database. */
#include "user.h"

#include <stdio.h>
#include <string.h>
#include <crypt.h>
#include <fcntl.h>
#include <unistd.h>

static int hash_password(const char *password, char output[PASSWORD_LENGTH])
{
    /* A random salt gives the same password a different saved hash each time. */
    unsigned char random_bytes[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    ssize_t got = read(fd, random_bytes, sizeof(random_bytes));
    close(fd);
    if (got != (ssize_t)sizeof(random_bytes)) return -1;
    char salt[64] = "$6$rounds=100000$";
    size_t at = strlen(salt);
    for (size_t i = 0; i < sizeof(random_bytes); ++i)
        at += (size_t)snprintf(salt + at, sizeof(salt) - at, "%02x", random_bytes[i]);
    strncat(salt, "$", sizeof(salt) - strlen(salt) - 1);
    struct crypt_data data = {0};
    char *result = crypt_r(password, salt, &data);
    if (!result || strlen(result) >= PASSWORD_LENGTH) return -1;
    snprintf(output, PASSWORD_LENGTH, "%s", result);
    return 0;
}

int load_users(User users[], int capacity)
{
    /* Read account records and upgrade older plaintext passwords if needed. */
    FILE *file = fopen(USER_DATABASE_PATH, "r");
    if (file == NULL) {
        return 0; /* A missing database is an empty database. */
    }

    int count = 0;
    char username[USERNAME_LENGTH];
    char password[PASSWORD_LENGTH];
    while (count < capacity && fscanf(file, "%31s %127s", username, password) == 2) {
        snprintf(users[count].username, sizeof(users[count].username), "%s", username);
        if (strncmp(password, "$6$", 3) == 0) {
            snprintf(users[count].password, sizeof(users[count].password), "%s", password);
        } else {
            /* Upgrade legacy plaintext records to salted hashes on load. */
            if (hash_password(password, users[count].password) != 0) { fclose(file); return -1; }
        }
        users[count].online = 0;
        users[count].socket_fd = -1;
        ++count;
    }
    fclose(file);
    if (save_users(users, count) != 0) return -1;
    return count;
}

int save_users(const User users[], int count)
{
    /* Save one username and salted password hash per line. */
    FILE *file = fopen(USER_DATABASE_PATH, "w");
    if (file == NULL) return -1;
    for (int i = 0; i < count; ++i) {
        if (fprintf(file, "%s %s\n", users[i].username, users[i].password) < 0) {
            fclose(file);
            return -1;
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}

int find_user(const User users[], int count, const char *username)
{
    for (int i = 0; i < count; ++i)
        if (strcmp(users[i].username, username) == 0) return i;
    return -1;
}

int register_user(User users[], int *count, const char *username, const char *password)
{
    /* Check the new account, hash its password, and save it. */
    if (count == NULL || username == NULL || password == NULL ||
        username[0] == '\0' || password[0] == '\0' ||
        strlen(username) >= USERNAME_LENGTH || strlen(password) >= PASSWORD_LENGTH ||
        strchr(username, ' ') != NULL || strchr(password, ' ') != NULL ||
        *count >= MAX_USERS || find_user(users, *count, username) >= 0)
        return -1;

    User user = {0};
    snprintf(user.username, sizeof(user.username), "%s", username);
    if (hash_password(password, user.password) != 0) return -1;
    user.socket_fd = -1;
    users[*count] = user;
    ++(*count);
    if (save_users(users, *count) != 0) {
        --(*count);
        return -1;
    }
    return 0;
}

int authenticate_user(const User users[], int count, const char *username, const char *password)
{
    /* Check a supplied password against the saved salted hash. */
    int index = find_user(users, count, username);
    if (index < 0) return -1;
    struct crypt_data data = {0};
    char *computed = crypt_r(password, users[index].password, &data);
    return computed && strcmp(computed, users[index].password) == 0 ? index : -1;
}
