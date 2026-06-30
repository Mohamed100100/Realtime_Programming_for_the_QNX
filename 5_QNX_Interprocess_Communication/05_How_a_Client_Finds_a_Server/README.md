# QNX Client-Server Discovery — name_attach() and name_open()

## Overview

This section covers how a **client finds a server** in QNX. Instead of hardcoding process IDs and channel IDs (which is impractical in real systems), QNX provides a **name registration and lookup** mechanism. The server registers a name in the pathname space, and the client looks up that name to establish a connection.

---

## 1. The Problem: How Does a Client Find a Server?

### The Hard Way (Exercise-Only)

In earlier exercises, the server printed its PID and channel ID, and the client passed them as command-line arguments:

```bash
# Server prints: PID=12345, Channel=1
./server

# Client must know these values
./client 12345 1 "Hello"
```

> **This is NOT a general solution.** In production, clients cannot know server PIDs in advance.

### The Real Solution: Name Registration

```
┌─────────────────┐                      ┌─────────────────┐
│   SERVER        │                      │   CLIENT        │
│                 │                      │                 │
│  name_attach()  │───▶ /dev/mysrv ─────▶│  name_open()    │
│  ("my_server")  │   (pathname space)   │  ("my_server")  │
│                 │                      │                 │
│  MsgReceive()   │◀───── messages ──────│  MsgSend()      │
│                 │                      │                 │
└─────────────────┘                      └─────────────────┘
```

**Both methods follow the same pattern:**

1. **Server** puts a name in the pathname space
2. **Client** and **Server** agree on that name
3. **Client** does an `open()`-style call on that name
4. The `open()` creates the connection automatically

---

## 2. Two Methods: Resource Manager vs. Simple Receive Loop

| Server Type                  | Server Registers With | Client Opens With | Connection Created By                 |
| ---------------------------- | --------------------- | ----------------- | ------------------------------------- |
| **Resource Manager**         | `resmgr_attach()`     | `open()`          | `open()` returns fd (which is a coid) |
| **Simple MsgReceive() Loop** | `name_attach()`       | `name_open()`     | `name_open()` returns coid            |

> This section focuses on **simple receive loops** using `name_attach()` and `name_open()`.

---

## 3. Server Side: name_attach()

### Concept

`name_attach()` registers a name in the QNX pathname space, creates a channel automatically, and returns a handle containing the channel ID.

```c
#include <sys/iofunc.h>
#include <sys/dispatch.h>

name_attach_t *name_attach(dispatch_t *dpp,        // NULL for simple servers
                           const char *path,       // Name to register (e.g., "my_server")
                           unsigned flags);        // Flags (usually 0)
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `dpp`     | Dispatch handle. Use `NULL` for simple message-passing servers (not resource managers). |
| `path`    | The name to register. This becomes `/dev/name/local/<path>` in the pathname space. |
| `flags`   | Flags. Use `0` for default behavior. `NAME_FLAG_ATTACH_GLOBAL` makes the name global across the network. |

**Return Value:**

- **Success:** Pointer to `name_attach_t` structure
- **Failure:** `NULL`, `errno` set

### The `name_attach_t` Structure

```c
typedef struct _name_attach {
    dispatch_t      *dpp;         // Dispatch handle
    int             chid;         // Channel ID (use this for MsgReceive())
    int             mount_id;     // Mount ID for pathname space
    struct _iofunc_attr attr;     // Attributes
    unsigned        flags;        // Flags
} name_attach_t;
```

**Key field:** `chid` — the channel ID to pass to `MsgReceive()`.

### Server Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>

#define SERVER_NAME "my_checksum_server"

int main(void) {
    name_attach_t *attach;
    int rcvid;
    char buffer[512];
    struct _msg_info info;

    // Register the name and create a channel
    attach = name_attach(NULL, SERVER_NAME, 0);
    if (attach == NULL) {
        perror("name_attach failed");
        exit(EXIT_FAILURE);
    }

    printf("Server registered as '%s'
", SERVER_NAME);
    printf("Channel ID: %d
", attach->chid);
    printf("Waiting for clients...

");

    // Use attach->chid for MsgReceive()
    for (;;) {
        rcvid = MsgReceive(attach->chid, buffer, sizeof(buffer), &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            // Handle pulse (see Section 5)
            handle_pulse((struct _pulse *)buffer);
            continue;
        }

        // Handle message
        printf("Message from client PID %d
", info.pid);

        // Process and reply...
        MsgReply(rcvid, EOK, NULL, 0);
    }

    // Cleanup (unreachable in this example)
    name_detach(attach, 0);
    return 0;
}
```

