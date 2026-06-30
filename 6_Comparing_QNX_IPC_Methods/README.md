# QNX IPC Methods Comparison — Overview

> **Companion to:** QNX Message Passing Fundamentals, Deadlock Avoidance, Server Designs, Multi-Part Messages, Event Delivery, and Shared Memory

This document provides a high-level **comparative overview** of all Inter-Process Communication (IPC) methods available in QNX Neutrino, helping you choose the right tool for each situation.

---

## The IPC Landscape in QNX

```
┌─────────────────────────────────────────────────────────┐
│              QNX IPC Methods at a Glance                │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │           MESSAGE PASSING (Synchronous)         │    │
│  │  MsgSend() ──▶ MsgReceive() ──▶ MsgReply()      │    │
│  │  Single-buffer, blocking, priority inheritance  │    │
│  │                                                 │    │
│  │  MsgSendv() ──▶ MsgReceivev() ──▶ MsgReplyv()   │    │
│  │  Multi-part (IOV), scatter/gather, zero-copy    │    │
│  │                                                 │    │
│  │  MsgRead() / MsgWrite() ── On-demand data xfer  │    │
│  └─────────────────────────────────────────────────┘    │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │           PULSES (Asynchronous)                 │    │
│  │  MsgSendPulse() ──▶ rcvid == 0 in MsgReceive()  │    │
│  │  Non-blocking, 8 bytes, priority inheritance    │    │
│  └─────────────────────────────────────────────────┘    │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │           EVENTS (Notification)                 │    │
│  │  struct sigevent + SIGEV_PULSE_INIT /           │    │
│  │  SIGEV_INTR_INIT / SIGEV_SIGNAL_INIT /          │    │
│  │  SIGEV_THREAD_INIT + MsgDeliverEvent()          │    │
│  └─────────────────────────────────────────────────┘    │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │           SHARED MEMORY (Zero-Copy)             │    │
│  │  shm_open() / SHM_ANON + ftruncate() + mmap()   │    │
│  │  shm_create_handle() / shm_open_handle()        │    │
│  │  Requires explicit synchronization              │    │
│  └─────────────────────────────────────────────────┘    │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │           SIGNALS (Unix Compatibility)          │    │
│  │  kill() / sigaction() / sigwait()               │    │
│  │  Limited payload, async delivery                │    │
│  └─────────────────────────────────────────────────┘    │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │           INTERRUPTS (Kernel → Thread)          │    │
│  │  InterruptAttach() / InterruptAttachEvent()     │    │
│  │  InterruptWait() / InterruptUnmask()            │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

---

## Comparison by Characteristics

| Method                                 | Type         | Blocking?           | Direction                  | Payload Size                |
| -------------------------------------- | ------------ | ------------------- | -------------------------- | --------------------------- |
| **MsgSend / MsgReceive / MsgReply**    | Synchronous  | Yes                 | Bidirectional (req/reply)  | Up to ~4GB                  |
| **MsgSendv / MsgReceivev / MsgReplyv** | Synchronous  | Yes                 | Bidirectional (multi-part) | Up to ~4GB                  |
| **MsgSendPulse**                       | Asynchronous | **No**              | Unidirectional (notify)    | 8 bytes (code + value)      |
| **MsgDeliverEvent**                    | Asynchronous | **No**              | Unidirectional (notify)    | Configurable (via sigevent) |
| **Shared Memory (mmap)**               | Synchronous  | No (manual sync)    | Bidirectional (read/write) | Limited by RAM              |
| **Signals**                            | Asynchronous | Configurable        | Unidirectional             | None (just number)          |
| **Interrupts**                         | Asynchronous | Yes (InterruptWait) | Kernel → Thread            | None (just event)           |

| Method                      | Setup Cost    | Per-Operation Cost     | Data Copies              | Throughput  | Latency    |
| --------------------------- | ------------- | ---------------------- | ------------------------ | ----------- | ---------- |
| **MsgSend (single buffer)** | Low           | Medium                 | 1 kernel copy            | Medium      | Medium     |
| **MsgSendv (IOV)**          | Low           | Medium                 | 1 kernel copy (gathered) | High        | Medium     |
| **MsgRead / MsgWrite**      | Low           | Low                    | On-demand                | High        | Low        |
| **Pulse**                   | Low           | **Very Low**           | None                     | Very High   | Very Low   |
| **Shared Memory**           | Medium (mmap) | **Zero** (after setup) | **None**                 | **Highest** | **Lowest** |
| **Signals**                 | Low           | Low                    | None                     | Low         | Medium     |
| **Interrupts**              | Medium        | Very Low               | None                     | Very High   | Very Low   |

| Method            | Built-in Sync  | Priority Inheritance | Failure Isolation | Deadlock Risk             |
| ----------------- | -------------- | -------------------- | ----------------- | ------------------------- |
| **MsgSend**       | Yes (blocking) | Yes (automatic)      | Strong            | Medium (hierarchy needed) |
| **Pulse**         | No             | Yes (delivery)       | Strong            | None (non-blocking)       |
| **Shared Memory** | No (manual)    | No (manual)          | Weak              | None (but data races)     |
| **Signals**       | No             | No                   | Medium            | None                      |
| **Interrupts**    | No             | Yes (event delivery) | Strong            | None                      |

---

## Comparison by Use Case

| Scenario                                     | Best Choice                                  | Why                                                      |
| -------------------------------------------- | -------------------------------------------- | -------------------------------------------------------- |
| **Client asks server to do work**            | `MsgSend` / `MsgReply`                       | Built-in sync, priority inheritance, structured protocol |
| **Server says "data ready"**                 | `MsgSendPulse`                               | Non-blocking, no deadlock risk                           |
| **Transfer large data (>1KB)**               | Shared Memory + `MsgSend` control            | Zero copy after setup                                    |
| **Variable-length protocol (header + data)** | `MsgSendv` + `MsgReadv`                      | No assembly copies, on-demand read                       |
| **Hardware interrupt arrives**               | `InterruptAttachEvent` + pulse               | Kernel-integrated, low latency                           |
| **Timer fires periodically**                 | `timer_create` + `SIGEV_PULSE_INIT`          | Kernel-managed, no busy-wait                             |
| **Lower-level notifies higher-level**        | `MsgSendPulse` (non-blocking)                | Deadlock avoidance in hierarchy                          |
| **Multiple processes share resource**        | Shared Memory + robust mutexes               | Low latency, handles process death                       |
| **Unix signal compatibility**                | `sigaction` / `sigwait` / `kill`             | Portability                                              |
| **Async I/O completion**                     | `MsgDeliverEvent` with registered `sigevent` | Secure, typed delivery                                   |

---

## Decision Matrix

```
What is the communication pattern?
│
├─ Synchronous request/response?
│  └─ YES → MsgSend / MsgReceive / MsgReply
│     ├─ Data > 1KB or multi-part? → MsgSendv / MsgReceivev (IOV)
│     ├─ Variable-length data? → MsgReceive(header) + MsgReadv(data)
│     └─ Reply built in chunks? → MsgWritev() + MsgReply(status)
│
├─ Asynchronous notification only?
│  └─ YES → MsgSendPulse (non-blocking)
│     ├─ From kernel (interrupt/timer)? → sigevent + InterruptAttachEvent / timer_create
│     └─ From thread/process? → MsgDeliverEvent() with registered sigevent
│
├─ Large bulk data transfer?
│  └─ YES → Shared Memory + MsgSend for control
│     ├─ Need security (known client)? → SHM_ANON + shm_create_handle()
│     └─ Multiple unknown consumers? → Named shm_open() + file permissions
│
├─ Need cross-process synchronization?
│  └─ YES → Shared Memory + robust mutexes / semaphores
│
├─ Unix compatibility required?
│  └─ YES → Signals (sigaction, sigwait, kill)
│
└─ Kernel event (IRQ, timer)?
   └─ YES → InterruptAttach() / InterruptAttachEvent() + InterruptWait()
