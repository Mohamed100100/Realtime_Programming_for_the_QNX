# QNX Kernel Timeouts — Complete Guide

---

## Overview

QNX provides a kernel mechanism to prevent threads from blocking indefinitely on kernel calls. Using `TimerTimeout()`, you can set a maximum wait time for blocking operations like message send/receive/reply.

```
Problem: Client thread blocked forever waiting for server
═══════════════════════════════════════════════════════════════════

   Client Thread                           Server Process
   ─────────────                           ──────────────
        │                                        │
        │  MsgSend(chid, ...)                    │
        │───────────────────────────────────────►│
        │                                        │
        │  [BLOCKED — SEND]                      │  [Not receiving]
        │                                        │
        │  [BLOCKED — SEND]                      │  [Crashed?]
        │                                        │
        │  [BLOCKED — SEND]                      │  [Hung?]
        │                                        │
        │  ←── BLOCKED FOREVER! ──►              │
        │                                        │

   Without timeout: thread hangs indefinitely
```

```
Solution: TimerTimeout prevents indefinite blocking
═══════════════════════════════════════════════════════════════════

   Client Thread                           Server Process
   ─────────────                           ──────────────
        │                                        │
        │  TimerTimeout(2.5s)  ← set timeout     │
        │                                        │
        │  MsgSend(chid, ...)                    │
        │───────────────────────────────────────►│
        │                                        │
        │  [BLOCKED — SEND]                      │  [Not receiving]
        │                                        │
        │  ... 2.5 seconds pass ...            │
        │                                        │
        │  ←── ETIMEDOUT ──                      │
        │  MsgSend returns -1                    │
        │                                        │
        │  Thread unblocked, can handle error    │
        │                                        │
```

---

## How TimerTimeout Works

`TimerTimeout()` sets a one-shot timeout that applies to the **next blocking kernel call** made by the thread.

```
TimerTimeout Parameters
════════════════════════

   TimerTimeout(clock_id, flags, event, ntime, otime)
        │          │        │      │      │      │
        │          │        │      │      │      └── old timeout (optional)
        │          │        │      │      └── timeout duration
        │          │        │      └── event to deliver (NULL = none)
        │          │        └── flags (e.g., TIMER_TOLERANCE)
        │          └── CLOCK_REALTIME or CLOCK_MONOTONIC
        └── kernel call


   ntime (struct _itimer):
   ┌─────────────────────────────────────┐
   │  nsec = timeout in nanoseconds       │
   │  Example: 2.5s = 2,500,000,000 ns   │
   └─────────────────────────────────────┘
```

### Timeout Application

```
Timeout applies to blocking states:
════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  SEND Block        │  Thread waiting for MsgReceive()       │
   │  RECEIVE Block     │  Thread waiting for MsgReceive()       │
   │  REPLY Block       │  Thread waiting for MsgReply()        │
   │  ... and others    │  Various kernel blocking states        │
   └─────────────────────────────────────────────────────────────┘
```

### Example: 2.5 Second Timeout on MsgSend

```c
#include <sys/neutrino.h>
#include <errno.h>

struct _itimer timeout;
timer_t timerid;

/* Set timeout: 2.5 seconds */
timeout.nsec = 2500000000LL;   /* 2,500,000,000 nanoseconds = 2.5s */

/* Apply timeout to next blocking kernel call */
TimerTimeout(CLOCK_MONOTONIC, 0, NULL, &timeout, NULL);

/* Next kernel call: MsgSend — will timeout after 2.5s if blocked */
if (MsgSend(coid, msg, sizeof(msg), reply, sizeof(reply)) == -1) {
    if (errno == ETIMEDOUT) {
        /* Timeout occurred — server didn't receive/reply in time */
        printf("MsgSend timed out after 2.5 seconds\n");
    }
}
```

---

## Critical Rule: Timeout + Kernel Call Must Be Adjacent

