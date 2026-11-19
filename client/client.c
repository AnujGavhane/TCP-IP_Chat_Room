/* Minimal TCP client for Phase 1: connect and exchange one line. */
#include "common.h"
#include "user.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void *receive_updates(void *argument)
{
    int fd = *(int *)argument;
    char line[MAX_MESSAGE];
    int result;
    while ((result = receive_line(fd, line, sizeof(line))) > 0) {
        printf("\n%s\n", line);
        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *server_ip = argc > 1 ? argv[1] : "127.0.0.1";
    int socket_fd;
    struct sockaddr_in server_address;
    char reply[MAX_MESSAGE];
    char username[USERNAME_LENGTH], password[PASSWORD_LENGTH], request[MAX_MESSAGE];

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [server-ip]\n", argv[0]);
        return 1;
    }

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", server_ip);
        close(socket_fd);
        return 1;
    }

    printf("Connecting to %s:%d...\n", server_ip, SERVER_PORT);
    if (connect(socket_fd, (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {
        perror("connect");
        close(socket_fd);
        return 1;
    }
    puts("Connected.");

    int choice;
    printf("1) Register  2) Login\nChoice: ");
    if (scanf("%d", &choice) != 1 || (choice != 1 && choice != 2)) { close(socket_fd); return 1; }
    printf("Username: ");
    if (scanf("%31s", username) != 1) { close(socket_fd); return 1; }
    printf("Register password: ");
    if (scanf("%127s", password) != 1) { close(socket_fd); return 1; }
    snprintf(request, sizeof(request), "%s %s %s\n", choice == 1 ? "SIGNUP" : "SIGNIN", username, password);
    if (send_line(socket_fd, request) < 0) {
        perror("send");
        close(socket_fd);
        return 1;
    }

    int result = receive_line(socket_fd, reply, sizeof(reply));
    if (result < 0) {
        perror("recv");
        close(socket_fd);
        return 1;
    }
    if (result == 0) {
        puts("Server disconnected without replying.");
        close(socket_fd);
        return 1;
    }

    printf("Server says: %s\n", reply);
    if (strcmp(reply, "LOGIN_SUCCESS") == 0) {
        pthread_t receiver;
        int thread_error = pthread_create(&receiver, NULL, receive_updates, &socket_fd);
        if (thread_error != 0) {
            fprintf(stderr, "Could not start receive thread (%d).\n", thread_error);
            close(socket_fd);
            return 1;
        }
        char input[MAX_MESSAGE];
        int logging_out = 0;
        while (!logging_out) {
            printf("Enter /msg username message, /all message, or /logout: ");
            fflush(stdout);
            if (fgets(input, sizeof(input), stdin) == NULL) {
                /* Close the server-side session before joining the receiver. */
                (void)send_line(socket_fd, "LOGOUT\n");
                logging_out = 1;
                continue;
            }
            input[strcspn(input, "\r\n")] = '\0';
            if (strcmp(input, "/logout") == 0) {
                (void)send_line(socket_fd, "LOGOUT\n");
                logging_out = 1;
            } else if (strncmp(input, "/msg ", 5) == 0) {
                char *recipient = input + 5;
                char *space = strchr(recipient, ' ');
                if (space == NULL || space == recipient || space[1] == '\0') {
                    puts("Usage: /msg username message");
                    continue;
                }
                *space++ = '\0';
                char request[MAX_MESSAGE];
                int length = snprintf(request, sizeof(request), "PRIVATE %s %s\n", recipient, space);
                if (length < 0 || (size_t)length >= sizeof(request)) puts("Message too long.");
                else (void)send_line(socket_fd, request);
            } else if (strncmp(input, "/all ", 5) == 0 && input[5] != '\0') {
                char request[MAX_MESSAGE];
                int length = snprintf(request, sizeof(request), "GROUP %s\n", input + 5);
                if (length < 0 || (size_t)length >= sizeof(request)) puts("Message too long.");
                else (void)send_line(socket_fd, request);
            } else {
                puts("Unknown command.");
            }
        }
        (void)pthread_join(receiver, NULL);
    }
    close(socket_fd);
    return 0;
}
