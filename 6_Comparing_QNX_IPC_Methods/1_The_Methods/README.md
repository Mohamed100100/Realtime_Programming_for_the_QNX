# QNX IPC Methods — Detailed Comparison

> **Companion to:** QNX IPC Methods Comparison Overview, Message Passing Fundamentals, Deadlock Avoidance, Server Designs, Multi-Part Messages, Event Delivery, and Shared Memory

This document provides a detailed comparison of **all IPC methods available in QNX Neutrino**, including both QNX-native mechanisms and POSIX/Unix-compatible APIs. Understanding the trade-offs of each method is essential for designing efficient embedded systems and porting existing code.

---

## 1. The IPC Methods Landscape

QNX provides two categories of IPC mechanisms:

```
┌──────────────────────────────────────────────────────────┐
│              QNX IPC Methods Landscape                   │
│                                                          │
│  ┌─────────────────────────┐  ┌─────────────────────────┐│
│  │   QNX NATIVE METHODS    │  │  POSIX / UNIX METHODS   ││
│  │   (Unique to QNX)       │  │  (Portable APIs)        ││
│  │                         │  │                         ││
│  │  • QNX Messages         │  │  • Signals              ││
│  │    MsgSend/Receive/Reply│  │  • Shared Memory        ││
│  │                         │  │  • Pipes                ││
│  │  • QNX Pulses           │  │  • POSIX Message Queues ││
│  │    MsgSendPulse         │  │  • TCP/IP Sockets       ││
│  │                         │  │                         ││
│  │  Built into microkernel │  │  Built on top of QNX    ││
│  │  No extra processes     │  │  messaging (require     ││
│  │  needed                 │  │  helper processes)      ││
│  └─────────────────────────┘  └─────────────────────────┘│
│                                                          │
│  ┌──────────────────────────────────────────────────────┐│
│  │         FILE DESCRIPTORS / RESOURCE MANAGERS         ││
│  │                                                      ││
│  │  • open() / read() / write() / fclose()              ││
│  │  • Built on QNX native messaging                     ││
│  │  • POSIX interface for clients                       ││
│  │  • QNX-aware resource manager on server side         ││
│  └──────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────┘
```

---

## 2. QNX Native Messaging

### Messages (MsgSend / MsgReceive / MsgReply)

QNX native messaging is the **foundational IPC mechanism** built directly into the microkernel. It follows a **client-server / remote procedure call (RPC)** model.

**How it works:**

```
┌──────────────────────────────────────────────────────────┐
│              QNX Native Message Passing                  │
│                                                          │
│  Client                        Server                    │
│  ──────                        ──────                    │
│                                                          │
│  MsgSend(coid,               MsgReceive(chid, ...)       │
│           request,                    │                  │
│           req_len,                    │                  │
│           reply,                      ▼                  │
│           reply_len)           Process request           │
│       │                              │                   │
│       │                              ▼                   │
│       │                        MsgReply(rcvid,           │
│       │                                status,           │
│       │                                reply_data)       │
│       │                              │                   │
│       ▼                              │                   │
│  Client unblocks ◀───────────────────┘                   │
│  with reply data                                         │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Blocking: Client blocks until server replies  │    │
│  │  • Sync: Built-in synchronization (no extra primitives)│
│  │  • Priority: Server inherits client's priority   │    │
│  │  • Data size: Any size (small to large, variable)│    │
│  │  • Copies: 1 kernel copy per MsgSend             │    │
│  │  • Overhead: Low (direct microkernel path)       │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property                 | Behavior                               |
| ------------------------ | -------------------------------------- |
| **Model**                | Client-server RPC                      |
| **Blocking**             | Client blocks until `MsgReply()`       |
| **Synchronization**      | Built-in (no extra primitives needed)  |
| **Priority inheritance** | Yes — server runs at client's priority |
| **Data size**            | Any size (small, large, variable)      |
| **Data copy**            | 1 kernel copy per send                 |
| **Extra processes**      | None (built into microkernel)          |
| **Portability**          | QNX-only                               |

**Example:**

```c
// Client
int status = MsgSend(coid, &request, sizeof(request), &reply, sizeof(reply));
// Blocks here until server replies

