

# Thread Synchronization in QNX

## Overview

This module covers the synchronization mechanisms available in QNX for coordinating access to shared resources among multiple threads within a process. Threads share all process resources — memory, file descriptors, channels, timers — which enables efficient communication but introduces synchronization problems. This module focuses on the three most commonly used tools: **mutexes**, **condition variables (convars)**, and **atomic operations**, while also introducing other synchronization primitives available in the QNX threading library.

---

## The Synchronization Problem

When multiple threads access shared resources concurrently, several problems arise:

| Problem | Description | Example |
|---------|-------------|---------|
| **Multiple writers** | Two or more threads write simultaneously | Both threads increment a counter; one update lost |
| **Read during write** | Reader sees partially updated data | Thread reads 64-bit value while another writes it; gets mixed old/new bytes |
| **Stale data** | Reader sees old value after writer has updated | Control flag not yet visible to reader due to compiler/CPU reordering |
| **GPIO conflicts** | Multiple threads control same hardware pin | One thread sets pin high, another sets low; indeterminate state |

These are **synchronization problems** — the fundamental challenge of multi-threaded programming. Synchronization primitives provide controlled, predictable access to shared resources.

---

## Core Synchronization Primitives

### Mutexes (Mutual Exclusion Locks)

A **mutex** ensures that only one thread at a time can access a critical section — a region of code or data protected by the lock.

**Basic operations:**
- **Lock** (`pthread_mutex_lock()`): Acquire the mutex. If already held, the calling thread blocks until the mutex is released.
- **Unlock** (`pthread_mutex_unlock()`): Release the mutex. If threads are waiting, the highest-priority longest-waiting thread acquires it.

**Use case:** Protecting shared data structures, counters, buffers, or hardware registers where only one thread should access at a time.

```
┌─────────────────────────────────────────────────────────────────────┐
│              MUTEX PROTECTING SHARED COUNTER                           │
│                                                                      │
│  Shared resource: global_counter                                      │
│                                                                      │
│  ┌─────────────────┐              ┌─────────────────┐               │
│  │   THREAD A      │              │   THREAD B      │               │
│  │   (Priority 20) │              │   (Priority 15) │               │
│  │                 │              │                 │               │
│  │  pthread_mutex_lock(&mutex);   │                 │               │
│  │       ↓ ACQUIRED              │  pthread_mutex_lock(&mutex);   │
│  │  global_counter++;            │       ↓ BLOCKS (mutex held)     │
│  │  pthread_mutex_unlock(&mutex);│                 │               │
│  │       ↓ RELEASED              │       ↓ UNBLOCKS              │
│  │                 │              │  global_counter++;            │
│  │                 │              │  pthread_mutex_unlock(&mutex);│
│  │                 │              │       ↓ RELEASED              │
│  └─────────────────┘              └─────────────────┘               │
│                                                                      │
│  RESULT: Counter increments are atomic with respect to each other.   │
│  No lost updates, no partial reads.                                   │
│                                                                      │
│  Without mutex: Both threads read same value, both increment,          │
│  one write overwrites the other — counter only increases by 1.       │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Condition Variables (Convars)

A **condition variable** allows threads to wait for a specific condition to become true, and to be notified when that condition changes. Condition variables are always used with a mutex.

**Basic operations:**
- **Wait** (`pthread_cond_wait()`): Atomically release mutex and block until signaled. When unblocked, re-acquire mutex before returning.
- **Signal** (`pthread_cond_signal()`): Wake up one waiting thread (highest priority, longest waiting).
- **Broadcast** (`pthread_cond_broadcast()`): Wake up all waiting threads.

**Use case:** Producer-consumer patterns, where one thread produces data and another waits for data to be available.

```
┌─────────────────────────────────────────────────────────────────────┐
│              CONDITION VARIABLE: PRODUCER-CONSUMER                       │
│                                                                      │
│  Shared resource: data_buffer, buffer_count                           │
│                                                                      │
│  ┌─────────────────┐              ┌─────────────────┐               │
│  │   PRODUCER      │              │   CONSUMER      │               │
│  │                 │              │                 │               │
│  │  // Produce data                                               │               │
│  │  pthread_mutex_lock(&mutex);                                    │               │
│  │  data_buffer[write_idx] = new_data;                            │               │
│  │  buffer_count++;                                                │               │
│  │  pthread_cond_signal(&cond);    ← "data available!"             │               │
│  │  pthread_mutex_unlock(&mutex);                                  │               │
│  │                 │              │                 │               │
│  │                 │              │  pthread_mutex_lock(&mutex);     │               │
│  │                 │              │  while (buffer_count == 0) {    │               │
│  │                 │              │      pthread_cond_wait(&cond,   │               │
│  │                 │              │                         &mutex); │               │
│  │                 │              │      // BLOCKS, releases mutex  │               │
│  │                 │              │      // On signal: re-acquires  │               │
│  │                 │              │  }                              │               │
│  │                 │              │  data = data_buffer[read_idx];  │               │
│  │                 │              │  buffer_count--;                  │               │
│  │                 │              │  pthread_mutex_unlock(&mutex);  │               │
│  │                 │              │  process(data);                   │               │
│  └─────────────────┘              └─────────────────┘               │
│                                                                      │
│  KEY POINT: pthread_cond_wait() atomically releases mutex AND blocks. │
│  When signaled, it re-acquires mutex before returning.                 │
│  Consumer always checks condition in while loop (spurious wakeups).  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Atomic Operations

