

# POSIX Condition Variables (condvars) in QNX

## Overview

QNX provides POSIX condition variables (`pthread_cond_t`) as the standard mechanism for thread notification and blocking synchronization. This module covers the condvar API, the producer/consumer pattern, and a four-state machine exercise that demonstrates why `pthread_cond_broadcast()` is essential when multiple threads wait on different conditions.

Condition variables solve the problem of **blocking until shared state changes**, then **notifying** waiting threads when that change occurs. They are always used with a mutex to protect the shared state being checked.

---

## The Condvar API

QNX implements the POSIX standard condition variable interface. These functions are portable across Linux, BSD, and other POSIX-compliant systems.

| Function | Purpose |
|----------|---------|
| `pthread_cond_init()` | Initialize a condition variable (dynamic) |
| `pthread_cond_destroy()` | Destroy a condition variable |
| `pthread_cond_wait(&cond, &mutex)` | Atomically unlock mutex and wait for notification; re-lock mutex upon return |
| `pthread_cond_signal(&cond)` | Wake up **one** waiting thread (highest priority, longest waiting) |
| `pthread_cond_broadcast(&cond)` | Wake up **all** waiting threads |

### Static Initialization

For global condition variables, use the static initializer:

```c
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
```

### The `pthread_cond_wait()` Contract

```c
pthread_mutex_lock(&mutex);
while (condition_not_met) {
    pthread_cond_wait(&cond, &mutex);  // Atomically: unlock → wait → re-lock
}
// ... critical section ...
pthread_mutex_unlock(&mutex);
```

When `pthread_cond_wait()` is called:

1. **Unlocks** `mutex` atomically (as a single kernel operation)
2. **Blocks** the thread — it gives up the CPU and waits in the kernel
3. When notified, **re-locks** `mutex` before returning

The function always returns with the mutex locked. This is why the caller must lock the mutex **before** calling `pthread_cond_wait()`.

### Why `pthread_cond_wait()` Unlocks the Mutex

The mutex must be unlocked during the wait so that **other threads can modify the shared state**. Consider a producer/consumer example:

- The consumer locks the mutex, checks `data_ready`, sees it is `0`
- The consumer calls `pthread_cond_wait()` — the mutex is unlocked
- The producer can now lock the mutex, set `data_ready = 1`, and signal
- If the mutex remained locked, the producer would deadlock trying to acquire it

---

## Signal vs. Broadcast

| Function | Behavior | Use When |
|----------|----------|----------|
| `pthread_cond_signal()` | Wakes **one** waiter | Only one thread is waiting, or all waiters are identical and any one can proceed |
| `pthread_cond_broadcast()` | Wakes **all** waiters | Multiple threads wait on different conditions, or you need all to re-check state |

### Lost Signals

If `pthread_cond_signal()` or `pthread_cond_broadcast()` is called when **no thread is waiting**, the notification is **lost** — it is a no-op. This is why a boolean flag (like `data_ready`) is used alongside the condvar:

```c
// Consumer
pthread_mutex_lock(&mutex);
while (!data_ready) {           // Check state BEFORE waiting
    pthread_cond_wait(&cond, &mutex);
}
// Process data ...
data_ready = 0;                 // Reset flag for next cycle
pthread_mutex_unlock(&mutex);
```

If the producer signals while the consumer is not yet waiting (e.g., the consumer is busy writing to hardware), the signal is lost. But because `data_ready` was set to `1`, the consumer will see it on the next loop iteration and skip the wait entirely.

### Broadcast and the Mutex

When `pthread_cond_broadcast()` wakes multiple threads, they all become runnable and compete for the mutex. Only one thread locks the mutex at a time; the others block on the mutex until it is released. They return from `pthread_cond_wait()` one by one, each with the mutex locked.

---

## The Original Exercise: Two-State Machine

The starting point is a producer/consumer pattern with two threads and two states.

### Behavior

```
State: 0 → 1 → 0 → 1 → 0 → 1 → ... (back and forth)
```

### Threads

| Thread | Waits For | Action | Notifies |
|--------|-----------|--------|----------|
| `state_0()` | `state == 0` | Sets `state = 1` | `pthread_cond_signal()` |
| `state_1()` | `state == 1` | Sets `state = 0` | `pthread_cond_signal()` |

### Original Code

```c
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
static int state = 0;

void *state_0(void *arg) {
    while (1) {
        pthread_mutex_lock(&mutex);
        while (state != 0) {
            pthread_cond_wait(&cond, &mutex);
        }
        state = 1;
        printf("State: 0 -> 1\n");
        pthread_cond_signal(&cond);   // Notify state_1()
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

void *state_1(void *arg) {
    while (1) {
        pthread_mutex_lock(&mutex);
        while (state != 1) {
            pthread_cond_wait(&cond, &mutex);
        }
        state = 0;
        printf("State: 1 -> 0\n");
        pthread_cond_signal(&cond);   // Notify state_0()
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main() {
    pthread_t t0, t1;
    pthread_create(&t0, NULL, state_0, NULL);
    pthread_create(&t1, NULL, state_1, NULL);
    pthread_join(t0, NULL);
    pthread_join(t1, NULL);
    return 0;
}
```

