# QNX Timers — Complete Guide

---

## Overview

QNX timer handling is **inherently tickless**. Unlike traditional OSes that fire a periodic timer interrupt (e.g., every 1ms), QNX only configures the hardware timer to generate an interrupt **when actually needed**.

```
Traditional OS (Tick-based)              QNX (Tickless)
=========================                  ==============

   Interrupt    Interrupt    Interrupt        (no regular interrupts)
       ▼           ▼           ▼
  ─────┼──────────┼──────────┼─────        ─────────────────────────
       │          │          │                          ▼
       │          │          │                    Interrupt only
       │          │          │                    when timer expires
       │          │          │
   Wasted CPU cycles                    Power efficient, on-demand
```

> **Key Point:** While tickless at the interrupt level, QNX still maintains a conceptual **ticksize** as a resolution/clock period for timer alignment and rounding.

---

## Ticksize & Resolution

The **ticksize** is the system's timer resolution. Most timers round to this value.

```
Ticksize Timeline (default = 1ms)
═══════════════════════════════════════════════════════════════════

   0ms        1ms        2ms        3ms        4ms        5ms
    │          │          │          │          │          │
────┼──────────┼──────────┼──────────┼──────────┼──────────┼────
    ▲          ▲          ▲          ▲          ▲          ▲
 Tick 0     Tick 1     Tick 2     Tick 3     Tick 4     Tick 5


Round-Robin Timeslice = 4 × ticksize
─────────────────────────────────────
   If ticksize = 1ms  →  Timeslice = 4ms
   If ticksize = 2ms  →  Timeslice = 8ms
   (Fixed 4× multiplier, cannot change)

   ┌────────────────────────────────────────┐
   │  High-Resolution Timers = EXCEPTION    │
   │  Not bound by ticksize resolution      │
   └────────────────────────────────────────┘
```

### Changing the Ticksize

Use the `-c` option with `procnto` in startup code:

```c
// In startup script / buildfile
procnto -c 500000    // Set ticksize to 0.5ms (500,000 nanoseconds)
```

### Querying Current Ticksize

```c
struct _clockperiod period;
ClockPeriod(CLOCK_REALTIME, NULL, &period, 0);
// period.nsec contains ticksize in nanoseconds
```

---

## Timer Rounding Behavior

Timers round **UP** to the nearest complete tick. Per POSIX: *"You cannot sleep for less than you asked for, but you can sleep for more."*

### Scenario A: Thread sleeps 3.5ms at tick boundary

```
   0ms        1ms        2ms        3ms        4ms
    │          │          │          │          │
────┼──────────┼──────────┼──────────┼──────────┼────
    ▲                                             ▲
  Thread sleeps                                Thread wakes
  for 3.5ms                                    after 4 ticks
                                               (4ms actual)

  Requested: 3.5ms  →  Actual: 4ms  ✓ (rounds UP)
```

### Scenario B: Thread sleeps 3.5ms starting mid-tick

```
   0ms        1ms        2ms        3ms        4ms        5ms
    │          │          │          │          │          │
────┼──────────┼──────────┼──────────┼──────────┼──────────┼────
              ▲                                             ▲
           Thread sleeps                                Thread wakes
           for 3.5ms                                    after 4 full ticks
                                                        (~4ms+ actual)

  Error is NEVER more than 1 ticksize
```

> **⚠️ Scheduling Delay:** After the timer fires and the thread becomes **runnable**, a higher-priority thread may still be running. The thread won't execute until the scheduler dispatches it — this is an additional delay beyond ticksize rounding.

---

## Per-Core Timer Implementation

Active timers are implemented on a **per-core basis**. When a thread creates a timer, it is added to the timer list of the core where that thread is currently running.