**Atomic operations** perform read-modify-write sequences that cannot be interrupted by other threads. They are implemented using hardware instructions (atomic CPU instructions) and do not require mutexes for simple operations.

**Available operations:**
- Atomic increment/decrement
- Atomic compare-and-swap (CAS)
- Atomic load/store with memory ordering constraints

**Use case:** Simple counters, flags, reference counts where the operation is a single hardware instruction. More efficient than mutexes for simple cases.

**Limitations:** Cannot protect complex multi-step operations or data structure traversals. For those, mutexes are still required.

---

## Other Synchronization Primitives

### Semaphores

A **semaphore** provides a notification mechanism combined with a counter. It tracks how many resources are available or how many events have occurred.

**Characteristics:**
- Maintains a count value
- `sem_wait()` decrements; blocks if count is zero
- `sem_post()` increments; unblocks waiting threads if any
- Can be used across processes (named semaphores) or within a process (unnamed)

**Use case:** Resource pools with limited slots (e.g., connection pool with max 10 connections), event counting, dispatch notification.

### Reader/Writer Locks (rwlocks)

A **reader/writer lock** allows multiple concurrent readers or a single exclusive writer, but never both simultaneously.

| Scenario | Allowed | Behavior |
|----------|---------|----------|
| Multiple readers | Yes | All readers acquire lock concurrently |
| Single writer | Yes | Writer gets exclusive access |
| Reader + Writer | No | Writer blocks until all readers release; readers block until writer releases |

**Use case:** Data structures that are read frequently but written rarely. Readers do not block each other, improving concurrency.

### Barriers

A **barrier** blocks threads until a specified number of threads have reached the barrier point, then releases all of them simultaneously.

**Use case:** Initialization phases where multiple threads perform setup in parallel, then all must complete before any proceeds to the main execution phase.

