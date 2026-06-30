# QNX Message Passing — Priority Handling

## Overview

This section covers how QNX handles priorities in message passing. Priority management ensures real-time determinism, prevents priority inversion, and maintains fair scheduling across multiple clients. Messages and pulses are both queued and delivered according to priority and time order.

---

## 1. Message Queue Ordering

### Priority-First, Time-Second

When multiple clients send messages to a server, the kernel queues them using two criteria:

| Order   | Criteria     | Description                                                  |
| ------- | ------------ | ------------------------------------------------------------ |
| **1st** | **Priority** | Higher priority messages are received first                  |
| **2nd** | **Time**     | At the same priority, the message waiting longest is received first |

```
Server Message Queue (ordered by priority, then time)

Priority 5 (highest)     ┌─────────────────┐
  t1 sent first ────────▶│ Message from T1 │  ← Received FIRST
                         └─────────────────┘

Priority 10              ┌─────────────────┐
  t2 sent first ────────▶│ Message from T2 │  ← Received SECOND
                         └─────────────────┘
  t3 sent second ───────▶│ Message from T3 │  ← Received THIRD
                         └─────────────────┘

Priority 20 (lowest)     ┌─────────────────┐
  t4 sent first ────────▶│ Message from T4 │  ← Received LAST
                         └─────────────────┘
```

### Same Rules Apply to Pulses

Pulses follow the **exact same ordering** as messages:

```
Pulse Queue (ordered by priority, then time)

Priority 5     ┌─────────┐     ┌─────────┐
               │ Pulse A │────▶│ Pulse B │  (A before B: same priority, A sent first)
               └─────────┘     └─────────┘

Priority 10    ┌─────────┐
               │ Pulse C │
               └─────────┘
```

### Messages and Pulses Are Interleaved

Messages and pulses are **not** delivered in separate queues. They are interleaved in a single unified queue ordered by priority and time:

```
Unified Queue (messages + pulses)

┌─────────────────────────────────────────────────────────────┐
│ Priority 5  │ Pulse A (t=0)  │ Message T1 (t=1)             │
│ Priority 10 │ Message T2 (t=0) │ Pulse B (t=1)              │
│ Priority 20 │ Message T3 (t=0)                              │
└─────────────────────────────────────────────────────────────┘

Delivery Order:
  1. Pulse A (prio 5, sent first)
  2. Message T1 (prio 5, sent second)
  3. Message T2 (prio 10, sent first)
  4. Pulse B (prio 10, sent second)
  5. Message T3 (prio 20, sent first)
```

> **Key Point:** There is no "messages first" or "pulses first" rule. Both are treated equally in the queue.

---

## 2. Server Priority Inheritance

### The Problem: Priority Inversion

Without priority inheritance, a high-priority client could be delayed by a low-priority server:

```
Without Priority Inheritance (BAD):

Time ──▶

T1 (prio 13):   [RUNNING] ──MsgSend()──▶ [SEND BLOCKED] ───────▶ [REPLY BLOCKED]
                                              │                        ▲
                                              │                        │
Server (prio 22): [RECEIVE] ──MsgReceive()──▶ [RUNNING] ──────────── [REPLY]
                                              │
                                              ▼
T3 (prio 15):   [RUNNING] ──────────────────▶ [PREEMPTS SERVER]
                                              │
                                              ▼
                                              Server at prio 22 is preempted by T3 at prio 15!
                                              T1 (prio 13) is delayed by T3 (prio 15)
                                              → PRIORITY INVERSION!
```

### The Solution: Priority Inheritance

When a server receives a message, its thread **inherits** the sender's priority:

```
With Priority Inheritance (GOOD):

Time ──▶

T1 (prio 13):   [RUNNING] ──MsgSend()──▶ [SEND BLOCKED] ───────▶ [REPLY BLOCKED]
                                              │                        ▲
                                              │                        │
Server:          [RECEIVE] ──MsgReceive()──▶ [RUNNING @ prio 13] ── [REPLY]
                                              │
                                              ▼
T3 (prio 15):   [RUNNING] ──────────────────▶ [CANNOT PREEMPT]
                                              │
                                              ▼
                                              Server runs at T1's priority (13)
                                              T3 (prio 15) CAN preempt and run
                                              → No inversion for T1!
```

### Complete Priority Inheritance Example

