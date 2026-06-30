# QNX Message Passing — Shared Devices

## Overview

This section covers QNX native message passing, the primary Inter-Process Communication (IPC) mechanism in QNX. Message passing follows a **client/server model** with **bidirectional synchronous communication**. The complete architecture behaves like a **Remote Procedure Call (RPC)**, where the client sends a message, the server processes it, and replies back.

The key principle is **synchronous execution** — the client blocks until the server replies, and the server inherits the client's priority to avoid priority inversion.

---

## 1. Synchronous Communication Model

### Concept

In QNX message passing, a thread in one process sends a message to a thread in another process. The communication is **completely synchronous** — the client blocks until the server replies.

### Architecture

```
┌─────────────────┐         ┌─────────────────┐
│   CLIENT        │         │   SERVER        │
│                 │         │                 │
│  ┌───────────┐  │         │  ┌───────────┐  │
│  │ Thread    │  │         │  │ Thread    │  │
│  │ (priority │  │         │  │ (inherits │  │
│  │  = client)│  │         │  │  priority)│  │
│  └─────┬─────┘  │         │  └─────┬─────┘  │
│        │        │         │        │        │
│  ┌─────┴─────┐  │         │  ┌─────┴─────┐  │
│  │ MsgSend() │──┼────────►│  │ MsgReceive│  │
│  │           │  │         │  │ ()        │  │
│  │  [SEND    │  │         │  │  [PROCESS]│  │
│  │   BLOCKED]│  │         │  │           │  │
│  │           │◄─┼─────────│  │ MsgReply()│  │
│  │  [REPLY   │  │         │  │           │  │
│  │   BLOCKED]│  │         │  │           │  │
│  └───────────┘  │         │  └───────────┘  │
└─────────────────┘         └─────────────────┘
```

### Why Synchronous?

| Advantage                     | Explanation                                                |
| ----------------------------- | ---------------------------------------------------------- |
| **Deterministic**             | Predictable timing for real-time systems                   |
| **No hidden queues**          | No buffering overhead; the kernel doesn't queue messages   |
| **Natural flow control**      | Server controls processing rate; backpressure is automatic |
| **Simplified error handling** | Client's exact state is known (blocked on MsgSend)         |
| **Priority inheritance**      | Server runs at client's priority, preventing inversion     |

---

## 2. Priority Inheritance

### Concept

A critical feature of QNX Message Passing is **priority inheritance**:

> **The server's thread receives the client message at the priority at which the client sent it.**

### Example

```
Server Thread Priority = 22 (default)

Client T1 (priority 13) sends message ──▶ Server runs at Priority 13
Client T2 (priority 10) sends message ──▶ Server runs at Priority 10

Without priority inheritance: T2 would get work done at priority 22 (inversion!)
With priority inheritance:    Server runs at sender's priority
```

### Priority Inversion Prevention

```
Scenario without inheritance:
  Server at priority 10 receives message from T2 (priority 10)
  T1 (priority 13) sends message ── server still at 10
  T3 (priority 11) preempts server ──▶ T1 delayed by lower-priority T3!

Scenario with inheritance:
  Server at priority 10 receives message from T2 (priority 10)
  T1 (priority 13) sends message ── server boosted to 13 immediately
  T3 (priority 11) cannot preempt ──▶ T1 is not delayed
```

> **Note:** Priority inheritance can be disabled by setting `_NTO_CHF_FIXED_PRIORITY` in `ChannelCreate()`.

---

## 3. Communication Scenarios

### Scenario A: Server is Available (RECEIVE Blocked)

The server is idle, waiting for messages in the **RECEIVE blocked** state.

```
Time ──▶

Client:  [RUNNABLE] ──MsgSend()──▶ [REPLY BLOCKED] ──────▶ [RUNNABLE]
                                              ▲                    │
                                              │                    │
Server:  [RECEIVE BLOCKED] ──MsgReceive()──▶ [PROCESSING] ──MsgReply()──▶ [RECEIVE BLOCKED]
                                              │
                                              ▼
                                    Kernel copies data from
                                    client buffer to server buffer
```

**Step-by-step:**

