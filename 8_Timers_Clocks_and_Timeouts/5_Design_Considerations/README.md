# QNX Timing Design Considerations

---

## Overview

Two common timing design issues in QNX:

1. **Accumulating error** when running periodic tasks
2. **Erratic timer expiry** with high-frequency timers

This guide explains why these happen and how to fix them.

---

## Periodic Execution Without Accumulating Error

### The Wrong Way: Delay Loops

Using `delay()` or `sleep()` in a loop causes **accumulating error** because each delay starts from when the previous delay returns, not from the original base time.

```
BAD: delay() loop — Error accumulates
═══════════════════════════════════════════════════════════════════

Tick:  0      1      2      3      4      5      6      7      8      9     10     11     12     13     14
       │      │      │      │      │      │      │      │      │      │      │      │      │      │      │
───────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼────
       │             │                    │                           │                                  │
       ▲             ▲                    ▲                           ▲                                  ▲
    delay(5ms)    wake up             delay(5ms)                delay(5ms)                          wake up
    called        (HP thread          called                     called                              (HP thread
                  running)            at t=6ms                   at t=11ms                           running)
                  at t=3ms

       │←─1ms err─→│
       │←─────────8ms─────────→│     ← Should be 10ms (2 × 5ms), but it's 8ms
                                    Error: +3ms accumulated

       │←───────────13ms──────────────→│  ← Should be 15ms (3 × 5ms), but it's ~13ms
                                           Error keeps growing!

Problem: Each delay() is relative to when it was CALLED, not the original base time.
         Scheduling delays + tick alignment cause drift.
```

#### Why Error Accumulates

```
Timeline with delay() loop (5ms requested, 1ms ticksize)
══════════════════════════════════════════════════════════

   Call delay(5ms) at t=0
   │
   ▼
   Kernel aligns to tick: wakes at t=5ms (on tick boundary)
   │
   ▼
   But HP thread running → we don't run until t=6ms
   │
   ▼
   We call delay(5ms) again at t=6ms
   │
   ▼
   Kernel aligns to NEXT tick boundary: wakes at t=11ms
   │
   ▼
   But HP thread running → we don't run until t=13ms
   │
   ▼
   We call delay(5ms) again at t=13ms
   │
   ▼
   ... and so on. Each cycle adds error.

   After 4 iterations:
   Expected: 4 × 5ms = 20ms
   Actual:   ~27ms (7ms error accumulated!)
```

### The Right Way: Kernel-Managed Periodic Timer

Use `timer_create()` + `timer_settime()` with a **periodic timer**. The kernel handles the timing internally, maintaining the base reference point.

```
GOOD: Periodic timer — No accumulated error
═══════════════════════════════════════════════════════════════════

Tick:  0      1      2      3      4      5      6      7      8      9     10     11     12     13     14     15     16     17     18     19     20
       │      │      │      │      │      │      │      │      │      │      │      │      │      │      │      │      │      │      │      │      │
───────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼────
       │             │             │             │             │             │
       ▲             ▲             ▲             ▲             ▲
    timer_set     wake up      wake up      wake up      wake up
    (periodic    (HP thread   (no HP       (HP thread   (no HP
     5ms)        running)     thread)      running)     thread)
                  at t=3ms     at t=5ms     at t=8ms     at t=10ms

       │←─3ms─→│←──2ms──→│←──3ms──→│←──2ms──→│
       │←────5ms────→│←────5ms────→│←────5ms────→│←────5ms────→│

   After 4 periods:
   Expected: 4 × 5ms = 20ms
   Actual:   ~20ms ✓ (no accumulated error!)

   The kernel maintains the base time and always schedules
   the NEXT expiry relative to the ORIGINAL start time.
```

#### Code Comparison

```
┌─────────────────────────────────────┬─────────────────────────────────────┐
│         BAD: delay() loop           │      GOOD: Periodic timer           │
├─────────────────────────────────────┼─────────────────────────────────────┤
│                                     │                                     │
│  while (1) {                        │  // Create periodic timer           │
│      delay(5ms);   ← rel. to now    │  timer_create(CLOCK_MONOTONIC,      │
│      do_something();                │              &event, &timerid);     │
│  }                                  │                                     │
│                                     │  itimer.it_value.tv_sec = 0;        │
│  Error accumulates each iteration   │  itimer.it_value.tv_nsec = 5000000; │
│                                     │  itimer.it_interval =itimer.it_value│
│                                     │                                     │
│                                     │  timer_settime(timerid, 0,          │
│                                     │                  &itimer, NULL);    │
│                                     │                                     │
│                                     │  while (1) {                        │
│                                     │      wait_for_timer();  ← block     │
│                                     │      do_something();                │
│                                     │  }                                  │
│                                     │                                     │
│                                     │  Kernel maintains base time         │
│                                     │  No accumulated error               │
└─────────────────────────────────────┴─────────────────────────────────────┘
```

