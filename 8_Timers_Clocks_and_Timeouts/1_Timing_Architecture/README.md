# QNX Neutrino Timing Architecture Guide

> **Companion to:** QNX Hardware Interrupt Handling Guide, QNX Hardware I/O Programming Guide

This document provides a comprehensive introduction to the timing architecture in QNX Neutrino. It covers the two major components of time handling — free-running counters for high-resolution time queries and system timers for scheduled notifications — along with the hardware requirements, API usage, and system page information needed for precise time management.

---

## 1. Overview

Time handling in QNX Neutrino has two major parts:

| Component                 |                       Purpose                        |                 Mechanism                 |
| ------------------------- | :--------------------------------------------------: | :---------------------------------------: |
| **Free-Running Counters** | "What time is it now?" — High-resolution time query  | Hardware counter read via `ClockCycles()` |
| **System Timers**         | "Notify me after time has passed" — Scheduled events |       Per-core hardware timer + IST       |

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    QNX Timing Architecture Overview                         │
│                                                                             │
│   ┌─────────────────────────────┐    ┌─────────────────────────────┐        │
│   │   FREE-RUNNING COUNTERS     │    │      SYSTEM TIMERS          │        │
│   │                             │    │                             │        │
│   │  Purpose: Time query        │    │  Purpose: Notifications     │        │
│   │  Question: "What time is    │    │  Question: "Tell me when    │        │
│   │            it now?"         │    │            time expires"    │        │
│   │                             │    │                             │        │
│   │  API: ClockCycles()         │    │  API: timer_create(),       │        │
│   │                             │    │       timer_settime(),      │        │
│   │  Hardware: CPU timestamp    │    │       nanosleep(),          │        │
│   │            counter          │    │       clock_nanosleep()     │        │
│   │                             │    │                             │        │
│   │  Synchronized across        │    │  Hardware: Per-core timer   │        │
│   │  ALL CPU cores              │    │  device (countdown/countup) │        │
│   │                             │    │                             │        │
│   │  Resolution: Nanosecond     │    │  Default tick: 1 ms         │        │
│   │  order (sub-ns to few ns)   │    │  Resolution: 1 ms default   │        │
│   │                             │    │  (configurable)             │        │
│   └─────────────────────────────┘    └─────────────────────────────┘        │
│                                                                             │
│   Both are initialized from information in the SYSTEM PAGE                  │
│   (provided by startup code, read by procnto at boot)                       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Free-Running Counters (ClockCycles)

### 2.1 What is ClockCycles?

`ClockCycles()` is a hardware-independent function that returns the current value of a **free-running counter** — a hardware register that increments continuously at a fixed rate. It is the foundation of high-resolution time measurement in QNX.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Free-Running Counter Concept                             │
│                                                                             │
│   The counter is a hardware register that increments continuously:          │
│                                                                             │
│   Time ──▶                                                                  │
│                                                                             │
│   Counter:  0 ──▶ 1 ──▶ 2 ──▶ 3 ──▶ ... ──▶ N ──▶ N+1 ──▶ N+2 ──▶ ...       │
│                                                                             │
│   • Never wraps around in practice (64-bit counters)                        │
│   • Monotonically increasing (always goes up, never down)                   │
│   • Synchronized across all CPU cores                                       │
│   • Resolution: nanosecond order (sub-ns to few ns)                         │
│   • Rate: Hardware-dependent (e.g., 2.6 GHz = 2.6 billion ticks/second)     │
│                                                                             │
│   To get elapsed time:                                                      │
│   elapsed_seconds = (end_cycles - start_cycles) / cycles_per_second         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Using ClockCycles()