| Step | Client State      | Server State    | Action                                                 |
| ---- | ----------------- | --------------- | ------------------------------------------------------ |
| 1    | RUNNABLE          | RECEIVE BLOCKED | Server idle, waiting for messages                      |
| 2    | RUNNABLE          | RECEIVE BLOCKED | Client calls `MsgSend()`                               |
| 3    | **REPLY BLOCKED** | RUNNING         | Kernel copies data; server receives via `MsgReceive()` |
| 4    | REPLY BLOCKED     | RUNNING         | Server processes the message                           |
| 5    | REPLY BLOCKED     | RUNNING         | Server calls `MsgReply()`                              |
| 6    | RUNNABLE          | RECEIVE BLOCKED | Kernel copies reply; client unblocks                   |

**Key Points:**

- Data copy happens **immediately** because server is ready
- Client skips SEND BLOCKED, goes directly to **REPLY BLOCKED**
- Server can go back to RECEIVE BLOCKED to wait for next message

---

### Scenario B: Server is Busy

The server is busy processing and is **NOT** in the RECEIVE blocked state.

```
Time ──▶

Client:  [RUNNABLE] ──MsgSend()──▶ [SEND BLOCKED] ──▶ [REPLY BLOCKED] ──▶ [RUNNABLE]
                                              ▲              ▲                  │
                                              │              │                  │
Server:  [RUNNING/BUSY] ──────────────▶ [MsgReceive()]──▶ [PROCESSING] ──MsgReply()──▶ [...]
                                              │
                                              ▼
                                    Client was SEND BLOCKED until
                                    server became available
```

**Step-by-step:**

| Step | Client State      | Server State    | Action                                      |
| ---- | ----------------- | --------------- | ------------------------------------------- |
| 1    | RUNNABLE          | RUNNING (busy)  | Server processing, not waiting              |
| 2    | RUNNABLE          | RUNNING (busy)  | Client calls `MsgSend()`                    |
| 3    | **SEND BLOCKED**  | RUNNING (busy)  | Server not available; client blocks waiting |
| 4    | SEND BLOCKED      | RECEIVE BLOCKED | Server finally calls `MsgReceive()`         |
| 5    | **REPLY BLOCKED** | RUNNING         | Kernel copies data; server receives message |
| 6    | REPLY BLOCKED     | RUNNING         | Server processes the message                |
| 7    | REPLY BLOCKED     | RUNNING         | Server calls `MsgReply()`                   |
| 8    | RUNNABLE          | [...]           | Kernel copies reply; client unblocks        |

**Key Points:**

- Client first enters **SEND BLOCKED** state
- No data copy occurs until server calls `MsgReceive()`
- When server receives, client transitions from SEND BLOCKED to **REPLY BLOCKED**
- Client remains REPLY BLOCKED until `MsgReply()` is called

---

## 4. Channels and Connections

### Channels (Server Side)

A **channel** is a kernel object created by a server to receive messages. A server needs **only one channel** to receive messages from **multiple clients**.

```
                    ┌─────────────────┐
     Client A ─────▶│                 │
     (coid 1)       │   Server        │
                    │   Channel       │◀── MsgReceive()
     Client B ─────▶│   (chid)        │
     (coid 2)       │                 │
                    │   [Thread Pool] │
     Client C ─────▶│                 │
     (coid 3)       └─────────────────┘
```

**Channel Characteristics:**

- Created once per server (or per service)
- Multiple clients can connect to the same channel
- Multiple server threads can receive from the same channel — kernel picks available thread
- Kernel load-balances messages across available server threads
- Messages are queued in **priority order** when no thread is waiting

### Connections (Client Side)

A **connection** (Connection ID, or `coid`) is a client-side handle to a server's channel.

```
┌─────────────┐                      ┌─────────────────┐
│   Client A  │── coid=1 ─────────▶  │  Server C       │
│             │── coid=2 ─────────▶  │  (Channel 1)    │
│             │                      └─────────────────┘
│             │── coid=3 ─────────▶┌─────────────────┐
└─────────────┘                    │  Server D       │
                                   │  (Channel 2)    │
                                   └─────────────────┘
```

**Connection Scenarios:**

- One client → Multiple connections to one server
- One client → Connections to multiple servers
- Multiple clients → One connection each to one server (typical)

---

## 5. Core API Reference

### ChannelCreate()

Creates a channel for the calling process (server) to receive messages.