### Why `pthread_cond_signal()` Works Here

Only one thread is ever waiting at a time:
- When `state == 0`, only `state_0()` can proceed; `state_1()` is waiting
- When `state == 1`, only `state_1()` can proceed; `state_0()` is waiting

There is never ambiguity about which thread to wake up. Signal is sufficient.

---

## The Exercise: Four-State Machine

Extend the two-state machine to four states with four threads. The state transitions alternate between two paths through states 2 and 3.

### Target Behavior

```
State: 0 → 1 → 2 → 0 → 1 → 3 → 0 → 1 → 2 → 0 → 1 → 3 → ...
```

### Thread Responsibilities

| Thread | Waits For | Action | Next State | Notifies |
|--------|-----------|--------|------------|----------|
| `state_0()` | `state == 0` | Sets `state = 1` | `1` | `pthread_cond_broadcast()` |
| `state_1()` | `state == 1` | Sets `state = 2` or `3` (alternating) | `2` or `3` | `pthread_cond_broadcast()` |
| `state_2()` | `state == 2` | Sets `state = 0` | `0` | `pthread_cond_broadcast()` |
| `state_3()` | `state == 3` | Sets `state = 0` | `0` | `pthread_cond_broadcast()` |

### The `is_even` Toggle in `state_1()`

`state_1()` must alternate between setting `state = 2` and `state = 3`. A simple integer flag tracks this:

| Iteration | `is_even` (initial) | Sets `state` | New `is_even` | Printed |
|-----------|---------------------|--------------|---------------|---------|
| 1 | `0` | `3` | `1` | `1 -> 3` |
| 2 | `1` | `2` | `0` | `1 -> 2` |
| 3 | `0` | `3` | `1` | `1 -> 3` |
| 4 | `1` | `2` | `0` | `1 -> 2` |

This produces the alternating sequence: `1 → 3 → 1 → 2 → 1 → 3 → 1 → 2 → ...`

Combined with the other threads:
```
0 → 1 → 3 → 0 → 1 → 2 → 0 → 1 → 3 → 0 → 1 → 2 → ...
```

---

## The Critical Problem: Why `pthread_cond_signal()` Fails

In the four-state machine, `pthread_cond_signal()` causes **deadlock**. Here is why.

### Scenario with `pthread_cond_signal()` (FAILURE)

```
1. state_0() sets state = 1, calls pthread_cond_signal()
2. Signal wakes state_2() (highest priority, longest waiting — but wrong thread)
3. state_2() acquires mutex, checks: while (state != 2) → TRUE (state is 1)
4. state_2() calls pthread_cond_wait(), goes back to waiting
5. state_1() was never signaled, still waiting
6. state_3() also waiting
7. state_0() has finished and is waiting for state = 0
8. ALL threads are now waiting. DEADLOCK.
```

The signal woke the wrong thread. Because only one thread was woken, and it was not the thread that could proceed, the system froze.

### Scenario with `pthread_cond_broadcast()` (SUCCESS)

```
1. state_0() sets state = 1, calls pthread_cond_broadcast()
2. All waiting threads wake up: state_1(), state_2(), state_3()
3. They all compete for the mutex (only one locks it at a time)
4. state_2() gets mutex first: while (state != 2) → TRUE, waits again
5. state_3() gets mutex: while (state != 3) → TRUE, waits again
6. state_1() gets mutex: while (state != 1) → FALSE, PROCEEDS
7. state_1() sets state = 3 (or 2), broadcasts, unlocks mutex
8. Now state_3() (or state_2()) will proceed when it gets the mutex
```

All threads are woken; only the correct one proceeds. The others re-check their condition and go back to waiting. No deadlock.

---

## Complete Solution: Four-State Machine

