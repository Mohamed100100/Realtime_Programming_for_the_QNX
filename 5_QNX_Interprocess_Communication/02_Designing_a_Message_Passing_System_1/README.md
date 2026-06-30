# QNX Message Passing Interface Design

## Overview

This section covers how to design a robust message passing interface between client and server processes in QNX. A well-designed interface uses shared header files with typed message structures, avoids conflicts with QNX system messages, and follows a clear branching pattern on the server side.

---

## 1. Message Types and Structures

### Concept

Every message in QNX starts with a **message type** — an unsigned 16-bit integer (`uint16_t`) that identifies what kind of message is being sent. The server uses this type to determine how to process the message.

```
┌─────────────────────────────────────────────────────────┐
│                    Message Layout                       │
├─────────────────────────────────────────────────────────┤
│  uint16_t type    │  Message type identifier (0-65535)  │
├─────────────────────────────────────────────────────────┤
│  struct body      │  Message-specific data follows      │
│  (varies by type) │                                     │
└─────────────────────────────────────────────────────────┘
```

### Header File Design

Create a **shared header file** included by both client and server:

```c
// msg_types.h
// Shared header for client and server

#ifndef MSG_TYPES_H
#define MSG_TYPES_H

#include <stdint.h>
#include <sys/neutrino.h>

// ============================================================
// Message Type Ranges
// ============================================================
// QNX system messages: 0 to _IO_MAX (511)
// User-defined messages: 512 to 65000 (recommended)
// ============================================================

#define MSG_BASE        0x0200   // 512 — start of user message range

// Message Types
#define MSG_TYPE_ECHO       (MSG_BASE + 0)   // Simple echo request
#define MSG_TYPE_CHECKSUM   (MSG_BASE + 1)   // Calculate checksum
#define MSG_TYPE_REVERSE    (MSG_BASE + 2)   // Reverse string
#define MSG_TYPE_STATS      (MSG_BASE + 3)   // Get server statistics

// ============================================================
// Message Structures (Client → Server)
// ============================================================

// Common header — every message starts with this
struct msg_header {
    uint16_t type;      // Message type
    uint16_t subtype;   // Optional subtype for related messages
};

// MSG_TYPE_ECHO: Client sends a string, server echoes it back
struct msg_echo {
    struct msg_header hdr;
    char text[256];     // String to echo
};

// MSG_TYPE_CHECKSUM: Client sends a string, server returns checksum
struct msg_checksum {
    struct msg_header hdr;
    char text[256];     // String to calculate checksum on
};

// MSG_TYPE_REVERSE: Client sends a string, server returns reversed
struct msg_reverse {
    struct msg_header hdr;
    char text[256];     // String to reverse
};

// MSG_TYPE_STATS: Client requests server statistics (no payload needed)
struct msg_stats {
    struct msg_header hdr;
    // No additional data — just a request
};

// ============================================================
// Reply Structures (Server → Client)
// ============================================================

// Reply for MSG_TYPE_ECHO
struct reply_echo {
    char text[256];     // Echoed string
};

// Reply for MSG_TYPE_CHECKSUM
struct reply_checksum {
    uint32_t checksum;  // Calculated checksum
};

// Reply for MSG_TYPE_REVERSE
struct reply_reverse {
    char text[256];     // Reversed string
};

// Reply for MSG_TYPE_STATS
struct reply_stats {
    uint32_t messages_received;   // Total messages processed
    uint32_t messages_replied;      // Total replies sent
    uint32_t uptime_seconds;        // Server uptime
};

// ============================================================
// Union for All Messages (optional but useful)
// ============================================================

union msg_union {
    struct msg_header       hdr;
    struct msg_echo         echo;
    struct msg_checksum     checksum;
    struct msg_reverse      reverse;
    struct msg_stats        stats;
};

union reply_union {
    struct reply_echo       echo;
    struct reply_checksum   checksum;
    struct reply_reverse    reverse;
    struct reply_stats      stats;
};

#endif // MSG_TYPES_H
```

---

## 2. Message Type Range: Why Avoid 0–511?

### QNX System Message Range

| Range                  | Used By                   | Examples                                                |
| ---------------------- | ------------------------- | ------------------------------------------------------- |
| `0` to `_IO_MAX` (511) | QNX system libraries      | `read()`, `write()`, `open()`, `procmgr_event_notify()` |
| `512` to `65000`       | **User-defined messages** | Your application messages                               |
| `65001` to `65535`     | Reserved                  | Future QNX use                                          |

### Why Avoid the System Range?