#### Visual Difference After 4 Iterations

```
After 4 × 5ms periods (should be 20ms total):
═══════════════════════════════════════════════════════════════════

   delay() loop:                          Periodic timer:
   ────────────                           ──────────────

   t=0  ─┬─ delay(5ms)                       t=0   ─┬─ timer_settime(5ms periodic)
         │                                          │
   t=3  ─┼─ wake (HP thread)                 t=3   ─┼─ wake (HP thread)
         │  error: +0ms                             │  error: +0ms
         │                                          │
   t=6  ─┼─ delay(5ms) again                  t=5  ─┼─ wake (on time!)
         │                                          │  kernel kept base time
   t=8  ─┼─ wake (on tick)                   t=8   ─┼─ wake (HP thread)
         │  error: +3ms                             │  still aligned to base
         │                                          │
   t=11 ─┼─ delay(5ms) again                  t=10 ─┼─ wake (on time!)
         │                                          │
   t=13 ─┼─ wake (HP thread)                 t=13  ─┼─ wake (HP thread)
         │  error: +7ms!                            │  still no accumulated error
         │                                          │
   t=16 ─┼─ delay(5ms) again                  t=15 ─┼─ wake (on time!)
         │                                          │
   t=18 ─┼─ wake (on tick)                   t=18  ─┼─ wake (HP thread)
         │  error: +11ms!                           │  total: ~20ms ✓
         │                                          │  
   Total: ~27ms (7ms late!)               Total: ~20ms ✓ (on time!)
```

---

## High-Frequency Timer Issues

When your timer period is **not an exact multiple** of the ticksize, you get **erratic wake-up times** that alternate between two values.

### Example: 1.5ms Timer with 1ms Ticksize

```
1.5ms timer on 1ms ticksize — Erratic behavior
═══════════════════════════════════════════════════════════════════

Tick:  0      1      2      3      4      5      6      7      8      9     10     11     12
       │      │      │      │      │      │      │      │      │      │      │      │      │
───────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼──────┼────
       │      │      │      │      │      │      │      │      │      │      │      │      │
       ▲      ▲             ▲      ▲             ▲      ▲             ▲      ▲
      t=0   t=1           t=2    t=3           t=4    t=5           t=6    t=7
       │     │             │      │             │      │             │      │
    timer   wake         wake   wake         wake   wake         wake   wake
    set     (1ms)        (2ms)  (1ms)        (2ms)  (1ms)        (2ms)  (1ms)

       │←─1ms─→│←────2ms────→│←─1ms─→│←────2ms────→│←─1ms─→│←────2ms────→│

   Requested: 1.5ms every time
   Actual:    1ms, 2ms, 1ms, 2ms, 1ms, 2ms ... (alternating!)
   Average:   1.5ms ✓  but individual periods are erratic
```

### Why This Happens

```
Detailed breakdown of 1.5ms timer on 1ms ticksize
═══════════════════════════════════════════════════

   Step 1: timer_settime(1.5ms) called at t=0
   │
   ▼
   Kernel aligns to next tick boundary: timer fires at t=1ms
   │
   ▼
   Actual wake: t=1ms (0.5ms late — error for this period)
   │
   ▼
   Step 2: Kernel calculates next expiry from BASE time
           Base + 1.5ms = t=1.5ms, but aligned to tick → t=2ms
   │
   ▼
   Actual wake: t=2ms (exactly 1ms after previous wake)
   │
   ▼
   Step 3: Next expiry from BASE time
           Base + 3.0ms = t=3.0ms, aligned to tick → t=3ms
   │
   ▼
   Actual wake: t=3ms (exactly 1ms after previous wake)
   │
   ▼
   Step 4: Next expiry from BASE time
           Base + 4.5ms = t=4.5ms, aligned to tick → t=5ms
   │
   ▼
   Actual wake: t=5ms (2ms after previous wake!)
   │
   ▼
   Pattern continues: 1ms, 2ms, 1ms, 2ms, 1ms, 2ms ...

   The kernel maintains the base time correctly (no accumulated error),
   but tick alignment forces alternating 1ms and 2ms intervals.
```

### The Core Problem

