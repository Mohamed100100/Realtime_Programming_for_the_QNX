# QNX Pulses — Non-Blocking Notifications

## Overview

This section covers **pulses**, the lightweight, non-blocking notification mechanism in QNX. Pulses are used for event signaling, kernel notifications, and asynchronous communication where a full synchronous message would be overkill.

---

## 1. What is a Pulse?

### Concept

A **pulse** is a small, **non-blocking** message sent from one process to another (or from the kernel to a process). Unlike regular QNX messages, pulses do not require a reply.

| Characteristic | Pulse                             | Regular Message                 |
| -------------- | --------------------------------- | ------------------------------- |
| **Blocking**   | No — "send and forget"            | Yes — client blocks until reply |
| **Reply**      | None                              | Required (`MsgReply()`)         |
| **Data size**  | Small — 72 bits of sender payload | Large — up to `SSIZE_MAX` bytes |
| **Direction**  | Unidirectional                    | Bidirectional (request/reply)   |
| **Speed**      | Faster and cheaper                | Slower due to synchronization   |
| **Use case**   | Notifications, signals, events    | Data transfer, RPC-style calls  |

### Pulse Payload

The sender only controls **two fields**: `code` and `value`. The kernel fills the rest.

```
Sender calls: MsgSendPulse(coid, priority, code, value)
                    │                              │
                    │                              └──→ pulse.value (64-bit)
                    └──────────────────────────────────→ pulse.code (8-bit)

Kernel delivers full struct _pulse (24 bytes):
┌──────────────────────────────────────────────────────────────┐
│  uint16_t type       │  _PULSE_TYPE (kernel sets)            │
│  uint16_t subtype    │  0 (kernel sets)                      │
│  int8_t   code       │  From MsgSendPulse() — sender controls│
│  uint8_t  zero1      │  0 (kernel sets, padding)             │
│  union value {       │  From MsgSendPulse() — sender controls│
│    int32_t sival_int │                                       │
│    void*   sival_ptr │                                       │
│    int64_t sival_long│                                       │
│  }                   │                                       │
│  int32_t  zero2      │  0 (kernel sets)                      │
│  int32_t  scoid      │  Server connection ID (kernel sets)   │
└──────────────────────────────────────────────────────────────┘
```

> **Key Point:** Pulses are ideal for quick notifications ("wake up, something changed") — not for transferring large amounts of data. Don't send 4KB of data as 512 pulses!

---

## 2. Pulse Code Ranges

### System vs. User Codes

| Range             | Values         | Used By                                                      |
| ----------------- | -------------- | ------------------------------------------------------------ |
| **System/Kernel** | `-128` to `-1` | QNX kernel notifications (client disconnect, unblock, thread death, etc.) |
| **User-defined**  | `0` to `127`   | Your application pulses                                      |

### User-Defined Code Constants

```c
#include <sys/neutrino.h>

// User pulse code range
_PULSE_CODE_MINAVAIL   // 0  — minimum available user pulse code
_PULSE_CODE_MAXAVAIL   // 127 — maximum available user pulse code
```

**Recommended practice:**

```c
// Define your application pulse codes starting from _PULSE_CODE_MINAVAIL
#define MY_PULSE_WAKEUP      (_PULSE_CODE_MINAVAIL + 0)
#define MY_PULSE_DATA_READY  (_PULSE_CODE_MINAVAIL + 1)
#define MY_PULSE_TIMER       (_PULSE_CODE_MINAVAIL + 2)
#define MY_PULSE_SHUTDOWN    (_PULSE_CODE_MINAVAIL + 3)
```

### System Pulse Codes (Negative Values)

| Pulse Code                | Value    | Triggered By                                            |
| ------------------------- | -------- | ------------------------------------------------------- |
| `_PULSE_CODE_COIDDEATH`   | Negative | Server's channel destroyed; client's coid invalidated   |
| `_PULSE_CODE_DISCONNECT`  | Negative | Client detached all connections (or died)               |
| `_PULSE_CODE_UNBLOCK`     | Negative | REPLY-blocked client wants to unblock (timeout, signal) |
| `_PULSE_CODE_THREADDEATH` | Negative | Thread in the channel's process died                    |

These are delivered automatically when the corresponding `ChannelCreate()` flags are set (`_NTO_CHF_DISCONNECT`, `_NTO_CHF_UNBLOCK`, etc.).