```c
#include <sys/neutrino.h>
#include <stdint.h>
#include <stdio.h>

/*
 * ClockCycles() returns the current value of the free-running counter.
 * 
 * IMPORTANT: Despite the capital-C naming convention (like kernel calls),
 * ClockCycles() is NOT a kernel call. It is essentially inline assembly
 * that directly reads the hardware counter with minimal overhead.
 */
void measure_elapsed_time(void) {
    uint64_t start, end;
    uint64_t elapsed_cycles;
    double elapsed_ns;
    uint64_t cps;  /* Cycles per second */

    /* Get the counter update rate from the system page */
    cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    /* Record start time */
    start = ClockCycles();

    /* === Do some work === */
    for (volatile int i = 0; i < 1000000; i++) {
        /* busy work */
    }

    /* Record end time */
    end = ClockCycles();

    /* Calculate elapsed time */
    elapsed_cycles = end - start;
    elapsed_ns = (double)elapsed_cycles * 1e9 / (double)cps;

    printf("Elapsed cycles: %llu\n", (unsigned long long)elapsed_cycles);
    printf("Elapsed time:   %.3f nanoseconds\n", elapsed_ns);
    printf("Counter rate:   %llu Hz (%.3f GHz)\n",
           (unsigned long long)cps, (double)cps / 1e9);
}
```

### 2.3 Hardware Implementations

| Platform       | Hardware Counter         | Instruction                    | Notes                           |
| -------------- | ------------------------ | ------------------------------ | ------------------------------- |
| **x86/x86_64** | TSC (Time Stamp Counter) | `RDTSC` / `RDTSCP`             | Read via `ClockCycles()`        |
| **ARM64**      | Generic Timer (CNTVCT)   | `MRS CNTVCT_EL0`               | Architectural, always available |
| **ARM32**      | Generic Timer            | `MRC p15, 0, <Rd>, c9, c13, 0` | Platform-dependent              |

> **Key Requirement:** The counter value must be **synchronized across all CPU cores**. If you read `ClockCycles()` on Core 0, then migrate to Core 1 and read again, the second value must be ≥ the first.

### 2.4 Synchronization Guarantee

```
┌──────────────────────────────────────────────────────────────────────────────┐
│              ClockCycles Synchronization Across Cores                        │
│                                                                              │
│   Core 0                              Core 1                                 │
│   ──────                              ──────                                 │
│                                                                              │
│   cycles = ClockCycles()              (thread migrates to Core 1)            │
│        │                                                                     │
│        │                              cycles = ClockCycles()                 │
│        │                                   │                                 │
│        ▼                                   ▼                                 │
│   Value: 1,000,000                    Value: 1,000,005                       │
│                                                                              │
│   GUARANTEE: Core 1 value ≥ Core 0 value                                     │
│   The counter NEVER goes backward when read across different cores.          │
│                                                                              │
│   This is a HARDWARE REQUIREMENT for QNX-supported platforms.                │
│   The startup code verifies this; procnto relies on it.                      │
│                                                                              │
│   Test scenario:                                                             │
│   1. Read on Core 0 → value A                                                │
│   2. IPI (inter-processor interrupt) to Core 1                               │
│   3. Read on Core 1 → value B                                                │
│   4. IPI back to Core 0                                                      │
│   5. Read on Core 0 → value C                                                │
│                                                                              │
│   Result: A ≤ B ≤ C (monotonically non-decreasing)                           │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.5 Precision Requirements

| Requirement         | Specification                                          |
| ------------------- | ------------------------------------------------------ |
| **Resolution**      | Nanosecond order (sub-nanosecond to a few nanoseconds) |
| **Monotonicity**    | Values never decrease across cores or time             |
| **Synchronization** | Counter kept in sync across all CPU cores by hardware  |
| **Availability**    | Required on all QNX-supported hardware platforms       |

> Counter precision much higher than a few nanoseconds is **not acceptable** for QNX-supported hardware.

---

## 3. System Timers

### 3.1 What are System Timers?

System timers are per-core hardware devices that generate interrupts when a programmed time expires. They are used for:

- Thread sleep (`nanosleep()`, `clock_nanosleep()`)
- POSIX timers (`timer_create()`, `timer_settime()`)
- Kernel scheduling (time slicing, timeouts)
- Periodic callbacks

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                    System Timer Architecture                                 │
│                                                                              │
│   Each CPU core has its own timer hardware:                                  │
│                                                                              │
│   Core 0                              Core 1          Core N                 │
│   ──────                              ──────          ──────                 │
│                                                                              │
│   ┌─────────────┐                    ┌─────────────┐    ┌─────────────┐      │
│   │ HW Timer 0  │                    │ HW Timer 1  │    │ HW Timer N  │      │
│   │ (countdown  │                    │ (countdown  │    │ (countdown  │      │
│   │  or countup)│                    │  or countup)│    │  or countup)│      │
│   └──────┬──────┘                    └──────┬──────┘    └──────┬──────┘      │
│          │                                  │                  │             │
│          │ Expires                          │ Expires          │ Expires     │
│          ▼                                  ▼                  ▼             │
│   ┌─────────────┐                    ┌─────────────┐    ┌─────────────┐      │
│   │  Clock IST  │                    │  Clock IST  │    │  Clock IST  │      │
│   │  (Core 0)   │                    │  (Core 1)   │    │  (Core N)   │      │
│   │  Priority   │                    │  Priority   │    │  Priority   │      │
│   │  253        │                    │  253        │    │  253        │      │
│   └──────┬──────┘                    └──────┬──────┘    └──────┬──────┘      │
│          │                                  │                  │             │
│          └───────────────────┬─────────────────────────────────┘             │
│                              │                                               │
│                              ▼                                               │
│                       Kernel Timer Queue                                     │
│                       (sorted by expiry)                                     │
│                              │                                               │
│                              ▼                                               │
│                    Wake up sleeping threads                                  │
│                    Deliver timer signals                                     │
│                    Reschedule if needed                                      │
│                                                                              │
│   Default system tick: 1 millisecond                                         │
│   (Configurable via ClockPeriod())                                           │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Timer Types

| Timer Type                     | Hardware Mechanism                           | Use Case                         |
| ------------------------------ | -------------------------------------------- | -------------------------------- |
| **Countdown Timer**            | Counts down to zero, fires interrupt         | ARM Generic Timer, many embedded |
| **Countup Timer (Comparator)** | Counts up, fires when reaching compare value | x86 LAPIC timer, some ARM        |
| **One-shot**                   | Single expiry, must be reprogrammed          | POSIX timers, nanosleep          |
| **Periodic**                   | Automatically reloads, fires repeatedly      | Kernel tick, profiling           |

### 3.3 Default System Tick

The default system tick size (timer resolution) in QNX is **1 millisecond**:

```c
#include <sys/neutrino.h>
#include <sys/siginfo.h>
#include <time.h>
#include <stdio.h>