### name_detach()

Removes the name from the pathname space and destroys the channel.

```c
int name_detach(name_attach_t *attach, int flags);
```

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `attach`  | The handle returned by `name_attach()`                       |
| `flags`   | Use `0` to destroy the channel. Use `NAME_FLAG_DETACH_SAVE_CHAN` to keep the channel. |

```c
// Clean shutdown
name_detach(attach, 0);  // Removes name AND destroys channel
```

---

## 4. Client Side: name_open()

### Concept

`name_open()` looks up a registered name in the pathname space and creates a connection to it. It returns a connection ID (coid) directly — no need for `ConnectAttach()` or knowing the server's PID.

```c
#include <sys/iofunc.h>
#include <sys/dispatch.h>

int name_open(const char *name,     // Name to look up (same as server registered)
              int flags);            // Flags (usually 0)
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `name`    | The name the server registered (e.g., `"my_checksum_server"`). |
| `flags`   | Use `0` for local lookup. `NAME_FLAG_ATTACH_GLOBAL` for network-wide lookup. |

**Return Value:**

- **Success:** Connection ID (coid) — use directly with `MsgSend()`
- **Failure:** `-1`, `errno` set

### Client Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>

#define SERVER_NAME "my_checksum_server"

int main(int argc, char *argv[]) {
    int coid;
    char send_buffer[256] = "Hello Server!";
    char reply_buffer[256];
    int status;

    // Look up the server by name and create a connection
    coid = name_open(SERVER_NAME, 0);
    if (coid == -1) {
        perror("name_open failed — is the server running?");
        exit(EXIT_FAILURE);
    }

    printf("Connected to '%s', coid=%d
", SERVER_NAME, coid);

    // Send message using the coid
    status = MsgSend(coid, send_buffer, strlen(send_buffer) + 1,
                     reply_buffer, sizeof(reply_buffer));
    if (status == -1) {
        perror("MsgSend failed");
        name_close(coid);
        exit(EXIT_FAILURE);
    }

    printf("Server replied: %s
", reply_buffer);

    // Close the connection
    name_close(coid);
    return 0;
}
```

### name_close()

Closes the connection created by `name_open()`.

```c
int name_close(int coid);
```

```c
name_close(coid);  // Detaches the connection
```

---

## 5. Channel Flags Set by name_attach()

### What name_attach() Does Internally

```c
// name_attach() internally calls:
chid = ChannelCreate(_NTO_CHF_DISCONNECT |
                     _NTO_CHF_COID_DISCONNECT |
                     _NTO_CHF_UNBLOCK);
```

**Critical:** The server **must** handle the pulses generated by these flags.

### Flag Breakdown

| Flag                       | Set By `name_attach()` | Pulse Code When Triggered | Meaning                                     |
| -------------------------- | ---------------------- | ------------------------- | ------------------------------------------- |
| `_NTO_CHF_DISCONNECT`      | ✅ Yes                  | `_PULSE_CODE_DISCONNECT`  | A client of this server went away           |
| `_NTO_CHF_COID_DISCONNECT` | ✅ Yes                  | `_PULSE_CODE_COIDDEATH`   | A server this process connects to went away |
| `_NTO_CHF_UNBLOCK`         | ✅ Yes                  | `_PULSE_CODE_UNBLOCK`     | A REPLY-blocked client wants to unblock     |

### Pulse Handling in name_attach() Servers