```
Scenario: Server starts at priority 22, three client threads

┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
│   T1    │     │   T2    │     │   T3    │     │ Server  │
│ prio 13 │     │ prio 10 │     │ prio 15 │     │ prio 22 │
└────┬────┘     └────┬────┘     └────┬────┘     └────┬────┘
     │               │               │               │
     │               │               │               │
     │               │               │               │ [RECEIVE BLOCKED]
     │               │               │               │
     │               │               │               │
     │               │               │               │
     │               │               │               │
     │               │               │               │
     ▼               ▼               ▼               ▼

Step 1: T2 (prio 10) sends message first
─────────────────────────────────────────
T2:     MsgSend() ──▶ Server receives, inherits prio 10
Server: [RUNNING @ prio 10] processing T2's message

Step 2: While server is at prio 10, T1 (prio 13) sends message
───────────────────────────────────────────────────────────
T1:     MsgSend() ──▶ Server's priority BOOSTED to 13
Server: [RUNNING @ prio 13] processing T1's message (higher priority client)

Step 3: T3 (prio 15) is running, can preempt server at prio 13
────────────────────────────────────────────────────────────
T3:     [RUNNING] ──▶ preempts server (15 > 13)
Server: [READY] waiting for CPU

Step 4: T3 finishes, server resumes at prio 13
────────────────────────────────────────────────
T3:     [DONE]
Server: [RUNNING @ prio 13] resumes T1's work

Step 5: Server replies to T1, priority drops back
────────────────────────────────────────────────
Server: MsgReply() ──▶ T1 unblocks
Server: [RECEIVE BLOCKED] ──▶ priority returns to 22 (or next message's priority)
```

### Why Delay the Priority Drop?

The server does **not** drop priority immediately after `MsgReply()`. It waits until the next `MsgReceive()` to inherit the next client's priority:

```
Priority Drop Timing:

Server handling T2 (prio 10):
  MsgReceive() ──▶ inherits prio 10
  [processing at prio 10]
  MsgReply() ──▶ T2 unblocks
  [STILL at prio 10 — does not drop yet!]
  MsgReceive() ──▶ if no messages, goes RECEIVE BLOCKED at prio 10

When T1 (prio 13) sends:
  Server wakes up, inherits prio 13
  [processing at prio 13]
```

**Reason for delay:** Prevents artificial priority boosts. If the server immediately returned to its base priority (22), it could be preempted by medium-priority threads while handling high-priority work.

---

## 3. Priority Inheritance Rules Summary

| Event                                                 | Server Thread Priority                         |
| ----------------------------------------------------- | ---------------------------------------------- |
| `MsgReceive()` with no messages                       | Base priority (or inherited from last message) |
| `MsgReceive()` receives message from client at prio X | **Inherits priority X**                        |
| During `MsgReply()`                                   | Remains at client's priority X                 |
| After `MsgReply()`                                    | **Stays at X** until next `MsgReceive()`       |
| Next `MsgReceive()` from client at prio Y             | **Inherits priority Y**                        |
| `MsgReceive()` with no messages (idle)                | Base priority                                  |

### Priority Inheritance Can Be Disabled

Use `_NTO_CHF_FIXED_PRIORITY` in `ChannelCreate()` to disable inheritance:

```c
// Server always runs at its own priority, regardless of client
int chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY);
```

> **Warning:** Only use this if you fully understand the priority inversion risks.

---

## 4. Pulse Priority Handling

### Pulses Store Priority, Don't Inherit

Unlike messages, pulses are **non-blocking**. The sender is not waiting, so the server cannot inherit its priority at receive time. Instead:

| Aspect                 | Messages                      | Pulses                                |
| ---------------------- | ----------------------------- | ------------------------------------- |
| **Priority mechanism** | Inheritance at `MsgReceive()` | Stored with pulse at `MsgSendPulse()` |
| **Sender state**       | Blocked (REPLY)               | Non-blocking (continues running)      |
| **Server priority**    | Becomes sender's priority     | Becomes pulse's stored priority       |
| **When set**           | At `MsgReceive()` time        | At `MsgSendPulse()` time              |

### Pulse Priority Parameter

```c
// Priority = -1: use caller's current priority
MsgSendPulse(coid, -1, MY_PULSE_TIMER, 0);

// Priority = explicit value: store this priority with pulse
MsgSendPulse(coid, 10, MY_PULSE_URGENT, 0);   // High priority
MsgSendPulse(coid, 100, MY_PULSE_UPDATE, 0);  // Low priority
```

