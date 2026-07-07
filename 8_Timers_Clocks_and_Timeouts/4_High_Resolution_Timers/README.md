# QNX High-Resolution Timers — Complete Guide

---

## Overview

High-resolution timers provide **more precise** control over when a timer notification fires. Unlike regular timers that align to the system ticksize (default 1ms), high-resolution timers attempt to fire at or just after the exact requested expiry time.

```
Regular Timer (aligned to tick)          High-Resolution Timer (precise)
═══════════════════════════════════       ═══════════════════════════════════

   Requested: 4.5ms                          Requested: 4.5ms
       │                                         │
       ▼                                         ▼
   Rounds UP to nearest tick                 Fires at ~4.5ms
       │                                         │
   ┌──┴──┐                                   ┌──┴──┐
   │ 5ms │                                   │4.5ms│  ← as close as HW allows
   └──┬──┘                                   └──┬──┘
       │                                         │
   Timer fires                               Timer fires
   (0.5ms late)                              (on time)
```

> **Precision vs Accuracy:** High-resolution timers give you **precision** (exact timing), not accuracy (correct absolute time). The actual achievable precision is bounded by the underlying timing hardware resolution.

---

## How High-Resolution Timers Work

### Initial Fire Time

The **first firing** is **not aligned** to the system tick. The kernel calculates the exact expiry time using CPU clock cycles and programs the hardware timer to fire as close as possible.

```
System Tick Alignment (Regular Timer)
═══════════════════════════════════════

   0ms      1ms      2ms      3ms      4ms      5ms
    │        │        │        │        │        │
────┼────────┼────────┼────────┼────────┼────────┼────
    ▲                                   ▲        ▲
 Requested                          Nearest  Timer
 4.5ms                              tick     fires
                                    (5ms)    (0.5ms late)


High-Resolution Timer (No Tick Alignment)
═════════════════════════════════════════

   0ms      1ms      2ms      3ms      4ms      5ms
    │        │        │        │        │        │
────┼────────┼────────┼────────┼────────┼────────┼────
    ▲                              ▲
 Requested                      Actual fire
 4.5ms                          (~4.5ms)
                                (hardware-limited precision)
```

### Repeat / Reload Time

For **periodic timers**, the repeat interval is also affected by the tolerance setting. The kernel attempts to maintain the specified interval as precisely as the hardware allows.

```
Regular Periodic Timer                      High-Res Periodic Timer
═══════════════════════════                 ═══════════════════════════

   │    │    │    │    │    │                 │  │  │  │  │  │
   ▼    ▼    ▼    ▼    ▼    ▼                 ▼  ▼  ▼  ▼  ▼  ▼
   │    │    │    │    │    │                 │  │  │  │  │  │
   │    │    │    │    │    │                 │  │  │  │  │  │
   │    │    │    │    │    │                 │  │  │  │  │  │

  Aligned to ticks (~1ms apart)            Precise interval
  Jitter = up to 1 ticksize                Jitter = tolerance value
```

---

## Tolerance & Repeat Behavior

The `TIMER_TOLERANCE` flag controls how the kernel schedules repeating timers.

### Key Rules

```
┌─────────────────────────────────────────────────────────────────────┐
│  TOLERANCE RULES                                                    │
│                                                                     │
│  1. Tolerance MUST be LESS than system ticksize (default: 1ms)      │
│                                                                     │
│  2. For ONE-SHOT timers:                                            │
│     → Tolerance is IGNORED for initial fire                         │
│     → Kernel tries to fire as close as possible regardless          │
│                                                                     │
│  3. For PERIODIC timers:                                            │
│     → Interval = MAX(tolerance, repeat_value)                       │
│     → Tolerance affects repeat precision                            │
│                                                                     │
│  4. Extra overhead:                                                 │
│     → Extra calculations in kernel                                  │
│     → Extra wake-ups (can't coalesce with tick interrupts)          │
│     → Therefore: PRIVILEGED OPERATION                               │
└─────────────────────────────────────────────────────────────────────┘
```

### Tolerance vs Repeat Value