```
┌─────────────────────────────────────────────────────────────────────┐
│              BARRIER: INITIALIZATION SYNCHRONIZATION                   │
│                                                                      │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐       │
│  │   THREAD 1      │  │   THREAD 2      │  │   THREAD 3      │       │
│  │                 │  │                 │  │                 │       │
│  │  init_hardware()│  │  init_network() │  │  init_logging() │       │
│  │       │         │  │       │         │  │       │         │       │
│  │       ▼         │  │       ▼         │  │       ▼         │       │
│  │  pthread_barrier_wait(&barrier);     │  │                 │       │
│  │       ↓ BLOCKS  │  │  pthread_barrier_wait(&barrier);   │       │
│  │       │         │  │       ↓ BLOCKS  │  │  pthread_barrier_wait(&barrier);│
│  │       │         │  │       │         │  │       ↓ BLOCKS  │       │
│  │       │         │  │       │         │  │       │         │       │
│  │  ═══════════════════════════════════════════════════════════       │
│  │              ALL 3 THREADS HAVE REACHED BARRIER                    │
│  │  ═══════════════════════════════════════════════════════════       │
│  │       │         │  │       │         │  │       │         │       │
│  │       ▼ UNBLOCKS│  │       ▼ UNBLOCKS│  │       ▼ UNBLOCKS│       │
│  │  main_loop();   │  │  main_loop();   │  │  main_loop();   │       │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘       │
│                                                                      │
│  Barrier initialized with count = 3.                                  │
│  No thread proceeds to main_loop() until all 3 have completed init.  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Once Control (pthread_once)

**Once control** ensures that a function is executed exactly once during the lifetime of a process, regardless of how many threads call it.

**Use case:** Library initialization that must happen exactly once, singleton setup, global configuration that cannot be repeated.

```
┌─────────────────────────────────────────────────────────────────────┐
│              ONCE CONTROL: ONE-TIME INITIALIZATION                     │
│                                                                      │
│  static pthread_once_t init_once = PTHREAD_ONCE_INIT;                 │
│                                                                      │
│  void initialize_library(void) {                                      │
│      // This function runs EXACTLY ONCE, regardless of how many        │
│      // threads call it or when they call it                          │
│      allocate_global_resources();                                     │
│      setup_hardware_interface();                                      │
│      register_with_system();                                          │
│  }                                                                   │
│                                                                      │
│  // Called from multiple threads, potentially simultaneously:         │
│  void library_function(void) {                                        │
│      pthread_once(&init_once, initialize_library);                    │
│      // After this point, initialization is guaranteed complete       │
│      perform_work();                                                  │
│  }                                                                   │
│                                                                      │
│  Thread 1 calls library_function() → blocks in pthread_once,          │
│    initialize_library() runs                                          │
│  Thread 2 calls library_function() → returns immediately,            │
│    initialization already done                                        │
│  Thread 3 calls library_function() 5 minutes later → returns        │
│    immediately, initialization done once                                │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Thread-Local Storage (TLS)

**Thread-local storage** provides per-thread instances of variables. Each thread sees its own copy of the variable, with no sharing between threads.

**Use case:** Libraries that need to maintain state but do not know whether they will be used in single-threaded or multi-threaded programs. Each thread gets independent data without requiring explicit per-thread allocation.

---

## Priority and Wait Time in Synchronization

When multiple threads are blocked waiting for a synchronization object to be released, QNX uses two criteria to determine which thread acquires it:

| Criterion | Priority |
|-----------|----------|
| **First** | **Highest priority** — the thread with the highest scheduling priority wins |
| **Second** | **Longest waiting** — if multiple threads at the same priority, the one that has been waiting longest wins |

**Important:** "Blocked" means blocked in the QNX sense — the thread is not consuming CPU time. It is in a blocked state waiting for the synchronization event, not in a busy-wait or polling loop.

```
┌─────────────────────────────────────────────────────────────────────┐
│              SYNCHRONIZATION WAKEUP ORDER                              │
│                                                                      │
│  Mutex being unlocked — 4 threads waiting:                            │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  WAITING QUEUE (ordered by priority, then wait time)         │   │
│  │                                                             │   │
│  │  1. Thread D — Priority 25, waiting 50ms    ← WINS (highest) │   │
│  │  2. Thread B — Priority 20, waiting 200ms                    │   │
│  │  3. Thread C — Priority 20, waiting 100ms   ← Same priority,  │   │
│  │  4. Thread A — Priority 15, waiting 10ms      B wins (longer) │   │
│  │                                                             │   │
│  │  On mutex unlock: Thread D acquires mutex                    │   │
│  │                                                             │   │
│  │  If Thread D did not exist: Thread B acquires (priority 20,   │   │
│  │    longer wait than Thread C at same priority)                │   │
│  │                                                             │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  This applies to: mutexes, condition variables, semaphores,          │
│  reader/writer locks, barriers                                       │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Summary

| Primitive | Use Case | Key Characteristic |
|-----------|----------|-------------------|
| **Mutex** | Exclusive access to shared data | One thread at a time; simple lock/unlock |
| **Condition variable** | Wait for condition, signal when true | Always used with mutex; atomic wait-release |
| **Atomic operations** | Simple counters, flags, reference counts | Hardware-level; no blocking; limited to simple ops |
| **Semaphore** | Resource counting, event notification | Counter-based; can be cross-process |
| **Reader/writer lock** | Many readers, occasional writers | Concurrent reads; exclusive writes |
| **Barrier** | Synchronize phase transitions | All threads wait; all released together |
| **Once control** | One-time initialization | Exactly once per process lifetime |
| **Thread-local storage** | Per-thread data in libraries | Automatic per-thread instances |

---



*Happy coding!* 🚀