```c
void handle_pulse(struct _pulse *pulse, name_attach_t *attach) {
    switch (pulse->code) {

        case _PULSE_CODE_DISCONNECT:
            // A client of THIS server went away
            printf("Client disconnected: scoid=%d
", pulse->scoid);

            // 1. Clean up any per-client state (buffers, pending requests, etc.)
            cleanup_client(pulse->scoid);

            // 2. MUST call ConnectDetach() to free the scoid
            ConnectDetach(pulse->scoid);
            break;

        case _PULSE_CODE_COIDDEATH:
            // A server that THIS process connects to went away
            printf("Server disconnected: coid=%d
", pulse->value.sival_int);

            // Clean up the now-invalid connection
            cleanup_server_connection(pulse->value.sival_int);
            break;

        case _PULSE_CODE_UNBLOCK:
            // A REPLY-blocked client wants to unblock (signal, timeout, etc.)
            printf("Client wants to unblock: rcvid=%d
", pulse->value.sival_int);

            // Option 1: If reply is imminent, ignore the pulse
            // Option 2: Cancel operation and reply with error
            handle_unblock_request(pulse->value.sival_int);
            break;

        default:
            printf("Unknown pulse code: %d
", pulse->code);
            break;
    }
}
```

---

## 6. Per-Client Tracking with scoid

### Why Track Clients?

Servers often need to maintain state for each connected client:

| Use Case                 | What to Track                       |
| ------------------------ | ----------------------------------- |
| **Pending I/O requests** | Buffer, byte count, hardware handle |
| **Configuration**        | Client-specific settings            |
| **Notifications**        | Events the client subscribed to     |
| **Authentication**       | Verified credentials, permissions   |

### Client List Data Structure

```c
#define MAX_CLIENTS 64

struct client_entry {
    int scoid;              // Server connection ID — unique client identifier
    pid_t pid;              // Client process ID
    int active;             // Is this slot in use?

    // Per-client state
    char pending_buffer[4096];
    int pending_bytes;
    int notify_enabled;
    time_t connect_time;
};

struct client_entry client_list[MAX_CLIENTS];
```

### Adding a Client (First Message)

```c
void add_client(struct _msg_info *info) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!client_list[i].active) {
            client_list[i].scoid = info->scoid;
            client_list[i].pid = info->pid;
            client_list[i].active = 1;
            client_list[i].connect_time = time(NULL);
            client_list[i].pending_bytes = 0;
            client_list[i].notify_enabled = 0;

            printf("New client added: scoid=%d, pid=%d
", info->scoid, info->pid);
            return;
        }
    }
    printf("ERROR: Client list full!
");
}

struct client_entry *find_client(int scoid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_list[i].active && client_list[i].scoid == scoid) {
            return &client_list[i];
        }
    }
    return NULL;
}
```

### Removing a Client (Disconnect Pulse)

```c
void remove_client(int scoid) {
    struct client_entry *client = find_client(scoid);
    if (client) {
        // 1. Cancel any pending I/O
        cancel_pending_io(client);

        // 2. Free any allocated buffers
        free_client_buffers(client);

        // 3. Mark slot as free
        client->active = 0;

        printf("Client removed: scoid=%d
", scoid);
    }

    // 4. MUST detach the scoid (required when _NTO_CHF_DISCONNECT is set)
    ConnectDetach(scoid);
}
```

### Complete Server with Client Tracking

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>

#define SERVER_NAME     "my_server"
#define MAX_CLIENTS     64
#define MSG_TYPE_ECHO   0x0200

// Client tracking structure
struct client_entry {
    int scoid;
    pid_t pid;
    int active;
    time_t connect_time;
    int message_count;
};

// Message structure
struct msg_echo {
    uint16_t type;
    char text[256];
};

// Union receive buffer
union recv_buffer {
    struct _pulse       pulse;
    uint16_t            type;
    struct msg_echo     echo;
};

struct client_entry clients[MAX_CLIENTS];

void init_clients(void) {
    memset(clients, 0, sizeof(clients));
}

