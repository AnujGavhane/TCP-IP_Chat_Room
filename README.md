# TCP/IP Chat Room (C / Linux)

Command-line IPv4 TCP chat application for Ubuntu 24.04 under WSL. The server accepts multiple clients using one detached POSIX worker thread per connection. Accounts are stored in `database/users.txt`; passwords are stored as salted SHA-512 `crypt` hashes.

## Objectives and current features

- Practice TCP sockets, message framing, client/server design, Pthreads and mutexes.
- Register accounts, reject duplicate usernames, and authenticate users.
- Prevent a second active login for the same account.
- Show the online list and notify users with `username JOINED` / `username LEFT`.
- Send private messages and broadcast group messages.
- Clear online status on logout or unexpected disconnect.

## Technologies and architecture

C11, GCC, Make, Linux IPv4 sockets, TCP, POSIX threads, mutexes, and the system `libcrypt` library. TCP is a byte stream, so the protocol uses newline-terminated messages. `send_line()` retries partial writes; `receive_line()` reads through a newline. Credentials are hashed on registration and legacy plaintext database entries are upgraded when loaded.

The server loads the database at startup, accepts connections on port 6333, and starts a detached worker for each client. A user-data mutex protects account and online state. Socket writes are serialized separately. The client has a receiver thread so incoming messages and presence events display while the user types.

## Directory structure

```text
include/common.h       Shared port, limits, and framing declarations
include/user.h         User record and database API
common/protocol.c      Line send/receive helpers
server/server.c        Concurrent server, login, presence, and message routing
server/database.c      Account persistence and password hashing
client/client.c        Interactive terminal client
database/users.txt     Persistent account records (keep private)
Makefile               Builds server_app and client_app
```

## Build

In Ubuntu WSL:

```bash
cd /mnt/c/Users/Anuj/Desktop/TCP_IP_Chat_Room
make clean
make
```

The server build links `-pthread` and `-lcrypt`. Compiler flags include `-Wall -Wextra -Wpedantic`.

## Run

Terminal 1:

```bash
./server_app
```

Terminal 2 (repeat in additional terminals for other users):

```bash
./client_app 127.0.0.1
```

Choose `1` to register or `2` to log in, then enter username and password. Use `/msg username message` for a private message, `/all message` for a group message, and `/logout` to leave. For another LAN computer, pass the server machine's reachable IPv4 address instead of `127.0.0.1` and allow TCP port 6333 through its firewall.

## Protocol examples

```text
SIGNUP alice passphrase
SIGNIN alice passphrase
PRIVATE bob hello Bob
GROUP hello everyone
LOGOUT
```

Server replies include `REGISTER_SUCCESS`, `DUPLICATE_OR_INVALID`, `LOGIN_SUCCESS`, `LOGIN_FAILED`, `USER_ALREADY_ONLINE`, `PRIVATE_SENT`, `USER_OFFLINE`, and `LOGOUT_SUCCESS`. Logged-in clients also receive `ONLINE ...`, `username JOINED`, `username LEFT`, `PRIVATE sender: message`, and `GROUP sender: message` lines.

## Test procedure

1. Start the server and register two different users from separate client runs.
2. Try registering one username again; expect `DUPLICATE_OR_INVALID`.
3. Try a wrong password and unknown username; each should fail login.
4. Log in as both accounts in separate terminals. Confirm the online list and `JOINED` notice.
5. Try logging in a second time as an already-online account; expect `USER_ALREADY_ONLINE`.
6. Send `/msg other_user hello`; confirm only the recipient sees the private message. Try an offline username; expect `USER_OFFLINE`.
7. Send `/all hello everyone`; confirm all logged-in clients see it.
8. `/logout`; confirm other clients see `LEFT`, then log in again.
9. Close a logged-in client abruptly; confirm its account becomes available again and peers see `LEFT`.
10. Restart the server and confirm registered accounts still log in.

## Limitations

- The client is a minimal terminal interface; it does not provide an editable chat history or a menu-driven conversation view.
- The database is a simple text file and is intended for a learning project. Restrict its filesystem permissions.
- The protocol sends credentials over TCP without TLS; use only on trusted test networks.
- Salted SHA-512 via `crypt` is better than plaintext but a modern password KDF such as Argon2id or bcrypt is preferable for a production service.
- Abrupt server termination relies on the operating system to close sockets; a graceful server shutdown path is not yet implemented.
