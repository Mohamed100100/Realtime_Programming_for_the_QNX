

# Mutexes in QNX

## Overview

A **mutex** (short for "mutual exclusion") ensures that only one thread at a time can access a critical section of code or, more commonly, a critical piece of data. The data protected by a mutex can be anything — a simple variable, a complex structure, a linked list, an array of buffer pointers, or even hardware registers and device buffers. This module covers POSIX mutex operations, initialization patterns, common pitfalls, deadlock prevention, priority inheritance, and a practical coding exercise demonstrating the dramatic impact of mutexes on both correctness and performance.

---

## Mutex Basics

### Core Operations

| Operation | Function | Purpose |
|-----------|----------|---------|
| **Initialize** | `pthread_mutex_init()` | Set up mutex data structure before use |
| **Lock** | `pthread_mutex_lock()` | Acquire mutex; block if already held |
| **Unlock** | `pthread_mutex_unlock()` | Release mutex; wake waiting threads |
| **Destroy** | `pthread_mutex_destroy()` | Clean up mutex resources |

### Lock Behavior

When a thread calls `pthread_mutex_lock()`:

| Scenario | Result |
|----------|--------|
| Mutex is **unlocked** | Thread marks it as locked by itself and returns immediately |
| Mutex is **locked by another thread** | Calling thread blocks until the mutex is released |
| Mutex is **locked by calling thread itself** | Error — `EDEADLK` returned (QNX implementation) |

### Unlock Rules

When a thread calls `pthread_mutex_unlock()`:

| Scenario | Result |
|----------|--------|
| Mutex is **locked by calling thread** | Mutex marked unlocked; if threads waiting, highest-priority longest-waiting thread acquires it |
| Mutex is **locked by different thread** | Error — only the locking thread may unlock |
| Mutex is **already unlocked** | Error — undefined behavior |

**Critical principle:** The thread that locks the mutex is the **only** thread that can unlock it. This ownership model ensures that the protected data is accessed exclusively by one thread at a time.

---

## Mutex Initialization Patterns

### Explicit Initialization

Use `pthread_mutex_init()` when you need to configure mutex attributes:

```c
pthread_mutex_t my_mutex;
pthread_mutex_init(&my_mutex, NULL);  // NULL = default attributes
```

For simple intra-process mutexes, this initialization never fails in QNX — it only sets up data structures without allocating kernel resources. However, certain mutex types (like robust mutexes) require kernel allocation and can fail.

### Static Initialization (Auto-Init on First Use)

Use `PTHREAD_MUTEX_INITIALIZER` when you don't have a convenient initialization point and want the kernel to initialize the mutex on first use:

```c
pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;
```

This is particularly useful for library code where you cannot require callers to call an initialization function before using your API. The first `pthread_mutex_lock()` on this mutex triggers automatic initialization.

### Process-Shared Mutexes (Shared Memory)

When a mutex resides in shared memory and must be accessible across multiple processes, use `pthread_mutexattr_setpshared()`:

```c
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

pthread_mutex_t *shared_mutex = (pthread_mutex_t *)shared_memory_ptr;
pthread_mutex_init(shared_mutex, &attr);
```

This requires kernel resources for cross-process tracking and **can fail** — always check the return value.

---

## A Practical Example: Protecting a Free List

Consider a buffer allocation system with a linked-list free list. In a single-threaded program, this works perfectly:

```
┌─────────────────────────────────────────────────────────────────────┐
│              SINGLE-THREADED: NO PROTECTION NEEDED                     │
│                                                                      │
│  buf_alloc(size):                                                    │
│      walk free_list                                                  │
│      find entry with matching size                                   │
│      if found:                                                       │
│          remove from list                                            │
│          return pointer                                              │
│      else:                                                           │
│          return NULL                                                 │
│                                                                      │
│  Thread A calls buf_alloc(1024):                                     │
│      • Finds entry #2 matches                                        │
│      • Removes entry #2                                              │
│      • Returns pointer to buffer                                     │
│                                                                      │
│  RESULT: Correct — single thread, no conflicts                       │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

Add multiple threads, and the same code breaks catastrophically:

```
┌─────────────────────────────────────────────────────────────────────┐
│              MULTI-THREADED: RACE CONDITION (BROKEN)                     │
│                                                                      │
│  Thread A (buf_alloc(1024))          Thread B (buf_alloc(1024))       │
│  ─────────────────────────            ─────────────────────────         │
│                                                                      │
│  walk list → entry #2 matches        walk list → entry #2 matches    │
│      ↓ (context switch)                  ↓                           │
│                                      remove entry #2                 │
│  remove entry #2                     return pointer to buffer        │
│  return pointer to buffer                                                │
│                                                                      │
│  RESULT: BOTH threads return pointer to SAME buffer!                   │
│                                                                      │
│  Thread A writes data → Thread B overwrites with different data →    │
│  Both threads see corrupted, garbage data                            │
│                                                                      │
│  This is a "race condition" — timing-dependent, may occur rarely,    │
│  making it extremely difficult to reproduce and debug.               │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

The fix: add a mutex around all free list operations:

```
┌─────────────────────────────────────────────────────────────────────┐
│              MULTI-THREADED: MUTEX-PROTECTED (CORRECT)                 │
│                                                                      │
│  pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;            │
│                                                                      │
│  buf_alloc(size):                                                    │
│      pthread_mutex_lock(&alloc_mutex);    ← EXCLUSIVE ACCESS         │
│      walk free_list                                                  │
│      find entry with matching size                                   │
│      if found:                                                       │
│          remove from list                                            │
│          pthread_mutex_unlock(&alloc_mutex);                         │
│          return pointer                                              │
│      else:                                                           │
│          pthread_mutex_unlock(&alloc_mutex);                         │
│          return NULL                                                 │
│                                                                      │
│  Thread A: locks mutex → walks list → removes entry → unlocks         │
│  Thread B: tries lock → BLOCKS (mutex held by A) → waits            │
│                                                                      │
│  RESULT: Only one thread accesses free list at a time. Correct.        │
│                                                                      │
│  CRITICAL: Unlock on EVERY code path — success AND failure!          │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Mutex as a "Painted Line" — Not a Wall

A critical insight: **a mutex does not actually protect data**. The operating system has no knowledge of the association between a mutex and the data it guards. The mutex is like a painted line on the floor that all threads agree not to cross — but there is no wall preventing access.

```
┌─────────────────────────────────────────────────────────────────────┐
│              THE MUTEX IS A CONVENTION, NOT ENFORCEMENT                │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  "ROOM" — PROTECTED DATA (var1, var2, linked list, etc.)    │   │
│  │                                                             │   │
│  │  ┌─────────────────────────────────────────────────────┐   │   │
│  │  │  var1 = 42;                                         │   │   │
│  │  │  var2 = 100;                                        │   │   │
│  │  │  free_list = entry → entry → NULL                   │   │   │
│  │  └─────────────────────────────────────────────────────┘   │   │
│  │                                                             │   │
│  │  PAINTED LINE ON FLOOR (mutex)                              │   │
│  │  ═══════════════════════════════════════════════════════   │   │
│  │                                                             │   │
│  │  KEY hanging by door: mutex_lock(&door_key)                │   │
│  │                                                             │   │
│  │  Thread 1: grabs key → enters room → works → leaves →      │   │
│  │            hangs key (mutex_unlock)                         │   │
│  │                                                             │   │
│  │  Thread 2: grabs key → enters room → works → leaves →        │   │
│  │            hangs key                                        │   │
│  │                                                             │   │
│  │  MALICIOUS THREAD: steps over painted line WITHOUT key      │   │
│  │                    → directly accesses var1, var2           │   │
│  │                    → CORRUPTS DATA                          │   │
│  │                                                             │   │
│  │  ════════════════════════════════════════════════════════   │   │
│  │  OS ENFORCEMENT: None — the "wall" is only in programmer's  │   │
│  │                  mind. All threads MUST follow the convention.  │   │
│  │                                                             │   │
│  │  CONSEQUENCE: "Quick peek" without locking seems to work     │   │
│  │  most of the time, but creates intermittent corruption that  │   │
│  │  may take millions of iterations to reproduce. Impossible    │   │
│  │  to debug in production, often dismissed as "can't reproduce". │
│  │                                                             │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  RULE: Every access to protected data — read OR write — must hold    │
│  the mutex. No exceptions, no "quick looks", no "it seems fine".    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Deadlock: The Two-Mutex Problem