void check_tick_size(void) {
    struct _clockperiod period;

    /* Get current tick size */
    ClockPeriod(CLOCK_REALTIME, NULL, &period, 0);

    printf("Current tick size: %d nanoseconds (%.3f ms)\n",
           period.nsec, period.nsec / 1e6);
    printf("Current tick count: %lld\n", (long long)period.fract);

    /* Default is typically 1 ms = 1,000,000 nanoseconds */
}

/* Change tick size (requires privileges) */
void set_tick_size(long ns) {
    struct _clockperiod period;
    struct _clockperiod old_period;

    period.nsec = ns;
    period.fract = 0;

    if (ClockPeriod(CLOCK_REALTIME, &period, &old_period, 0) == -1) {
        perror("ClockPeriod");
        return;
    }

    printf("Tick size changed from %d ns to %ld ns\n",
           old_period.nsec, ns);
}
```

> **Note:** Smaller tick sizes improve timer resolution but increase interrupt overhead. The default 1 ms is a good balance for most systems.

---

## 4. Hardware Requirements

### 4.1 Free-Running Counter Requirements

```
┌──────────────────────────────────────────────────────────────────────────────┐
│              QNX Hardware Requirements: Free-Running Counter                 │
│                                                                              │
│   REQUIRED:                                                                  │
│   ─────────                                                                  │
│   1. Free-running counter available on ALL CPU cores                         │
│                                                                              │
│   2. Counter values SYNCHRONIZED across all cores                            │
│      • Reading on Core A then Core B: B ≥ A                                  │
│      • No backward movement across core migration                            │
│                                                                              │
│   3. Nanosecond-order precision                                              │
│      • Sub-nanosecond to a few nanoseconds acceptable                        │
│      • Much coarser precision NOT acceptable                                 │
│                                                                              │
│   4. Continuously incrementing (monotonic)                                   │
│      • Never stops, never wraps in practical use                             │
│                                                                              │
│   EXAMPLES:                                                                  │
│   ─────────                                                                  │
│   • x86: RDTSC (Time Stamp Counter) — read via ClockCycles()                 │
│   • ARM64: CNTVCT_EL0 (Virtual Counter) — architectural, always there        │
│                                                                              │
│   VERIFICATION:                                                              │
│   ─────────────                                                              │
│   Startup code checks these properties and reports to system page.           │
│   procnto uses system page info to validate platform support.                │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 System Timer Requirements