---

## 3. Sending Pulses

### MsgSendPulse()

Sends a pulse to a channel. Non-blocking — returns immediately.

```c
#include <sys/neutrino.h>

int MsgSendPulse(int coid,           // Connection ID to target channel
                 int priority,       // Priority for pulse delivery (-1 = caller's priority)
                 int code,           // 8-bit pulse code (0-127 for user)
                 int value);         // 32-bit value payload
```

**Parameters:**

| Parameter  | Description                                                  |
| ---------- | ------------------------------------------------------------ |
| `coid`     | Connection ID to the target channel (from `ConnectAttach()`). |
| `priority` | Priority for pulse delivery and server thread handling. `-1` = use caller's thread priority. `1` to `253` = explicit priority. |
| `code`     | 8-bit pulse code. Use `0` to `127` for user-defined codes.   |
| `value`    | 32-bit integer payload.                                      |

**Return Value:**

- **Success:** `EOK`
- **Failure:** `-1`, `errno` set

**Example:**

```c
// Send a "data ready" notification pulse
int coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);

// Send pulse with caller's priority
MsgSendPulse(coid, -1, MY_PULSE_DATA_READY, 42);

// Send pulse with explicit priority
MsgSendPulse(coid, 20, MY_PULSE_WAKEUP, 0);
```

### MsgSendPulsePtr()

Sends a pulse with a pointer value instead of an integer.

```c
#include <sys/neutrino.h>

int MsgSendPulsePtr(int coid,        // Connection ID
                    int priority,    // Priority
                    int code,        // Pulse code
                    void *value);    // 64-bit pointer value
```

**Use when:** You need to pass a pointer (e.g., to a shared memory structure) rather than a small integer.

```c
struct shared_data *data = get_shared_data();
MsgSendPulsePtr(coid, -1, MY_PULSE_DATA_READY, data);
```

---

## 4. Pulse Priority

### How Priority Works for Pulses

Unlike messages where the server **inherits** the client's priority, pulses store the priority **with the pulse data** because the sender is not blocked.

```
Message Priority Inheritance:
  Client (priority 20) ──MsgSend()──▶ Server runs at priority 20

Pulse Priority Delivery:
  Client (priority 20) ──MsgSendPulse(coid, -1, ...)──▶ Pulse stored with priority 20
                                                         Server thread runs at priority 20
                                                         when handling this pulse
```

### Priority Parameter Options

| Value        | Behavior                                      |
| ------------ | --------------------------------------------- |
| `-1`         | Use the **calling thread's current priority** |
| `1` to `253` | Use an **explicit priority** for this pulse   |

**Example:**

```c
// Pulse inherits caller's priority
MsgSendPulse(coid, -1, MY_PULSE_EVENT, 0);

// Pulse with high priority (urgent notification)
MsgSendPulse(coid, 10, MY_PULSE_URGENT, 0);

// Pulse with low priority (background update)
MsgSendPulse(coid, 100, MY_PULSE_UPDATE, 0);
```

---

## 5. Security: Sending Pulses to Other Processes

### Privilege Requirements

Sending pulses to another process requires appropriate privileges — similar to Unix signal rules:

| Scenario          | Requirement                               |
| ----------------- | ----------------------------------------- |
| Same user ID      | Allowed automatically                     |
| Different user ID | Requires **root privilege** (QNX ability) |

**Why?** Since pulses are non-blocking, a malicious process could flood another process with pulses, causing denial of service (memory exhaustion from pulse queuing). Requiring privileges prevents this attack.

> **Note:** Messages don't need this restriction because the server can inherently flow-control by delaying replies.

---

## 6. Receiving Pulses

### MsgReceive() Handles Both Messages and Pulses

Pulses are received on the **same channel** as regular messages, using the same `MsgReceive()` call.

```c
#include <sys/neutrino.h>

rcvid_t MsgReceive(int chid, void *msg, size_t bytes, struct _msg_info *info);
```

### Distinguishing Messages from Pulses

| Return Value (`rcvid`) | Meaning              | Action                                                 |
| ---------------------- | -------------------- | ------------------------------------------------------ |
| `> 0`                  | **Message received** | Use `rcvid` to reply with `MsgReply()` or `MsgError()` |
| `0`                    | **Pulse received**   | Process pulse data; **DO NOT reply**                   |
| `-1`                   | **Error**            | Check `errno`                                          |