```
                    CPU Cores
                    ═════════

   ┌─────────────────────┐          ┌─────────────────────┐
   │    🔲 CPU Core 0    │          │    🔲 CPU Core 1    │
   │                     │          │                     │
   │  Timer List:        │          │  Timer List:        │
   │  [T1] → [T2] → [T3]│           │  [T4] → [T5]        │
   │                     │          │                     │
   │  Thread A created   │          │  Thread C created   │
   │  T1, T2 (here)      │          │  T4, T5 (here)      │
   │  Thread B created   │          │                     │
   │  T3 (here)          │          │                     │
   │                     │          │                     │
   │  ⏱️ HW Timer        │          │  ⏱️ HW Timer        │
   │  (next expiry)      │          │  (next expiry)      │
   └─────────────────────┘          └─────────────────────┘
            │                                │
            └──────── Load Balanced ─────────┘


┌─────────────────────────────────────────────────────────────────┐
│  ⚠️  CORE AFFINITY BEST PRACTICE                                │
│                                                                 │
│  If you bind threads to specific cores, SET AFFINITY FIRST,     │
│  then create timers. This ensures timers are on the same        │
│  core as the thread, saving scheduling overhead.                │
│                                                                 │
│  GOOD:  ThreadCtl(runmask) → timer_create()                     │
│  BAD:   timer_create() → ThreadCtl(runmask)   ← overhead!       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Timer Types & Configuration

When creating a timer, you configure these parameters:

```
                    ┌─────────────────┐
                    │     TIMER       │
                    │   CONFIGURATION │
                    └────────┬────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
        ▼                    ▼                    ▼
   ┌─────────┐        ┌─────────────┐       ┌─────────────┐
   │  Type   │        │Clock Source │       │ Initial Fire│
   │         │        │             │       │    Time     │
   │•One-shot│        │•REALTIME    │       │             │
   │•Periodic│        │•MONOTONIC   │       │•Absolute    │
   └─────────┘        └─────────────┘       │•Relative    │
                                            └─────────────┘
        ┌────────────────────┐
        │      Reload        │
        │     (Period)       │
        │  For periodic:     │
        │  repeat interval   │
        └────────────────────┘

        ┌────────────────────┐
        │   Event Delivery   │
        │                    │
        │ • SIGEV_PULSE      │
        │ • SIGEV_SIGNAL     │
        │ • SIGEV_THREAD     │
        └────────────────────┘
```

| Parameter        | Description                | Options                                       |
| ---------------- | -------------------------- | --------------------------------------------- |
| **Timer Type**   | How many times it fires    | One-shot, Periodic                            |
| **Clock Source** | Which clock drives it      | `CLOCK_REALTIME`, `CLOCK_MONOTONIC`           |
| **Initial Fire** | When it first expires      | Absolute (fixed time), Relative (from now)    |
| **Reload Value** | Repeat interval (periodic) | Set `it_interval` in `itimerspec`             |
| **Event**        | What happens on expiry     | `SIGEV_PULSE`, `SIGEV_SIGNAL`, `SIGEV_THREAD` |

---

## POSIX Timer API

### Creating a Timer

```c
timer_t timerid;
struct sigevent event;

// Configure event: deliver a pulse to our channel
SIGEV_PULSE_INIT(&event, coid, priority, code, value);

// Create timer on monotonic clock
timer_create(CLOCK_MONOTONIC, &event, &timerid);
```

### Setting / Arming a Timer

```c
struct itimerspec timer_spec;

// Initial fire time: 1.5 seconds from now (relative)
timer_spec.it_value.tv_sec     = 1;
timer_spec.it_value.tv_nsec    = 500000000;   // 500ms

// Reload value: repeat every 1.2 seconds (periodic)
timer_spec.it_interval.tv_sec  = 1;
timer_spec.it_interval.tv_nsec = 200000000;   // 200ms

// Arm the timer (0 = relative, TIMER_ABSTIME = absolute)
timer_settime(timerid, 0, &timer_spec, NULL);
```

### Timer Firing Sequence

```
   NOW                                    1.5s              2.7s              3.9s
    │                                      │                 │                 │
────┼──────────────────────────────────────┼─────────────────┼─────────────────┼────
    ▲                                      ▲                 ▲                 ▲
 timer_settime()                       First fire        Second fire      Third fire
 called                                (it_value)        (+1.2s interval) (+1.2s interval)


  it_value = 1.5s  ──────────────────────────────────────►
  it_interval = 1.2s  ───────────────────────────────────►  ───────────────────►