// Server
int rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);
// Process message...
MsgReply(rcvid, EOK, &reply, sizeof(reply));
// Client unblocks with reply
```

---

### Pulses (MsgSendPulse)

Pulses are **non-blocking notifications** that integrate seamlessly with QNX message passing channels.

**How it works:**

```
┌──────────────────────────────────────────────────────────┐
│              QNX Pulse Notification                      │
│                                                          │
│  Sender                        Receiver                  │
│  ──────                        ────────                  │
│                                                          │
│  MsgSendPulse(coid,            MsgReceive(chid, ...)     │
│               priority,              │                   │
│               code,                  │                   │
│               value)                 │                   │
│       │                            rcvid == 0            │
│       │                            (pulse received)      │
│       │                            code = N              │
│       │                            value = V             │
│       ▼                                                  │
│  Sender DOES NOT BLOCK                                   │
│  Continues execution immediately                         │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Blocking: NO — sender never blocks            │    │
│  │  • Payload: 8 bytes (code + value)               │    │
│  │  • Priority: Carried in pulse, inherited by      │    │
│  │    receiving thread                              │    │
│  │  • Sync: Compatible with MsgReceive()            │    │
│  │  • Use case: Notifications, events, interrupts   │    │
│  │  • Deadlock risk: NONE (non-blocking)            │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property                 | Behavior                                       |
| ------------------------ | ---------------------------------------------- |
| **Model**                | Non-blocking notification                      |
| **Blocking**             | **No** — sender continues immediately          |
| **Payload**              | 8 bytes (8-bit code + 32-bit value + priority) |
| **Priority inheritance** | Yes — receiver adopts pulse priority           |
| **Synchronization**      | None built-in (just notification)              |
| **Data copy**            | None                                           |
| **Extra processes**      | None                                           |
| **Portability**          | QNX-only                                       |

**Example:**

```c
// Sender (ISR, timer, or another thread)
MsgSendPulse(coid, priority, PULSE_CODE_DATA_READY, buffer_id);
// Returns immediately — no blocking

// Receiver
int rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);
if (rcvid == 0) {
    // It's a pulse!
    struct _pulse *pulse = (struct _pulse *)&msg;
    printf("Pulse: code=%d, value=%d\n", pulse->code, pulse->value);
}
```

---

## 3. POSIX / Unix Compatible Methods

### Signals

Signals are a **POSIX non-blocking notification** mechanism. They interrupt the target process.

