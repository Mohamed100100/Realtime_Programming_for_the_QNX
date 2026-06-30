# QNX Client Information — The `_msg_info` Structure

## Overview

This section covers how a server obtains detailed information about the client that sent a message. When `MsgReceive()` is called with a non-NULL fourth parameter, the kernel populates a `_msg_info` structure with metadata about the sender. This information is essential for authentication, logging, buffer size management, and resource tracking.

---

## 1. The `_msg_info` Structure

### Concept

The `_msg_info` structure is filled by the kernel during `MsgReceive()`. It contains comprehensive metadata about the client process and thread that sent the message.

```c
#include <sys/neutrino.h>

struct _msg_info {
    uint32_t  nd;           // Node descriptor of sender
    uint32_t  srcnd;        // Source node descriptor
    pid_t     pid;          // Process ID of sender
    int32_t   tid;          // Thread ID of sender
    int32_t   chid;         // Channel ID (server's channel that received the message)
    int32_t   scoid;        // Server connection ID — kernel's internal handle for this client
    int32_t   coid;         // Client's connection ID
    int32_t   msglen;       // Number of bytes actually copied into server's buffer
    int32_t   srcmsglen;    // Number of bytes the client tried to send
    int32_t   dstmsglen;    // Number of bytes the client's reply buffer can hold
    int16_t   priority;     // Priority of the sending thread
    int16_t   flags;        // Flags
    int32_t   reserved;     // Reserved for future use
};
```

### Field-by-Field Breakdown

| Field       | Type       | Description                                         | Common Use                                     |
| ----------- | ---------- | --------------------------------------------------- | ---------------------------------------------- |
| `nd`        | `uint32_t` | Node descriptor of the sender                       | Multi-node systems (use `0` for local)         |
| `srcnd`     | `uint32_t` | Source node descriptor                              | Networked IPC                                  |
| `pid`       | `pid_t`    | **Process ID of the client**                        | Logging, authentication, debugging             |
| `tid`       | `int32_t`  | **Thread ID that called `MsgSend()`**               | Debugging, thread-specific tracking            |
| `chid`      | `int32_t`  | Server's channel ID that received the message       | Multi-channel servers                          |
| `scoid`     | `int32_t`  | **Server connection ID** — kernel's internal handle | Authentication, cleanup, `ConnectClientInfo()` |
| `coid`      | `int32_t`  | Client's connection ID                              | Debugging, tracing                             |
| `msglen`    | `int32_t`  | **Actual bytes copied** into receive buffer         | Verify data completeness                       |
| `srcmsglen` | `int32_t`  | **Bytes client tried to send**                      | Detect truncation                              |
| `dstmsglen` | `int32_t`  | **Bytes client's reply buffer can hold**            | Prevent reply overflow                         |
| `priority`  | `int16_t`  | **Priority of sending thread**                      | Priority inheritance verification              |
| `flags`     | `int16_t`  | Message flags                                       | Internal kernel use                            |
| `reserved`  | `int32_t`  | Reserved                                            | Future expansion                               |

---

## 2. Using `_msg_info` with `MsgReceive()`

### The Fourth Parameter

```c
#include <sys/neutrino.h>

struct _msg_info info;
int rcvid;
char buffer[512];

// Pass &info as the 4th parameter to get client information
rcvid = MsgReceive(chid, buffer, sizeof(buffer), &info);

if (rcvid > 0) {
    // Message received — info is now populated
    printf("Message from PID %d, TID %d, priority %d
",
           info.pid, info.tid, info.priority);
}
```

### What Happens When You Pass NULL

```c
// Passing NULL — no client information retrieved
rcvid = MsgReceive(chid, buffer, sizeof(buffer), NULL);

// You CANNOT later retrieve info for this message!
// MsgInfo() only works if you originally passed a non-NULL info pointer
```

> **Important:** If you pass `NULL` to `MsgReceive()`, you **cannot** later call `MsgInfo()` to get the information. The kernel only captures this data when requested during `MsgReceive()`.

---

## 3. Key Use Cases

### 3.1 Authentication and Access Control

The server can use `pid`, `scoid`, and `ConnectClientInfo()` to verify the client's identity and permissions.