| Reason                   | Explanation                                                  |
| ------------------------ | ------------------------------------------------------------ |
| **Event tracing**        | QNX debugging tools (e.g., `tracelogger`, IDE trace views) interpret messages in the 0–511 range as system I/O messages. Your data will display incorrectly. |
| **Accidental delivery**  | A system message (e.g., from `read()`) could be sent to your server. Using a distinct range lets you safely ignore unrecognized messages. |
| **Future compatibility** | QNX may add new system message types. Staying above 512 avoids collisions. |

### Recommended Practice

```c
#define MSG_BASE    0x0200   // 512 — safe starting point

// All your message types start from MSG_BASE
#define MSG_TYPE_FOO    (MSG_BASE + 0)
#define MSG_TYPE_BAR    (MSG_BASE + 1)
```

---

## 3. Using Message Types and Subtypes

### When to Use Subtypes

Use **subtypes** when you have a family of related messages that share a common structure:

```c
// File operation messages — all related, use subtype
#define MSG_TYPE_FILE     (MSG_BASE + 10)

#define FILE_SUBTYPE_OPEN     1
#define FILE_SUBTYPE_READ     2
#define FILE_SUBTYPE_WRITE    3
#define FILE_SUBTYPE_CLOSE    4

struct msg_file {
    struct msg_header hdr;      // type = MSG_TYPE_FILE
    uint16_t subtype;           // OPEN, READ, WRITE, CLOSE
    uint32_t fd;                // File descriptor (for READ/WRITE/CLOSE)
    uint32_t offset;            // Read/write offset
    uint32_t length;            // Number of bytes
    char data[512];             // Data buffer
};
```

**Server branching:**

```c
struct msg_file *msg = (struct msg_file *)recv_buffer;

switch (msg->hdr.type) {
    case MSG_TYPE_FILE:
        switch (msg->subtype) {
            case FILE_SUBTYPE_OPEN:  handle_open(rcvid, msg);  break;
            case FILE_SUBTYPE_READ:  handle_read(rcvid, msg);  break;
            case FILE_SUBTYPE_WRITE: handle_write(rcvid, msg); break;
            case FILE_SUBTYPE_CLOSE: handle_close(rcvid, msg); break;
            default:
                MsgError(rcvid, ENOSYS);
        }
        break;
    // ... other message types
}
```

---

## 4. Using Unions for Unrelated Messages

### When to Use Unions

Use a **union** when messages have completely different structures and no common fields beyond the header:

```c
// Unrelated messages — different sizes, different purposes
union msg_union {
    struct msg_header   hdr;        // Common header (always accessible)
    struct msg_echo     echo;       // 2 + 256 = 258 bytes
    struct msg_checksum checksum;   // 2 + 256 = 258 bytes
    struct msg_stats    stats;      // 2 bytes only
};

// Server receives into the union
union msg_union msg;
rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

switch (msg.hdr.type) {
    case MSG_TYPE_ECHO:
        // Access msg.echo.text
        handle_echo(rcvid, &msg.echo);
        break;
    case MSG_TYPE_CHECKSUM:
        // Access msg.checksum.text
        handle_checksum(rcvid, &msg.checksum);
        break;
    case MSG_TYPE_STATS:
        // No payload beyond header
        handle_stats(rcvid);
        break;
}
```

**Benefits of unions:**

- Single receive buffer size — large enough for the biggest message
- Type-safe access to message-specific fields
- Clean, maintainable code

---

## 5. Reply Structures

### Designing Replies

Every message type that expects data back needs a matching reply structure:

```c
// Client sends this
struct msg_checksum {
    struct msg_header hdr;
    char text[256];
};

// Server replies with this
struct reply_checksum {
    uint32_t checksum;
};
```

**Server reply:**

```c
struct reply_checksum reply;
reply.checksum = calculate_checksum(msg->text);
MsgReply(rcvid, EOK, &reply, sizeof(reply));
```

**Client receives:**

```c
struct reply_checksum reply;
status = MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply));
if (status != -1) {
    printf("Checksum: %u
", reply.checksum);
}
```

### Reply for Status-Only Operations

For operations that don't return data (e.g., a "set" or "write" operation):

```c
// Server — no data reply, just status
MsgReply(rcvid, EOK, NULL, 0);

// Client — no reply buffer needed
status = MsgSend(coid, &msg, sizeof(msg), NULL, 0);
if (status == -1) {
    perror("MsgSend failed");
}
```

---

## 6. Server Message Branching Pattern

### The Standard Server Loop