```
┌─────────────────────────────────────────────────────────────────────┐
│  ROOT CAUSE                                                         │
│                                                                     │
│  Timer period (1.5ms) is NOT an exact multiple of ticksize (1ms)   │
│                                                                     │
│  1.5ms / 1ms = 1.5 → not an integer!                              │
│                                                                     │
│  Kernel MUST align to tick boundaries, so:                          │
│  • Sometimes rounds UP to next tick (2ms)                         │
│  • Sometimes rounds DOWN/already on tick (1ms)                    │
│                                                                     │
│  Result: Alternating intervals that average to 1.5ms               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Solutions for Erratic Timers

### Solution 1: Use a Separate Hardware Timer

Program your own timer interrupt directly instead of using the kernel's tick-based timer.

```
┌─────────────────────────────────────────────────────────────────────┐
│  SOLUTION 1: Custom Hardware Timer                                │
│                                                                     │
│  Instead of:                                                        │
│    timer_create(CLOCK_MONOTONIC, ...)  ← uses kernel tick timer    │
│                                                                     │
│  Use:                                                               │
│    Your own timer interrupt handler                                 │
│    Program chip timer directly                                      │
│    Interrupt fires at exact 1.5ms (not aligned to tick)            │
│                                                                     │
│  Pros:  Full control, exact timing                                  │
│  Cons:  More complex, platform-specific                           │
└─────────────────────────────────────────────────────────────────────┘
```

### Solution 2: Reduce Ticksize

Make the ticksize much smaller than your timer period.

```
┌─────────────────────────────────────────────────────────────────────┐
│  SOLUTION 2: Smaller Ticksize                                       │
│                                                                     │
│  Current:  ticksize = 1ms,  timer = 1.5ms  → erratic              │
│                                                                     │
│  Better:   ticksize = 0.1ms (100μs), timer = 1.5ms               │
│                                                                     │
│  1.5ms / 0.1ms = 15 → exact multiple! No erratic behavior         │
│                                                                     │
│  Set in startup:                                                    │
│    procnto -c 100000    // 100,000 ns = 0.1ms                    │
│                                                                     │
│  ⚠️  Trade-off: More frequent tick interrupts = more CPU overhead  │
│     Every tick causes interrupt handling (high priority)           │
└─────────────────────────────────────────────────────────────────────┘
```

### Solution 3: Exact Multiple of Ticksize

Query the actual ticksize and use an exact multiple.

```
┌─────────────────────────────────────────────────────────────────────┐
│  SOLUTION 3: Query Ticksize & Use Exact Multiple                    │
│                                                                     │
│  struct _clockperiod period;                                        │
│  ClockPeriod(CLOCK_REALTIME, NULL, &period, 0);                   │
│  // period.nsec = actual ticksize (e.g., 998470 ns ≈ 0.998ms)    │
│                                                                     │
│  DON'T assume ticksize = 1ms!                                       │
│  Query it and use exact multiples:                                │
│                                                                     │
│  timer_period = period.nsec * 2;   // exactly 2 ticks             │
│                                                                     │
│  This ensures every wake-up is exactly on a tick boundary           │
│  with NO alternating behavior                                       │
└─────────────────────────────────────────────────────────────────────┘
```

### Solution 4: Use CLOCK_MONOTONIC

Always use `CLOCK_MONOTONIC` so timers are not affected by system time changes.

```
┌─────────────────────────────────────────────────────────────────────┐
│  SOLUTION 4: Use CLOCK_MONOTONIC                                    │
│                                                                     │
│  BAD:  timer_create(CLOCK_REALTIME, ...)                           │
│        ← Timer affected by NTP, ClockAdjust, etc.                  │
│                                                                     │
│  GOOD: timer_create(CLOCK_MONOTONIC, ...)                          │
│        ← Timer runs independently of system time changes             │
│                                                                     │
│  CLOCK_MONOTONIC = nanoseconds since boot (never changes)         │
│  CLOCK_REALTIME  = nanoseconds since 1970 (can jump)               │
└─────────────────────────────────────────────────────────────────────┘
```

### Solution 5: Use High-Resolution Timer

Use `TIMER_TOLERANCE` flag for precise timing without tick alignment.

```
┌─────────────────────────────────────────────────────────────────────┐
│  SOLUTION 5: High-Resolution Timer                                  │
│                                                                     │
│  TimerSettime(timerid, TIMER_TOLERANCE, &tol, &spec, NULL);       │
│                                                                     │
│  • Kernel does NOT align to tick boundary                         │
│  • Reprograms hardware timer for exact expiry time                  │
│  • No alternating behavior                                        │
│                                                                     │
│  ⚠️  Trade-offs:                                                    │
│    • Requires PROCMGR_AID_HIGH_RESOLUTION_TIMER privilege           │
│    • More system overhead (can't coalesce with tick interrupts)    │
│    • Timer interrupts fire more frequently                          │
└─────────────────────────────────────────────────────────────────────┘
```

### Solutions Summary

```
┌─────────────────────┬────────────────────────┬────────────────────────┐
│     Solution        │      How it works      │      Trade-offs        │
├─────────────────────┼────────────────────────┼────────────────────────┤
│ Custom HW Timer     │ Program chip directly  │ Complex, platform-dep  │
│ Smaller Ticksize    │ ticksize << period     │ More CPU overhead      │
│ Exact Multiple      │ period = N × ticksize  │ May need to query first  │
│ CLOCK_MONOTONIC     │ Immune to time changes │ Doesn't fix erratic    │
│ High-Res Timer      │ TIMER_TOLERANCE flag   │ Privileged, overhead     │
└─────────────────────┴────────────────────────┴────────────────────────┘
```

---

## Quick Reference

### Rule 1: Never Use delay() for Periodic Tasks

```
┌─────────────────────────────────────────────────────────────────────┐
│  ❌ WRONG: delay() loop                                             │
│                                                                     │
│     while (1) {                                                     │
│         delay(period);      ← starts from NOW, error accumulates   │
│         do_work();                                                  │
│     }                                                               │
│                                                                     │
│  ✅ RIGHT: Periodic timer                                           │
│                                                                     │
│     timer_create(CLOCK_MONOTONIC, &event, &timerid);               │
│     timer_settime(timerid, 0, &periodic_spec, NULL);               │
│     while (1) {                                                     │
│         wait_for_timer();   ← kernel manages base time             │
│         do_work();                                                  │
│     }                                                               │
└─────────────────────────────────────────────────────────────────────┘
```

### Rule 2: Avoid Non-Multiple Timer Periods

```
┌─────────────────────────────────────────────────────────────────────┐
│  ❌ BAD:  timer period = 1.5ms, ticksize = 1ms                     │
│           → erratic 1ms/2ms alternation                            │
│                                                                     │
│  ✅ GOOD: timer period = 2.0ms, ticksize = 1ms                     │
│           → consistent 2ms every time                              │
│                                                                     │
│  ✅ GOOD: timer period = 1.5ms, ticksize = 0.5ms                 │
│           → 1.5ms / 0.5ms = 3 (exact multiple)                     │
│                                                                     │
│  ✅ GOOD: Use high-resolution timer with TIMER_TOLERANCE           │
│           → no tick alignment, precise timing                        │
└─────────────────────────────────────────────────────────────────────┘
```

### Rule 3: Always Use CLOCK_MONOTONIC

```
┌─────────────────────────────────────────────────────────────────────┐
│  ❌ BAD:  CLOCK_REALTIME                                            │
│           ← Affected by NTP, ClockAdjust, RTC updates               │
│                                                                     │
│  ✅ GOOD: CLOCK_MONOTONIC                                           │
│           ← Steady time since boot, never jumps                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Complete Best-Practice Pattern

```c
/* ============================================================
 * QNX Periodic Timer — Best Practice Pattern
 * No accumulated error, stable timing
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/neutrino.h>

#define TIMER_PULSE_CODE    _PULSE_CODE_MINAVAIL

int main(void) {
    int             chid, coid, rcvid;
    timer_t         timerid;
    struct sigevent event;
    struct itimerspec itimer;
    struct _pulse   pulse;
    struct _clockperiod period;

    /* Step 1: Query actual ticksize */
    ClockPeriod(CLOCK_REALTIME, NULL, &period, 0);
    printf("Ticksize: %lld ns\n", period.nsec);

    /* Step 2: Create channel */
    chid = ChannelCreate(0);
    coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);

    /* Step 3: Set up pulse event */
    SIGEV_PULSE_INIT(&event, coid,
                     SIGEV_PULSE_PRIO_INHERIT,
                     TIMER_PULSE_CODE, 0);

    /* Step 4: Create timer on MONOTONIC clock */
    timer_create(CLOCK_MONOTONIC, &event, &timerid);

    /* Step 5: Configure periodic timer
     * Use exact multiple of ticksize to avoid erratic behavior */
    long period_ns = period.nsec * 5;   /* 5 ticks = ~5ms */

    itimer.it_value.tv_sec     = period_ns / 1000000000LL;
    itimer.it_value.tv_nsec    = period_ns % 1000000000LL;
    itimer.it_interval         = itimer.it_value;  /* same period */

    timer_settime(timerid, 0, &itimer, NULL);

    /* Step 6: Periodic work loop */
    while (1) {
        rcvid = MsgReceive(chid, &pulse, sizeof(pulse), NULL);
        if (rcvid == 0 && pulse.code == TIMER_PULSE_CODE) {
            do_periodic_work();
        }
    }

    return 0;
}
```

---

> **📌 Key Takeaways:**
>
> - **Never** use `delay()` loops for periodic tasks — error accumulates
> - **Always** use kernel-managed periodic timers (`timer_settime` with `it_interval`)
> - **Query** actual ticksize with `ClockPeriod()` — don't assume 1ms
> - **Use** exact multiples of ticksize to avoid erratic alternation
> - **Always** use `CLOCK_MONOTONIC` to avoid system time changes
> - **Consider** high-resolution timers (`TIMER_TOLERANCE`) for precise control