```

### Deleting a Timer

```c
// Permanently delete and free resources
timer_delete(timerid);
```

---

## Complete Server Example

A server that handles both **client messages** and **periodic timer pulses** for maintenance checks.

```
                    Server Architecture
                    ═══════════════════

        ┌─────────────────────────────────────────┐
        │           🖥️  SERVER PROCESS            │
        │                                         │
        │    ┌─────────────────────────────┐      │
        │    │    Channel (MsgReceive)     │      │
        │    └─────────────┬───────────────┘      │
        │                  │                      │
        │    ┌─────────────┴─────────────┐        │
        │    │  union { _pulse pulse;    │        │
        │    │         msg_t msg; }      │        │
        │    └─────────────┬─────────────┘        │
        │                  │                      │
        │           ┌──────┴──────┐               │
        │           ▼             ▼               │
        │      rcvid == 0?    rcvid > 0?          │
        │           │             │               │
        │      YES  │        NO   │               │
        │           ▼             ▼               │
        │    ┌──────────┐   ┌──────────┐          │
        │    │  PULSE   │   │ MESSAGE  │          │
        │    │ received │   │ received │          │
        │    │          │   │          │          │
        │    │ Check    │   │ Handle   │          │
        │    │ pulse    │   │ client   │          │
        │    │ code     │   │ request  │          │
        │    │          │   │          │          │
        │    │ MAINT_   │   │ MsgReply │          │
        │    │ PULSE →  │   │          │          │
        │    │ do check │   │          │          │
        │    └──────────┘   └──────────┘          │ 
        │                                         │
        └─────────────────────────────────────────┘

   ⏱️ Timer Config:                👤 Client:
   ───────────────                  ────────
   Clock: MONOTONIC                 Sends messages
   Initial: 1.5s                    to server
   Interval: 1.2s                   channel
   Event: SIGEV_PULSE
   Code: MAINTENANCE_PULSE
```

### Source Code

```c
/* ============================================================
 * QNX Timer Server Example
 * Server receives messages from clients AND periodic timer pulses
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/neutrino.h>

/* Pulse codes */
#define MAINTENANCE_PULSE   _PULSE_CODE_MINAVAIL

/* Message structure from clients */
typedef struct {
    uint16_t type;
    uint16_t subtype;
    int      data;
} client_msg_t;

/* Union: overlays pulse and message for MsgReceive */
typedef union {
    struct _pulse   pulse;
    client_msg_t    msg;
} msg_union_t;

int main(void) {
    int              chid, rcvid, coid;
    timer_t          timerid;
    struct sigevent  event;
    struct itimerspec timer_spec;
    msg_union_t      msg;

    /* Step 1: Create a channel for receiving messages & pulses */
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        return EXIT_FAILURE;
    }

    /* Step 2: Attach to our own channel to receive pulses */
    coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach");
        return EXIT_FAILURE;
    }

    /* Step 3: Set up SIGEV_PULSE event structure */
    SIGEV_PULSE_INIT(&event, coid,
                     SIGEV_PULSE_PRIO_INHERIT,   /* inherit thread priority */
                     MAINTENANCE_PULSE,          /* custom pulse code */
                     0);                         /* pulse value */

    /* Step 4: Create timer on CLOCK_MONOTONIC */
    if (timer_create(CLOCK_MONOTONIC, &event, &timerid) == -1) {
        perror("timer_create");
        return EXIT_FAILURE;
    }

    /* Step 5: Configure timer spec
     *   - Initial fire: 1.5 seconds from now (relative)
     *   - Repeat interval: 1.2 seconds (periodic)
     */
    timer_spec.it_value.tv_sec     = 1;           /* 1 second */
    timer_spec.it_value.tv_nsec    = 500000000;   /* + 500ms = 1.5s total */
    timer_spec.it_interval.tv_sec  = 1;           /* 1 second */
    timer_spec.it_interval.tv_nsec = 200000000;   /* + 200ms = 1.2s total */

    /* Step 6: Arm the timer (0 = relative time) */
    if (timer_settime(timerid, 0, &timer_spec, NULL) == -1) {
        perror("timer_settime");
        return EXIT_FAILURE;
    }

    printf("Server running. Waiting for messages and timer pulses...\n");

    /* Step 7: Main event loop — handle both messages and pulses */
    while (1) {
        rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);

        if (rcvid == 0) {
            /* ================= PULSE RECEIVED ================= */
            switch (msg.pulse.code) {
                case MAINTENANCE_PULSE:
                    printf("[TIMER] Performing periodic maintenance check...\n");
                    /* Run integrity checks, cleanup, etc. */
                    break;

                default:
                    printf("[PULSE] Unknown pulse code: %d\n", msg.pulse.code);
                    break;
            }
        }
        else if (rcvid > 0) {
            /* ================= MESSAGE RECEIVED ================= */
            printf("[MSG] Client request: type=%d, data=%d\n",
                   msg.msg.type, msg.msg.data);

            /* Process client request... */
            int reply = 0;
            MsgReply(rcvid, EOK, &reply, sizeof(reply));
        }
        else {
            perror("MsgReceive");
            break;
        }
    }

    timer_delete(timerid);
    ConnectDetach(coid);
    ChannelDestroy(chid);
    return EXIT_SUCCESS;
}
```

---

## Cancelling & Restarting Timers

You do **NOT** need to destroy a timer to stop it. Simply set all values to zero.

```
   ┌──────────────────────────┐         ┌──────────────────────────┐
   │      ❌ CANCEL TIMER     │         │      🔄 RESTART TIMER    │
   │                          │         │                          │
   │  1. Set all itimerspec   │         │  1. Fill itimerspec with │
   │     values to 0          │         │     new values           │
   │                          │         │                          │
   │  2. Call timer_settime() │         │  2. Call timer_settime() │
   │                          │         │                          │
   │  ✓ Timer stops           │         │  ✓ Timer restarts with   │
   │  ✓ timerid still valid   │         │    new timing            │
   │  ✓ Can restart anytime   │         │  ✓ Same timerid reused   │
   └──────────────────────────┘         └──────────────────────────┘