Deadlock occurs when two or more threads each hold a resource the other needs, and both wait forever.

```
┌─────────────────────────────────────────────────────────────────────┐
│              DEADLOCK SCENARIO: TWO THREADS, TWO MUTEXES               │
│                                                                      │
│  Thread 1 (Priority 20)              Thread 2 (Priority 15)            │
│  ─────────────────────              ─────────────────────              │
│                                                                      │
│  pthread_mutex_lock(&mutex_A);      pthread_mutex_lock(&mutex_B);    │
│      ↓ ACQUIRED                        ↓ ACQUIRED                      │
│                                                                      │
│  // Work on data A                   // Work on data B                 │
│                                                                      │
│  pthread_mutex_lock(&mutex_B);      pthread_mutex_lock(&mutex_A);    │
│      ↓ BLOCKS (B held by Thread 2)     ↓ BLOCKS (A held by Thread 1) │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  RESULT: DEADLOCK — both threads wait forever                        │
│                                                                      │
│  Thread 1 cannot proceed until Thread 2 releases B                    │
│  Thread 2 cannot proceed until Thread 1 releases A                    │
│  Neither can release — both are blocked                               │
│                                                                      │
│  OS does NOT detect this in general case (O(N!) complexity)          │
│  Static analysis tools may warn, but runtime detection is impractical │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Solution: Locking Hierarchy

Establish a **global order** for all mutexes in your system. Always acquire them in that order, always release in reverse order.

| Locking Order | Thread 1 | Thread 2 |
|--------------|----------|----------|
| **Rule: Always A before B** | Lock A, then B | Lock A, then B |
| **Execution** | Lock A → Lock B → work → unlock B → unlock A | Lock A → wait if held → Lock B → work → unlock B → unlock A |

If Thread 2 needs B first but the rule says A first, it must **unlock B, lock A, then re-lock B**. This prevents the circular wait condition that causes deadlock.

---

## Priority Inversion and Priority Inheritance

Priority inversion occurs when a high-priority thread is indirectly blocked by a lower-priority thread.

```
┌─────────────────────────────────────────────────────────────────────┐
│              PRIORITY INVERSION WITHOUT INHERITANCE                    │
│                                                                      │
│  Timeline →                                                          │
│                                                                      │
│  Time 1: Thread A (Priority 10) locks mutex, starts work             │
│          ┌─────────┐                                                 │
│          │ Thread A│ running                                          │
│          │  Prio 10│                                                │
│          │  [mutex │ locked                                          │
│          │   held] │                                                │
│          └─────────┘                                                 │
│                                                                      │
│  Time 2: Thread B (Priority 20) preempts Thread A                   │
│          ┌─────────┐  ┌─────────┐                                    │
│          │ Thread A│  │ Thread B│ running                            │
│          │  Prio 10│  │  Prio 20│                                    │
│          │  [mutex │  │         │                                    │
│          │   held] │  │         │                                    │
│          │  BLOCKED│  │         │                                    │
│          └─────────┘  └─────────┘                                    │
│                                                                      │
│  Time 3: Thread C (Priority 25) preempts Thread B                   │
│          ┌─────────┐  ┌─────────┐  ┌─────────┐                      │
│          │ Thread A│  │ Thread B│  │ Thread C│ running                │
│          │  Prio 10│  │  Prio 20│  │  Prio 25│                      │
│          │  [mutex │  │ BLOCKED │  │         │                      │
│          │   held] │  │         │  │         │                      │
│          │  BLOCKED│  │         │  │         │                      │
│          └─────────┘  └─────────┘  └─────────┘                      │
│                                                                      │
│  Time 4: Thread C needs mutex held by Thread A (Priority 10)         │
│          ┌─────────┐  ┌─────────┐  ┌─────────┐                      │
│          │ Thread A│  │ Thread B│  │ Thread C│ BLOCKED                │
│          │  Prio 10│  │  Prio 20│  │  Prio 25│ needs mutex           │
│          │  [mutex │  │ RUNNING │  │         │                      │
│          │   held] │  │         │  │         │                      │
│          │  BLOCKED│  │         │  │         │                      │
│          └─────────┘  └─────────┘  └─────────┘                      │
│                                                                      │
│  PROBLEM: Thread C (Priority 25) waits for Thread A (Priority 10)   │
│  But Thread A cannot run because Thread B (Priority 20) preempts it │
│  Thread C is effectively blocked by Thread B — lower priority than C!  │
│  This is "priority inversion" — high priority thread delayed by       │
│  medium priority thread that has nothing to do with the resource      │
│                                                                      │
│  Famous example: Mars Pathfinder lander — priority inversion caused   │
│  system resets, nearly mission failure. Fixed by priority inheritance. │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### QNX Solution: Priority Inheritance (Default)

