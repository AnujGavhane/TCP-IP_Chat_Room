/* TCP chat server: one worker thread handles each connected client. */
#include "common.h"
#include "user.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static User users[MAX_USERS];
static int user_count;
static pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t writes_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Keep writes from different worker threads from mixing on one socket. */
static int safe_send(int fd, const char *line)
{
    pthread_mutex_lock(&writes_mutex);
    int result = send_line(fd, line);
    pthread_mutex_unlock(&writes_mutex);
    return result;
}

/* Build the online-user reply while the shared user list is locked. */
static void send_online_list(int fd)
{
    char line[MAX_MESSAGE] = "ONLINE ";
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < user_count; ++i) {
        if (users[i].online) {
            size_t used = strlen(line);
            if (used + strlen(users[i].username) + 2 < sizeof(line)) {
                snprintf(line + used, sizeof(line) - used, "%s%s",
                         used > 7 ? "," : "", users[i].username);
            }
        }
    }
    pthread_mutex_unlock(&users_mutex);
    strncat(line, "\n", sizeof(line) - strlen(line) - 1);
    (void)safe_send(fd, line);
}

/* Copy active socket numbers under lock, then send after unlocking. */
static void broadcast_presence(const char *line, int excluded_fd)
{
    int sockets[MAX_USERS];
    int n = 0;
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < user_count; ++i)
        if (users[i].online && users[i].socket_fd >= 0 && users[i].socket_fd != excluded_fd)
            sockets[n++] = users[i].socket_fd;
    pthread_mutex_unlock(&users_mutex);
    for (int i = 0; i < n; ++i) (void)safe_send(sockets[i], line);
}