```c
#include <sys/neutrino.h>

void authenticate_client(struct _msg_info *info, int rcvid) {
    struct _client_info ci;

    // Get detailed client information using scoid
    if (ConnectClientInfo(info->scoid, &ci, sizeof(ci)) == -1) {
        perror("ConnectClientInfo failed");
        MsgError(rcvid, EPERM);
        return;
    }

    // Check user ID
    if (ci.cred.euid != 0) {
        printf("Rejecting client: PID %d, UID %d (not root)
",
               info->pid, ci.cred.euid);
        MsgError(rcvid, EACCES);
        return;
    }

    printf("Authenticated: PID %d, UID %d (root)
",
           info->pid, ci.cred.euid);
}
```

### 3.2 Buffer Size Management

Use `msglen`, `srcmsglen`, and `dstmsglen` to handle data truncation and reply sizing.

```c
void handle_message(int rcvid, char *buffer, struct _msg_info *info) {
    // Check if message was truncated
    if (info->msglen < info->srcmsglen) {
        printf("WARNING: Message truncated! "
               "Received %d of %d bytes
",
               info->msglen, info->srcmsglen);
    }

    // Check how much reply data the client can accept
    printf("Client's reply buffer can hold %d bytes
", info->dstmsglen);

    // Ensure reply doesn't overflow client's buffer
    char reply[256];
    int reply_size = prepare_reply(reply, sizeof(reply));

    if (reply_size > info->dstmsglen) {
        printf("WARNING: Reply (%d bytes) exceeds client's buffer (%d bytes)
",
               reply_size, info->dstmsglen);
        // Truncate reply or return error
        reply_size = info->dstmsglen;
    }

    MsgReply(rcvid, EOK, reply, reply_size);
}
```

### 3.3 Logging and Debugging

```c
void log_message(struct _msg_info *info, const char *operation) {
    printf("[%s] PID=%d TID=%d Priority=%d "
           "SCOID=%d MsgLen=%d/%d ReplyBuf=%d
",
           operation,
           info->pid,
           info->tid,
           info->priority,
           info->scoid,
           info->msglen,
           info->srcmsglen,
           info->dstmsglen);
}

// Usage in server loop:
rcvid = MsgReceive(chid, buffer, sizeof(buffer), &info);
if (rcvid > 0) {
    log_message(&info, "RECEIVED");
    // ... process message ...
    log_message(&info, "REPLIED");
}
```

### 3.4 Resource Tracking with `scoid`

The `scoid` (server connection ID) is a kernel-assigned identifier for each client connection. Servers use it to track per-client state.

```c
#define MAX_CLIENTS 64

struct client_state {
    int scoid;
    pid_t pid;
    int message_count;
    time_t connect_time;
    int active;
};

struct client_state clients[MAX_CLIENTS];

void track_client(struct _msg_info *info) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].scoid = info->scoid;
            clients[i].pid = info->pid;
            clients[i].message_count = 0;
            clients[i].connect_time = time(NULL);
            clients[i].active = 1;
            printf("New client tracked: scoid=%d, pid=%d
",
                   info->scoid, info->pid);
            return;
        }
    }
    printf("WARNING: Max clients reached!
");
}

void cleanup_client(int scoid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].scoid == scoid) {
            clients[i].active = 0;
            printf("Client cleaned up: scoid=%d
", scoid);
            return;
        }
    }
}
```

---

## 4. `ConnectClientInfo()` — Extended Client Information

### When to Use

If `_msg_info` doesn't provide enough detail (e.g., you need user ID, group ID, or process capabilities), use `ConnectClientInfo()`.

```c
#include <sys/neutrino.h>

int ConnectClientInfo(int scoid,                    // From _msg_info.scoid
                      struct _client_info *info,    // Output structure
                      int info_size);               // sizeof(struct _client_info)
```

### The `_client_info` Structure

```c
struct _client_info {
    int32_t           nd;           // Node descriptor
    pid_t             pid;          // Process ID
    pid_t             sid;          // Session ID
    pid_t             pgrp;       // Process group
    uid_t             uid;          // Real user ID
    gid_t             gid;          // Real group ID
    uid_t             euid;         // Effective user ID
    gid_t             egid;         // Effective group ID
    uid_t             suid;         // Saved user ID
    gid_t             sgid;         // Saved group ID
    struct _cred_info cred;         // Credentials (capabilities, etc.)
    uint32_t          flags;        // Flags
    sigset_t          dynsigno;     // Dynamic signal mask
};
```

### Example: Full Authentication