### The `_pulse` Structure

```c
#include <sys/neutrino.h>

struct _pulse {
    uint16_t type;          // _PULSE_TYPE (kernel sets)
    uint16_t subtype;       // Reserved (0) (kernel sets)
    int8_t   code;          // Pulse code (8-bit: -128 to +127) — SENDER controls
    uint8_t  zero1;         // Reserved (0) (kernel sets, padding)
    union {
        int32_t  sival_int;     // 32-bit integer value — SENDER controls
        void    *sival_ptr;     // 64-bit pointer (via MsgSendPulsePtr) — SENDER controls
        int64_t  sival_long;    // 64-bit long value — SENDER controls
    } value;
    int32_t  zero2;         // Reserved (0) (kernel sets)
    int32_t  scoid;         // Server connection ID (kernel sets, identifies sender process)
};
```

**Key Fields:**

| Field            | Set By     | Description                                                  |
| ---------------- | ---------- | ------------------------------------------------------------ |
| `code`           | **Sender** | The pulse code passed to `MsgSendPulse()`                    |
| `value`          | **Sender** | The value/pointer passed to `MsgSendPulse()` / `MsgSendPulsePtr()` |
| `scoid`          | **Kernel** | Server connection ID — identifies which client process sent the pulse |
| `type`           | **Kernel** | Always `_PULSE_TYPE` (internal use)                          |
| `subtype`        | **Kernel** | Always `0`                                                   |
| `zero1`, `zero2` | **Kernel** | Reserved/alignment padding                                   |

> **Note:** Unlike messages, pulses do **not** fill in the `_msg_info` structure. Use `scoid` with `ConnectClientInfo()` to get more information about the sender.

---

## 7. Receive Buffer Size: Critical Requirement

### The 24-Byte Rule

The `_pulse` structure is **24 bytes**. Your receive buffer **must** be at least this large, or the pulse will be **lost**.

```
If receive buffer < 24 bytes:
  → Kernel dequeues pulse
  → Tries to copy to buffer → WON'T FIT
  → Returns -1, errno = EFAULT
  → Pulse data is LOST (cannot be recovered)
```

### Safe Buffer Design with Unions

The recommended pattern is to use a **union** that includes `struct _pulse`:

```c
#include <sys/neutrino.h>

// Define your message structures
struct msg_header {
    uint16_t type;
};

struct msg_calculate {
    struct msg_header hdr;
    int32_t operand_a;
    int32_t operand_b;
};

struct msg_status {
    struct msg_header hdr;
    uint32_t request_id;
};

// Union: guarantees buffer is at least as large as the biggest member
union my_message {
    struct _pulse       pulse;      // 24 bytes — ensures minimum size
    struct msg_header   hdr;
    struct msg_calculate calc;
    struct msg_status   status;
};

// Usage in server
union my_message msg;
struct _msg_info info;
int rcvid;

rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

if (rcvid > 0) {
    // Regular message — branch on type
    switch (msg.hdr.type) {
        case MSG_TYPE_CALCULATE:  handle_calc(&msg.calc);  break;
        case MSG_TYPE_STATUS:     handle_status(&msg.status); break;
        // ...
    }
} else if (rcvid == 0) {
    // Pulse — do NOT reply
    handle_pulse(&msg.pulse);
}
```

> **C Language Guarantee:** The size of a union is at least the size of its largest member. Including `struct _pulse` ensures the buffer is always ≥ 24 bytes.

---

## 8. Pulse Queuing and Reliability

### Default Behavior: Unlimited Queuing

By default, pulses are queued until:

- A thread calls `MsgReceive()` to consume them
- The channel is destroyed
- **System runs out of memory**

```
Pulse Queue (default, per channel):
┌─────────────────────────────────────────────────────────────┐
│  Pulse 1  │  Pulse 2  │  Pulse 3  │  ...  │  Pulse N        │
│  (24B)    │  (24B)    │  (24B)    │       │  (24B)          │
└─────────────────────────────────────────────────────────────┘
     ▲                                                    │
     │                                                    │
  MsgSendPulse()                                    MsgReceive()
  (sender adds)                                     (server removes)
```

| Concern               | Risk                                                         |
| --------------------- | ------------------------------------------------------------ |
| **Dropped pulses**    | If a server is slow, pulses pile up. Dropped pulses may not be detectable. |
| **Memory exhaustion** | A misbehaving process flooding pulses can drain system memory, affecting other processes. |