| Requirement                     | Description                                         |
| ------------------------------- | --------------------------------------------------- |
| **Per-core**                    | Each CPU core must have its own timer device        |
| **Interrupt-capable**           | Timer must be able to generate interrupts on expiry |
| **Programmable**                | Software must be able to set expiry time            |
| **One-shot and periodic modes** | Support both single-shot and repeating timers       |

---

## 5. Accessing Timing Information

### 5.1 System Page: qtime Entry

The system page contains timing information populated by the startup code. Access it via the `SYSPAGE_ENTRY()` macro.

```c
#include <sys/syspage.h>
#include <stdio.h>
#include <stdint.h>

void print_timing_info(void) {
    struct qtime_entry *qtime;

    /*
     * SYSPAGE_ENTRY(qtime) returns a pointer to the qtime entry
     * in the read-only system page mapping.
     */
    qtime = SYSPAGE_ENTRY(qtime);

    printf("=== QNX Timing Information (from System Page) ===\n\n");

    /* ClockCycles update rate */
    printf("ClockCycles rate:\n");
    printf("  cycles_per_sec: %llu\n", (unsigned long long)qtime->cycles_per_sec);
    printf("  (approximately %.3f GHz)\n\n", (double)qtime->cycles_per_sec / 1e9);

    /* Timer interrupt information */
    printf("Timer interrupt:\n");
    printf("  intr:           %d\n", qtime->intr);
    printf("  (IRQ number for system timer)\n\n");

    /* Clock period (tick size) */
    printf("Clock period:\n");
    printf("  nsec:           %d ns\n", qtime->nsec);
    printf("  nsec_inc:       %d ns\n", qtime->nsec_inc);
    printf("  boot_time:      %lld (seconds since epoch)\n", (long long)qtime->boot_time);

    /* Scale factor for converting cycles to nanoseconds */
    printf("\nConversion factors:\n");
    printf("  ns_per_tick:    %u\n", qtime->ns_per_tick);
    printf("  tick_size:      %u fs (femtoseconds)\n", qtime->tick_size);
}
```

### 5.2 Using pidin to Query Timing Info

```bash
# Display system page timing information
pidin syspage=qtime

# Example output:
# qtime:
#   cycles_per_sec = 2600000000
#   intr           = 0
#   nsec           = 1000000
#   nsec_inc       = 1000000
#   boot_time      = 1704067200
#   ns_per_tick    = 1000000
#   tick_size      = 384615384

# The cycles_per_sec value matches CPU speed:
# 2600000000 = 2.6 GHz
```

### 5.3 System Page qtime Structure

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    struct qtime_entry (System Page)                         │
│                                                                             │
│   Field              │ Type      │ Description                              │
│   ───────────────────┼───────────┼──────────────────────────────────────────│
│   cycles_per_sec     │ uint64_t  │ ClockCycles counter update rate (Hz)     │
│   intr               │ uint32_t  │ Interrupt number for system timer        │
│   nsec               │ uint32_t  │ Current nanosecond offset within tick    │
│   nsec_inc           │ uint32_t  │ Nanoseconds added per tick               │
│   boot_time          │ uint64_t  │ Time of boot (seconds since epoch)       │
│   ns_per_tick        │ uint32_t  │ Nanoseconds per system tick              │
│   tick_size          │ uint32_t  │ Tick size in femtoseconds                │
│   spare              │ uint32_t[]│ Reserved                                 │
│                                                                             │
│   Access: SYSPAGE_ENTRY(qtime)->field_name                                  │
│                                                                             │
│   Example:                                                                  │
│   uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;                      │
│   printf("CPU runs at %.2f GHz\n", cps / 1e9);                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.4 Converting ClockCycles to Real Time