```
⚠️  CRITICAL RULE
═══════════════════════════════════════════════════════════════════

   TimerTimeout is RELATIVE to when it is called.
   It is AUTOMATICALLY CANCELLED on the NEXT kernel call.


   CORRECT: Adjacent calls
   ───────────────────────

      Thread execution:
      │
      ▼
   ┌────────────────┐
   │ TimerTimeout() │  ← set 2.5s timeout
   └───────┬────────┘
           │
           ▼
   ┌────────────────┐
   │   MsgSend()    │  ← timeout APPLIED here ✓
   │   (blocks)     │
   └────────────────┘
           │
           ▼
      [2.5s pass] → ETIMEDOUT


   WRONG: Non-adjacent calls
   ─────────────────────────

      Thread execution:
      │
      ▼
   ┌────────────────┐
   │ TimerTimeout() │  ← set 2.5s timeout
   └───────┬────────┘
           │
           ▼
   ┌────────────────┐
   │  SomeKernel()  │  ← timeout CANCELLED here ✗
   │   (any call)   │     (TimerTimeout has no effect!)
   └───────┬────────┘
           │
           ▼
   ┌────────────────┐
   │   MsgSend()    │  ← NO timeout applied!
   │   (blocks)     │     Can hang forever!
   └────────────────┘
```

### Visual Timeline: Correct vs Wrong

```
CORRECT: Timeout applied to MsgSend
═══════════════════════════════════════════════════════════════════

   Time →
   │
   │  TimerTimeout(2.5s) ─┐
   │                      │
   │  MsgSend() ──────────┼──► [BLOCKED]
   │                      │
   │                      │     ... 2.5s ...
   │                      │
   │                      └──► ETIMEDOUT ✓
   │                           MsgSend returns -1
   │


WRONG: Timeout cancelled by intermediate call
═══════════════════════════════════════════════════════════════════

   Time →
   │
   │  TimerTimeout(2.5s) ─┐
   │                      │
   │  SomeKernelCall() ───┼──► [CANCELLED] ✗
   │                      │     timeout lost!
   │                      │
   │  MsgSend() ──────────┼──► [BLOCKED]
   │                      │
   │                      │     ... forever ...
   │                      │
   │                      └──► HANGS! No timeout!
   │
```

### What Counts as a "Kernel Call"?

```
┌─────────────────────────────────────────────────────────────────────┐
│  ANY kernel call cancels the timeout:                               │
│                                                                     │
│    MsgSend()      MsgReceive()      MsgReply()                      │
│    ConnectAttach()  ChannelCreate()  ThreadCtl()                    │
│    ClockPeriod()  timer_create()    timer_settime()               │
│    ... and many more                                                │
│                                                                     │
│  Even "harmless" calls like ClockPeriod() cancel it!               │
│                                                                     │
│  RULE: TimerTimeout() must be immediately followed by the           │
│        blocking kernel call you want to timeout.                    │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Timeout States

### SEND Block Timeout

```
Client times out waiting for server to receive message
═══════════════════════════════════════════════════════

   Client                              Server
   ──────                              ──────
      │                                   │
      │  TimerTimeout(2.5s)               │
      │  MsgSend(coid, ...)               │
      │──────────────────────────────────►│
      │                                   │
      │  [SEND blocked]                   │  [Not calling MsgReceive]
      │                                   │
      │  ... 2.5s ...                     │
      │                                   │
      │  ← ETIMEDOUT                      │
      │  MsgSend returns -1               │
      │                                   │
      │  Client can retry or abort        │
```

### REPLY Block Timeout

```
Client times out waiting for server reply
══════════════════════════════════════════

   Client                              Server
   ──────                              ──────
      │                                   │
      │  MsgSend(coid, ...)               │  MsgReceive(chid, ...)
      │──────────────────────────────────►│
      │                                   │
      │  [REPLY blocked]                  │  [Processing... slow]
      │                                   │
      │  TimerTimeout(2.5s)               │
      │  ← TOO LATE!                      │
      │  Already in REPLY block           │
      │                                   │
      │  ⚠️  TimerTimeout must be BEFORE  │
      │      MsgSend, not after!          │