```

---

## Hybrid Strategies

### Messages for Control, Shared Memory for Data

```
┌─────────┐                    ┌─────────┐
│ Client  │                    │ Server  │
│  ┌─────┐│                    │┌─────┐  │
│  │ SHM ││◀────── Shared ────▶││ SHM │  │
│  │Slot ││      Memory        ││Slot │  │
│  └─────┘│                    │└─────┘  │
│         │                    │         │
│ MsgSend │──"Process slot 3"─▶│ MsgRecv │
│         │◀────"Done"─────────│ MsgReply│
└─────────┘                    └─────────┘
```

- **Synchronization:** Implicit in MsgSend/MsgReply
- **Data transfer:** Zero copy via shared memory
- **Security:** Server controls which client gets handle

### Pulse + Message (Input-Driven)

```
Hardware ──IRQ──▶ ISR ──MsgSendPulse──▶ Server ──MsgReceive(rcvid==0)
                                              │
                                              ▼
                                         Process pulse (quick)
                                              │
                                              ▼
                                         MsgReceive(rcvid>0)
                                         (client request)
                                              │
                                              ▼
                                         MsgReply (with data)
```

### Event Registration (Async Notification)

```
1. Client creates channel for pulses
2. Client initializes sigevent (SIGEV_PULSE_INIT)
3. Client sends sigevent to server via MsgSend
4. Server verifies (MsgVerifyEvent) and stores
5. Server replies (client unblocks)
6. Later: Server calls MsgDeliverEvent(rcvid, &ev)
7. Client receives pulse on its channel
```

---

## Performance Hierarchy

### Latency (Lowest to Highest)

| Rank | Method                     | Typical Latency |
| ---- | -------------------------- | --------------- |
| 1    | **Shared Memory (cached)** | ~10-50 ns       |
| 2    | **Pulse**                  | ~1-5 μs         |
| 3    | **Interrupt → Thread**     | ~2-10 μs        |
| 4    | **MsgSend (small, local)** | ~5-20 μs        |
| 5    | **MsgSendv (IOV)**         | ~5-25 μs        |
| 6    | **MsgSend (large data)**   | ~10-100+ μs     |
| 7    | **Signal**                 | ~10-50 μs       |

### Throughput (Highest to Lowest)

| Rank | Method                      | Typical Throughput |
| ---- | --------------------------- | ------------------ |
| 1    | **Shared Memory**           | GB/s               |
| 2    | **MsgSendv (large IOV)**    | 100MB-1GB/s        |
| 3    | **MsgRead / MsgWrite**      | 100MB-1GB/s        |
| 4    | **MsgSend (single buffer)** | 10-100MB/s         |
| 5    | **Pulse**                   | Millions/sec       |
| 6    | **Signal**                  | Thousands/sec      |

---

## Quick Reference

| Task                 | Function                                                     |
| -------------------- | ------------------------------------------------------------ |
| Create named SHM     | `shm_open(name, O_CREAT \| O_RDWR, 0600)`                    |
| Allocate SHM memory  | `ftruncate(fd, size)`                                        |
| Map SHM to pointer   | `mmap(NULL, size, PROT_READ \| PROT_WRITE, MAP_SHARED, fd, 0)` |
| Create anonymous SHM | `shm_open(SHM_ANON, O_CREAT \| O_RDWR, 0600)`                |
| Create SHM handle    | `shm_create_handle(fd, pid, perms, &handle, 0)`              |
| Open SHM handle      | `shm_open_handle_pid(handle, perms, server_pid)`             |
| Remove SHM name      | `shm_unlink(name)`                                           |
| Unmap SHM            | `munmap(ptr, size)`                                          |
| Send message         | `MsgSend(coid, msg, msg_len, reply, reply_len)`              |
| Send multi-part      | `MsgSendv(coid, siov, sparts, riov, rparts)`                 |
| Receive message      | `MsgReceive(chid, msg, msg_len, &info)`                      |
| Reply to client      | `MsgReply(rcvid, status, reply, reply_len)`                  |
| Send pulse           | `MsgSendPulse(coid, priority, code, value)`                  |
| Deliver event        | `MsgDeliverEvent(rcvid, &sigevent)`                          |
| Verify event         | `MsgVerifyEvent(rcvid, &sigevent)`                           |
| Register event       | `MsgRegisterEvent(&sigevent, coid)`                          |
| Attach interrupt     | `InterruptAttachEvent(irq, &sigevent, flags)`                |
| Wait for interrupt   | `InterruptWait(0, NULL)`                                     |
| Create timer         | `timer_create(CLOCK_MONOTONIC, &sigevent, &timerid)`         |
| Set timer            | `timer_settime(timerid, 0, &its, NULL)`                      |
| Process-shared mutex | `pthread_mutexattr_setpshared(attr, PTHREAD_PROCESS_SHARED)` |
| Robust mutex         | `pthread_mutexattr_setrobust(attr, PTHREAD_MUTEX_ROBUST)`    |
| Recover mutex        | `pthread_mutex_consistent(&mutex)`                           |

---

## Key Takeaways

### The Golden Rules

| #    | Rule                                                         |
| ---- | ------------------------------------------------------------ |
| 1    | **Use MsgSend for control** — Synchronous, structured, safe  |
| 2    | **Use Pulses for notification** — Non-blocking, no deadlock  |
| 3    | **Use Shared Memory for bulk data** — Zero copy, highest throughput |
| 4    | **Use MsgSendv for multi-part data** — No assembly copies    |
| 5    | **Use sigevent for kernel events** — Interrupts, timers, async I/O |
| 6    | **Never send upward in hierarchy** — Use pulses instead      |
| 7    | **Always verify events** — `MsgVerifyEvent()` before storing |
| 8    | **Use robust mutexes in SHM** — Handle process death gracefully |
| 9    | **Combine methods for best results** — Messages + SHM is canonical |
| 10   | **Profile before optimizing** — Measure, don't guess         |

### The "Right Tool for the Job"

| Job                              | Right Tool                          | Wrong Tool                        |
| -------------------------------- | ----------------------------------- | --------------------------------- |
| Client asks server to do work    | `MsgSend` / `MsgReply`              | Shared memory (no sync)           |
| Server says "data ready"         | `MsgSendPulse`                      | `MsgSend` (deadlock risk)         |
| Transfer 1MB image               | Shared memory + `MsgSend` control   | `MsgSend` (slow, copies)          |
| Hardware interrupt arrives       | `InterruptAttachEvent` + pulse      | Polling (wastes CPU)              |
| Timer fires every 10ms           | `timer_create` + `SIGEV_PULSE_INIT` | Thread + `sleep()` (imprecise)    |
| Multiple processes share counter | Shared memory + robust mutex        | `MsgSend` (too slow)              |
| Variable-length write()          | `MsgSendv` + `MsgReadv`             | Fixed-size buffer (wastes memory) |
| Unix signal needed               | `sigaction` / `sigwait`             | `MsgSendPulse` (not compatible)   |

### The QNX IPC Philosophy

> **"QNX gives you primitives. You compose solutions."**
>
> - Message passing is the foundation
> - Pulses extend it for async notification
> - Shared memory optimizes for bulk data
> - Events unify kernel and thread notifications
> - Signals provide Unix compatibility
>
> The best QNX systems use **all of these together**, each for what it does best.

---

## Related Documentation

- **Part 1:** QNX Message Passing Fundamentals (MsgSend, MsgReceive, MsgReply)
- **Part 2:** QNX Deadlock Avoidance — Send Hierarchy & Pulses
- **Part 3:** QNX Server Designs — Thread Pools, Priority Inheritance, Delayed Reply
- **Part 4:** QNX Multi-Part Messages — IOVs, MsgSendv, MsgRead, MsgWrite
- **Part 5:** QNX Event Delivery & sigevent Architecture
- **Part 6:** QNX Shared Memory — Named, Anonymous, Synchronization
- **QNX Official:** [Interprocess Communication](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/ipc.html)