/* Handle one connection from authentication until logout or disconnect. */
static void *serve_client(void *argument)
{
    int fd = *(int *)argument;
    free(argument);
    char message[MAX_MESSAGE];
    char operation[16] = {0}, username[USERNAME_LENGTH] = {0};
    char password[PASSWORD_LENGTH] = {0};
    int active_user = -1;
    int received = receive_line(fd, message, sizeof(message));
    if (received <= 0) {
        if (received < 0) perror("recv request");
        else puts("Client disconnected before sending a request.");
        goto done;
    }

    /* The first message must request signup or signin. */
    int fields = sscanf(message, "%15s %31s %127s", operation, username, password);
    const char *response = "INVALID_REQUEST\n";
    if (fields == 3 && strcmp(operation, "SIGNUP") == 0) {
        pthread_mutex_lock(&users_mutex);
        int rc = register_user(users, &user_count, username, password);
        pthread_mutex_unlock(&users_mutex);
        response = rc == 0 ? "REGISTER_SUCCESS\n" : "DUPLICATE_OR_INVALID\n";
        printf("Registration for '%s': %s", username, response);
    } else if (fields == 3 && strcmp(operation, "SIGNIN") == 0) {
        pthread_mutex_lock(&users_mutex);
        int index = authenticate_user(users, user_count, username, password);
        if (index < 0) response = "LOGIN_FAILED\n";
        else if (users[index].online) response = "USER_ALREADY_ONLINE\n";
        else {
            users[index].online = 1;
            users[index].socket_fd = fd;
            active_user = index;
            response = "LOGIN_SUCCESS\n";
        }
        pthread_mutex_unlock(&users_mutex);
        printf("Login for '%s': %s", username, response);
    } else {
        printf("Rejected malformed/unknown request: %s\n", message);
    }

    if (safe_send(fd, response) < 0) { perror("send response"); goto done; }
    fflush(stdout);
    if (active_user < 0) goto done;
    send_online_list(fd);
    char notice[MAX_MESSAGE];
    snprintf(notice, sizeof(notice), "%s JOINED\n", username);
    broadcast_presence(notice, fd);
    /* Keep an authenticated socket open until explicit logout or peer close. */
    while ((received = receive_line(fd, message, sizeof(message))) > 0) {
        if (strcmp(message, "LOGOUT") == 0) {
            (void)safe_send(fd, "LOGOUT_SUCCESS\n");
            printf("User '%s' logged out.\n", username);
            break;
        }
        /* Route a private message to the named online user. */
        if (strncmp(message, "PRIVATE ", 8) == 0) {
            char recipient[USERNAME_LENGTH] = {0};
            char *text = message + 8;
            char *separator = strchr(text, ' ');
            if (separator == NULL || separator == text || separator[1] == '\0') {
                (void)safe_send(fd, "PRIVATE_ERROR usage: PRIVATE username message\n");
                continue;
            }
            size_t name_length = (size_t)(separator - text);
            if (name_length >= sizeof(recipient)) {
                (void)safe_send(fd, "PRIVATE_ERROR invalid username\n");
                continue;
            }
            memcpy(recipient, text, name_length);
            *separator++ = '\0';
            int recipient_fd = -1;
            pthread_mutex_lock(&users_mutex);
            int recipient_index = find_user(users, user_count, recipient);
            if (recipient_index >= 0 && users[recipient_index].online)
                recipient_fd = users[recipient_index].socket_fd;
            pthread_mutex_unlock(&users_mutex);
            if (recipient_fd < 0) {
                (void)safe_send(fd, "USER_OFFLINE\n");
                continue;
            }
            char delivery[MAX_MESSAGE];
            snprintf(delivery, sizeof(delivery), "PRIVATE %s: %s\n", username, separator);
            if (safe_send(recipient_fd, delivery) == 0)
                (void)safe_send(fd, "PRIVATE_SENT\n");
            else
                (void)safe_send(fd, "PRIVATE_ERROR delivery failed\n");
            continue;
        }
        /* Send a group message to every online socket. */
        if (strncmp(message, "GROUP ", 6) == 0) {
            char *text = message + 6;
            if (*text == '\0') {
                (void)safe_send(fd, "GROUP_ERROR empty message\n");
                continue;
            }
            char delivery[MAX_MESSAGE];
            snprintf(delivery, sizeof(delivery), "GROUP %s: %s\n", username, text);
            int recipients[MAX_USERS];
            int recipient_count = 0;
            pthread_mutex_lock(&users_mutex);
            for (int i = 0; i < user_count; ++i) {
                if (users[i].online && users[i].socket_fd >= 0)
                    recipients[recipient_count++] = users[i].socket_fd;
            }
            pthread_mutex_unlock(&users_mutex);
            int failed = 0;
            for (int i = 0; i < recipient_count; ++i)
                if (safe_send(recipients[i], delivery) < 0) failed = 1;
            if (failed) (void)safe_send(fd, "GROUP_PARTIAL_FAILURE\n");
            continue;
        }
        (void)safe_send(fd, "INVALID_REQUEST\n");
    }

done:
    /* Clear the account's online state on logout or disconnect. */
    if (active_user >= 0) {
        char notice[MAX_MESSAGE];
        snprintf(notice, sizeof(notice), "%s LEFT\n", username);
        pthread_mutex_lock(&users_mutex);
        users[active_user].online = 0;
        users[active_user].socket_fd = -1;
        pthread_mutex_unlock(&users_mutex);
        broadcast_presence(notice, -1);
        printf("Session for '%s' closed; account is offline.\n", username);
    }
    close(fd);
    return NULL;
}

int main(void)
{
    int server_fd;
    struct sockaddr_in address;
    const int reuse_address = 1;
    user_count = load_users(users, MAX_USERS);
    if (user_count < 0) { fputs("Could not load user database.\n", stderr); return 1; }
    (void)signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) < 0) { perror("setsockopt"); close(server_fd); return 1; }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(SERVER_PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(server_fd, 5) < 0) { perror("bind/listen"); close(server_fd); return 1; }

    printf("Server listening on port %d...\n", SERVER_PORT);
    /* Accept connections continuously; each worker serves one client. */
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { perror("accept"); continue; }
        int *client_argument = malloc(sizeof(*client_argument));
        if (client_argument == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        *client_argument = client_fd;
        pthread_t worker;
        int thread_error = pthread_create(&worker, NULL, serve_client, client_argument);
        if (thread_error != 0) {
            errno = thread_error;
            perror("pthread_create");
            free(client_argument);
            close(client_fd);
            continue;
        }
        int detach_error = pthread_detach(worker);
        if (detach_error != 0) {
            errno = detach_error;
            perror("pthread_detach; joining worker");
            int join_error = pthread_join(worker, NULL);
            if (join_error != 0) {
                errno = join_error;
                perror("pthread_join");
            }
        }
        puts("Client connected; worker started.");
        fflush(stdout);
    }
}