```

```
CORRECT: Timeout set BEFORE MsgSend
══════════════════════════════════════

   Client                              Server
   ──────                              ──────
      │                                   │
      │  TimerTimeout(2.5s)               │
      │  MsgSend(coid, ...)               │  MsgReceive(chid, ...)
      │──────────────────────────────────►│
      │                                   │
      │  [REPLY blocked]                  │  [Processing... slow]
      │                                   │
      │  ... 2.5s ...                     │
      │                                   │
      │  ← ETIMEDOUT                      │
      │  MsgSend returns -1               │
```

---

## Return Values

```
┌─────────────────────────────────────────────────────────────────────┐
│  TimerTimeout Return Value                                          │
│                                                                     │
│    Returns: 0 on success (timeout was set)                        │
│                                                                     │
│  The BLOCKING CALL returns:                                         │
│                                                                     │
│    On timeout:                                                      │
│      return value = -1                                              │
│      errno          = ETIMEDOUT                                     │
│                                                                     │
│    On success (before timeout):                                     │
│      Normal return value of the kernel call                         │
│                                                                     │
│    On other error:                                                  │
│      errno = appropriate error code                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Error Handling Pattern

```c
#include <sys/neutrino.h>
#include <errno.h>
#include <stdio.h>

struct _itimer timeout;
int ret;

/* Set 2.5 second timeout */
timeout.nsec = 2500000000LL;

/* MUST be immediately followed by the blocking call */
TimerTimeout(CLOCK_MONOTONIC, 0, NULL, &timeout, NULL);

ret = MsgSend(coid, send_buf, send_len, reply_buf, reply_len);

if (ret == -1) {
    switch (errno) {
        case ETIMEDOUT:
            printf("Operation timed out after 2.5s\n");
            /* Handle timeout: retry, abort, etc. */
            break;

        case EINTR:
            printf("Interrupted by signal\n");
            break;

        case ESRCH:
            printf("Server process died\n");
            break;

        default:
            printf("Other error: %d\n", errno);
            break;
    }
}
```

---

## Immediate Timeout (Non-Blocking)

You can set a timeout of **zero** (or use `TIMER_IMMEDIATE`) to make a kernel call non-blocking — it returns immediately if it would block.

```
Non-Blocking Kernel Call
════════════════════════

   TimerTimeout(0)  ← zero timeout
   │
   ▼
   MsgReceive(chid, ...)  ← returns immediately if no message
   │
   ▼
   If message available:  returns msg size
   If no message:         returns -1, errno = ETIMEDOUT
```

```c
struct _itimer timeout;

/* Zero timeout = non-blocking */
timeout.nsec = 0;

TimerTimeout(CLOCK_MONOTONIC, 0, NULL, &timeout, NULL);

/* Try to receive without blocking */
rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);

if (rcvid == -1 && errno == ETIMEDOUT) {
    /* No message available right now */
    /* Do other work and retry later */
}
```

---

## Complete Examples

### Example 1: Client with Send Timeout

```c
/* ============================================================
 * Client with 2.5s timeout on MsgSend
 * Prevents indefinite blocking if server is down
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/neutrino.h>

#define SERVER_NAME     "my_server"
#define TIMEOUT_NS      2500000000LL   /* 2.5 seconds */

typedef struct {
    int  request_id;
    int  data;
} request_t;

typedef struct {
    int  status;
    int  result;
} reply_t;

int main(void) {
    int         coid, ret;
    request_t   req = {1, 42};
    reply_t     reply;
    struct _itimer timeout;

    /* Find server */
    coid = name_open(SERVER_NAME, 0);
    if (coid == -1) {
        perror("name_open");
        return EXIT_FAILURE;
    }

    /* Set timeout: 2.5 seconds */
    timeout.nsec = TIMEOUT_NS;

    /* CRITICAL: TimerTimeout immediately followed by MsgSend */
    TimerTimeout(CLOCK_MONOTONIC, 0, NULL, &timeout, NULL);

    ret = MsgSend(coid, &req, sizeof(req), &reply, sizeof(reply));

    if (ret == -1) {
        if (errno == ETIMEDOUT) {
            printf("[TIMEOUT] Server did not respond within 2.5s\n");
            printf("[TIMEOUT] Server may be down or overloaded\n");
            /* Handle: retry, fail, alert user, etc. */
        } else {
            perror("MsgSend");
        }
        name_close(coid);
        return EXIT_FAILURE;
    }

    printf("[OK] Server replied: status=%d, result=%d\n",
           reply.status, reply.result);

    name_close(coid);
    return EXIT_SUCCESS;
}
```