```

### Cancel Timer (Stop Without Deleting)

```c
struct itimerspec stop_spec;
memset(&stop_spec, 0, sizeof(stop_spec));  /* All zeros = disarmed */
timer_settime(timerid, 0, &stop_spec, NULL);
/* Timer is now cancelled but timerid remains valid */
```

### Restart Timer (Reuse Same timerid)

```c
struct itimerspec restart_spec;
restart_spec.it_value.tv_sec     = 2;          /* New initial: 2s */
restart_spec.it_value.tv_nsec    = 0;
restart_spec.it_interval.tv_sec  = 1;          /* New interval: 1s */
restart_spec.it_interval.tv_nsec = 0;
timer_settime(timerid, 0, &restart_spec, NULL);
/* Timer restarts with new timing — same timerid, no recreate needed! */
```

---

## Quick Reference

| Concept                   | Description                                           | Key Function / Value                 |
| ------------------------- | ----------------------------------------------------- | ------------------------------------ |
| **Tickless Architecture** | No periodic timer interrupts; HW configured on-demand | Interrupt only when timer expires    |
| **Ticksize**              | Timer resolution / clock period (default: 1ms)        | `procnto -c <ns>`                    |
| **Round-Robin**           | Timeslice = 4 × ticksize (fixed multiplier)           | 4ms default (1ms ticksize)           |
| **Timer Rounding**        | Always rounds UP to complete ticks                    | Error ≤ 1 ticksize                   |
| **Per-Core Timers**       | Each core maintains its own timer list                | Set affinity BEFORE `timer_create()` |
| **Create Timer**          | Allocate a timer with event configuration             | `timer_create()`                     |
| **Arm Timer**             | Set initial fire time and repeat interval             | `timer_settime()`                    |
| **Cancel Timer**          | Stop timer without destroying it                      | Set `itimerspec` to all zeros        |
| **Delete Timer**          | Permanently free timer resources                      | `timer_delete()`                     |
| **High-Res Timers**       | Not bound by ticksize resolution                      | Exception to ticksize rules          |

---

> **📌 Remember:**
>
> - QNX timers are **tickless at the interrupt level** but use **ticksize for rounding**
> - Timers are **per-core** — set thread affinity before creating timers
> - Use `SIGEV_PULSE_INIT` for server-style event delivery
> - Cancel timers by zeroing `itimerspec` — no need to destroy and recreate