```
Scenario 1: Tolerance < Repeat Value
═══════════════════════════════════════

   Tolerance = 5ns
   Repeat    = 4.5ms

   Interval used = MAX(5ns, 4.5ms) = 4.5ms

   Result: Timer repeats every ~4.5ms (repeat value dominates)


Scenario 2: Tolerance > Repeat Value
═════════════════════════════════════

   Tolerance = 2ms
   Repeat    = 1.5ms

   Interval used = MAX(2ms, 1.5ms) = 2ms

   Result: Timer repeats every ~2ms (tolerance dominates)


Scenario 3: One-Shot Timer
═══════════════════════════

   Tolerance = 5ns
   it_value  = 4.5ms
   it_interval = 0 (non-repeating)

   → Tolerance IGNORED for initial fire
   → Kernel fires as close to 4.5ms as hardware allows
```

---

## Privilege Requirements

High-resolution timers require a **special ability** because they impose extra overhead on the system.

```
┌─────────────────────────────────────────────────────────────────────┐
│  ⚠️  REQUIRED: PROCMGR_AID_HIGH_RESOLUTION_TIMER                    │
│                                                                     │
│  Without this ability, TimerSettime() with TIMER_TOLERANCE fails.   │
│                                                                     │
│  Why privileged?                                                    │
│  • Extra kernel calculations                                        │
│  • Extra hardware timer wake-ups                                    │
│  • Can't coalesce with regular tick interrupts                      │
│  • More CPU overhead per timer                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Granting the Ability

```c
#include <sys/procmgr.h>

// Grant high-resolution timer ability to this process
procmgr_ability(PROCMGR_ADN_ROOT,
                PROCMGR_AOP_ALLOW | PROCMGR_AID_HIGH_RESOLUTION_TIMER,
                PROCMGR_AOP_DENY  | PROCMGR_AID_EOL);
```

---

## Configuration API

### Use QNX Kernel Call (Recommended)

Since `TIMER_TOLERANCE` is **QNX-specific** and non-portable, use the kernel call `TimerSettime()` to make this explicit in your code:

```
POSIX timer_settime()          vs.          QNX TimerSettime()
═══════════════════════                     ═══════════════════════

   Portable                                  QNX-specific
   Generic                                   Explicitly non-portable
   Flags: TIMER_ABSTIME                      Flags: TIMER_ABSTIME |
                                               TIMER_TOLERANCE
```

### TimerSettime with Tolerance

```c
#include <sys/neutrino.h>

struct _itimer timer_tol;       // Tolerance specification
struct _itimer timer_spec;      // Actual timer specification

/* Step 1: Set tolerance (must be < ticksize) */
timer_tol.nsec = 5;             // 5 nanoseconds tolerance

/* Step 2: Set up timer parameters */
timer_spec.nsec = 4500000;      // 4.5 milliseconds (initial fire)

/* Step 3: Configure with TIMER_TOLERANCE flag */
TimerSettime(timerid,           // Timer ID from timer_create()
             TIMER_TOLERANCE,   // Enable high-resolution mode
             &timer_tol,        // Tolerance value (5ns)
             &timer_spec,       // Timer spec (4.5ms)
             NULL);             // No old value needed
```

### Using POSIX timer_settime()

You can also use the POSIX function, but the tolerance is passed differently:

```c
#include <time.h>

struct itimerspec timer_tol;    // Tolerance in it_value field
struct itimerspec timer_spec;   // Actual timer specification

/* Tolerance: stored in it_value */
timer_tol.it_value.tv_sec     = 0;
timer_tol.it_value.tv_nsec    = 5;          // 5ns tolerance

/* Interval must be 0 for tolerance struct */
timer_tol.it_interval.tv_sec  = 0;
timer_tol.it_interval.tv_nsec = 0;

/* Actual timer: 4.5ms one-shot */
timer_spec.it_value.tv_sec     = 0;
timer_spec.it_value.tv_nsec    = 4500000;   // 4.5ms
timer_spec.it_interval.tv_sec  = 0;
timer_spec.it_interval.tv_nsec = 0;         // Non-repeating

timer_settime(timerid,
              TIMER_TOLERANCE,  // QNX-specific flag
              &timer_tol,       // Tolerance in it_value
              NULL);
