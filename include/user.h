#ifndef CHAT_USER_H
#define CHAT_USER_H

#define MAX_USERS 100
#define USERNAME_LENGTH 32
#define PASSWORD_LENGTH 128
#define USER_DATABASE_PATH "database/users.txt"

typedef struct {
    char username[USERNAME_LENGTH];
    /* Store the salted password hash, never the original password. */
    char password[PASSWORD_LENGTH];
    int online;
    int socket_fd;
} User;

int load_users(User users[], int capacity);
int save_users(const User users[], int count);
int find_user(const User users[], int count, const char *username);
int register_user(User users[], int *count, const char *username, const char *password);
int authenticate_user(const User users[], int count, const char *username, const char *password);

#endif