```
┌──────────────────────────────────────────────────────────┐
│              POSIX Signals                               │
│                                                          │
│  Sender                        Target                    │
│  ──────                        ──────                    │
│                                                          │
│  kill(pid, SIGUSR1)            Signal handler fires      │
│       │                        OR sigwait() returns      │
│       │                                                  │
│       ▼                                                  │
│  Sender DOES NOT BLOCK                                   │
│  Target is INTERRUPTED                                   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Blocking: NO for sender                       │    │
│  │  • Payload: None (just signal number)            │    │
│  │  • Priority: NO priority information             │    │
│  │  • Sync: Difficult — need signal handlers or     │    │
│  │    sigwait() structure                           │    │
│  │  • Portability: POSIX (Linux, BSD, etc.)         │    │
│  │  • Use case: Unix compatibility, simple alerts   │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property                 | Behavior                           |
| ------------------------ | ---------------------------------- |
| **Model**                | POSIX async notification           |
| **Blocking**             | No for sender; interrupts target   |
| **Payload**              | None (only signal number)          |
| **Priority inheritance** | **No**                             |
| **Synchronization**      | Difficult (requires handler setup) |
| **Data copy**            | None                               |
| **Extra processes**      | None                               |
| **Portability**          | POSIX (Linux, BSD, Unix)           |

**When to use:** Porting Unix/Linux code that relies on signals. Not recommended for new QNX-specific designs.

---

### Shared Memory

Shared memory is a **POSIX mechanism** that eliminates data copies by mapping the same physical memory into multiple processes.

```
┌──────────────────────────────────────────────────────────┐
│              POSIX Shared Memory                         │
│                                                          │
│  Process A                     Process B                 │
│  ─────────                     ─────────                 │
│                                                          │
│  shm_open("/myshm", ...)       shm_open("/myshm", ...)   │
│  ftruncate(fd, size)           (no ftruncate)            │
│  ptr = mmap(...)               ptr = mmap(...)           │
│       │                              │                   │
│       └────────┬─────────────────────┘                   │
│                │                                         │
│                ▼                                         │
│         ┌─────────────┐                                  │
│         │ Same Physical│                                 │
│         │   Memory     │                                 │
│         └─────────────┘                                  │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Blocking: NO (after setup)                    │    │
│  │  • Payload: Unlimited (limited by RAM)           │    │
│  │  • Priority: NO priority information             │    │
│  │  • Sync: Manual (mutexes, semaphores required)   │    │
│  │  • Data copy: ZERO (after mmap)                  │    │
│  │  • Portability: POSIX                            │    │
│  │  • Use case: Large data, images, video frames    │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property                 | Behavior                            |
| ------------------------ | ----------------------------------- |
| **Model**                | Shared memory region                |
| **Blocking**             | No (manual synchronization)         |
| **Payload**              | Unlimited (limited by RAM)          |
| **Priority inheritance** | **No**                              |
| **Synchronization**      | Manual (robust mutexes, semaphores) |
| **Data copy**            | **Zero** (after setup)              |
| **Extra processes**      | None                                |
| **Portability**          | POSIX                               |

**When to use:** Large data transfer where zero-copy is critical. Combine with QNX messages for synchronization.

---

### Pipes

Pipes are a **POSIX mechanism** built on top of QNX native messaging. They require the **pipe process** to be running.

```
┌──────────────────────────────────────────────────────────┐
│              POSIX Pipes                                 │
│                                                          │
│  Writer                        Reader                    │
│  ──────                        ──────                    │
│                                                          │
│  write(fd, data, len)    ──▶  pipe process               │
│       │                        (copies data)             │
│       │                              │                   │
│       │                              ▼                   │
│       │                        read(fd, buf, len)        │
│       │                        (copies from pipe)        │
│       │                                                  │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Built on: QNX native messaging                │    │
│  │  • Data copies: 2 (write to pipe, read from pipe)│    │
│  │  • Context switches: Multiple (pipe process)     │    │
│  │  • Priority: NO priority information             │    │
│  │  • Speed: Relatively slow                        │    │
│  │  • Extra process: pipe process required          │    │
│  │  • Portability: POSIX                            │    │
│  │  • Use case: Porting Unix/Linux code             │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property                 | Behavior                              |
| ------------------------ | ------------------------------------- |
| **Model**                | Stream-based byte pipe                |
| **Blocking**             | Configurable                          |
| **Data copies**          | **2** (write to pipe, read from pipe) |
| **Context switches**     | Multiple (through pipe process)       |
| **Priority inheritance** | **No**                                |
| **Extra process**        | `pipe` process required               |
| **Portability**          | POSIX                                 |
| **Performance**          | Relatively slow                       |

**When to use:** Porting existing Unix/Linux code that uses pipes. **Not recommended** for new high-performance QNX designs.

---

### POSIX Message Queues

POSIX message queues provide **internal message queuing** with priority-ordered delivery. They require the **mqueue process**.

```
┌──────────────────────────────────────────────────────────┐
│              POSIX Message Queues                        │
│                                                          │
│  Sender                        Receiver                  │
│  ──────                        ────────                  │
│                                                          │
│  mq_send(mqd, msg,           mq_receive(mqd,             │
│           len, prio)                  buf, len, &prio)   │
│       │                              │                   │
│       └────────▶ mqueue process ◀───┘                    │
│                     (queues messages)                    │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Built on: Internal message queuing            │    │
│  │  • Data copies: 2 (send to queue, receive)       │    │
│  │  • Priority: Affects delivery order, NOT thread  │    │
│  │    scheduling                                    │    │
│  │  • Sync: Queue-based (async by default)          │    │
│  │  • Extra process: mqueue process required        │    │
│  │  • Portability: POSIX                            │    │
│  │  • Use case: Porting code, decoupled messaging   │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property            | Behavior                                          |
| ------------------- | ------------------------------------------------- |
| **Model**           | Priority-ordered message queue                    |
| **Blocking**        | Configurable (can block on empty/full)            |
| **Data copies**     | **2**                                             |
| **Priority**        | Affects delivery order, **not** thread scheduling |
| **Synchronization** | Queue-based                                       |
| **Extra process**   | `mqueue` process required                         |
| **Portability**     | POSIX                                             |