QNX mutexes use **priority inheritance** by default. When a high-priority thread blocks on a mutex held by a low-priority thread, the kernel **boosts the holder's priority** to that of the highest waiting thread.

```
┌─────────────────────────────────────────────────────────────────────┐
│              PRIORITY INHERITANCE IN ACTION                            │
│                                                                      │
│  Time 4 (same scenario): Thread C (Priority 25) needs mutex         │
│                                                                      │
│  KERNEL ACTION: Boost Thread A to Priority 25 (same as Thread C)    │
│                                                                      │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐                              │
│  │ Thread A│  │ Thread B│  │ Thread C│                                │
│  │  Prio 25│  │  Prio 20│  │  Prio 25│ BLOCKED                      │
│  │  [mutex │  │ BLOCKED │  │         │ needs mutex                   │
│  │   held] │  │         │  │         │                              │
│  │  RUNNING│  │         │  │         │ ← A runs at boosted priority  │
│  └─────────┘  └─────────┘  └─────────┘                              │
│                                                                      │
│  Time 5: Thread A completes work, unlocks mutex                       │
│          • Priority drops back to original (10)                      │
│          • Thread C acquires mutex, runs at Priority 25              │
│                                                                      │
│  RESULT: Priority inversion eliminated — high-priority thread        │
│  blocked only for the duration of the critical section, not          │
│  indefinitely delayed by unrelated medium-priority work.             │
│                                                                      │
│  KEY RULE: Keep critical sections SHORT to minimize boosted priority   │
│  execution time. Long critical sections = long priority inversion.    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Performance: The Cost of Correctness

Mutexes add overhead. The magnitude depends on contention — how often threads compete for the same mutex.

### Fast Path vs. Slow Path

| Path | Condition | Cost | Mechanism |
|------|-----------|------|-----------|
| **Fast path** | Mutex unlocked, no contention | ~10-50 cycles | Local atomic "test and set" |
| **Slow path** | Mutex locked, must block | ~1000+ cycles | Kernel call, context switch, queue management |

### Practical Exercise: Measuring Mutex Impact

A simple loop with two integer operations demonstrates the dramatic cost:

| Configuration | Iterations in 15 seconds | Relative Speed |
|-------------|------------------------|----------------|
| **Single thread, no mutex** | ~2.3 billion | 1x (baseline) |
| **4 threads, no mutex** | Corrupted data | — (broken) |
| **4 threads, with mutex** | ~795,000 | ~0.0003x (~3000x slower) |

This is a **worst-case scenario**:
- The protected work is trivial (just a few integer operations)
- Multiple threads loop tightly on the same mutex
- Nearly every lock operation hits the slow path (kernel call)
- The mutex serializes what was embarrassingly parallel

**Real-world perspective:** In practice, mutex-protected sections contain meaningful work (file I/O, message processing, hardware access). The relative overhead is much smaller. The 3000x slowdown is an educational extreme, not a typical production scenario.

---

## Complete Exercise Solution Pattern

The mutex_sync exercise demonstrates proper mutex usage with a critical real-world technique: **copying data locally to minimize lock duration**.

```
┌─────────────────────────────────────────────────────────────────────┐
│              PROPER SOLUTION: MINIMIZE LOCK TIME                       │
│                                                                      │
│  BEFORE (problematic — printf inside critical section):              │
│  ────────────────────────────────────────────────────────────          │
│                                                                      │
│  pthread_mutex_lock(&var_mutex);                                     │
│  if (var1 != var2) {                                                 │
│      printf("Mismatch: var1=%d, var2=%d\n", var1, var2);  ← BAD!   │
│  }                                                                   │
│  var1++;                                                             │
│  var2++;                                                             │
│  pthread_mutex_unlock(&var_mutex);                                    │
│                                                                      │
│  PROBLEM: printf() may take unbounded time (serial port, network)    │
│  Mutex held during printf = other threads blocked for long time      │
│  Priority inheritance may boost low-priority thread for duration     │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  AFTER (correct — copy locally, print outside):                      │
│  ────────────────────────────────────────────────────────────          │
│                                                                      │
│  int local_var1, local_var2;                                         │
│  int need_print = 0;                                                 │
│                                                                      │
│  pthread_mutex_lock(&var_mutex);                                     │
│  if (var1 != var2) {                                                 │
│      local_var1 = var1;    ← COPY while locked                       │
│      local_var2 = var2;    ← COPY while locked                       │
│      need_print = 1;                                                 │
│  }                                                                   │
│  var1++;                                                             │
│  var2++;                                                             │
│  pthread_mutex_unlock(&var_mutex);    ← RELEASE ASAP                  │
│                                                                      │
│  if (need_print) {                                                   │
│      printf("Mismatch: var1=%d, var2=%d\n",                          │
│              local_var1, local_var2);  ← PRINT unlocked              │
│  }                                                                   │
│                                                                      │
│  BENEFITS:                                                           │
│  • Mutex held only for simple integer operations (microseconds)      │
│  • printf() runs without blocking other threads                      │
│  • No priority inheritance boost during slow I/O                     │
│  • Multiple threads can printf() concurrently (different mutexes)    │
│                                                                      │
│  CRITICAL: Branch paths must BOTH unlock!                              │
│  if (condition) { lock(); ... unlock(); } else { lock(); unlock(); }  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Summary

| Principle | Guidance |
|-----------|----------|
| **Mutex ownership** | Only the thread that locked may unlock |
| **Initialize before use** | `pthread_mutex_init()` or `PTHREAD_MUTEX_INITIALIZER` |
| **Unlock on all paths** | Every lock must have a matching unlock, including error branches |
| **Protect all access** | Every read AND write to shared data must hold the mutex |
| **Keep it short** | Minimize time mutex is held; avoid I/O, blocking calls, complex logic |
| **Use local copies** | Copy data out, release mutex, then perform slow operations |
| **Establish lock hierarchy** | Always acquire multiple mutexes in the same global order |
| **Priority inheritance** | QNX default; keep critical sections short to minimize boost duration |
| **Fast path vs. slow path** | Uncontended locks are cheap; contended locks are expensive |

---

*Happy coding!* 🚀