```c
#include <sys/neutrino.h>

int ChannelCreate(unsigned flags);
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `flags`   | Bitmask of channel behavior flags. Use `0` for default public channel. |

**Return Value:**

- **Success:** Channel ID (`chid`) — positive integer
- **Failure:** `-1`, `errno` set

**Description:**

- Creates a channel attached to the calling process
- If process has multiple threads, kernel dispatches incoming messages to whichever thread calls `MsgReceive()`
- Kernel load-balances across threads waiting on same channel
- To create a **public channel**, process must have `PROCMGR_AID_PUBLIC_CHANNEL` ability

**Example:**

```c
int chid = ChannelCreate(0);  // Public channel with default flags
if (chid == -1) {
    perror("ChannelCreate failed");
    exit(EXIT_FAILURE);
}
```

---

### ChannelCreate() Flags

| Flag                       | Description                                                  |
| -------------------------- | ------------------------------------------------------------ |
| `_NTO_CHF_PRIVATE`         | **Private Channel** — For internal process use only. Other processes cannot connect. Useful for receiving kernel notifications without exposing channel externally. |
| `_NTO_CHF_COID_DISCONNECT` | **COID Disconnect Pulse** — For processes that are both server AND client. When a connection ID (coid) that this process uses as a client becomes invalid, kernel delivers a pulse (`_PULSE_CODE_COIDDEATH`) with the coid value. |
| `_NTO_CHF_DISCONNECT`      | **Client Disconnect Pulse** — When a client detaches all connections (or dies), kernel delivers a pulse (`_PULSE_CODE_DISCONNECT`). Server must call `ConnectDetach(scoid)` to clean up. |
| `_NTO_CHF_UNBLOCK`         | **Unblock Pulse** — When a REPLY-blocked client wants to unblock (timeout, signal, killed), kernel sends a pulse (`_PULSE_CODE_UNBLOCK`) with the rcvid. Server can cancel operation and reply with error. |
| `_NTO_CHF_FIXED_PRIORITY`  | **Disable Priority Inheritance** — Receiving threads do NOT inherit sender's priority. Use with caution. |
| `_NTO_CHF_THREAD_DEATH`    | **Thread Death Pulse** — Deliver a pulse (`_PULSE_CODE_THREADDEATH`) on death of any thread in the process that owns the channel. |
| `_NTO_CHF_INHERIT_RUNMASK` | **Inherit Runmask** — Receiver inherits sender's processor runmask. |

**Example with flags:**

```c
int chid = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK);
// Channel notifies when clients disconnect or want to unblock
```

---

### ConnectAttach()

Creates a connection from a client process to a server's channel.

```c
#include <sys/neutrino.h>

int ConnectAttach(uint32_t nd,     // Node descriptor (0 = local)
                  pid_t pid,       // Server's process ID
                  int chid,        // Server's channel ID
                  unsigned index,  // Must be _NTO_SIDE_CHANNEL
                  int flags);      // Connection flags (usually 0)
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `nd`      | Node descriptor. Use `0` for local node.                     |
| `pid`     | Process ID of the server process.                            |
| `chid`    | Channel ID returned by server's `ChannelCreate()`.           |
| `index`   | **Must be `_NTO_SIDE_CHANNEL`**. Specifies side channel type coid. |
| `flags`   | Connection flags. Usually `0`.                               |

**Return Value:**

- **Success:** Connection ID (`coid`) — positive integer in side channel range
- **Failure:** `-1`, `errno` set

**Why `_NTO_SIDE_CHANNEL` is mandatory:**

QNX has two types of Connection IDs:

| Type                | Range                        | Used By                            |
| ------------------- | ---------------------------- | ---------------------------------- |
| **File Descriptor** | Low values (0, 1, 2, 3, ...) | `open()`, `socket()`, standard I/O |
| **Side Channel**    | High values (0x40000000+)    | `ConnectAttach()`, message passing |

**Problems if you don't use `_NTO_SIDE_CHANNEL`:**

- coid could conflict with file descriptors used by library functions like `open()`
- After `fork()`, child process could inherit conflicting coids → deadlock or failure
- Unpredictable behavior when mixing file I/O with message passing

**Example:**

```c
int coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);
if (coid == -1) {
    perror("ConnectAttach failed");
    exit(EXIT_FAILURE);
}
```

---

### MsgSend()

Sends a message to a server and blocks until the server replies.

```c
#include <sys/neutrino.h>

long MsgSend(int coid,              // Connection ID to server
             const void *smsg,      // Message to send
             size_t sbytes,         // Size of message
             void *rmsg,            // Buffer for reply
             size_t rbytes);        // Size of reply buffer
```