```c
/*
 * condvar_four_state.c
 *
 * Four-state machine using POSIX condition variables.
 *
 * State transitions:
 *   0 -> 1 -> 2 -> 0 -> 1 -> 3 -> 0 -> 1 -> 2 -> 0 -> ...
 *
 * Compile: gcc -o condvar_four_state condvar_four_state.c -pthread
 * Run:     ./condvar_four_state
 */

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

/* Shared state and synchronization */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond  = PTHREAD_COND_INITIALIZER;
static int state = 0;           /* Current state: 0, 1, 2, or 3 */
static int is_even = 0;         /* Toggle: 0 -> next is 3, 1 -> next is 2 */
static int running = 1;         /* Shutdown control */

/* Thread 0: state 0 -> 1 */
void *state_0_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&mutex);

        while (state != 0) {
            pthread_cond_wait(&cond, &mutex);
        }

        state = 1;
        printf("State: 0 -> 1\n");
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

/* Thread 1: state 1 -> 2 or 3 (alternating) */
void *state_1_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&mutex);

        while (state != 1) {
            pthread_cond_wait(&cond, &mutex);
        }

        if (is_even == 0) {
            state = 3;
            is_even = 1;
            printf("State: 1 -> 3\n");
        } else {
            state = 2;
            is_even = 0;
            printf("State: 1 -> 2\n");
        }

        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

/* Thread 2: state 2 -> 0 */
void *state_2_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&mutex);

        while (state != 2) {
            pthread_cond_wait(&cond, &mutex);
        }

        state = 0;
        printf("State: 2 -> 0\n");
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

/* Thread 3: state 3 -> 0 */
void *state_3_thread(void *arg) {
    while (running) {
        pthread_mutex_lock(&mutex);

        while (state != 3) {
            pthread_cond_wait(&cond, &mutex);
        }

        state = 0;
        printf("State: 3 -> 0\n");
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main() {
    pthread_t t0, t1, t2, t3;

    printf("Four-State Machine with Condition Variables\n");
    printf("Pattern: 0->1->2->0->1->3->0->1->2->0->1->3...\n\n");

    pthread_create(&t0, NULL, state_0_thread, NULL);
    pthread_create(&t1, NULL, state_1_thread, NULL);
    pthread_create(&t2, NULL, state_2_thread, NULL);
    pthread_create(&t3, NULL, state_3_thread, NULL);

    sleep(20);  /* Run for 20 seconds */

    running = 0;

    pthread_cancel(t0);
    pthread_cancel(t1);
    pthread_cancel(t2);
    pthread_cancel(t3);

    pthread_join(t0, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);

    printf("\nFour-state machine completed.\n");
    return 0;
}
```

---

## Design Principles: Mutex vs. Condvar

| Primitive | Purpose | When to Use |
|-----------|---------|-------------|
| **Mutex** | Protect shared resources/variables | Any time multiple threads access shared data |
| **Condition Variable** | Notify threads of state changes | When a thread must block until a condition becomes true |

The mutex controls **access** to shared variables. The condition variable provides **notification** when those variables change. They are complementary:

- **Mutex alone** can protect data but cannot make a thread wait efficiently (busy-wasting CPU)
- **Condvar alone** cannot protect data (no mutual exclusion)
- **Together** they enable efficient, safe blocking and notification

### Advanced Pattern: Multiple Condvars for Different Conditions

For complex systems, multiple condition variables can be associated with different subsets of shared state:

```
Shared variables: U, V, W, X, Y, Z

Condvar A: signaled when U, V, W change → wakes threads interested in subset {U,V,W}
Condvar B: signaled when W, X change    → wakes threads interested in subset {W,X}
Condvar C: signaled when Y, Z change    → wakes threads interested in subset {Y,Z}
```

This reduces unnecessary wakeups compared to using a single condvar for all state changes.

---

## Common Pitfalls

| Pitfall | Problem | Solution |
|---------|---------|----------|
| Using `if` instead of `while` | Spurious wakeups or broadcast wakeups cause incorrect execution | Always use `while (condition)` around `pthread_cond_wait()` |
| Using `signal()` with multiple condition waiters | Wrong thread wakes, correct thread never signaled → deadlock | Use `broadcast()` when multiple threads check different conditions |
| Forgetting to lock mutex before `wait()` | Undefined behavior; wait/unlock must be atomic | Lock mutex before calling `pthread_cond_wait()` |
| Not re-checking condition after `wait()` returns | Another thread may have changed state before mutex re-acquired | `while` loop ensures condition is re-verified |
| Signaling without setting state flag | If no thread is waiting, signal is lost → missed event | Always set a state flag (e.g., `data_ready`) before signaling |

---

## Summary

| API / Concept | Recommendation |
|---------------|----------------|
| **Primary API** | POSIX `pthread_cond_*` — portable, complete, well-documented |
| **Wait pattern** | Lock mutex → `while (condition)` → `pthread_cond_wait()` → work → unlock |
| **Notification** | Use `signal()` for single waiter; `broadcast()` for multiple waiters with different conditions |
| **State flags** | Always pair condvars with boolean flags to handle lost signals |
| **Loop check** | Always use `while`, never `if`, around `pthread_cond_wait()` |
| **Mutex association** | One mutex protects the shared state; one or more condvars provide notification |

---


*This module covers the condvar exercise from the QNX threading course, extending the two-state producer/consumer to a four-state machine with alternating transitions.*