```c
#include "msg_types.h"

int main(void) {
    int chid = ChannelCreate(0);
    union msg_union msg;
    struct _msg_info info;
    int rcvid;

    printf("Server PID: %d, Channel: %d
", getpid(), chid);

    for (;;) {
        // Block waiting for message or pulse
        rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

        if (rcvid == -1) {
            perror("MsgReceive failed");
            continue;
        }

        // Handle pulse
        if (rcvid == 0) {
            handle_pulse((struct _pulse *)&msg);
            continue;
        }

        // Branch based on message type
        switch (msg.hdr.type) {

            case MSG_TYPE_ECHO:
                handle_echo(rcvid, &msg.echo);
                break;

            case MSG_TYPE_CHECKSUM:
                handle_checksum(rcvid, &msg.checksum);
                break;

            case MSG_TYPE_REVERSE:
                handle_reverse(rcvid, &msg.reverse);
                break;

            case MSG_TYPE_STATS:
                handle_stats(rcvid);
                break;

            default:
                // Unknown message type — reject it
                fprintf(stderr, "Unknown message type: %d
", msg.hdr.type);
                MsgError(rcvid, ENOSYS);
                break;
        }
    }

    return 0;
}
```

### Handler Functions

```c
void handle_echo(int rcvid, struct msg_echo *msg) {
    struct reply_echo reply;
    strncpy(reply.text, msg->text, sizeof(reply.text) - 1);
    reply.text[sizeof(reply.text) - 1] = '';
    MsgReply(rcvid, EOK, &reply, sizeof(reply));
}

void handle_checksum(int rcvid, struct msg_checksum *msg) {
    struct reply_checksum reply;
    reply.checksum = calculate_checksum(msg->text);
    MsgReply(rcvid, EOK, &reply, sizeof(reply));
}

void handle_reverse(int rcvid, struct msg_reverse *msg) {
    struct reply_reverse reply;
    int len = strlen(msg->text);
    for (int i = 0; i < len; i++) {
        reply.text[i] = msg->text[len - 1 - i];
    }
    reply.text[len] = '';
    MsgReply(rcvid, EOK, &reply, sizeof(reply));
}

void handle_stats(int rcvid) {
    struct reply_stats reply;
    reply.messages_received = stats.messages_received;
    reply.messages_replied = stats.messages_replied;
    reply.uptime_seconds = time(NULL) - stats.start_time;
    MsgReply(rcvid, EOK, &reply, sizeof(reply));
}
```

---

## 7. Complete Example: Calculator Server

### msg_types.h

```c
#ifndef MSG_TYPES_H
#define MSG_TYPES_H

#include <stdint.h>

#define MSG_BASE        0x0200   // 512

#define MSG_TYPE_ADD        (MSG_BASE + 0)
#define MSG_TYPE_SUBTRACT   (MSG_BASE + 1)
#define MSG_TYPE_MULTIPLY   (MSG_BASE + 2)
#define MSG_TYPE_DIVIDE     (MSG_BASE + 3)

struct msg_header {
    uint16_t type;
};

struct msg_calc {
    struct msg_header hdr;
    int32_t operand_a;
    int32_t operand_b;
};

struct reply_calc {
    int32_t result;
    int32_t error;      // 0 = success, non-zero = error (e.g., divide by zero)
};

union msg_union {
    struct msg_header   hdr;
    struct msg_calc     calc;
};

#endif
```

### server.c

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include "msg_types.h"

void handle_calc(int rcvid, struct msg_calc *msg, uint16_t type) {
    struct reply_calc reply;
    reply.error = 0;

    switch (type) {
        case MSG_TYPE_ADD:
            reply.result = msg->operand_a + msg->operand_b;
            break;
        case MSG_TYPE_SUBTRACT:
            reply.result = msg->operand_a - msg->operand_b;
            break;
        case MSG_TYPE_MULTIPLY:
            reply.result = msg->operand_a * msg->operand_b;
            break;
        case MSG_TYPE_DIVIDE:
            if (msg->operand_b == 0) {
                reply.error = 1;  // Divide by zero
                reply.result = 0;
            } else {
                reply.result = msg->operand_a / msg->operand_b;
            }
            break;
    }

    MsgReply(rcvid, EOK, &reply, sizeof(reply));
}

