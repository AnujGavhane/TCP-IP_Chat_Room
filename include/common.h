/* Shared helpers for the Phase 1 line-based TCP protocol. */
#ifndef CHAT_COMMON_H
#define CHAT_COMMON_H

#include <stddef.h>

#define SERVER_PORT 6333
#define MAX_MESSAGE 1024

/* Send every byte in a string, including its terminating newline. */
int send_line(int socket_fd, const char *line);

/* Read one newline-terminated message, leaving room for a final '\0'. */
int receive_line(int socket_fd, char *buffer, size_t capacity);

#endif