### Pulse Conglomeration (Kernel Optimization)

The kernel optimizes pulse queuing by **conglomerating identical pulses**:

```
Multiple identical pulses (same code, value, priority, origin):
  → Stored as ONE queue entry with a COUNT
  → Saves memory, reduces queue pressure

Different pulses (different code, value, or origin):
  → Each gets its own queue entry
```

**Example:**

```c
// 100 identical pulses sent rapidly
for (int i = 0; i < 100; i++) {
    MsgSendPulse(coid, -1, MY_PULSE_TIMER, 0);
}
// Kernel stores: 1 queue entry with count = 100

// 100 different pulses
for (int i = 0; i < 100; i++) {
    MsgSendPulse(coid, -1, MY_PULSE_TIMER, i);  // Different value each time
}
// Kernel stores: 100 separate queue entries
```

> **Important:** Conglomeration only happens when **all fields match**: identical `code`, `value`, `priority`, and origin. Different values → separate entries.

---

## 9. Pulse Pools: Pre-Allocated Queues

### Why Use Pulse Pools?

For **safety-critical systems**, unlimited pulse queuing is dangerous:

- A rogue or buggy process could flood your server with pulses
- Memory exhaustion affects the entire system
- Dropped pulses may leave the system in an inconsistent state

**Solution:** `ChannelCreatePulsePool()` pre-allocates a fixed number of pulse slots.

### ChannelCreatePulsePool()

```c
#include <sys/neutrino.h>

struct nto_channel_config {
    struct sigevent event;          // Notification event when pool exhausted
    unsigned num_pulses;            // Number of pulses to pre-allocate
    unsigned rearm_threshold;       // Flow-control rearm threshold
    unsigned options;               // Options flags
    unsigned reserved[3];           // Reserved
};

int ChannelCreatePulsePool(unsigned flags, struct nto_channel_config *config);
```

**Configuration Fields:**

| Field             | Description                                                  |
| ----------------- | ------------------------------------------------------------ |
| `num_pulses`      | Guaranteed minimum number of pulse slots pre-allocated       |
| `rearm_threshold` | Flow-control behavior when pool is exhausted: `0` = fire once and never rearm; `1` to `num_pulses` = rearm when utilization drops below threshold; `> num_pulses` = permanently armed |
| `options`         | `_NTO_CHO_CUSTOM_EVENT` — use custom `sigevent` instead of default SIGKILL |
| `event`           | Custom notification event (must be `SIGEV_SEM` or `SIGEV_NONE`) |

### Behavior When Pool is Exhausted

| Configuration                                            | Behavior on Overflow                                        |
| -------------------------------------------------------- | ----------------------------------------------------------- |
| **Default** (no custom event)                            | Process receives **SIGKILL** — terminated immediately       |
| **Custom event** (`_NTO_CHO_CUSTOM_EVENT` + `SIGEV_SEM`) | Specified semaphore is posted; server can handle gracefully |
| **`SIGEV_NONE`**                                         | Pulses are **silently dropped** — use with extreme caution  |

> **Warning:** Dropping pulses can leave the system inconsistent: a dropped interrupt pulse could leave an IRQ masked; a dropped disconnect pulse could leak a `scoid`.

### Pulse Pool Example

```c
#include <sys/neutrino.h>
#include <semaphore.h>

// Create a pulse pool with 64 pre-allocated slots
struct nto_channel_config config = {
    .event = { .sigev_notify = SIGEV_NONE },  // Default: SIGKILL on overflow
    .num_pulses = 64,                           // Pre-allocate 64 pulse slots
    .rearm_threshold = 32,                       // Rearm notification when below 32
    .options = 0
};

int chid = ChannelCreatePulsePool(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK, &config);
if (chid == -1) {
    perror("ChannelCreatePulsePool");
    exit(EXIT_FAILURE);
}

printf("Channel created with pulse pool: %d slots
", config.num_pulses);
```

### Pulse Pool with Custom Notification