**When to use:** Porting code that uses `mq_open()` / `mq_send()` / `mq_receive()`. For new QNX designs, prefer QNX native messages.

---

### TCP/IP Sockets

TCP/IP sockets are **POSIX-compliant** and built on QNX messaging. They require the **io-sock process**.

```
┌──────────────────────────────────────────────────────────┐
│              TCP/IP Sockets                              │
│                                                          │
│  Client                        Server                    │
│  ──────                        ──────                    │
│                                                          │
│  socket() ──▶ connect() ──▶  socket() ──▶ bind()         │
│       │                              │                   │
│  send() ────▶ io-sock ────▶  recv()                      │
│  (copy)       (copy)         (copy)                      │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Built on: QNX native messaging + io-sock      │    │
│  │  • Data copies: 2+ (stack copies)                │    │
│  │  • Context switches: Multiple                    │    │
│  │  • Priority: NO priority information             │    │
│  │  • Speed: Not fastest for local comms            │    │
│  │  • Extra process: io-sock required               │    │
│  │  • Portability: Universal (all OSes)             │    │
│  │  • Use case: Remote/network communication        │    │
│  │  • Bonus: Client can be local OR remote          │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property                 | Behavior                                 |
| ------------------------ | ---------------------------------------- |
| **Model**                | Network socket (stream or datagram)      |
| **Blocking**             | Configurable                             |
| **Data copies**          | **2+** (stack overhead)                  |
| **Context switches**     | Multiple                                 |
| **Priority inheritance** | **No**                                   |
| **Extra process**        | `io-sock` process required               |
| **Portability**          | Universal (all operating systems)        |
| **Unique advantage**     | **Remote communication** (cross-machine) |

**When to use:**

- Communication with external systems (non-QNX)
- Network protocols (HTTP, MQTT, etc.)
- Client-server where client may be remote
- **Not recommended** for local high-performance IPC

---

## 4. File Descriptors & Resource Managers

File descriptors (`open()`, `read()`, `write()`) provide a **POSIX interface** built on QNX native messaging. The client uses standard C library calls; the server is a QNX resource manager.

```
┌──────────────────────────────────────────────────────────┐
│              File Descriptors / Resource Managers        │
│                                                          │
│  Client (POSIX)                Server (QNX-aware)        │
│  ────────────────              ─────────────────         │
│                                                          │
│  fd = open("/dev/mydev",    MsgReceive(chid, ...)        │
│          O_RDWR)                   │                     │
│       │                      (receives _IO_CONNECT)      │
│       │                            │                     │
│  read(fd, buf, len)  ──────▶ MsgReceive()                │
│  (libc)                    (receives _IO_READ)           │
│       │                            │                     │
│       │                      Process read request        │
│       │                            │                     │
│       │                      MsgReply()                  │
│       │                            │                     │
│  Data returned to client ◀─────────┘                    │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  CHARACTERISTICS:                                │    │
│  │  • Client interface: POSIX (open/read/write)     │    │
│  │  • Underlying: QNX native messages               │    │
│  │  • Data copies: 1 (like MsgSend)                 │    │
│  │  • Priority: YES (inherits via QNX messages)     │    │
│  │  • Server: Must be QNX messaging aware           │    │
│  │  • Use case: Device drivers, filesystems         │    │
│  │  • Benefit: Client doesn't know it's messaging   │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property                 | Behavior                                          |
| ------------------------ | ------------------------------------------------- |
| **Client interface**     | POSIX (`open()`, `read()`, `write()`)             |
| **Underlying mechanism** | QNX native messages                               |
| **Data copies**          | **1** (same as MsgSend)                           |
| **Priority inheritance** | **Yes** (via QNX messages)                        |
| **Server requirement**   | Must be QNX messaging aware (resource manager)    |
| **Portability**          | Client: POSIX; Server: QNX-specific               |
| **Use case**             | Device drivers, filesystems, hardware abstraction |