void add_client(struct _msg_info *info) {
    // Check if already exists
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].scoid == info->scoid) {
            return;  // Already tracked
        }
    }

    // Find free slot
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].scoid = info->scoid;
            clients[i].pid = info->pid;
            clients[i].active = 1;
            clients[i].connect_time = time(NULL);
            clients[i].message_count = 0;
            printf("[CLIENT] Added: scoid=%d, pid=%d
", info->scoid, info->pid);
            return;
        }
    }
    printf("[ERROR] Client list full!
");
}

void remove_client(int scoid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].scoid == scoid) {
            printf("[CLIENT] Removed: scoid=%d, pid=%d, messages=%d
",
                   clients[i].scoid, clients[i].pid, clients[i].message_count);
            clients[i].active = 0;
            break;
        }
    }

    // MUST call ConnectDetach() when _NTO_CHF_DISCONNECT is set
    ConnectDetach(scoid);
}

void handle_pulse(struct _pulse *pulse) {
    switch (pulse->code) {
        case _PULSE_CODE_DISCONNECT:
            printf("[PULSE] Client disconnect: scoid=%d
", pulse->scoid);
            remove_client(pulse->scoid);
            break;

        case _PULSE_CODE_COIDDEATH:
            printf("[PULSE] Server we connect to died: coid=%d
",
                   pulse->value.sival_int);
            break;

        case _PULSE_CODE_UNBLOCK:
            printf("[PULSE] Client unblock request: rcvid=%d
",
                   pulse->value.sival_int);
            break;

        default:
            printf("[PULSE] Unknown code=%d, value=%d
",
                   pulse->code, pulse->value.sival_int);
            break;
    }
}

void handle_message(int rcvid, union recv_buffer *msg, struct _msg_info *info) {
    // Track this client
    add_client(info);

    // Update message count
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].scoid == info->scoid) {
            clients[i].message_count++;
            break;
        }
    }

    switch (msg->type) {
        case MSG_TYPE_ECHO:
            printf("[MSG] Echo from PID %d: "%s"
", info->pid, msg->echo.text);
            MsgReply(rcvid, EOK, msg->echo.text, strlen(msg->echo.text) + 1);
            break;

        default:
            printf("[ERROR] Unknown message type: 0x%04x
", msg->type);
            MsgError(rcvid, ENOSYS);
            break;
    }
}

int main(void) {
    name_attach_t *attach;
    union recv_buffer msg;
    struct _msg_info info;
    int rcvid;

    init_clients();

    // Register name — creates channel with _NTO_CHF_DISCONNECT,
    // _NTO_CHF_COID_DISCONNECT, and _NTO_CHF_UNBLOCK
    attach = name_attach(NULL, SERVER_NAME, 0);
    if (attach == NULL) {
        perror("name_attach");
        exit(EXIT_FAILURE);
    }

    printf("========================================
");
    printf("  Server: '%s'
", SERVER_NAME);
    printf("  Channel ID: %d
", attach->chid);
    printf("========================================

");

    for (;;) {
        rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            handle_pulse(&msg.pulse);
        } else {
            handle_message(rcvid, &msg, &info);
        }
    }

    name_detach(attach, 0);
    return 0;
}
```

---

## 7. Complete Client Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>

#define SERVER_NAME "my_server"
#define MSG_TYPE_ECHO 0x0200

struct msg_echo {
    uint16_t type;
    char text[256];
};

int main(int argc, char *argv[]) {
    int coid;
    struct msg_echo msg;
    char reply[256];
    int status;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <message>
", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Find server by name — no PID or channel ID needed!
    coid = name_open(SERVER_NAME, 0);
    if (coid == -1) {
        perror("name_open — is the server running?");
        exit(EXIT_FAILURE);
    }

    printf("Connected to '%s' (coid=%d)
", SERVER_NAME, coid);

    // Prepare and send message
    msg.type = MSG_TYPE_ECHO;
    strncpy(msg.text, argv[1], sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '';

    status = MsgSend(coid, &msg, sizeof(msg), reply, sizeof(reply));
    if (status == -1) {
        perror("MsgSend");
        name_close(coid);
        exit(EXIT_FAILURE);
    }

    printf("Server echoed: '%s'
", reply);

    name_close(coid);
    return 0;
}
```

---

## 8. Architecture Summary

### Without name_attach() / name_open() (Hardcoded)

```
Server:                     Client:
  ChannelCreate() ──▶ chid    ConnectAttach(0, PID, CHID, ...) ──▶ coid
  print PID, chid             (must know PID and CHID!)
  MsgReceive(chid, ...)
                              MsgSend(coid, ...)
```

### With name_attach() / name_open() (Name-based)

```
Server:                           Client:
  name_attach(NULL, "my_srv")     name_open("my_srv")
    ├── creates channel             ├── looks up name
    ├── registers name              └── returns coid
    └── sets _NTO_CHF_DISCONNECT
        _NTO_CHF_COID_DISCONNECT
        _NTO_CHF_UNBLOCK

  MsgReceive(attach->chid, ...)   MsgSend(coid, ...)

  name_detach(attach, 0)          name_close(coid)
```

---

## 9. Key Principles

| Principle                                            | Explanation                                                  |
| ---------------------------------------------------- | ------------------------------------------------------------ |
| **Use `name_attach()` for simple servers**           | Registers a name, creates a channel, and sets necessary pulse flags automatically |
| **Use `name_open()` for clients**                    | Looks up a name and returns a coid — no PID/CHID knowledge needed |
| **Always handle disconnect pulses**                  | `name_attach()` sets `_NTO_CHF_DISCONNECT` — you must handle `_PULSE_CODE_DISCONNECT` |
| **Always call `ConnectDetach(scoid)` on disconnect** | Required cleanup when `_NTO_CHF_DISCONNECT` is set           |
| **Track clients with `scoid`**                       | The server connection ID is the unique identifier for each client connection |
| **Use `name_detach()` on shutdown**                  | Removes the name from the pathname space and destroys the channel |
| **Use `name_close()` to clean up**                   | Closes the connection created by `name_open()`               |
| **Handle all three pulse types**                     | Disconnect, COID death, and unblock pulses are all generated by `name_attach()` |

---

## 10. Common Pitfalls

| Pitfall                                          | Problem                                           | Solution                                                   |
| ------------------------------------------------ | ------------------------------------------------- | ---------------------------------------------------------- |
| **Not handling `_PULSE_CODE_DISCONNECT`**        | Resource leaks, stale client entries              | Always handle disconnect pulses and call `ConnectDetach()` |
| **Forgetting `ConnectDetach(scoid)`**            | scoid leaks, eventually exhausts connection table | Mandatory cleanup in disconnect pulse handler              |
| **Using `name_attach()` with resource managers** | Conflicts with `resmgr_attach()`                  | Use `resmgr_attach()` + `open()` for resource managers     |
| **Not checking `name_open()` return**            | Server not running, client crashes                | Always check for `-1` and report error                     |
| **Hardcoding PID/CHID in production**            | Brittle, breaks on restart                        | Always use `name_attach()` / `name_open()`                 |
| **Ignoring `_PULSE_CODE_UNBLOCK`**               | Client stuck REPLY-blocked after signal/timeout   | Handle unblock pulses by canceling and replying            |

---

## 11. API Quick Reference

| Function          | Side   | Purpose                                        |
| ----------------- | ------ | ---------------------------------------------- |
| `name_attach()`   | Server | Register name, create channel, set pulse flags |
| `name_detach()`   | Server | Unregister name, destroy channel               |
| `name_open()`     | Client | Look up name, create connection                |
| `name_close()`    | Client | Close connection                               |
| `ConnectDetach()` | Server | Clean up scoid in disconnect pulse handler     |

---

*This module covers client-server discovery using `name_attach()` and `name_open()`. Advanced topics such as global names (network-wide), resource manager name registration, and custom dispatchers are covered in subsequent modules.*