```c
#include <semaphore.h>

sem_t pulse_sem;

// Initialize semaphore
sem_init(&pulse_sem, 0, 0);

// Configure custom notification via semaphore
struct nto_channel_config config = {
    .event = {
        .sigev_notify = SIGEV_SEM,
        .sigev_sem = &pulse_sem
    },
    .num_pulses = 128,
    .rearm_threshold = 64,
    .options = _NTO_CHO_CUSTOM_EVENT
};

int chid = ChannelCreatePulsePool(_NTO_CHF_DISCONNECT, &config);

// In a monitoring thread:
while (1) {
    sem_wait(&pulse_sem);
    printf("WARNING: Pulse pool nearly exhausted!
");
    // Take corrective action: drain pulses, alert operator, etc.
}
```

### Pulse Pool vs. Default Queuing

| Aspect                | Default ChannelCreate()                 | ChannelCreatePulsePool()           |
| --------------------- | --------------------------------------- | ---------------------------------- |
| **Memory**            | Dynamic allocation from global pool     | Pre-allocated, fixed size          |
| **Overflow behavior** | Unbounded growth until OOM              | SIGKILL or custom notification     |
| **Isolation**         | Other processes can exhaust global pool | Dedicated pool per channel         |
| **Conglomeration**    | Yes                                     | Yes                                |
| **Use case**          | General-purpose, non-critical           | Safety-critical, real-time systems |

---

## 10. Complete Server Example: Handling Messages and Pulses

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <errno.h>

#define MSG_BASE        0x0200
#define MSG_TYPE_CALC   (MSG_BASE + 0)
#define MSG_TYPE_STATUS (MSG_BASE + 1)

#define MY_PULSE_TIMER      (_PULSE_CODE_MINAVAIL + 0)
#define MY_PULSE_DATA_READY (_PULSE_CODE_MINAVAIL + 1)

// Message structures
struct msg_header {
    uint16_t type;
};

struct msg_calc {
    struct msg_header hdr;
    int32_t a;
    int32_t b;
};

// Union for receive buffer — includes struct _pulse for safety
union my_message {
    struct _pulse       pulse;
    struct msg_header   hdr;
    struct msg_calc     calc;
};

// Reply structures
struct reply_calc {
    int32_t result;
};