**Example:**

```c
// Client (standard POSIX — works on any Unix)
int fd = open("/dev/serial1", O_RDWR);
write(fd, "Hello", 5);
read(fd, buffer, 100);
close(fd);

// Server (QNX resource manager)
// Receives _IO_CONNECT, _IO_READ, _IO_WRITE messages
// Processes them and replies
```

---

## 5. Comprehensive Comparison Table

| Method               | Origin     | Blocking     | Payload   | Copies | Priority   | Sync           | Extra Process | Best For                   |
| -------------------- | ---------- | ------------ | --------- | ------ | ---------- | -------------- | ------------- | -------------------------- |
| **QNX Messages**     | QNX Native | Yes (client) | Any size  | 1      | **Yes**    | Built-in       | None          | Control, RPC, real-time    |
| **QNX Pulses**       | QNX Native | **No**       | 8 bytes   | 0      | **Yes**    | None           | None          | Notifications, events      |
| **Signals**          | POSIX      | No (sender)  | None      | 0      | **No**     | Difficult      | None          | Unix compatibility         |
| **Shared Memory**    | POSIX      | No           | Unlimited | **0**  | **No**     | Manual         | None          | Large data, zero-copy      |
| **Pipes**            | POSIX      | Configurable | Stream    | **2**  | **No**     | Stream-based   | `pipe`        | Porting Unix code          |
| **POSIX Msg Queues** | POSIX      | Configurable | Message   | **2**  | Order only | Queue-based    | `mqueue`      | Porting code               |
| **TCP/IP Sockets**   | POSIX      | Configurable | Stream    | **2+** | **No**     | Protocol-based | `io-sock`     | Remote/network             |
| **File Descriptors** | POSIX/QNX  | Configurable | Any       | 1      | **Yes**    | Built-in       | None          | Drivers, resource managers |

### Performance Ranking (Local Communication)

| Rank | Method               | Speed     | Notes                          |
| ---- | -------------------- | --------- | ------------------------------ |
| 1    | **Shared Memory**    | Fastest   | Zero copy after setup          |
| 2    | **QNX Pulses**       | Very Fast | No copy, no block              |
| 3    | **QNX Messages**     | Fast      | 1 kernel copy                  |
| 4    | **File Descriptors** | Fast      | Same as QNX messages           |
| 5    | **Signals**          | Medium    | Interrupt overhead             |
| 6    | **POSIX Msg Queues** | Slow      | 2 copies + mqueue process      |
| 7    | **Pipes**            | Slow      | 2 copies + pipe process        |
| 8    | **TCP/IP Sockets**   | Slowest   | 2+ copies + io-sock + protocol |

### Feature Matrix