```c
#include <sys/neutrino.h>
#include <pwd.h>

void full_authenticate(int rcvid, struct _msg_info *msg_info) {
    struct _client_info ci;
    struct passwd *pw;

    // Get extended client info using scoid
    if (ConnectClientInfo(msg_info->scoid, &ci, sizeof(ci)) == -1) {
        perror("ConnectClientInfo");
        MsgError(rcvid, EPERM);
        return;
    }

    // Get username from UID
    pw = getpwuid(ci.cred.euid);

    printf("Client Details:
");
    printf("  Process:     PID=%d, TID=%d
", msg_info->pid, msg_info->tid);
    printf("  User:        UID=%d (%s), EUID=%d
",
           ci.cred.uid, pw ? pw->pw_name : "unknown", ci.cred.euid);
    printf("  Group:       GID=%d, EGID=%d
", ci.cred.gid, ci.cred.egid);
    printf("  Priority:    %d
", msg_info->priority);
    printf("  Connection:  scoid=%d, coid=%d
",
           msg_info->scoid, msg_info->coid);

    // Authorization check
    if (ci.cred.euid != 0 && ci.cred.euid != 1000) {
        printf("ACCESS DENIED: User not authorized
");
        MsgError(rcvid, EACCES);
        return;
    }

    printf("ACCESS GRANTED
");
    MsgReply(rcvid, EOK, NULL, 0);
}
```

---

## 5. Pulses and `_msg_info`

### Critical Difference

> **For pulses, `_msg_info` is NOT updated by the kernel.**

When `MsgReceive()` returns `0` (pulse received), the `_msg_info` structure contains:

- **Stale data** from the previous message, OR
- **Garbage values** that don't make sense

```c
rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

if (rcvid == 0) {
    // PULSE received
    // DO NOT use info.pid, info.tid, info.scoid, etc.!
    // These are NOT valid for pulses.

    // For pulse sender info, use pulse.scoid instead:
    printf("Pulse from scoid: %d
", msg.pulse.scoid);

} else if (rcvid > 0) {
    // MESSAGE received
    // info is VALID here:
    printf("Message from PID: %d
", info.pid);
    printf("Message from TID: %d
", info.tid);
}
```

### Why Pulses Don't Fill `_msg_info`

| Reason                      | Explanation                                                  |
| --------------------------- | ------------------------------------------------------------ |
| **Non-blocking nature**     | The sender is gone by the time the pulse is delivered — no thread to describe |
| **Kernel-generated pulses** | System pulses (disconnect, unblock) have no "sender thread"  |
| **Efficiency**              | Avoids unnecessary kernel work for lightweight notifications |

**For pulse sender identification, use `pulse.scoid` instead.**

---

## 6. Complete Server Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/neutrino.h>
#include <pwd.h>

#define BUFFER_SIZE 512
#define MSG_TYPE_ECHO       0x0200
#define MSG_TYPE_STATUS     0x0201

struct msg_header {
    uint16_t type;
};

struct msg_echo {
    struct msg_header hdr;
    char text[BUFFER_SIZE];
};

union recv_buffer {
    struct _pulse       pulse;
    struct msg_header   hdr;
    struct msg_echo     echo;
};

struct reply_echo {
    char text[BUFFER_SIZE];
};

void log_client_info(struct _msg_info *info, const char *event) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[26];
    strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    printf("[%s] %s | PID=%d TID=%d Prio=%d SCOID=%d "
           "Msg=%d/%d bytes ReplyBuf=%d bytes
",
           time_str, event,
           info->pid, info->tid, info->priority,
           info->scoid,
           info->msglen, info->srcmsglen,
           info->dstmsglen);
}