```c
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <stdint.h>
#include <stdio.h>

/* Convert ClockCycles difference to nanoseconds */
uint64_t cycles_to_ns(uint64_t cycles) {
    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    
    /* 
     * To avoid 64-bit overflow with large cycle counts:
     * Use 128-bit intermediate or do division first
     */
    return (cycles * 1000000000ULL) / cps;
}

/* Convert ClockCycles difference to microseconds */
uint64_t cycles_to_us(uint64_t cycles) {
    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    return (cycles * 1000000ULL) / cps;
}

/* Convert ClockCycles difference to milliseconds */
uint64_t cycles_to_ms(uint64_t cycles) {
    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    return (cycles * 1000ULL) / cps;
}

/* High-precision elapsed time measurement */
void precise_timing_example(void) {
    uint64_t start, end;
    uint64_t elapsed_ns;

    start = ClockCycles();
    
    /* === Work to measure === */
    volatile int sum = 0;
    for (int i = 0; i < 100000; i++) {
        sum += i;
    }
    
    end = ClockCycles();

    elapsed_ns = cycles_to_ns(end - start);
    printf("Operation took %llu nanoseconds (%.3f microseconds)\n",
           (unsigned long long)elapsed_ns, elapsed_ns / 1000.0);
}
```

---

## 6. Complete Code Examples

### 6.1 Example 1: Basic ClockCycles Usage

```c
/* clockcycles_basic.c - Demonstrate ClockCycles() for timing */
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    uint64_t cps;
    uint64_t start, end;
    uint64_t elapsed;
    double elapsed_ms;

    /* Get counter rate from system page */
    cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    printf("=== QNX ClockCycles Demo ===\n\n");
    printf("Counter rate: %llu Hz (%.3f GHz)\n\n",
           (unsigned long long)cps, cps / 1e9);

    /* Measure a short operation */
    start = ClockCycles();
    
    /* Some work */
    volatile int x = 0;
    for (int i = 0; i < 10000000; i++) {
        x = x * 2 + i;
    }
    
    end = ClockCycles();

    elapsed = end - start;
    elapsed_ms = (double)elapsed * 1000.0 / (double)cps;

    printf("Start cycles:  %llu\n", (unsigned long long)start);
    printf("End cycles:    %llu\n", (unsigned long long)end);
    printf("Elapsed:       %llu cycles\n", (unsigned long long)elapsed);
    printf("Elapsed time:  %.3f milliseconds\n", elapsed_ms);

    return EXIT_SUCCESS;
}
```

### 6.2 Example 2: High-Resolution Profiling

```c
/* hrtimer_profile.c - Profile function execution time */
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITERATIONS 1000

typedef struct {
    uint64_t min;
    uint64_t max;
    uint64_t total;
    uint64_t samples[ITERATIONS];
} profile_t;

void profile_init(profile_t *p) {
    p->min = UINT64_MAX;
    p->max = 0;
    p->total = 0;
    memset(p->samples, 0, sizeof(p->samples));
}

void profile_record(profile_t *p, int idx, uint64_t cycles) {
    p->samples[idx] = cycles;
    p->total += cycles;
    if (cycles < p->min) p->min = cycles;
    if (cycles > p->max) p->max = cycles;
}

void profile_report(profile_t *p, const char *name) {
    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    double avg_ns = ((double)p->total / ITERATIONS) * 1e9 / cps;
    double min_ns = (double)p->min * 1e9 / cps;
    double max_ns = (double)p->max * 1e9 / cps;

    printf("\n=== Profile: %s ===\n", name);
    printf("  Iterations: %d\n", ITERATIONS);
    printf("  Min:        %llu cycles (%.3f ns)\n",
           (unsigned long long)p->min, min_ns);
    printf("  Max:        %llu cycles (%.3f ns)\n",
           (unsigned long long)p->max, max_ns);
    printf("  Average:    %.1f cycles (%.3f ns)\n",
           (double)p->total / ITERATIONS, avg_ns);
}

/* Function to profile */
void function_to_profile(void) {
    volatile int arr[100];
    for (int i = 0; i < 100; i++) {
        arr[i] = i * i;
    }
}

int main(void) {
    profile_t prof;
    uint64_t start, end;

    profile_init(&prof);

    for (int i = 0; i < ITERATIONS; i++) {
        start = ClockCycles();
        function_to_profile();
        end = ClockCycles();
        profile_record(&prof, i, end - start);
    }

    profile_report(&prof, "function_to_profile()");

    return EXIT_SUCCESS;
}
```

### 6.3 Example 3: System Page Timing Query