### Example 2: Server with Receive Timeout

```c
/* ============================================================
 * Server with timeout on MsgReceive
 * Allows server to do periodic work even with no clients
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/neutrino.h>

#define TIMEOUT_NS      500000000LL    /* 500 milliseconds */

typedef struct {
    int  type;
    int  data;
} client_msg_t;

int main(void) {
    int         chid, rcvid;
    client_msg_t msg;
    struct _itimer timeout;

    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        return EXIT_FAILURE;
    }

    printf("Server running (pid=%d, chid=%d)\n", getpid(), chid);

    while (1) {
        /* Set timeout: 500ms */
        timeout.nsec = TIMEOUT_NS;

        /* CRITICAL: TimerTimeout immediately followed by MsgReceive */
        TimerTimeout(CLOCK_MONOTONIC, 0, NULL, &timeout, NULL);

        rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1) {
            if (errno == ETIMEDOUT) {
                /* No client messages for 500ms */
                printf("[TIMEOUT] No messages — doing periodic maintenance\n");
                do_periodic_maintenance();
                continue;
            }
            perror("MsgReceive");
            break;
        }

        if (rcvid == 0) {
            /* Pulse received */
            printf("[PULSE] code=%d\n", msg.type);
            continue;
        }

        /* Message from client */
        printf("[MSG] from rcvid=%d, type=%d, data=%d\n",
               rcvid, msg.type, msg.data);

        /* Process and reply */
        MsgReply(rcvid, EOK, NULL, 0);
    }

    ChannelDestroy(chid);
    return EXIT_SUCCESS;
}
```

### Example 3: WRONG — Timeout Cancelled

```c
/* ============================================================
 * WRONG EXAMPLE — Do NOT do this!
 * Timeout gets cancelled by intermediate kernel call
 * ============================================================ */

#include <stdio.h>
#include <errno.h>
#include <sys/neutrino.h>

void bad_example(int coid) {
    struct _itimer timeout;
    struct _clockperiod period;  /* ← THIS IS THE PROBLEM */
    int ret;

    timeout.nsec = 2500000000LL;   /* 2.5s timeout */

    TimerTimeout(CLOCK_MONOTONIC, 0, NULL, &timeout, NULL);
    /* ↑ timeout is SET */

    /* WRONG: This kernel call CANCELS the timeout! */
    ClockPeriod(CLOCK_REALTIME, NULL, &period, 0);
    /* ↑ timeout is now CANCELLED! */

    /* This MsgSend has NO timeout — can block forever! */
    ret = MsgSend(coid, ...);

    if (ret == -1 && errno == ETIMEDOUT) {
        /* This will NEVER happen! Timeout was cancelled! */
        printf("This code is unreachable!\n");
    }
}
```

### Example 4: CORRECT — Timeout Properly Applied