void handle_message(int rcvid, union my_message *msg) {
    struct reply_calc reply;

    switch (msg->hdr.type) {
        case MSG_TYPE_CALC:
            reply.result = msg->calc.a + msg->calc.b;
            MsgReply(rcvid, EOK, &reply, sizeof(reply));
            break;

        default:
            fprintf(stderr, "Unknown message type: %d
", msg->hdr.type);
            MsgError(rcvid, ENOSYS);
            break;
    }
}

void handle_pulse(struct _pulse *pulse) {
    switch (pulse->code) {
        case MY_PULSE_TIMER:
            printf("[PULSE] Timer tick received, value=%d
", pulse->value.sival_int);
            break;

        case MY_PULSE_DATA_READY:
            printf("[PULSE] Data ready notification, value=%d
", pulse->value.sival_int);
            break;

        case _PULSE_CODE_DISCONNECT:
            printf("[PULSE] Client disconnected, scoid=%d
", pulse->scoid);
            ConnectDetach(pulse->scoid);
            break;

        case _PULSE_CODE_UNBLOCK:
            printf("[PULSE] Client wants to unblock, rcvid=%d
", pulse->value.sival_int);
            break;

        default:
            printf("[PULSE] Unknown pulse code: %d
", pulse->code);
            break;
    }
}

int main(void) {
    int chid;
    union my_message msg;
    struct _msg_info info;
    int rcvid;

    // Create channel with disconnect and unblock notifications
    chid = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK);
    if (chid == -1) {
        perror("ChannelCreate");
        exit(EXIT_FAILURE);
    }

    printf("Server started. PID: %d, Channel: %d
", getpid(), chid);

    for (;;) {
        // Receive both messages and pulses on the same channel
        rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            // PULSE — rcvid == 0, do NOT reply
            handle_pulse(&msg.pulse);
        } else {
            // MESSAGE — rcvid > 0, must reply eventually
            handle_message(rcvid, &msg);
        }
    }

    return 0;
}
```

---

## 11. Pulse Sender Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>

#define MY_PULSE_TIMER      (_PULSE_CODE_MINAVAIL + 0)
#define MY_PULSE_DATA_READY (_PULSE_CODE_MINAVAIL + 1)

int main(int argc, char *argv[]) {
    int coid;
    int server_pid, server_chid;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_pid> <server_chid>
", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_pid = atoi(argv[1]);
    server_chid = atoi(argv[2]);

    coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach");
        exit(EXIT_FAILURE);
    }

    // Send a timer pulse (using caller's priority)
    printf("Sending timer pulse...
");
    MsgSendPulse(coid, -1, MY_PULSE_TIMER, 42);

    // Send a data-ready pulse (with explicit high priority)
    printf("Sending data-ready pulse...
");
    MsgSendPulse(coid, 10, MY_PULSE_DATA_READY, 100);

    // Send a pulse with pointer value
    int shared_value = 999;
    printf("Sending pointer pulse...
");
    MsgSendPulsePtr(coid, -1, MY_PULSE_DATA_READY, &shared_value);

    ConnectDetach(coid);
    printf("Pulses sent successfully!
");

    return 0;
}
```

---

## 12. Messages vs. Pulses: When to Use Which

| Use Case                           | Mechanism      | Why                                             |
| ---------------------------------- | -------------- | ----------------------------------------------- |
| **Request data from server**       | Message        | Synchronous, guaranteed delivery, needs reply   |
| **Notify server of event**         | Pulse          | Non-blocking, no reply needed, lightweight      |
| **Periodic timer ticks**           | Pulse          | Fire-and-forget, server shouldn't block sender  |
| **Client disconnect notification** | Pulse (system) | Kernel delivers automatically, server cleans up |
| **Large data transfer**            | Message        | Pulses only carry 64 bits of value              |
| **RPC-style function call**        | Message        | Needs return value                              |
| **Interrupt handler → driver**     | Pulse          | ISR can't block; pulse wakes driver thread      |
| **Shared memory update signal**    | Pulse          | Notify readers that data changed                |

---

## 13. Key Principles

| Principle                            | Explanation                                                  |
| ------------------------------------ | ------------------------------------------------------------ |
| **Pulses are non-blocking**          | Sender returns immediately; no reply expected                |
| **Small payload only**               | Sender controls 72 bits (8-bit code + 64-bit value). Not for bulk data |
| **No reply to pulses**               | `rcvid == 0` means pulse — never call `MsgReply()`           |
| **Receive buffer ≥ 24 bytes**        | Include `struct _pulse` in a union to guarantee size         |
| **Use unions for receive buffers**   | Ensures space for both messages and pulses                   |
| **User codes: 0 to 127**             | Negative codes reserved for system/kernel use                |
| **Priority stored with pulse**       | `-1` = caller's priority; explicit value = custom priority   |
| **Privilege required for cross-UID** | Prevents DoS attacks via pulse flooding                      |
| **Consider pulse pools for safety**  | `ChannelCreatePulsePool()` prevents memory exhaustion        |
| **Pulses conglomerate if identical** | Same code/value/priority/origin → single queue entry with count |
| **Dropped pulses are dangerous**     | Can leave IRQs masked, scoids leaked, system inconsistent    |

---

## 14. Common Pitfalls

| Pitfall                                      | Problem                                    | Solution                                                     |
| -------------------------------------------- | ------------------------------------------ | ------------------------------------------------------------ |
| **Replying to a pulse**                      | `MsgReply(0, ...)` returns error           | Check `rcvid == 0` and skip reply                            |
| **Receive buffer too small**                 | Pulse lost, `errno = EFAULT`               | Use union with `struct _pulse`                               |
| **Using negative pulse codes**               | Conflicts with system pulses               | Start at `_PULSE_CODE_MINAVAIL` (0)                          |
| **Sending large data as pulses**             | Inefficient, wastes resources              | Use messages or shared memory                                |
| **No pulse pool in safety-critical systems** | Memory exhaustion risk                     | Use `ChannelCreatePulsePool()`                               |
| **Ignoring pulse pool overflow**             | Silent pulse drops or SIGKILL              | Set up custom notification with `SIGEV_SEM`                  |
| **Not handling system pulses**               | Resource leaks (scoid), missed disconnects | Set `_NTO_CHF_DISCONNECT` and handle `_PULSE_CODE_DISCONNECT` |
| **Assuming all pulses are delivered**        | With pools, pulses may be dropped          | Design for idempotency or use messages for critical data     |

---

*This module covers the fundamentals of QNX pulses. Advanced topics such as timer-based pulses, interrupt-to-pulse mapping, and pulse-driven state machines are covered in subsequent modules.*