```c
/* syspage_timing.c - Query all timing info from system page */
#include <sys/syspage.h>
#include <sys/neutrino.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    struct qtime_entry *qtime = SYSPAGE_ENTRY(qtime);

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║         QNX System Page Timing Information                   ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("┌─ Free-Running Counter ──────────────────────────────────────┐\n");
    printf("│ cycles_per_sec:  %15llu Hz\n", (unsigned long long)qtime->cycles_per_sec);
    printf("│                 ≈ %10.3f GHz\n", qtime->cycles_per_sec / 1e9);
    printf("│ Resolution:       ~%.3f nanoseconds\n",
           1e9 / qtime->cycles_per_sec);
    printf("└────────────────────────────────────────────────────────────┘\n\n");

    printf("┌─ System Timer ─────────────────────────────────────────────┐\n");
    printf("│ Timer IRQ:        %d\n", qtime->intr);
    printf("│ Tick size:        %d nanoseconds (%.3f ms)\n",
           qtime->nsec, qtime->nsec / 1e6);
    printf("│ Tick increment:  %d nanoseconds\n", qtime->nsec_inc);
    printf("│ ns_per_tick:      %u\n", qtime->ns_per_tick);
    printf("└────────────────────────────────────────────────────────────┘\n\n");

    printf("┌─ Boot Information ─────────────────────────────────────────┐\n");
    printf("│ Boot time:        %lld (seconds since epoch)\n",
           (long long)qtime->boot_time);
    printf("└────────────────────────────────────────────────────────────┘\n\n");

    /* Demonstrate ClockCycles */
    uint64_t c1 = ClockCycles();
    uint64_t c2 = ClockCycles();
    printf("ClockCycles() demo:\n");
    printf("  Sample 1: %llu\n", (unsigned long long)c1);
    printf("  Sample 2: %llu\n", (unsigned long long)c2);
    printf("  Delta:    %llu cycles\n", (unsigned long long)(c2 - c1));
    printf("  Time:     %.3f nanoseconds\n",
           (double)(c2 - c1) * 1e9 / qtime->cycles_per_sec);

    return EXIT_SUCCESS;
}
```

### 6.4 Example 4: Timer with ClockCycles Precision

```c
/* timer_precision.c - Compare ClockCycles vs system timer precision */
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_DURATION_MS 100

int main(void) {
    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    uint64_t start_cycles, end_cycles;
    struct timespec start_ts, end_ts;
    uint64_t expected_cycles;
    int64_t error_ns;

    printf("=== Timer Precision Comparison ===\n\n");
    printf("Test: Sleep for %d ms\n\n", TEST_DURATION_MS);

    /* Calculate expected cycles for the sleep duration */
    expected_cycles = (uint64_t)TEST_DURATION_MS * cps / 1000;

    /* Method 1: Use ClockCycles to measure nanosleep accuracy */
    start_cycles = ClockCycles();
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    struct timespec sleep_time = {
        .tv_sec = 0,
        .tv_nsec = TEST_DURATION_MS * 1000000
    };
    nanosleep(&sleep_time, NULL);

    end_cycles = ClockCycles();
    clock_gettime(CLOCK_MONOTONIC, &end_ts);

    /* Calculate actual elapsed time */
    uint64_t actual_cycles = end_cycles - start_cycles;
    double actual_ms = (double)actual_cycles * 1000.0 / cps;

    int64_t elapsed_ns = (end_ts.tv_sec - start_ts.tv_sec) * 1000000000LL
                        + (end_ts.tv_nsec - start_ts.tv_nsec);
    double elapsed_ms = elapsed_ns / 1e6;

    printf("Results:\n");
    printf("  Expected:       %d ms\n", TEST_DURATION_MS);
    printf("  ClockCycles:    %.3f ms (%.3f%% error)\n",
           actual_ms, (actual_ms - TEST_DURATION_MS) / TEST_DURATION_MS * 100);
    printf("  clock_gettime:  %.3f ms (%.3f%% error)\n",
           elapsed_ms, (elapsed_ms - TEST_DURATION_MS) / TEST_DURATION_MS * 100);
    printf("\n");
    printf("  Counter precision allows measuring sub-microsecond intervals.\n");
    printf("  System timer (1ms tick) has ~1ms resolution for scheduling.\n");

    return EXIT_SUCCESS;
}
```

---

## 7. Quick Reference

