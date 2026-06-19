
# Atomic Operations in QNX

## Overview

QNX provides atomic operations as a lightweight alternative to mutexes for protecting small, simple updates to shared variables. This module covers the QNX atomic API, the race condition problem they solve, and how they compare to mutexes and C11 atomics.

Atomic operations guarantee that a read-modify-write sequence on a 32-bit value completes **indivisibly** — no preemption, no interruption, no overlap between threads. This makes them ideal for counters, flags, and bit manipulations where mutex overhead would be excessive.

---

## The Problem: Non-Atomic Updates

Consider a shared counter updated by multiple threads without synchronization:

```c
static int var3 = 0;

void do_work() {
    var3++;                          /* NOT atomic */
    if (var3 % 10000000 == 0) {      /* NOT atomic */
        printf("Hit milestone: %d\n", var3);
    }
}
```

### The Race Condition

Two threads can interleave their operations on `var3`:

| Time | Thread A | Thread B | var3 (memory) |
|------|----------|----------|---------------|
| T1 | Read var3 = 9,999,999 | | 9,999,999 |
| T2 | | Read var3 = 9,999,999 | 9,999,999 |
| T3 | Increment to 10,000,000 | | 10,000,000 |
| T4 | | Increment to 10,000,000 | 10,000,000 |
| T5 | Write var3 = 10,000,000 | | 10,000,000 |
| T6 | | Write var3 = 10,000,000 | 10,000,000 |
| T7 | Check: 10,000,000 % 10M == 0 → print | | |
| T8 | | Check: 10,000,000 % 10M == 0 → print | |

Both threads read the same value, both increment, one write is lost. The milestone is hit twice, and future milestones may be skipped entirely. The counter is corrupted.

### Why This Happens

`var3++` compiles to three separate machine instructions:

1. **Load** `var3` from memory into a register
2. **Increment** the register value
3. **Store** the register back to memory

A thread switch can occur between any of these steps. Two threads loading the same value before either stores means one update is lost.

---

## Solution 1: Mutex (Heavyweight)

```c
static pthread_mutex_t var3_mutex = PTHREAD_MUTEX_INITIALIZER;

void do_work() {
    pthread_mutex_lock(&var3_mutex);
    var3++;
    if (var3 % 10000000 == 0) {
        printf("Hit milestone: %d\n", var3);
    }
    pthread_mutex_unlock(&var3_mutex);
}
```

### Cost of Mutex

| Operation | Cost |
|-----------|------|
| Lock | Atomic compare-and-exchange (best case) or kernel call (contention) |
| Unlock | Atomic operation |
| Contention | Thread blocks, context switch, kernel scheduling |

For a simple increment, this is **overkill**. The mutex protects the operation, but the overhead dominates the work.

---

## Solution 2: Atomic Operation (Lightweight)

```c
#include <atomic.h>

static volatile int var3 = 0;

void do_work() {
    int previous = atomic_add_value(&var3, 1);   /* Atomically: var3 += 1 */
    if (previous % 10000000 == 0) {
        printf("Hit milestone: %d\n", previous + 1);
    }
}
```

### How It Works

`atomic_add_value(&var3, 1)` performs the entire read-increment-write as a **single, indivisible CPU instruction**. No thread can interrupt or interleave in the middle. The function returns the **original value** before the addition.

| Time | Thread A | Thread B | var3 (memory) | Return A | Return B |
|------|----------|----------|---------------|----------|----------|
| T1 | atomic_add_value(&var3, 1) | | 9,999,999 → 10,000,000 | 9,999,999 | |
| T2 | | atomic_add_value(&var3, 1) | 10,000,000 → 10,000,001 | | 10,000,000 |
| T3 | Check: 9,999,999 % 10M != 0 → no print | | | | |
| T4 | | Check: 10,000,000 % 10M == 0 → print | | | |

Every increment is preserved. Milestones are hit exactly once every 10 million operations.

---

## QNX Atomic API

QNX provides atomic operations for 32-bit values. All operations are guaranteed atomic across all CPU cores and threads.

### Core Operations

| Function | Operation | Returns |
|----------|-----------|---------|
| `atomic_add_value(&var, n)` | `var += n` | **Original** value of `var` before addition |
| `atomic_sub_value(&var, n)` | `var -= n` | **Original** value of `var` before subtraction |
| `atomic_set_value(&var, mask)` | `var \|= mask` (bitwise OR) | **Original** value of `var` |
| `atomic_clr_value(&var, mask)` | `var &= ~mask` (clear bits) | **Original** value of `var` |
| `atomic_toggle_value(&var, mask)` | `var ^= mask` (bitwise XOR) | **Original** value of `var` |

### Non-Returning Variants

Each operation also has a variant that does not return the original value:

| Function | Operation | Returns |
|----------|-----------|---------|
| `atomic_add(&var, n)` | `var += n` | `void` |
| `atomic_sub(&var, n)` | `var -= n` | `void` |
| `atomic_set(&var, mask)` | `var \|= mask` | `void` |
| `atomic_clr(&var, mask)` | `var &= ~mask` | `void` |
| `atomic_toggle(&var, mask)` | `var ^= mask` | `void` |

Use the `_value` variants when you need the pre-operation value for conditional logic (e.g., checking milestones). Use the plain variants when you only need the side effect.

### Header

```c
#include <atomic.h>    /* QNX-specific atomic operations */
```

### Variable Declaration

```c
static volatile int counter = 0;   /* volatile prevents compiler optimization */
```

The `volatile` keyword tells the compiler that the variable may change outside the current thread's control, preventing it from caching the value in a register across the atomic operation.

---

## Usage Examples

### Example 1: Safe Counter with Milestone Check