```

### API Comparison

```
┌────────────────────────┬────────────────────────┬────────────────────────┐
│        Aspect          │    POSIX timer_settime │   QNX TimerSettime()   │
├────────────────────────┼────────────────────────┼────────────────────────┤
│ Portability            │   Portable (with flag) │   QNX-specific         │
│ Tolerance location     │   it_value field       │   _itimer.nsec field   │
│ Tolerance interval     │   Must be 0            │   Must be 0            │
│ Readability            │   Confusing (it_value  │   Clear (dedicated     │
│                        │   overloaded)          │   nsec field)          │
│ Recommended?           │   No                   │   Yes (explicit)       │
└────────────────────────┴────────────────────────┴────────────────────────┘
```

---

## Complete Example

### One-Shot High-Resolution Timer (4.5ms Pulse)

```
Setup Flow
══════════

   ┌─────────────────┐
   │ SIGEV_PULSE_INIT│  ← Configure pulse event
   │  (coid, code)   │
   └────────┬────────┘
            │
            ▼
   ┌─────────────────┐
   │   timer_create  │  ← Create timer on CLOCK_MONOTONIC
   │ (CLOCK_MONOTONIC)│
   └────────┬────────┘
            │
            ▼
   ┌─────────────────┐
   │  TimerSettime   │  ← Set tolerance + timer spec
   │ TIMER_TOLERANCE │
   │   tol = 5ns     │
   │   spec = 4.5ms  │
   └────────┬────────┘
            │
            ▼
   ┌─────────────────┐
   │  Kernel computes │  ← Uses ClockCycles() for precision
   │  exact expiry    │
   └────────┬────────┘
            │
            ▼
   ┌─────────────────┐
   │  HW timer fires │  ← ~4.5ms later (HW-limited)
   │  Pulse delivered│
   └─────────────────┘
```

### Source Code

```c
/* ============================================================
 * QNX High-Resolution Timer Example
 * One-shot timer: fire pulse as close to 4.5ms as possible
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/procmgr.h>

#define MAINTENANCE_PULSE   _PULSE_CODE_MINAVAIL

int main(void) {
    int              chid, coid, rcvid;
    timer_t          timerid;
    struct sigevent  event;
    struct _itimer   timer_tol;     /* QNX-specific: tolerance */
    struct _itimer   timer_spec;    /* QNX-specific: timer spec */
    struct _pulse    pulse;

    /* Step 0: Grant high-resolution timer ability (REQUIRED) */
    if (procmgr_ability(PROCMGR_ADN_ROOT,
                        PROCMGR_AOP_ALLOW | PROCMGR_AID_HIGH_RESOLUTION_TIMER,
                        PROCMGR_AOP_DENY  | PROCMGR_AID_EOL) == -1) {
        perror("procmgr_ability");
        return EXIT_FAILURE;
    }

    /* Step 1: Create channel for receiving pulses */
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        return EXIT_FAILURE;
    }

    /* Step 2: Attach to own channel */
    coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach");
        return EXIT_FAILURE;
    }

    /* Step 3: Configure pulse event */
    SIGEV_PULSE_INIT(&event, coid,
                     SIGEV_PULSE_PRIO_INHERIT,
                     MAINTENANCE_PULSE,
                     0);

    /* Step 4: Create timer on monotonic clock */
    if (timer_create(CLOCK_MONOTONIC, &event, &timerid) == -1) {
        perror("timer_create");
        return EXIT_FAILURE;
    }

    /* ============================================================
     * Step 5: Set TOLERANCE first (before arming)
     * ============================================================ */

    /* Tolerance: 5 nanoseconds (must be < ticksize, default 1ms) */
    timer_tol.nsec = 5;

    /* Interval must be 0 for tolerance struct */
    /* (Only used for repeating timer interval calculation) */

    /* ============================================================
     * Step 6: Set actual timer specification
     * ============================================================ */

    /* Initial fire: 4.5 milliseconds from now */
    timer_spec.nsec = 4500000;      /* 4.5ms = 4,500,000 nanoseconds */

    /* Non-repeating timer (one-shot) */
    /* For one-shot: tolerance is ignored for initial fire */
    /* Kernel fires as close to 4.5ms as hardware allows */

    /* ============================================================
     * Step 7: Arm timer with TIMER_TOLERANCE flag
     * ============================================================ */
    if (TimerSettime(timerid,
                     TIMER_TOLERANCE,     /* Enable high-resolution mode */
                     &timer_tol,          /* Tolerance: 5ns */
                     &timer_spec,         /* Fire at: 4.5ms */
                     NULL) == -1) {
        perror("TimerSettime");
        return EXIT_FAILURE;
    }

    printf("High-resolution timer armed for 4.5ms (tolerance: 5ns)...\n");

    /* Step 8: Wait for pulse */
    rcvid = MsgReceive(chid, &pulse, sizeof(pulse), NULL);

    if (rcvid == 0) {
        printf("[PULSE] Timer fired! Code: %d\n", pulse.code);
        /* Perform time-critical operation here */
    }

    /* Cleanup */
    timer_delete(timerid);
    ConnectDetach(coid);
    ChannelDestroy(chid);

    return EXIT_SUCCESS;
}
```

### Periodic High-Resolution Timer Example

```c
/* Periodic timer with tolerance affecting repeat interval */