### 7.1 Timing API Functions

| Function               | Header             | Purpose                                            |
| ---------------------- | ------------------ | -------------------------------------------------- |
| `ClockCycles()`        | `<sys/neutrino.h>` | Read free-running hardware counter                 |
| `ClockPeriod()`        | `<sys/neutrino.h>` | Get/set system tick size                           |
| `clock_gettime()`      | `<time.h>`         | Get current time (CLOCK_REALTIME, CLOCK_MONOTONIC) |
| `clock_settime()`      | `<time.h>`         | Set current time                                   |
| `nanosleep()`          | `<time.h>`         | Sleep for specified nanoseconds                    |
| `clock_nanosleep()`    | `<time.h>`         | Sleep on specific clock                            |
| `timer_create()`       | `<time.h>`         | Create POSIX timer                                 |
| `timer_settime()`      | `<time.h>`         | Arm POSIX timer                                    |
| `SYSPAGE_ENTRY(qtime)` | `<sys/syspage.h>`  | Access timing info in system page                  |

### 7.2 System Page Timing Fields

| Field            | Type       | Description                       |
| ---------------- | ---------- | --------------------------------- |
| `cycles_per_sec` | `uint64_t` | ClockCycles counter rate in Hz    |
| `intr`           | `uint32_t` | System timer interrupt number     |
| `nsec`           | `uint32_t` | Current nanosecond offset in tick |
| `nsec_inc`       | `uint32_t` | Nanoseconds added per tick        |
| `boot_time`      | `uint64_t` | Boot time (seconds since epoch)   |
| `ns_per_tick`    | `uint32_t` | Nanoseconds per system tick       |
| `tick_size`      | `uint32_t` | Tick size in femtoseconds         |

### 7.3 Clock Types

| Clock ID              | Description                                    |
| --------------------- | ---------------------------------------------- |
| `CLOCK_REALTIME`      | Wall-clock time (can be adjusted)              |
| `CLOCK_MONOTONIC`     | Monotonic time since boot (cannot go backward) |
| `CLOCK_SOFTTIME`      | Soft timer (affected by tick size)             |
| `CLOCK_MONOTONIC_RAW` | Hardware monotonic time (not adjusted)         |

### 7.4 Common Pitfalls

| Pitfall                            | Solution                                            |
| ---------------------------------- | --------------------------------------------------- |
| ClockCycles() overflow             | Use 64-bit arithmetic; counter is 64-bit            |
| Integer overflow in conversion     | Divide before multiply, or use 128-bit intermediate |
| Assuming constant CPU frequency    | Some CPUs vary frequency; check platform            |
| Ignoring counter synchronization   | QNX guarantees this; don't add software sync        |
| Confusing tick size with precision | Tick size (1ms) ≠ ClockCycles precision (ns)        |

### 7.5 Key Formulas

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                    Timing Conversion Formulas                                │
│                                                                              │
│   Given:                                                                     │
│   • cycles = ClockCycles() value                                             │
│   • cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec                               │
│                                                                              │
│   Cycles → Nanoseconds:                                                      │
│   ns = (cycles * 1,000,000,000) / cps                                        │
│                                                                              │
│   Cycles → Microseconds:                                                     │
│   us = (cycles * 1,000,000) / cps                                            │
│                                                                              │
│   Cycles → Milliseconds:                                                     │
│   ms = (cycles * 1,000) / cps                                                │
│                                                                              │
│   Time → Cycles:                                                             │
│   cycles = (time_in_seconds * cps)                                           │
│                                                                              │
│   CPU Frequency from cps:                                                    │
│   GHz = cps / 1,000,000,000                                                  │
│                                                                              │
│   Counter Resolution:                                                        │
│   resolution_ns = 1,000,000,000 / cps                                        │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## Related Documentation

- **QNX Hardware Interrupt Handling Guide** — System timer IST and interrupt architecture
- **QNX Hardware I/O Programming Guide** — System page access, memory mapping
- **QNX Official:** [ClockCycles()](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/c/clockcycles.html)
- **QNX Official:** [ClockPeriod()](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/c/clockperiod.html)
- **QNX Official:** [Understanding the Microkernel](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.sys_arch/topic/timers.html)
- **QNX Official:** [System Page](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.sys_arch/topic/syspage.html)