```c
#include <atomic.h>
#include <stdio.h>

static volatile int var3 = 0;

void do_work() {
    int prev = atomic_add_value(&var3, 1);
    
    /* prev holds the value BEFORE the increment */
    if (prev % 10000000 == 9999999) {
        printf("Milestone reached: %d\n", prev + 1);
    }
}
```

### Example 2: Bit Flags with Atomic Set/Clear

```c
#include <atomic.h>

#define FLAG_READY   0x01
#define FLAG_ERROR   0x02
#define FLAG_BUSY    0x04

static volatile int status = 0;

void set_ready() {
    atomic_set(&status, FLAG_READY);    /* Atomically: status |= FLAG_READY */
}

void clear_error() {
    atomic_clr(&status, FLAG_ERROR);    /* Atomically: status &= ~FLAG_ERROR */
}

void toggle_busy() {
    atomic_toggle(&status, FLAG_BUSY);  /* Atomically: status ^= FLAG_BUSY */
}

int get_status() {
    return status;   /* Read is atomic for 32-bit aligned int */
}
```

### Example 3: Reference Counting

```c
#include <atomic.h>
#include <stdlib.h>

typedef struct {
    volatile int ref_count;
    /* ... other fields ... */
} object_t;

object_t *object_create() {
    object_t *obj = malloc(sizeof(object_t));
    obj->ref_count = 1;
    return obj;
}

void object_ref(object_t *obj) {
    atomic_add(&obj->ref_count, 1);   /* Increment reference count */
}

void object_unref(object_t *obj) {
    if (atomic_sub_value(&obj->ref_count, 1) == 1) {
        /* Was 1, now 0 — last reference dropped */
        free(obj);
    }
}
```

---

## Comparison: Atomic Operations vs. Mutexes

| Criterion | Atomic Operations | Mutexes |
|-----------|-------------------|---------|
| **Scope** | Single 32-bit variable | Any shared resource, any size |
| **Operations** | Add, sub, set, clr, toggle | Arbitrary critical sections |
| **Overhead** | Single CPU instruction (nanoseconds) | Lock + unlock + possible kernel call (microseconds) |
| **Contention** | No blocking — threads proceed immediately | Blocking, context switches, priority inversion |
| **Complexity** | Simple, no deadlock risk | Requires careful lock ordering |
| **Use case** | Counters, flags, simple state variables | Complex data structures, multiple variables, long operations |

### Decision Rule

| Situation | Use |
|-----------|-----|
| Single integer counter, flag, or bit field | **Atomic operation** |
| Multiple related variables that must update together | **Mutex** |
| Complex logic with conditionals and loops | **Mutex** |
| Simple increment/decrement with no dependencies | **Atomic operation** |

---

## C11 Standard Atomics (Alternative)

QNX also supports the C11 standard atomic API (`stdatomic.h`). These are more portable but more verbose.

| QNX Function | C11 Equivalent |
|--------------|----------------|
| `atomic_add_value(&var, n)` | `atomic_fetch_add(&var, n)` |
| `atomic_sub_value(&var, n)` | `atomic_fetch_sub(&var, n)` |
| `atomic_set_value(&var, mask)` | `atomic_fetch_or(&var, mask)` |
| `atomic_clr_value(&var, mask)` | `atomic_fetch_and(&var, ~mask)` |
| `atomic_toggle_value(&var, mask)` | `atomic_fetch_xor(&var, mask)` |

### C11 Example

```c
#include <stdatomic.h>
#include <stdio.h>

static _Atomic int var3 = 0;

void do_work() {
    int prev = atomic_fetch_add(&var3, 1);
    if (prev % 10000000 == 9999999) {
        printf("Milestone: %d\n", prev + 1);
    }
}
```

### Why Prefer QNX Atomics

| Aspect | QNX Atomics | C11 Atomics |
|--------|-------------|-------------|
| **Simplicity** | Direct, intuitive names | Verbose, requires `_Atomic` type qualifier |
| **Verbosity** | Minimal | More boilerplate |
| **Portability** | QNX-specific | ISO standard — portable across C11 compilers |
| **Recommendation** | **Default for QNX development** | Use when cross-platform code is required |

For simple, frequent updates in QNX applications, the QNX atomic API is the pragmatic choice.

---

## Common Pitfalls

| Pitfall | Problem | Solution |
|---------|---------|----------|
| **Forgetting `volatile`** | Compiler caches variable in register, atomic operation sees stale value | Declare atomic variables as `volatile` |
| **Using atomics for compound operations** | `atomic_add_value` then separate read is not atomic as a pair | Use the returned value; do not re-read the variable |
| **64-bit variables** | QNX atomic API is for 32-bit values only | Use mutexes or C11 `_Atomic int64_t` |
| **Multiple variable updates** | Atomics protect one variable, not relationships between variables | Use a mutex if consistency across variables is required |
| **Assuming atomicity for non-atomic types** | `long long`, structs, arrays are not atomic | Use mutexes or C11 atomics with explicit types |

---

## Summary

| Concept | Key Point |
|---------|-----------|
| **Atomic guarantee** | Read-modify-write completes indivisibly — no preemption, no interleaving |
| **Primary API** | QNX `atomic.h` — `atomic_add_value`, `atomic_sub_value`, `atomic_set_value`, `atomic_clr_value`, `atomic_toggle_value` |
| **Return value** | `_value` variants return the **original** value before the operation |
| **vs. Mutex** | Atomics are faster and simpler for single-variable updates; mutexes are required for complex critical sections |
| **vs. C11** | QNX atomics are simpler and preferred for QNX-only code; C11 atomics for portability |
| **Declaration** | Always use `volatile` for atomic variables |

---


*This module covers atomic operations as a lightweight synchronization primitive in QNX, contrasting them with mutexes and C11 standard atomics.*