**Parameters:**

| Parameter | Description                                           |
| --------- | ----------------------------------------------------- |
| `coid`    | Connection ID from `ConnectAttach()`.                 |
| `smsg`    | Pointer to message buffer to send.                    |
| `sbytes`  | Number of bytes to send. Must not exceed `SSIZE_MAX`. |
| `rmsg`    | Pointer to buffer where server's reply is stored.     |
| `rbytes`  | Size of reply buffer. Must not exceed `SSIZE_MAX`.    |

**Return Value:**

- **Success:** Status value from server's `MsgReply()` (0 or positive)
- **Failure:** `-1`, `errno` set (server may have called `MsgError()`)

**Blocking States:**

| State         | Condition                                                    |
| ------------- | ------------------------------------------------------------ |
| `STATE_SEND`  | Message sent but not yet received. Skipped if server is RECEIVE-blocked. |
| `STATE_REPLY` | Message received but not yet replied to.                     |

**Example:**

```c
char send_buffer[256] = "Hello Server!";
char reply_buffer[256];

int status = MsgSend(coid, send_buffer, strlen(send_buffer) + 1, 
                     reply_buffer, sizeof(reply_buffer));
if (status == -1) {
    perror("MsgSend failed");
} else {
    printf("Server replied: %s\n", reply_buffer);
    printf("Status: %d\n", status);
}
```

---

### MsgReceive()

Receives a message on a channel. Blocks if no messages available.

```c
#include <sys/neutrino.h>

rcvid_t MsgReceive(int chid,            // Channel ID
                   void *msg,           // Buffer for received message
                   size_t bytes,        // Size of buffer
                   struct _msg_info *info);  // Sender info (can be NULL)
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `chid`    | Channel ID from `ChannelCreate()`. Use `-1` to dissociate from last channel. |
| `msg`     | Pointer to buffer for received data. **Must be big enough to contain a pulse.** |
| `bytes`   | Size of receive buffer. Must not exceed `SSIZE_MAX`.         |
| `info`    | Pointer to `_msg_info` for sender metadata. Can be `NULL`.   |

**Return Value:**

- **> 0:** Message received; value is `rcvid` (receive identifier) for replying
- **0:** Pulse received; `msg` contains `_pulse` structure
- **-1:** Error, `errno` set

**The `_msg_info` Structure:**

```c
struct _msg_info {
    uint32_t  nd;           // Node descriptor of sender
    uint32_t  srcnd;        // Source node descriptor
    pid_t     pid;          // Process ID of sender
    int32_t   tid;          // Thread ID of sender
    int32_t   chid;         // Channel ID
    int32_t   scoid;        // Server connection ID
    int32_t   coid;         // Client's connection ID
    int32_t   msglen;       // Length of message sent
    int32_t   srcmsglen;    // Length of source message
    int32_t   dstmsglen;    // Length of destination buffer
    int16_t   priority;     // Priority of sending thread
    int16_t   flags;        // Flags
    int32_t   reserved;     // Reserved
};
```

**Example:**

```c
char recv_buffer[512];
struct _msg_info info;

int rcvid = MsgReceive(chid, recv_buffer, sizeof(recv_buffer), &info);
if (rcvid == -1) {
    perror("MsgReceive failed");
    exit(EXIT_FAILURE);
}

if (rcvid == 0) {
    // Handle pulse
    struct _pulse *pulse = (struct _pulse *)recv_buffer;
    printf("Pulse received: code=%d, value=%d\n", pulse->code, pulse->value.sival_int);
} else {
    printf("Message from PID %d, TID %d, priority %d\n",
           info.pid, info.tid, info.priority);
    printf("Message: %s\n", recv_buffer);
    // Use rcvid to reply
}
```

---

### MsgReply()

Replies to a message, unblocking the client.

```c
#include <sys/neutrino.h>

int MsgReply(rcvid_t rcvid,           // Receive ID from MsgReceive()
             long status,             // Status for client's MsgSend()
             const void *msg,         // Reply message
             size_t bytes);           // Size of reply
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `rcvid`   | Receive ID from `MsgReceive()`. Identifies which client to reply to. |
| `status`  | Status returned to client's `MsgSend()`. **Must be 0 or positive.** Negative reserved for errors — use `MsgError()` instead. |
| `msg`     | Pointer to reply buffer. Can be `NULL` if no data.           |
| `bytes`   | Size of reply. Use `0` if `msg` is `NULL`. Must not exceed `SSIZE_MAX`. |

