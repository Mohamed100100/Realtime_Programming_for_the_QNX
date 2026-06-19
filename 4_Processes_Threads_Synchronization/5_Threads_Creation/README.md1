
# Thread Creation in QNX

## Overview

This module covers the POSIX `pthread_create()` function and its associated attribute structure for creating threads in QNX. You will learn how to create threads with default settings, how to customize thread attributes including scheduling policy and priority, how to manage stack size and guard pages, and how the initial thread differs from subsequently created threads.

---

## pthread_create() Function

The `pthread_create()` function is the standard POSIX mechanism for creating a new thread within a process.

### Function Signature

```
int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg);
```

| Parameter | Description | Required |
|-----------|-------------|----------|
| **thread** | Pointer to `pthread_t` that receives the new thread's ID | Yes (POSIX mandatory; QNX allows NULL if ID not needed) |
| **attr** | Pointer to attribute structure defining thread characteristics; NULL for defaults | No (optional) |
| **start_routine** | Function pointer to the thread's entry point function | Yes |
| **arg** | Void pointer passed to start_routine; unexamined by OS/library | Yes (can be NULL) |

### Return Value and Thread ID

On success, `pthread_create()` returns 0 and stores the new thread's ID in the location pointed to by `thread`. This ID is used for subsequent thread operations like `pthread_join()`, `pthread_cancel()`, or `pthread_detach()`.

QNX optionally allows passing `NULL` for the `thread` parameter if you do not need to track the thread ID. This is a QNX extension beyond strict POSIX compliance.

### The Thread Function

The thread function has a specific signature:

```
void *thread_function(void *arg)
```

| Characteristic | Description |
|---------------|-------------|
| **Return type** | `void *` — the thread's exit value |
| **Parameter** | `void *arg` — the value passed as 4th parameter to `pthread_create()` |
| **Execution** | OS performs setup, then calls this function |
| **Termination** | Returns from function, calls `pthread_exit()`, or is cancelled |

The `arg` parameter is **unexamined by the operating system and library code**. Whatever pointer or value you pass (up to 64 bits) is handed back to you exactly as provided. The OS does not validate it, dereference it, or interpret it. This allows passing any data: a pointer to a structure, an integer cast to pointer, or NULL.

---

## Thread Attributes (pthread_attr_t)

The `pthread_attr_t` structure configures how a new thread is created. It controls scheduling, stack, detach state, and other characteristics.

### Attribute Lifecycle

| Function | Purpose | When to Call |
|----------|---------|------------|
| `pthread_attr_init()` | Initialize attribute structure with defaults | Before any `pthread_attr_set*()` calls |
| `pthread_attr_set*()` | Modify specific attributes | After init, before `pthread_create()` |
| `pthread_attr_get*()` | Query current attribute values | Rarely used (configuration, not query structure) |
| `pthread_attr_destroy()` | Clean up attribute structure | After `pthread_create()` returns |

```
┌─────────────────────────────────────────────────────────────────────┐
│              THREAD ATTRIBUTE LIFECYCLE                                │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  STEP 1: DECLARE AND INITIALIZE                                │   │
│  │                                                              │   │
│  │  pthread_attr_t attr;                                        │   │
│  │  pthread_attr_init(&attr);      ← sets all defaults          │   │
│  │                                                              │   │
│  │  Default values after init:                                   │   │
│  │  • Scheduling: inherit from parent (PTHREAD_INHERIT_SCHED)   │   │
│  │  • Stack size: 256 KB (for created threads)                   │   │
│  │  • Guard page: 4 KB (one page)                                 │   │
│  │  • Detach state: joinable (PTHREAD_CREATE_JOINABLE)            │   │
│  │  • Priority: inherited from creating thread                    │   │
│  │                                                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  STEP 2: MODIFY ATTRIBUTES                                     │   │
│  │                                                              │   │
│  │  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED); │   │
│  │  pthread_attr_setschedpolicy(&attr, SCHED_RR);                │   │
│  │  struct sched_param param = { .sched_priority = 15 };         │   │
│  │  pthread_attr_setschedparam(&attr, &param);                   │   │
│  │  pthread_attr_setstacksize(&attr, 512 * 1024);                │   │
│  │                                                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  STEP 3: CREATE THREAD                                         │   │
│  │                                                              │   │
│  │  pthread_t tid;                                              │   │
│  │  pthread_create(&tid, &attr, my_thread_func, &my_data);     │   │
│  │                                                              │   │
│  │  // Thread now runs with:                                     │   │
│  │  // • Priority 15, Round Robin scheduling                      │   │
│  │  // • 512 KB stack size                                       │   │
│  │  // • 4 KB guard page (default)                               │   │
│  │                                                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  STEP 4: CLEAN UP                                              │   │
│  │                                                              │   │
│  │  pthread_attr_destroy(&attr);   ← release resources          │   │
│  │                                                              │   │
│  │  // attr can be reused (re-init) or discarded                  │   │
│  │                                                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  NOTE: Passing NULL for attr in pthread_create() is equivalent      │
│  to using a default-initialized attribute structure (init + no sets).   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Scheduling Attributes

Controlling thread scheduling requires explicit configuration through the attribute structure.

### Inheritance vs. Explicit Scheduling

| Setting | Value | Behavior |
|---------|-------|----------|
| `PTHREAD_INHERIT_SCHED` (default) | 0 | New thread inherits parent's priority and scheduling policy |
| `PTHREAD_EXPLICIT_SCHED` | 1 | New thread uses values specified in attribute structure |

To set custom scheduling, you **must** first set `PTHREAD_EXPLICIT_SCHED`:

```
pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
```

Without this step, any priority or policy changes in the attribute structure are ignored — the thread inherits from its creator.

### Scheduling Policies

QNX supports several POSIX scheduling policies, each with different characteristics:

| Policy | Constant | Parameters | Use Case |
|--------|----------|-----------|----------|
| **FIFO** | `SCHED_FIFO` | Priority only | Run until block; deterministic real-time |
| **Round Robin** | `SCHED_RR` | Priority + time slice | Time-sharing at same priority |
| **Other** | `SCHED_OTHER` | Priority (typically 0) | Non-real-time, system-dependent |
| **Sporadic** | `SCHED_SPORADIC` | Priority + low priority + budget + period | Aperiodic servers, budget control |
| **No Change** | `SCHED_NOCHANGE` | Parameters only | Keep existing policy, change parameters |

### Setting Priority and Policy

For `SCHED_FIFO`, `SCHED_RR`, and `SCHED_OTHER`:

```
struct sched_param param;

pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
pthread_attr_setschedpolicy(&attr, SCHED_RR);
param.sched_priority = 15;
pthread_attr_setschedparam(&attr, &param);
```

This creates a Round Robin thread at priority 15.

### Sporadic Scheduling (SCHED_SPORADIC)

Sporadic scheduling requires four parameters for budget-controlled execution:

| Parameter | Field | Description |
|-----------|-------|-------------|
| **Normal priority** | `sched_priority` | Priority during budget period |
| **Low priority** | `sched_ss_low_priority` | Priority after budget exhausted |
| **Replenishment period** | `sched_ss_repl_period` | Time before budget is restored |
| **Budget** | `sched_ss_init_budget` | Time allowed at normal priority per period |

```
struct sched_param param;

pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
pthread_attr_setschedpolicy(&attr, SCHED_SPORADIC);

param.sched_priority = 20;                    // Normal priority
param.sched_ss_low_priority = 5;              // Low priority after budget
param.sched_ss_repl_period.tv_sec = 0;        // Replenishment: 100 ms
param.sched_ss_repl_period.tv_nsec = 100000000;
param.sched_ss_init_budget.tv_sec = 0;        // Budget: 10 ms
param.sched_ss_init_budget.tv_nsec = 10000000;

pthread_attr_setschedparam(&attr, &param);
```

The thread runs at priority 20 for up to 10 milliseconds, then drops to priority 5. After 100 milliseconds, the budget is replenished and priority returns to 20.

---

## Stack Configuration

Each thread receives a private stack for function calls, local variables, and return addresses.

### Default Stack Sizes

| Thread Type | Default Stack Size | Notes |
|-------------|-------------------|-------|
| **Created threads** (pthread_create) | 256 KB | Configurable via attributes |
| **Initial thread** (main) | 512 KB | Fixed at compile time, not changeable via attributes |

### Stack Size Attribute

Set an explicit stack size with `pthread_attr_setstacksize()`:

```
pthread_attr_setstacksize(&attr, 512 * 1024);  // 512 KB
```

Constraints:
- Minimum: `PTHREAD_STACK_MIN` (platform-defined, typically 1-4 KB)
- Rounded up to nearest page size (4 KB on current QNX systems)
- Must accommodate the guard page

### Guard Pages

A **guard page** is a protected memory region at the end of the stack that detects stack overflow. If a thread's stack grows into the guard page, a memory fault occurs, crashing the thread (and potentially the process) rather than silently corrupting adjacent memory.

| Guard Page Characteristic | Default | Configurable |
|--------------------------|---------|--------------|
| **Size** | 4 KB (one page) | Yes via `pthread_attr_setguardsize()` |
| **Protection** | No access (read/write forbidden) | Fixed |
| **Purpose** | Catch stack overflow | Prevents silent memory corruption |

```
┌─────────────────────────────────────────────────────────────────────┐
│              THREAD STACK LAYOUT                                       │
│                                                                      │
│  High Addresses (stack base)                                          │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  USABLE STACK (256 KB default, or configured size)            │   │
│  │  Grows downward as functions are called                      │   │
│  │  Contains: local variables, return addresses, saved registers │   │
│  │  ┌─────────┐                                                │   │
│  │  │ main()  │  local vars, return addr                        │   │
│  │  ├─────────┤                                                │   │
│  │  │ funcA() │  local vars, return addr                        │   │
│  │  ├─────────┤                                                │   │
│  │  │ funcB() │  local vars, return addr                        │   │
│  │  └─────────┘                                                │   │
│  │       ▼  stack pointer moves down                           │   │
│  └─────────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  GUARD PAGE (4 KB default, or configured size)              │   │
│  │  ┌─────────────────────────────────────────────────────┐   │   │
│  │  │  NO ACCESS — read/write forbidden                   │   │   │
│  │  │  If stack overflows here → SIGSEGV (crash detected)  │   │   │
│  │  └─────────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────────┘   │
│  Low Addresses                                                        │
│                                                                      │
│  Stack size = committed RAM (physical or virtual)                     │
│  Guard page = reserved virtual address space (no RAM committed)       │
│                                                                      │
│  Total virtual address space used: stack_size + guard_size            │
│  Total RAM committed: stack_size (guard page faults on access)        │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Customizing Guard Page Size