### Pulse Queue Ordering Example

```
Pulse Queue with Different Priorities:

Sender          Priority    Time    Pulse
─────────────────────────────────────────────
T1 (prio 5)     5           t=0     P1 (urgent)
T2 (prio 10)    10          t=0     P2
T3 (prio 5)     5           t=1     P3
T4 (prio 10)    10          t=1     P4

Delivery Order:
  1. P1 (prio 5, sent first)
  2. P3 (prio 5, sent second)
  3. P2 (prio 10, sent first)
  4. P4 (prio 10, sent second)
```

---

## 5. Complete Priority Scenario Walkthrough

### Setup

```
┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
│   T1    │     │   T2    │     │   T3    │     │ Server  │
│ prio 13 │     │ prio 10 │     │ prio 15 │     │ prio 22 │
│ High    │     │ Medium  │     │ Higher  │     │ Base    │
└─────────┘     └─────────┘     └─────────┘     └─────────┘
```

### Timeline

```
Time ──▶

T=0:  Server calls MsgReceive() ──▶ [RECEIVE BLOCKED @ prio 22]

T=1:  T2 (prio 10) sends message
      Server receives, inherits prio 10
      Server: [RUNNING @ prio 10] processing T2's request
      T2: [REPLY BLOCKED]

T=2:  T1 (prio 13) sends message
      Message queued at prio 13 (higher than server's current 10)
      Server's priority BOOSTED to 13
      Server: [RUNNING @ prio 13] now processing T1's request (preempts T2 work)
      T1: [REPLY BLOCKED]
      T2: [REPLY BLOCKED] (still waiting)

T=3:  T3 (prio 15) is running, can preempt server at prio 13
      T3: [RUNNING]
      Server: [READY @ prio 13] (preempted by T3)

T=4:  T3 finishes its work
      Server: [RUNNING @ prio 13] resumes T1's request

T=5:  Server calls MsgReply() to T1
      T1: [RUNNABLE]
      Server: [RUNNING @ prio 13] (stays at 13, doesn't drop yet)

T=6:  Server calls MsgReceive()
      Finds T2's message (prio 10) still queued
      Server: [RUNNING @ prio 10] resumes T2's request

T=7:  Server calls MsgReply() to T2
      T2: [RUNNABLE]
      Server: [RECEIVE BLOCKED @ prio 22] (no more messages, returns to base)
```

---

## 6. Priority and Pulse Conglomeration

Recall that identical pulses are conglomerated (counted, not duplicated). Priority affects this:

```c
// These pulses are IDENTICAL — will conglomerate
MsgSendPulse(coid, -1, MY_PULSE_TIMER, 0);  // prio = caller's prio (say 10)
MsgSendPulse(coid, -1, MY_PULSE_TIMER, 0);  // prio = caller's prio (10)
// Result: 1 queue entry with count = 2

// These pulses are DIFFERENT — will NOT conglomerate
MsgSendPulse(coid, 10, MY_PULSE_TIMER, 0);   // prio = 10
MsgSendPulse(coid, 20, MY_PULSE_TIMER, 0);   // prio = 20
// Result: 2 separate queue entries
```

**Conglomeration requires ALL fields to match:** code, value, priority, and origin.

---

## 7. Practical Code Examples

### Server with Priority Logging

```c
#include <stdio.h>
#include <sys/neutrino.h>

void handle_message(int rcvid, struct _msg_info *info) {
    printf("[RECV] From PID %d, client priority: %d
",
           info->pid, info->priority);
    printf("[RECV] Server running at inherited priority: %d
",
           info->priority);

    // Process message...

    printf("[REPLY] Server still at priority: %d
",
           info->priority);
    MsgReply(rcvid, EOK, NULL, 0);

    // Priority will stay at this level until next MsgReceive()
}

int main(void) {
    int chid = ChannelCreate(0);
    char buffer[512];
    struct _msg_info info;
    int rcvid;

    printf("Server base priority: %d
", getprio(0));

    for (;;) {
        rcvid = MsgReceive(chid, buffer, sizeof(buffer), &info);
        if (rcvid > 0) {
            handle_message(rcvid, &info);
        }
    }

    return 0;
}
```