**Return Value:**

- **Success:** `EOK`
- **Failure:** `-1`, `errno` set

**Important Notes:**

- `MsgReply()` does **NOT** block
- Server's inherited priority does **NOT** revert after reply — remains until explicitly changed or another message received
- Can reply to messages in any order, but must eventually reply to each
- Any thread in the receiving process can reply

**Example:**

```c
// Reply with data
char reply_msg[] = "Message processed!";
MsgReply(rcvid, EOK, reply_msg, strlen(reply_msg) + 1);

// Reply with status only (no data)
MsgReply(rcvid, EOK, NULL, 0);
```

---

### MsgError()

Sends an error reply, unblocking the client with an error code.

```c
#include <sys/neutrino.h>

int MsgError(rcvid_t rcvid,           // Receive ID from MsgReceive()
             int error);              // Error code
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `rcvid`   | Receive ID from `MsgReceive()`.                              |
| `error`   | Error code: `-1` (unblock pulse only), `ERESTART`, `EOK`, or standard POSIX error (e.g., `ENOSYS`, `EINVAL`). |

**Behavior:**

- Client's `MsgSend()` returns `-1`
- Client's `errno` set to `error` value
- No data is transferred

**Example:**

```c
// Unsupported message type
MsgError(rcvid, ENOSYS);

// Invalid parameters
MsgError(rcvid, EINVAL);
```

**MsgReply() vs MsgError()**

|                              | `MsgReply()`          | `MsgError()`               |
| ---------------------------- | --------------------- | -------------------------- |
| Client's `MsgSend()` returns | `status` (≥ 0)        | `-1`                       |
| Client's `errno`             | Unchanged             | Set to `error`             |
| Data reply                   | Yes (optional)        | No                         |
| Use case                     | Success, normal reply | Error, unsupported request |

---

## 6. Data Copying Mechanism

QNX Message Passing involves **kernel-managed data copying** between process address spaces. The kernel **never passes pointers** — it always performs actual memory copies.

### Send Phase (Client → Server)

```
Client Address Space          Kernel              Server Address Space
┌─────────────────┐          ┌─────────┐          ┌─────────────────┐
│  smsg buffer    │─────────▶│  Copy   │─────────▶│  rmsg buffer    │
│  [message data] │          │  Data   │          │  [message data] │
│  size = sbytes  │          │         │          │  size = rbytes  │
└─────────────────┘          └─────────┘          └─────────────────┘
```

**Rule:** Kernel copies `min(sbytes, rbytes)` bytes. Excess data is NOT copied.

### Reply Phase (Server → Client)

```
Server Address Space          Kernel              Client Address Space
┌─────────────────┐          ┌─────────┐          ┌─────────────────┐
│  reply buffer   │─────────▶│  Copy   │─────────▶│  rmsg buffer    │
│  [reply data]   │          │  Data   │          │  [reply data]   │
│  size = sbytes  │          │         │          │  size = rbytes  │
└─────────────────┘          └─────────┘          └─────────────────┘
```

**Rule:** Kernel copies `min(sbytes, rbytes)` bytes. Same truncation behavior.

### Output-Only Operations (No Reply Data)

For operations where client sends data but expects no data back:

```c
// Client
MsgSend(coid, write_buffer, buffer_size, NULL, 0);