```c
/* ============================================================
 * CORRECT EXAMPLE — Do it this way!
 * ============================================================ */

#include <stdio.h>
#include <errno.h>
#include <sys/neutrino.h>

void good_example(int coid) {
    struct _itimer timeout;
    int ret;

    /* Do any setup calls BEFORE TimerTimeout */
    /* (ClockPeriod, ConnectAttach, etc.) */

    /* Now set timeout immediately before blocking call */
    timeout.nsec = 2500000000LL;   /* 2.5s timeout */

    /* CORRECT: TimerTimeout immediately followed by MsgSend */
    TimerTimeout(CLOCK_MONOTONIC, 0, NULL, &timeout, NULL);
    ret = MsgSend(coid, ...);

    if (ret == -1 && errno == ETIMEDOUT) {
        /* This WILL work! Timeout was applied to MsgSend! */
        printf("MsgSend timed out after 2.5s\n");
    }
}
```

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        TIMERTIMEOUT QUICK REF                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  FUNCTION:                                                                  │
│    int TimerTimeout(clock_id, flags, event, ntime, otime);                │
│                                                                             │
│  PARAMETERS:                                                                │
│    clock_id  → CLOCK_REALTIME or CLOCK_MONOTONIC                          │
│    flags     → 0 or TIMER_TOLERANCE, etc.                                 │
│    event     → sigevent for notification (NULL = none)                    │
│    ntime     → struct _itimer with timeout in nsec                        │
│    otime     → struct _itimer for old timeout (NULL = don't care)        │
│                                                                             │
│  CRITICAL RULE:                                                             │
│    TimerTimeout() MUST be immediately followed by the blocking            │
│    kernel call. Any intermediate kernel call CANCELS the timeout!           │
│                                                                             │
│  RETURN:                                                                    │
│    TimerTimeout returns 0 on success                                        │
│    Blocking call returns -1 with errno = ETIMEDOUT on timeout               │
│                                                                             │
│  TIMEOUT IS CANCELLED BY:                                                   │
│    • Any kernel call (MsgSend, MsgReceive, ClockPeriod, etc.)              │
│    • Timer is one-shot: applies only to the NEXT blocking call            │
│                                                                             │
│  USE CLOCK_MONOTONIC:                                                       │
│    Immune to system time changes (NTP, ClockAdjust, etc.)                  │
│                                                                             │
│  ZERO TIMEOUT:                                                              │
│    ntime.nsec = 0 → non-blocking call (returns ETIMEDOUT immediately)      │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Common Blocking Calls That Support Timeout

```
┌────────────────────────┬────────────────────────────────────────────────┐
│      Kernel Call       │           What it waits for                    │
├────────────────────────┼────────────────────────────────────────────────┤
│  MsgSend()             │  Server to receive + reply to message        │
│  MsgReceive()          │  Client to send a message                    │
│  MsgReceivePulse()     │  A pulse to arrive                           │
│  MsgReply()/MsgError() │  (rarely blocks)                             │
│  SignalWaitinfo()      │  A signal to be delivered                    │
│  InterruptWait()       │  An interrupt to occur                       │
│  sync_mutex_lock()     │  A mutex to become available                 │
│  sync_condvar_wait()   │  A condition variable to be signaled         │
└────────────────────────┴────────────────────────────────────────────────┘
```

### Do's and Don'ts

```
┌─────────────────────────────────────┬─────────────────────────────────────┐
│              ✅ DO                   │              ❌ DON'T                │
├─────────────────────────────────────┼─────────────────────────────────────┤
│                                     │                                     │
│  TimerTimeout();                    │  TimerTimeout();                    │
│  MsgSend();          ← adjacent    │  ClockPeriod();    ← cancels!      │
│                                     │  MsgSend();        ← no timeout      │
│                                     │                                     │
│  Use CLOCK_MONOTONIC               │  Use CLOCK_REALTIME                │
│  (immune to time changes)           │  (affected by NTP jumps)          │
│                                     │                                     │
│  Check errno == ETIMEDOUT          │  Ignore return value               │
│  after blocking call                │                                     │
│                                     │                                     │
│  Set timeout before each blocking  │  Set once and reuse                │
│  call (one-shot per call)          │  (gets cancelled!)                   │
│                                     │                                     │
│  Handle timeout gracefully         │  Assume timeout never fires        │
│  (retry, abort, alert)             │                                     │
│                                     │                                     │
└─────────────────────────────────────┴─────────────────────────────────────┘
```

---

> **📌 Key Takeaways:**
>
> - `TimerTimeout()` sets a one-shot timeout for the **next** blocking kernel call
> - The timeout is **automatically cancelled** by any intermediate kernel call
> - **Always** place `TimerTimeout()` immediately before the blocking call
> - Use `CLOCK_MONOTONIC` to avoid issues with system time changes
> - Check `errno == ETIMEDOUT` after the blocking call returns -1
> - Zero timeout (`ntime.nsec = 0`) makes the call non-blocking
> - Timeout is **per-call**: set it before every blocking operation you want to limit
