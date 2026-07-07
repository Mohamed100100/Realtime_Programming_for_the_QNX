# QNX System Clock Guide

---

## Overview

QNX internally handles time as **64-bit nanosecond values**. These values are exposed through two primary clocks that applications can access for different timing purposes.

---

## Clock Types

### CLOCK_MONOTONIC

- Represents the number of nanoseconds since system boot
- Derived directly from `ClockCycles()`, which uses the hardware CPU clock counter
- **Cannot be set** — it is strictly a measure of elapsed time since boot
- Ideal for measuring durations and intervals (unaffected by system time changes)

### CLOCK_REALTIME

- Represents the number of nanoseconds since **January 1st, 1970** (Unix epoch)
- Calculated as: `CLOCK_MONOTONIC + stored boot time`
- Also called the **system time** or **system clock**
- Essential for all POSIX functions that assume time since 1970
- Can be read and set by privileged processes

> **Boot Time Behavior:** The startup process may supply a current time to the kernel. If not, the boot time defaults to 0 seconds since the epoch, meaning the system will report the time as **January 1st, 1970** until explicitly set.

---

## Time Data Structures

### `time_t`

- POSIX standard: number of **seconds** since January 1st, 1970
- In QNX, defined as an **`int64`** type

### `struct timespec`

- POSIX-compliant structure with higher resolution
- Contains:
  - `time_t tv_sec` — seconds since epoch
  - `long tv_nsec` — nanoseconds since the last second

```c
struct timespec {
    time_t  tv_sec;   // seconds
    long    tv_nsec;  // nanoseconds (0 to 999,999,999)
};
```

---

## Reading and Setting the System Clock

### Reading the System Time

Use `clock_gettime()` with `CLOCK_REALTIME`:

```c
#include <time.h>

struct timespec ts;
clock_gettime(CLOCK_REALTIME, &ts);
// ts.tv_sec  = seconds since Jan 1, 1970
// ts.tv_nsec = nanoseconds since last second
```

### Setting the System Time

Use `clock_settime()` with a modified `struct timespec`:

```c
#include <time.h>

struct timespec ts;
clock_gettime(CLOCK_REALTIME, &ts);

// Add one day (24 hours × 60 minutes × 60 seconds)
ts.tv_sec += (long)(24 * 60 * 60);  // Use long to prevent overflow

clock_settime(CLOCK_REALTIME, &ts);
```

> **Note:** Use `long` for arithmetic to ensure the compiler doesn't misinterpret or overflow the calculation.

### Reading Other Clocks

You can read `CLOCK_MONOTONIC` using `clock_gettime()`, but **you cannot set it**:

```c
struct timespec mono;
clock_gettime(CLOCK_MONOTONIC, &mono);
```

---

## Gradual Time Adjustments

When synchronizing time (e.g., via NTP, PTP, or GPS), you often want to avoid sudden jumps that could cause processes to see time skip, jitter, or go backward. Use **`ClockAdjust()`** to apply changes gradually.

### How ClockAdjust Works

- Applies a time increment over a specified period
- **Tick count** indicates the duration in milliseconds (default system tick = 1ms)
- Distributes the adjustment smoothly across the specified period

### Parameters

| Field           | Description                                        |
| --------------- | -------------------------------------------------- |
| `tick_nsec_inc` | Nanoseconds to add (or subtract) per tick          |
| `tick_count`    | Number of ticks (milliseconds) over which to apply |

### Example: Smooth 1-Second Adjustment Over 100 Seconds

```c
#include <sys/neutrino.h>

struct _clockadjust adjust;
adjust.tick_nsec_inc = 10000;    // 10,000 ns = 10 microseconds per tick
adjust.tick_count    = 100000;   // 100,000 ticks = 100,000 ms = 100 seconds

// Total change: 10,000 ns × 100,000 = 1,000,000,000 ns = 1 second
// Applied over: 100 seconds

ClockAdjust(CLOCK_REALTIME, &adjust, NULL);
```

### Going Backward in Time

Simply use a **negative increment**:

```c
adjust.tick_nsec_inc = -10000;   // Subtract 10 microseconds per tick
adjust.tick_count    = 100000;   // Over 100 seconds
ClockAdjust(CLOCK_REALTIME, &adjust, NULL);
```

---

## QNX-Specific Time Functions

QNX provides convenience functions for converting between 64-bit nanoseconds and `struct timespec`:

### `timespec2nsec()`

Converts a `struct timespec` to a 64-bit nanosecond value:

```c
#include <time.h>

struct timespec ts;
// ... populate ts ...
uint64_t nsec = timespec2nsec(&ts);
```

### `nsec2timespec()`

Converts a 64-bit nanosecond value to a `struct timespec`:

```c
#include <time.h>

uint64_t nsec = 1234567890123ULL;
struct timespec ts;
nsec2timespec(&ts, nsec);
// ts.tv_sec  = 1234
// ts.tv_nsec = 567890123
```

### `clock_gettime_ns()`

Gets the time directly as a 64-bit nanosecond value:

```c
#include <time.h>

uint64_t nsec = clock_gettime_ns(CLOCK_REALTIME);   // or CLOCK_MONOTONIC
```

### Convenience Functions (No Arguments)

For quick access without passing clock IDs:

```c
#include <time.h>

uint64_t mono_nsec  = clock_gettime_monotonic_ns();  // CLOCK_MONOTONIC
uint64_t real_nsec  = clock_gettime_realtime_ns();   // CLOCK_REALTIME
```

---

## Required Abilities

To read, set, or adjust the system clock, your process must have the **`PROCMGR_AID_CLOCKSET`** ability.

| Operation         | Required Ability       |
| ----------------- | ---------------------- |
| `clock_gettime()` | None (always allowed)  |
| `clock_settime()` | `PROCMGR_AID_CLOCKSET` |
| `ClockAdjust()`   | `PROCMGR_AID_CLOCKSET` |

Without this ability, these functions will fail.

---

## Code Examples

### Example 1: Basic Time Read and Set

```c
#include <stdio.h>
#include <time.h>
#include <stdint.h>

int main() {
    struct timespec ts;
    
    // Read current system time
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        return 1;
    }
    
    printf("Current time: %ld seconds, %ld nanoseconds\n", 
           (long)ts.tv_sec, ts.tv_nsec);
    
    // Add one day
    ts.tv_sec += (long)(24 * 60 * 60);
    
    // Set the new time
    if (clock_settime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_settime");
        return 1;
    }
    
    printf("Time set to one day ahead\n");
    return 0;
}
```

### Example 2: Gradual Time Adjustment

```c
#include <stdio.h>
#include <sys/neutrino.h>

int main() {
    struct _clockadjust adjust;
    
    // Adjust forward by 1 second over 100 seconds
    adjust.tick_nsec_inc = 10000;   // +10 microseconds per tick
    adjust.tick_count    = 100000;  // 100,000 ticks (100 seconds)
    
    if (ClockAdjust(CLOCK_REALTIME, &adjust, NULL) == -1) {
        perror("ClockAdjust");
        return 1;
    }
    
    printf("Gradual adjustment started: +1 second over 100 seconds\n");
    return 0;
}
```

### Example 3: Using Nanosecond Convenience Functions

```c
#include <stdio.h>
#include <time.h>
#include <stdint.h>

int main() {
    // Get time as 64-bit nanoseconds
    uint64_t real_ns = clock_gettime_realtime_ns();
    uint64_t mono_ns = clock_gettime_monotonic_ns();
    
    printf("Realtime: %llu ns since epoch\n", (unsigned long long)real_ns);
    printf("Monotonic: %llu ns since boot\n", (unsigned long long)mono_ns);
    
    // Convert to timespec
    struct timespec ts;
    nsec2timespec(&ts, real_ns);
    printf("As timespec: %ld sec, %ld nsec\n", (long)ts.tv_sec, ts.tv_nsec);
    
    return 0;
}
```

---

## Time Synchronization Sources

After boot, the system time can be set or synchronized using:

| Source                    | Method                                          |
| ------------------------- | ----------------------------------------------- |
| **RTC (Real-Time Clock)** | Battery-backed hardware clock via `rtc` utility |
| **NTP**                   | Network Time Protocol                           |
| **PTP**                   | Precision Time Protocol (IEEE 1588)             |
| **GPS**                   | GPS driver receiving time from satellites       |

---

## Summary

| Task                     | Function                              | Notes                           |
| ------------------------ | ------------------------------------- | ------------------------------- |
| Read system time         | `clock_gettime(CLOCK_REALTIME, &ts)`  | POSIX standard                  |
| Set system time          | `clock_settime(CLOCK_REALTIME, &ts)`  | Requires `PROCMGR_AID_CLOCKSET` |
| Read monotonic time      | `clock_gettime(CLOCK_MONOTONIC, &ts)` | Cannot be set                   |
| Gradual adjustment       | `ClockAdjust()`                       | Smooth time sync without jumps  |
| Convert to nanoseconds   | `timespec2nsec()`                     | QNX-specific                    |
| Convert from nanoseconds | `nsec2timespec()`                     | QNX-specific                    |
| Get nanoseconds directly | `clock_gettime_ns()`                  | QNX-specific                    |
| Quick monotonic ns       | `clock_gettime_monotonic_ns()`        | No arguments                    |
| Quick realtime ns        | `clock_gettime_realtime_ns()`         | No arguments                    |

---

*This guide covers the core QNX system clock APIs. For advanced usage (e.g., high-resolution timers, timer interrupts), refer to the QNX Neutrino documentation.*