### Client Sending at Different Priorities

```c
#include <stdio.h>
#include <sys/neutrino.h>

int main(int argc, char *argv[]) {
    int coid = /* ... connect to server ... */;
    char msg[256] = "Hello";
    char reply[256];

    // Set our thread priority
    setprio(0, 10);  // Set to priority 10

    // Server will inherit priority 10
    printf("Sending at priority 10...
");
    MsgSend(coid, msg, sizeof(msg), reply, sizeof(reply));

    // Boost our priority
    setprio(0, 20);  // Set to priority 20

    // Server will inherit priority 20
    printf("Sending at priority 20...
");
    MsgSend(coid, msg, sizeof(msg), reply, sizeof(reply));

    return 0;
}
```

### Pulse with Explicit Priority

```c
#include <stdio.h>
#include <sys/neutrino.h>

#define MY_PULSE_URGENT  1
#define MY_PULSE_NORMAL  2

int main(void) {
    int coid = /* ... connect to server ... */;

    // Send urgent pulse with high priority
    MsgSendPulse(coid, 5, MY_PULSE_URGENT, 0);

    // Send normal pulse with low priority
    MsgSendPulse(coid, 50, MY_PULSE_NORMAL, 0);

    // Server will receive urgent pulse first (prio 5 < 50)

    return 0;
}
```

---

## 8. Key Principles

| Principle                                    | Explanation                                                  |
| -------------------------------------------- | ------------------------------------------------------------ |
| **Priority-first, time-second**              | Messages and pulses are queued by priority, then by arrival time |
| **Messages and pulses interleave**           | No separate queues — unified ordering by priority and time   |
| **Server inherits client's priority**        | At `MsgReceive()`, server thread runs at sender's priority   |
| **Priority inheritance prevents inversion**  | High-priority clients are not delayed by medium-priority threads |
| **Priority drop is delayed**                 | Server stays at inherited priority until next `MsgReceive()` |
| **Pulses store priority at send time**       | `MsgSendPulse()` embeds priority; server uses it at delivery |
| **Use `-1` for caller's priority in pulses** | Convenient default; use explicit values for urgency control  |
| **Disable inheritance with caution**         | `_NTO_CHF_FIXED_PRIORITY` removes protection against inversion |

---

## 9. Common Pitfalls

| Pitfall                                    | Problem                                                      | Solution                                                 |
| ------------------------------------------ | ------------------------------------------------------------ | -------------------------------------------------------- |
| **Assuming FIFO ordering**                 | Lower-priority messages wait indefinitely if higher-priority messages keep arriving | Design protocols with bounded priority levels            |
| **Not expecting priority changes**         | Server code assumes constant priority; real-time deadlines missed | Account for priority inheritance in timing analysis      |
| **Pulse priority mismatch**                | Urgent pulse sent with low priority gets delayed             | Use explicit high priority for urgent pulses             |
| **Disabling priority inheritance**         | Priority inversion causes missed deadlines                   | Only use `_NTO_CHF_FIXED_PRIORITY` with full analysis    |
| **Ignoring pulse conglomeration priority** | Pulses with different priorities don't conglomerate, filling queue | Keep pulse priorities consistent for same code/value     |
| **Server stays at high priority too long** | After handling high-priority client, server may starve others | Minimize processing time; use multiple threads if needed |

---

## 10. Summary Table

| Aspect                 | Messages                                   | Pulses                             |
| ---------------------- | ------------------------------------------ | ---------------------------------- |
| **Queue ordering**     | Priority, then time                        | Priority, then time                |
| **Interleaved?**       | Yes — unified queue                        | Yes — unified queue                |
| **Priority mechanism** | Inheritance at `MsgReceive()`              | Stored at `MsgSendPulse()`         |
| **Sender blocking?**   | Yes (REPLY BLOCKED)                        | No (non-blocking)                  |
| **Server priority**    | Becomes sender's priority                  | Becomes pulse's stored priority    |
| **Priority drop**      | After `MsgReply()`, at next `MsgReceive()` | N/A (server handles and continues) |
| **Can be disabled?**   | Yes (`_NTO_CHF_FIXED_PRIORITY`)            | N/A                                |

---

*This module covers priority handling in QNX message passing. Advanced topics such as adaptive partitioning, sporadic scheduling, and multi-core priority management are covered in subsequent modules.*