| Feature              | QNX Msg | Pulses | Signals | SHM  | Pipes | MsgQ | TCP/IP | FDs  |
| -------------------- | ------- | ------ | ------- | ---- | ----- | ---- | ------ | ---- |
| Built-in sync        | ✅       | ❌      | ❌       | ❌    | ✅     | ✅    | ✅      | ✅    |
| Priority inheritance | ✅       | ✅      | ❌       | ❌    | ❌     | ❌    | ❌      | ✅    |
| Zero copy            | ❌       | ✅      | ✅       | ✅    | ❌     | ❌    | ❌      | ❌    |
| Large data           | ✅       | ❌      | ❌       | ✅    | ✅     | ✅    | ✅      | ✅    |
| Non-blocking send    | ❌       | ✅      | ✅       | ✅    | ✅     | ✅    | ✅      | ❌    |
| Remote capable       | ❌       | ❌      | ❌       | ❌    | ❌     | ❌    | ✅      | ❌    |
| POSIX portable       | ❌       | ❌      | ✅       | ✅    | ✅     | ✅    | ✅      | ✅    |
| No extra process     | ✅       | ✅      | ✅       | ✅    | ❌     | ❌    | ❌      | ✅    |
| Real-time suitable   | ✅       | ✅      | ❌       | ✅*   | ❌     | ❌    | ❌      | ✅    |

*With manual real-time synchronization

---

## 6. When to Use What

### Decision Guide

```
Are you writing new QNX-specific code?
│
├─ YES → Use QNX native methods
│  │
│  ├─ Need request/response with sync?
│  │  └─ YES → QNX Messages (MsgSend/MsgReceive/MsgReply)
│  │
│  ├─ Need notification only?
│  │  └─ YES → QNX Pulses (MsgSendPulse)
│  │
│  ├─ Need large data transfer?
│  │  └─ YES → Shared Memory + QNX Messages for control
│  │
│  └─ Writing a driver/resource manager?
│     └─ YES → File Descriptors (POSIX client, QNX server)
│
└─ NO → Porting existing code?
   │
   ├─ Code uses signals?
   │  └─ YES → Signals (sigaction, sigwait)
   │
   ├─ Code uses pipes?
   │  └─ YES → Pipes (requires pipe process)
   │
   ├─ Code uses message queues?
   │  └─ YES → POSIX Message Queues (requires mqueue)
   │
   ├─ Code uses sockets?
   │  └─ YES → TCP/IP Sockets (requires io-sock)
   │     └─ Is it local-only? Consider QNX Messages instead
   │
   └─ Code uses shared memory?
      └─ YES → POSIX Shared Memory (shm_open/mmap)
```

### Use Case Recommendations

| Scenario                        | Recommended                         | Avoid                     |
| ------------------------------- | ----------------------------------- | ------------------------- |
| Real-time control system        | QNX Messages + Pulses               | Pipes, TCP/IP             |
| High-frequency data acquisition | QNX Messages + Shared Memory        | POSIX Msg Queues          |
| Device driver                   | File Descriptors (resource manager) | Direct MsgSend            |
| Interrupt handling              | Pulses (from ISR)                   | Signals                   |
| Timer-based periodic tasks      | Pulses (SIGEV_PULSE_INIT)           | sleep() in thread         |
| Cross-machine communication     | TCP/IP Sockets                      | QNX Messages              |
| Porting Linux daemon            | Signals + Pipes (as needed)         | Forced QNX rewrite        |
| Large image/video transfer      | Shared Memory + MsgSend control     | MsgSend with large buffer |
| Simple notification             | Pulses                              | Signals                   |
| Unix-compatible API             | File Descriptors                    | Raw QNX messages          |

---

## 7. Porting Considerations

### From Linux/Unix to QNX

| Linux/Unix Mechanism               | QNX Equivalent                | Notes                           |
| ---------------------------------- | ----------------------------- | ------------------------------- |
| `write()` / `read()` (pipe)        | QNX Messages or keep Pipes    | Pipes work but are slower       |
| `mq_open()` / `mq_send()`          | QNX Messages or keep POSIX MQ | MQ works but requires mqueue    |
| `socket()` (local)                 | QNX Messages or keep Sockets  | Sockets work but slower locally |
| `signal()` / `sigaction()`         | Keep Signals or use Pulses    | Signals work; Pulses are better |
| `shm_open()` / `mmap()`            | Keep Shared Memory            | Fully compatible                |
| `pthread_mutex_t` (process-shared) | Keep + add robust attribute   | Add `PTHREAD_MUTEX_ROBUST`      |
| `select()` / `poll()`              | `ionotify()` + pulses         | More efficient in QNX           |