// Server
MsgReply(rcvid, EOK, NULL, 0);  // Status = success, no data
```

- Client's reply buffer can be `NULL` with size `0`
- Server's `MsgReply()` has `msg = NULL` and `bytes = 0`
- `status` (`EOK`) still indicates successful delivery

---

## 7. Complete Code Examples

### Server Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <errno.h>

#define BUFFER_SIZE 512

int main(int argc, char *argv[]) {
    int chid;
    int rcvid;
    char recv_buffer[BUFFER_SIZE];
    char reply_buffer[BUFFER_SIZE];
    struct _msg_info info;

    // Step 1: Create channel with disconnect and unblock notifications
    chid = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK);
    if (chid == -1) {
        perror("ChannelCreate failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started. PID: %d, Channel ID: %d\n", getpid(), chid);
    printf("Waiting for messages...\n\n");

    // Step 2: Main message receive loop
    for (;;) {
        // Block waiting for message or pulse
        rcvid = MsgReceive(chid, recv_buffer, sizeof(recv_buffer), &info);
        if (rcvid == -1) {
            perror("MsgReceive failed");
            continue;
        }

        // Check if pulse (rcvid == 0)
        if (rcvid == 0) {
            struct _pulse *pulse = (struct _pulse *)recv_buffer;
            switch (pulse->code) {
                case _PULSE_CODE_DISCONNECT:
                    printf("Client disconnected (scoid=%d)\n", pulse->scoid);
                    ConnectDetach(pulse->scoid);
                    break;
                case _PULSE_CODE_UNBLOCK:
                    printf("Client wants to unblock (rcvid=%d)\n", pulse->value.sival_int);
                    break;
                default:
                    printf("Unknown pulse: code=%d\n", pulse->code);
            }
            continue;
        }

        printf("Message from PID %d, TID %d, priority %d\n",
               info.pid, info.tid, info.priority);
        printf("Content: %s\n", recv_buffer);

        // Step 3: Process and reply
        snprintf(reply_buffer, sizeof(reply_buffer), 
                 "Server received: %s", recv_buffer);

        if (MsgReply(rcvid, EOK, reply_buffer, strlen(reply_buffer) + 1) == -1) {
            perror("MsgReply failed");
        }
    }

    ChannelDestroy(chid);
    return 0;
}
```

### Client Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <errno.h>

#define BUFFER_SIZE 512

int main(int argc, char *argv[]) {
    int coid;
    int server_pid;
    int server_chid;
    char send_buffer[BUFFER_SIZE];
    char reply_buffer[BUFFER_SIZE];
    int status;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_pid> <server_chid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_pid = atoi(argv[1]);
    server_chid = atoi(argv[2]);

    // Step 1: Attach to server's channel
    coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server. PID: %d, Channel: %d, COID: %d\n",
           server_pid, server_chid, coid);

    // Step 2: Send message
    snprintf(send_buffer, sizeof(send_buffer), "Hello from PID %d!", getpid());
    printf("Sending: %s\n", send_buffer);

    status = MsgSend(coid, send_buffer, strlen(send_buffer) + 1,
                     reply_buffer, sizeof(reply_buffer));

    if (status == -1) {
        perror("MsgSend failed");
        ConnectDetach(coid);
        exit(EXIT_FAILURE);
    }

    printf("MsgSend() returned status: %d\n", status);
    printf("Server reply: %s\n", reply_buffer);

    // Step 3: Detach
    ConnectDetach(coid);
    return 0;
}
```

### Build and Run

```bash
# Compile server
qcc -o msg_server msg_server.c

# Compile client
qcc -o msg_client msg_client.c

# Terminal 1: Start server
./msg_server
# Output: Server started. PID: 12345, Channel ID: 1

# Terminal 2: Run client
./msg_client 12345 1
# Output: Connected to server. PID: 12345, Channel: 1, COID: 1073741825
#         Sending: Hello from PID 12346!
#         MsgSend() returned status: 0
#         Server reply: Server received: Hello from PID 12346!
```

---

## 8. Connection ID (coid) Types

QNX maintains two distinct ranges for Connection IDs:

### File Descriptor Range

- **Values:** Low integers (0, 1, 2, 3, ...)
- **Used by:** `open()`, `socket()`, `dup()`, standard I/O
- **Managed by:** Process's file descriptor table
- **Inherited on fork:** Yes (with appropriate flags)

### Side Channel Range

- **Values:** High values (typically `0x40000000` and above)
- **Used by:** `ConnectAttach()`, message passing APIs
- **Managed by:** Kernel's connection table
- **Purpose:** Avoid conflicts with file descriptors

### Viewing Connection IDs

**Method 1: Using pidin (Command Line)**

```bash
# View file descriptors and side channels
pidin fd | less

# View specific process
pidin -p <pid> fd

# Example output:
#  pid tid name               fd  type     offset  mount
# 12345   1 msg_server        0  CONSOLE       0  /dev/console
# 12345   1 msg_server     4001  SIDECHAN      0  Channel 1
```

**Method 2: Using QNX IDE (System Information Perspective)**

1. Switch to **QNX System Information** perspective
2. Expand the process tree
3. Select a process
4. View **Connection Information** tab
5. Click three dots → Select **"Full Side Channels"**
6. Side channels show as hexadecimal values (e.g., `0x40000001`)

---

## 9. Debugging with pidin

The `pidin` utility is essential for debugging QNX message passing.

### Viewing Thread States

```bash
pidin
```

**Example Output:**

```
  pid  tid name               prio STATE          Blocked