int main(void) {
    int chid = ChannelCreate(0);
    union msg_union msg;
    struct _msg_info info;
    int rcvid;

    printf("Calculator Server
");
    printf("PID: %d, Channel: %d

", getpid(), chid);

    for (;;) {
        rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            printf("Pulse received
");
            continue;
        }

        switch (msg.hdr.type) {
            case MSG_TYPE_ADD:
            case MSG_TYPE_SUBTRACT:
            case MSG_TYPE_MULTIPLY:
            case MSG_TYPE_DIVIDE:
                handle_calc(rcvid, &msg.calc, msg.hdr.type);
                break;
            default:
                fprintf(stderr, "Unknown type: %d
", msg.hdr.type);
                MsgError(rcvid, ENOSYS);
                break;
        }
    }

    return 0;
}
```

### client.c

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include "msg_types.h"

int main(int argc, char *argv[]) {
    int coid, server_pid, server_chid;
    union msg_union msg;
    struct reply_calc reply;
    int status;

    if (argc != 5) {
        fprintf(stderr, "Usage: %s <pid> <chid> <op> <a> <b>
", argv[0]);
        fprintf(stderr, "  op: add, sub, mul, div
");
        exit(EXIT_FAILURE);
    }

    server_pid = atoi(argv[1]);
    server_chid = atoi(argv[2]);

    coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach");
        exit(EXIT_FAILURE);
    }

    // Set message type based on operation
    if (strcmp(argv[3], "add") == 0)      msg.hdr.type = MSG_TYPE_ADD;
    else if (strcmp(argv[3], "sub") == 0)  msg.hdr.type = MSG_TYPE_SUBTRACT;
    else if (strcmp(argv[3], "mul") == 0)  msg.hdr.type = MSG_TYPE_MULTIPLY;
    else if (strcmp(argv[3], "div") == 0)  msg.hdr.type = MSG_TYPE_DIVIDE;
    else {
        fprintf(stderr, "Unknown operation: %s
", argv[3]);
        exit(EXIT_FAILURE);
    }

    msg.calc.operand_a = atoi(argv[4]);
    msg.calc.operand_b = atoi(argv[5]);

    status = MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply));
    if (status == -1) {
        perror("MsgSend");
        ConnectDetach(coid);
        exit(EXIT_FAILURE);
    }

    if (reply.error != 0) {
        printf("Error: operation failed (code %d)
", reply.error);
    } else {
        printf("Result: %d
", reply.result);
    }

    ConnectDetach(coid);
    return 0;
}
```

---

## 8. Design Patterns Summary

| Pattern               | Use When                                | Example                                   |
| --------------------- | --------------------------------------- | ----------------------------------------- |
| **Message Types**     | All messages have a distinct purpose    | `MSG_TYPE_ECHO`, `MSG_TYPE_CHECKSUM`      |
| **Subtypes**          | Related messages share common structure | File operations: OPEN, READ, WRITE, CLOSE |
| **Unions**            | Messages have unrelated structures      | Calculator ops vs. string ops             |
| **Reply Structures**  | Server returns data to client           | `reply_checksum`, `reply_stats`           |
| **Status-Only Reply** | Operation succeeds/fails, no data       | `MsgReply(rcvid, EOK, NULL, 0)`           |

---

## 9. Key Principles

| Principle                           | Explanation                                                  |
| ----------------------------------- | ------------------------------------------------------------ |
| **Shared header file**              | Both client and server include the same `msg_types.h` to ensure structure compatibility |
| **Start types at 512**              | Avoid 0–511 (QNX system message range) to prevent tracing/debugging conflicts |
| **Always use `uint16_t` for type**  | QNX message header convention — 16-bit unsigned integer      |
| **Match reply to message**          | Every message that needs data back has a dedicated reply structure |
| **Big switch on server**            | Server branches on `msg.hdr.type` to dispatch to the correct handler |
| **Handle unknown types gracefully** | Use `MsgError(rcvid, ENOSYS)` for unrecognized messages      |
| **Use unions for type safety**      | Access message-specific fields through the union member      |

---

## 10. Common Pitfalls

| Pitfall                        | Problem                                         | Solution                                 |
| ------------------------------ | ----------------------------------------------- | ---------------------------------------- |
| Type in 0–511 range            | Conflicts with QNX system messages              | Start at 512 (`0x0200`)                  |
| Mismatched structures          | Client and server have different struct layouts | Use shared header file                   |
| Missing reply                  | Client hangs in REPLY BLOCKED                   | Always call `MsgReply()` or `MsgError()` |
| Wrong reply size               | Buffer overflow or truncated data               | Use `sizeof()` consistently              |
| No default case in switch      | Unknown messages not handled                    | Add `default: MsgError(rcvid, ENOSYS);`  |
| Forgetting `_NTO_SIDE_CHANNEL` | coid conflicts with file descriptors            | Always specify in `ConnectAttach()`      |

---

*This module covers the fundamentals of designing message passing interfaces. Advanced topics such as variable-length messages, multipart messages (`MsgSendv()`), and resource manager integration are covered in subsequent modules.*