struct _itimer timer_tol;
struct _itimer timer_spec;

/* Tolerance: 100 microseconds */
timer_tol.nsec = 100000;        /* 100,000 ns = 100μs */

/* Timer spec: initial fire at 2ms, repeat every 5ms */
timer_spec.nsec = 2000000;      /* 2ms initial */

/* For periodic: interval = MAX(tolerance, repeat_value) */
/* If repeat_value is passed separately, kernel uses MAX */

TimerSettime(timerid,
             TIMER_TOLERANCE,
             &timer_tol,         /* Tolerance: 100μs */
             &timer_spec,        /* Initial: 2ms */
             NULL);

/* Result:
 *   Initial fire: ~2ms (as precise as possible)
 *   Repeat interval: MAX(100μs, configured_repeat)
 *   If repeat < 100μs, interval clamps to 100μs
 */
```

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     HIGH-RESOLUTION TIMER QUICK REF                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  PURPOSE:     Fire timers at precise times, not aligned to system tick      │
│                                                                             │
│  API:         TimerSettime()  [QNX kernel call, recommended]                │
│               timer_settime() [POSIX, portable but confusing]               │
│                                                                             │
│  FLAG:        TIMER_TOLERANCE                                               │
│                                                                             │
│  TOLERANCE:   Must be < system ticksize (default: < 1ms)                    │
│                                                                             │
│  ONE-SHOT:    Tolerance ignored for initial fire                            │
│               Kernel fires as close as HW allows                            │
│                                                                             │
│  PERIODIC:    Interval = MAX(tolerance, repeat_value)                       │
│               Tolerance affects repeat precision                            │
│                                                                             │
│  PRIVILEGE:   PROCMGR_AID_HIGH_RESOLUTION_TIMER required                    │
│               (extra overhead = privileged operation)                       │
│                                                                             │
│  PRECISION:   Bounded by underlying timing hardware resolution              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Comparison: Regular vs High-Resolution Timer

```
┌─────────────────────┬─────────────────────┬─────────────────────┐
│      Aspect         │   Regular Timer     │  High-Res Timer     │
├─────────────────────┼─────────────────────┼─────────────────────┤
│ Alignment           │   To system tick    │   None              │
│ Initial fire        │   Rounds UP to tick │   As precise as HW  │
│ Repeat interval     │   Aligned to tick   │   MAX(tol, repeat)  │
│ Overhead            │   Low               │   Higher            │
│ Privilege needed    │   No                │   Yes               │
│ API flag            │   None              │   TIMER_TOLERANCE   │
│ Coalescing          │   Yes (with ticks)  │   No (separate)     │
│ Use case            │   General timing    │   Precise control   │
└─────────────────────┴─────────────────────┴─────────────────────┘
```

---

> **📌 Key Takeaways:**
>
> - Use `TimerSettime()` (kernel call) to make QNX-specific code explicit
> - Tolerance **must** be less than ticksize (check with `ClockPeriod()`)
> - One-shot timers ignore tolerance for initial fire — always precise
> - Periodic timers use `MAX(tolerance, repeat_value)` for interval
> - Always grant `PROCMGR_AID_HIGH_RESOLUTION_TIMER` ability first
> - Precision is ultimately bounded by the hardware timer resolution
