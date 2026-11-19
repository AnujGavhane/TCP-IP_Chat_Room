/* Shared Phase 1 helpers for newline-delimited TCP messages. */
#include "common.h"

#include <errno.h>
#include <sys/socket.h>

int send_line(int socket_fd, const char *line)
{
    size_t remaining = 0;
    while (line[remaining] != '\0') {
        ++remaining;
    }

    const char *cursor = line;
    while (remaining > 0) {
        ssize_t sent = send(socket_fd, cursor, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        cursor += sent;
        remaining -= (size_t)sent;
    }
    return 0;
}

int receive_line(int socket_fd, char *buffer, size_t capacity)
{
    size_t used = 0;
    if (capacity < 2) {
        return -1;
    }

    while (used + 1 < capacity) {
        char byte;
        ssize_t received = recv(socket_fd, &byte, 1, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            if (used == 0) {
                return 0; /* Peer closed before sending another line. */
            }
            break; /* Return a final line even if its newline was omitted. */
        }
        if (byte == '\n') {
            break;
        }
        buffer[used++] = byte;
    }

    buffer[used] = '\0';
    return 1;
}
