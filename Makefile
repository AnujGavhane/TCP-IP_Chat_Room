CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude
LDLIBS = -lcrypt

.PHONY: all clean

all: server_app client_app

server_app: server/server.c server/database.c common/protocol.c include/common.h include/user.h
	$(CC) $(CFLAGS) -pthread server/server.c server/database.c common/protocol.c -o server_app $(LDLIBS)

client_app: client/client.c common/protocol.c include/common.h include/user.h
	$(CC) $(CFLAGS) -pthread client/client.c common/protocol.c -o client_app

clean:
	rm -f server_app client_app