12345    1 msg_server           10 RECEIVE        1
12346    1 msg_client           10 REPLY          12345
12347    1 busy_process         10 RUNNING
```

### Understanding States

| State     | Meaning                                                | Blocked Column             |
| --------- | ------------------------------------------------------ | -------------------------- |
| `RECEIVE` | Thread blocked in `MsgReceive()`, waiting for messages | Channel ID it's waiting on |
| `SEND`    | Thread called `MsgSend()`, server hasn't received yet  | Server's process ID        |
| `REPLY`   | Message received, waiting for `MsgReply()`             | Server's process ID        |
| `RUNNING` | Thread currently executing                             | —                          |
| `READY`   | Thread ready to run, waiting for CPU                   | —                          |

### Interpreting the Blocked Column

```
# Server in RECEIVE state
12345    1 msg_server           10 RECEIVE        1
#                                      │            │
#                                      │            └── Waiting on Channel ID 1
#                                      └── State: RECEIVE blocked

# Client in REPLY state
12346    1 msg_client           10 REPLY          12345
#                                      │            │
#                                      │            └── Waiting for reply from PID 12345
#                                      └── State: REPLY blocked

# Client in SEND state
12346    1 msg_client           10 SEND           12345
#                                      │            │
#                                      │            └── Waiting to send to PID 12345
#                                      └── State: SEND blocked
```

### Useful pidin Commands

```bash
# Show all threads with formatted output
pidin -F "%a %b %N %p %S %B"

# Show file descriptors and connections
pidin -p <pid> fd

# Show memory information
pidin -p <pid> mem

# Show channel information
pidin -p <pid> chid
```

---

## 10. Thread State Transitions

### Client Thread States

```
RUNNABLE ──MsgSend()──▶ SEND BLOCKED ──(server receives)──▶ REPLY BLOCKED ──MsgReply()──▶ RUNNABLE
                        (server busy)                      (server processing)

RUNNABLE ──MsgSend()──▶ REPLY BLOCKED ──MsgReply()──▶ RUNNABLE
                        (server available)

RUNNABLE ──MsgSend()──▶ RUNNABLE (error)
                        (server died while blocked)
```

### Server Thread States

```
RECEIVE BLOCKED ──MsgReceive()──▶ RUNNING (processing) ──MsgReply()──▶ RECEIVE BLOCKED

RUNNING ──MsgReceive()──▶ RUNNING (no block, message already queued)
```

---

## 11. Key Principles

| Principle                          | Explanation                                                  |
| ---------------------------------- | ------------------------------------------------------------ |
| **Synchronous by design**          | Client blocks until reply; no hidden message queues          |
| **Priority inheritance**           | Server inherits client's priority; prevents priority inversion |
| **Kernel copies data**             | No pointer passing; actual memory copies between address spaces |
| **One channel, many clients**      | Single server channel handles all client connections         |
| **Always use `_NTO_SIDE_CHANNEL`** | Prevents coid conflicts with file descriptors                |
| **Handle pulses**                  | Check `rcvid == 0` for disconnect/unblock notifications      |
| **Reply to every message**         | Client remains blocked until `MsgReply()` or `MsgError()`    |
| **Monitor with `pidin`**           | Essential for understanding thread states and diagnosing blocking |

---

## 12. Summary

### API Quick Reference

| Function          | Caller | Purpose                            |
| ----------------- | ------ | ---------------------------------- |
| `ChannelCreate()` | Server | Create channel to receive messages |
| `ConnectAttach()` | Client | Connect to server's channel        |
| `MsgSend()`       | Client | Send message and block for reply   |
| `MsgReceive()`    | Server | Receive message from client        |
| `MsgReply()`      | Server | Reply to message, unblock client   |
| `MsgError()`      | Server | Send error reply, unblock client   |

### Core Concepts

| Concept                  | Description                                                  |
| ------------------------ | ------------------------------------------------------------ |
| **Channel (`chid`)**     | Server-side object for receiving messages                    |
| **Connection (`coid`)**  | Client-side handle to a channel                              |
| **Receive ID (`rcvid`)** | Temporary identifier for in-flight message; used to reply    |
| **Pulse**                | Lightweight async notification; `rcvid == 0` in `MsgReceive()` |

---