For threads with large stack frames or deep recursion, a larger guard page may be desired:

```
pthread_attr_setguardsize(&attr, 16 * 1024);  // 16 KB guard page
```

This provides more protection margin but consumes additional virtual address space.

### Initial Thread Stack Size

The **initial thread** (the thread that runs `main()`) receives a **512 KB stack by default**. This is **not configurable** through `pthread_attr_t` because the initial thread is created by the process loader before `main()` begins.

To change the initial thread stack size, you must specify it at **compile time** using linker options or compiler flags specific to your toolchain.

---

## Complete Example: Creating a Custom Thread

```
┌─────────────────────────────────────────────────────────────────────┐
│              EXAMPLE: CREATING A PRIORITY 15 ROUND-ROBIN THREAD        │
│                                                                      │
│  #include <pthread.h>                                                │
│  #include <sched.h>                                                  │
│  #include <stdio.h>                                                  │
│                                                                      │
│  void *sensor_thread(void *arg) {                                     │
│      int sensor_id = *(int *)arg;    // Unpack argument             │
│                                                                      │
│      while (1) {                                                     │
│          read_sensor_data(sensor_id);                                 │
│          process_and_queue_data();                                   │
│          usleep(10000);              // 10ms sampling period          │
│      }                                                               │
│      return NULL;                                                    │
│  }                                                                   │
│                                                                      │
│  int main() {                                                        │
│      pthread_t tid;                                                  │
│      pthread_attr_t attr;                                            │
│      struct sched_param param;                                        │
│      int sensor_id = 3;                                              │
│                                                                      │
│      // Initialize attributes with defaults                            │
│      pthread_attr_init(&attr);                                      │
│                                                                      │
│      // Configure explicit scheduling                                │
│      pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);   │
│                                                                      │
│      // Set Round Robin policy                                       │
│      pthread_attr_setschedpolicy(&attr, SCHED_RR);                   │
│                                                                      │
│      // Set priority to 15 (higher than main thread's typical 10)    │
│      param.sched_priority = 15;                                     │
│      pthread_attr_setschedparam(&attr, &param);                      │
│                                                                      │
│      // Increase stack for heavy processing                          │
│      pthread_attr_setstacksize(&attr, 512 * 1024);                   │
│                                                                      │
│      // Create the thread                                            │
│      pthread_create(&tid, &attr, sensor_thread, &sensor_id);          │
│                                                                      │
│      // Clean up attribute structure (thread retains settings)        │
│      pthread_attr_destroy(&attr);                                    │
│                                                                      │
│      // Continue with other work...                                  │
│      // ...                                                          │
│                                                                      │
│      // Wait for thread completion (if it ever terminates)              │
│      pthread_join(tid, NULL);                                        │
│                                                                      │
│      return 0;                                                       │
│  }                                                                   │
│                                                                      │
│  RESULT:                                                             │
│  • Thread runs at priority 15 with Round Robin time-slicing           │
│  • 512 KB stack with 4 KB guard page                                │
│  • Receives pointer to sensor_id (value 3) as argument              │
│  • Can be joined by main thread when it terminates                   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Summary

| Attribute | Default | How to Change | Function |
|-----------|---------|---------------|----------|
| **Thread ID** | Returned in `pthread_t` | Pass NULL if not needed | `pthread_create()` 1st param |
| **Scheduling inheritance** | Inherit from parent | Set `PTHREAD_EXPLICIT_SCHED` | `pthread_attr_setinheritsched()` |
| **Scheduling policy** | Inherited | `SCHED_FIFO`, `SCHED_RR`, `SCHED_SPORADIC` | `pthread_attr_setschedpolicy()` |
| **Priority** | Inherited | 1-253 for user threads | `pthread_attr_setschedparam()` |
| **Stack size** | 256 KB (created), 512 KB (main) | Specify bytes (rounded to page) | `pthread_attr_setstacksize()` |
| **Guard page** | 4 KB | Specify bytes (rounded to page) | `pthread_attr_setguardsize()` |
| **Detach state** | Joinable | `PTHREAD_CREATE_DETACHED` | `pthread_attr_setdetachstate()` |

Key principles:
- Always `init` before `set`, `destroy` after `create`
- Must set `PTHREAD_EXPLICIT_SCHED` before custom priority/policy take effect
- Main thread stack (512 KB) is not configurable at runtime
- Guard pages protect against stack overflow — never disable in production
- `pthread_attr_get*()` functions exist but are rarely used in practice

---


*Happy coding!* 🚀