### Performance Optimization Path

```
Porting Phase 1: Get it working
    └─ Use POSIX APIs as-is (pipes, sockets, signals)

Porting Phase 2: Optimize for QNX
    ├─ Replace pipes with QNX Messages
    ├─ Replace local sockets with QNX Messages
    ├─ Replace signals with Pulses
    ├─ Keep Shared Memory (already optimal)
    └─ Add priority inheritance where needed

Porting Phase 3: Real-time tuning
    ├─ Use MsgSendv for multi-part data
    ├─ Use Shared Memory for bulk transfer
    ├─ Use robust mutexes in SHM
    └─ Profile and measure
```

---

## 8. Key Takeaways

### The Hierarchy of Choice

```
┌──────────────────────────────────────────────────────────┐
│              QNX IPC Selection Priority                  │
│                                                          │
│  1. QNX Native Messages (MsgSend/MsgReceive/MsgReply)    │
│     → Best for: Control, RPC, real-time, sync            │
│                                                          │
│  2. QNX Pulses (MsgSendPulse)                            │
│     → Best for: Notifications, events, async             │
│                                                          │
│  3. Shared Memory + QNX Messages                         │
│     → Best for: Large data, zero-copy, high throughput   │
│                                                          │
│  4. File Descriptors (Resource Managers)                 │
│     → Best for: Drivers, POSIX-compatible APIs           │
│                                                          │
│  5. POSIX Methods (for porting only)                     │
│     → Signals, Pipes, POSIX MQ, TCP/IP Sockets           │
│     → Use when code portability is required              │
│                                                          │
│  AVOID for new QNX designs:                              │
│  • Pipes (slow, extra process)                           │
│  • POSIX Message Queues (slow, extra process)            │
│  • Local TCP/IP Sockets (slow, extra process)            │
│  • Signals (no priority, hard to sync)                   │
└──────────────────────────────────────────────────────────┘
```

### Quick Decision Cheat Sheet

| Need               | Use                               | Don't Use                 |
| ------------------ | --------------------------------- | ------------------------- |
| Client-server RPC  | QNX `MsgSend`                     | POSIX MQ                  |
| Notification       | QNX `MsgSendPulse`                | Signals                   |
| Large data         | Shared Memory + `MsgSend` control | `MsgSend` with big buffer |
| Driver interface   | File Descriptors                  | Raw `MsgSend`             |
| Remote comms       | TCP/IP Sockets                    | QNX Messages              |
| Porting Unix code  | Keep POSIX API                    | Force QNX rewrite         |
| Real-time sync     | QNX Messages                      | Pipes                     |
| Interrupt → thread | `InterruptAttachEvent` + pulse    | Signal                    |
| Timer events       | `timer_create` + pulse            | Thread + `sleep()`        |

### Remember

> **"QNX native messaging is the foundation. Everything else is either built on it or a compromise for portability."**
>
> - QNX Messages = Best performance + features for local IPC
> - Pulses = Best for async notification
> - Shared Memory = Best for bulk data (when combined with message sync)
> - POSIX APIs = Use only when porting code or external compatibility is required

---

## Related Documentation

- **Part 1:** QNX Message Passing Fundamentals (MsgSend, MsgReceive, MsgReply)
- **Part 2:** QNX Deadlock Avoidance — Send Hierarchy & Pulses
- **Part 3:** QNX Server Designs — Thread Pools, Priority Inheritance, Delayed Reply
- **Part 4:** QNX Multi-Part Messages — IOVs, MsgSendv, MsgRead, MsgWrite
- **Part 5:** QNX Event Delivery & sigevent Architecture
- **Part 6:** QNX Shared Memory — Named, Anonymous, Synchronization
- **Part 7:** QNX IPC Methods Comparison — Overview
- **QNX Official:** [Interprocess Communication](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/ipc.html)
- **QNX Official:** [Resource Managers](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/resmgr.html)