void authenticate_and_handle(int rcvid, union recv_buffer *msg,
                              struct _msg_info *info) {
    struct _client_info ci;
    struct passwd *pw;

    // Log basic info
    log_client_info(info, "RECV");

    // Extended authentication
    if (ConnectClientInfo(info->scoid, &ci, sizeof(ci)) != -1) {
        pw = getpwuid(ci.cred.euid);
        printf("  User: %s (UID=%d, EUID=%d)
",
               pw ? pw->pw_name : "unknown",
               ci.cred.uid, ci.cred.euid);
    }

    // Check for message truncation
    if (info->msglen < info->srcmsglen) {
        printf("  WARNING: Message truncated (%d < %d bytes)
",
               info->msglen, info->srcmsglen);
    }

    // Handle message based on type
    switch (msg->hdr.type) {
        case MSG_TYPE_ECHO: {
            struct reply_echo reply;
            strncpy(reply.text, msg->echo.text, sizeof(reply.text) - 1);
            reply.text[sizeof(reply.text) - 1] = '';

            // Check if reply fits in client's buffer
            int reply_len = strlen(reply.text) + 1;
            if (reply_len > info->dstmsglen) {
                printf("  WARNING: Reply too large for client buffer
");
                reply_len = info->dstmsglen;
            }

            MsgReply(rcvid, EOK, &reply, reply_len);
            log_client_info(info, "REPLY");
            break;
        }

        default:
            printf("  ERROR: Unknown message type: 0x%04x
", msg->hdr.type);
            MsgError(rcvid, ENOSYS);
            break;
    }
}

void handle_pulse(struct _pulse *pulse) {
    printf("[PULSE] Code=%d Value=0x%lx SCOID=%d
",
           pulse->code,
           (unsigned long)pulse->value.sival_long,
           pulse->scoid);
}

int main(void) {
    int chid;
    union recv_buffer msg;
    struct _msg_info info;
    int rcvid;

    chid = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK);
    if (chid == -1) {
        perror("ChannelCreate");
        exit(EXIT_FAILURE);
    }

    printf("Server Started | PID=%d Channel=%d
", getpid(), chid);
    printf("================================================

");

    for (;;) {
        rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            // PULSE: info is NOT valid — use pulse.scoid instead
            handle_pulse(&msg.pulse);
        } else {
            // MESSAGE: info is fully populated by kernel
            authenticate_and_handle(rcvid, &msg, &info);
        }

        printf("------------------------------------------------
");
    }

    return 0;
}
```

---

## 7. Summary Table

| Information            | Source                | Valid For                                  | Use Case                                         |
| ---------------------- | --------------------- | ------------------------------------------ | ------------------------------------------------ |
| `pid`, `tid`           | `_msg_info`           | Messages only                              | Logging, debugging, process identification       |
| `priority`             | `_msg_info`           | Messages only                              | Verify priority inheritance, scheduling analysis |
| `msglen` / `srcmsglen` | `_msg_info`           | Messages only                              | Detect message truncation                        |
| `dstmsglen`            | `_msg_info`           | Messages only                              | Prevent reply buffer overflow                    |
| `scoid`                | `_msg_info`           | Messages + Pulses (pulse.scoid for pulses) | Authentication, resource tracking, cleanup       |
| `coid`                 | `_msg_info`           | Messages only                              | Connection tracing                               |
| `uid`, `gid`, `euid`   | `ConnectClientInfo()` | Messages only                              | Full authentication, access control              |
| `cred` (capabilities)  | `ConnectClientInfo()` | Messages only                              | Fine-grained permission checks                   |

---

## 8. Key Principles

| Principle                                        | Explanation                                                  |
| ------------------------------------------------ | ------------------------------------------------------------ |
| **Always pass `&info` to `MsgReceive()`**        | If you pass `NULL`, you cannot retrieve client information later |
| **`_msg_info` is only valid for messages**       | For pulses (`rcvid == 0`), the structure contains stale or garbage data |
| **Use `scoid` for client tracking**              | The server connection ID is the key to identifying and managing clients |
| **Use `ConnectClientInfo()` for authentication** | When you need user ID, group ID, or capabilities beyond `_msg_info` |
| **Check `msglen` vs `srcmsglen`**                | Detect if the client's message was truncated due to buffer size |
| **Respect `dstmsglen`**                          | Don't send a reply larger than the client's buffer can hold  |
| **Log `pid` and `tid` for debugging**            | Essential for tracing which process/thread sent each message |

---

## 9. Common Pitfalls

| Pitfall                           | Problem                            | Solution                                |
| --------------------------------- | ---------------------------------- | --------------------------------------- |
| Passing `NULL` for `_msg_info`    | Cannot retrieve sender information | Always pass a valid pointer             |
| Using `_msg_info` for pulses      | Contains stale/garbage data        | Use `pulse.scoid` for pulse sender info |
| Ignoring `msglen < srcmsglen`     | Silent data truncation             | Always check and handle truncation      |
| Ignoring `dstmsglen`              | Reply buffer overflow on client    | Truncate reply or return error          |
| Not calling `ConnectClientInfo()` | Cannot authenticate clients        | Use `scoid` to get full credentials     |
| Storing `_msg_info` for later use | Data becomes stale                 | Use immediately after `MsgReceive()`    |

---

*This module covers client information retrieval in QNX message passing. Advanced topics such as custom credential checks, multi-node client info, and security policy enforcement are covered in subsequent modules.*
