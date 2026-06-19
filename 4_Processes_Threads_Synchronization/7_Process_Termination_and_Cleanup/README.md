
# Process Termination and Cleanup in QNX

## Overview

This module covers what happens when a QNX process terminates — both the internal cleanup that occurs within the process itself and the external cleanup performed by the operating system. Understanding the distinction between normal and abnormal termination, what cleanup runs in each case, and what resources are automatically reclaimed is essential for designing reliable systems that handle both graceful shutdowns and unexpected failures correctly.

---

## Two Types of Cleanup

When a process terminates, two categories of cleanup occur:

| Type | Who Performs It | When It Runs | What It Handles |
|------|---------------|------------|---------------|
| **Internal cleanup** | Code within the process | During normal termination only | Application-level state, buffers, registered handlers |
| **External cleanup** | QNX operating system | Always — every termination | All process-owned resources: memory, files, connections, timers |

---

## Internal Cleanup: Normal Termination

Internal cleanup only occurs during **normal termination** — when the process exits through defined, controlled paths. This is the "graceful shutdown" path where the application has an opportunity to run final code.

### What Triggers Normal Termination

| Trigger | Description | Example |
|---------|-------------|---------|
| **Explicit `exit()` call** | Any thread calls `exit()` | Error handling path calls `exit(1)` |
| **Implicit `exit()` from `main()`** | Main thread returns from `main()` | `return 0;` at end of `main()` |
| **Last thread exits normally** | Final thread calls `pthread_exit()` or returns | Worker thread pool completes all tasks |

### What Runs During Normal Termination

| Cleanup Action | Description | Use Case |
|--------------|-------------|----------|
| **`atexit()` handlers** | Functions registered with `atexit()` execute in reverse registration order | Log shutdown, flush caches, notify peers |
| **stdio buffer flush** | All C standard I/O streams (`stdout`, `stderr`, `FILE*`) are flushed to underlying storage | Ensure log output is written, not lost in buffer |
| **C++ global destructors** | Destructors for global and static C++ objects execute | Release global resources, close singleton connections |
| **Thread-specific cleanup** | Thread-local storage destructors, cleanup handlers | Per-thread resource release |

```
┌─────────────────────────────────────────────────────────────────────┐
│              NORMAL TERMINATION FLOW                                   │
│                                                                      │
│  APPLICATION CODE                                                    │
│  ────────────────                                                    │
│                                                                      │
│  int main() {                                                        │
│      initialize_system();                                            │
│      atexit(cleanup_handler);        ← register exit handler        │
│      atexit(flush_logs);             ← register another (runs first) │
│                                                                      │
│      run_application();                                              │
│                                                                      │
│      return 0;                       ← implicit exit(0)            │
│  }                                                                   │
│                                                                      │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  NORMAL EXIT PROCESSING (runs in reverse registration order) │    │
│  │                                                             │    │
│  │  Step 3: flush_logs()                                        │    │
│  │          • Flush all stdio buffers to disk/files/streams     │    │
│  │                                                             │    │
│  │  Step 2: cleanup_handler()                                   │    │
│  │          • Close network connections                         │    │
│  │          • Release shared resources                        │    │
│  │          • Notify peer processes of shutdown               │    │
│  │                                                             │    │
│  │  Step 1: C++ global destructors                              │    │
│  │          • ~MySingleton()                                    │    │
│  │          • ~GlobalBuffer()                                   │    │
│  │                                                             │    │
│  │  Final: exit() returns status to parent/kernel               │    │
│  │                                                             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  EXTERNAL CLEANUP (always runs — see below)                          │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### `atexit()` Handler Registration

The `atexit()` function registers a function to be called during normal termination. Handlers execute in **reverse order of registration** — the last registered runs first.

```c
#include <stdlib.h>

void cleanup_network(void) {
    close_all_connections();
    notify_peers_shutdown();
}

void flush_data(void) {
    fflush(stdout);
    sync_data_files();
}

void release_locks(void) {
    unlock_global_mutex();
    release_resource_locks();
}

int main() {
    atexit(release_locks);    // Runs 3rd (registered 1st)
    atexit(flush_data);       // Runs 2nd (registered 2nd)
    atexit(cleanup_network);  // Runs 1st (registered 3rd)

    run_application();
    return 0;
}
```

**Limitations of `atexit()`:**
- Maximum 32 handlers (standard minimum; implementation may allow more)
- Handlers cannot call `exit()` — undefined behavior
- No parameters passed to handlers — use global state or closures

---

## Internal Cleanup: Abnormal Termination

**Abnormal termination** occurs when the process dies unexpectedly or through uncontrolled paths. In these cases, **no internal cleanup runs** — `atexit()` handlers do not execute, stdio buffers are not flushed, and C++ destructors do not run.

### What Triggers Abnormal Termination

| Trigger | Description | Example |
|---------|-------------|---------|
| **Last thread exits without `main()` returning** | Final thread calls `pthread_exit()` or returns from thread function; `main()` did not return | Thread pool design where main thread exits early |
| **Thread cancellation/abort** | Last thread is cancelled or aborted | Watchdog kills unresponsive thread |
| **Crash (hardware exception)** | CPU fault: divide by zero, invalid memory access | `int x = 1 / 0;` or `*NULL = 42;` → `SIGSEGV` |
| **Unhandled signal** | Signal delivered with no handler or default action is terminate | `SIGKILL` (cannot be handled), `SIGTERM` without handler |
| **Explicit `abort()`** | Program calls `abort()` generating `SIGABRT` | Assertion failure, critical error detection |

### What Does NOT Run During Abnormal Termination

| Skipped Cleanup | Consequence |
|-----------------|-------------|
| `atexit()` handlers | Registered cleanup functions never execute |
| stdio buffer flush | Buffered output lost — logs may be incomplete |
| C++ global destructors | Global resources leaked, connections not closed |
| Thread-local destructors | Per-thread cleanup skipped |

**Critical implication:** Applications that rely on `atexit()` or C++ destructors for critical cleanup (releasing locks, saving state, notifying peers) will fail to perform these actions if the process crashes or is killed. Design for external cleanup and idempotent operations.

---

## External Cleanup: Operating System Responsibility

Regardless of how a process terminates — normal exit, crash, signal kill, or thread abort — the QNX operating system performs **complete external cleanup** of all process-owned resources. This is a fundamental guarantee of the QNX microkernel architecture.

### Resources Automatically Reclaimed

| Resource Category | What Happens | Example |
|-------------------|-------------|---------|
| **Memory mappings** | All virtual memory unmapped; physical RAM freed | Stack, heap, code, data segments released |
| **File descriptors** | All open files closed | `open()`, `socket()`, `pipe()` descriptors closed |
| **IPC connections** | Message connections destroyed | `ConnectAttach()` coids invalidated |
| **IPC channels** | Message channels destroyed | `ChannelCreate()` chids closed; waiting clients get errors |
| **Shared memory** | Mappings removed; reference counts decremented | `mmap()` of `/dev/shmem/` objects unmapped |
| **Hardware mappings** | Physical memory mappings removed | `mmap()` of device registers unmapped |
| **Timers** | All POSIX and kernel timers deleted | `timer_create()` timers stopped and freed |
| **Interrupt attachments** | Interrupt handlers detached | `InterruptAttach()` ISTs unregistered |
| **Signal handlers** | Signal state cleared | Pending signals discarded |
| **Thread resources** | Thread control blocks freed; stacks released | All thread stacks reclaimed |
| **Process identity** | PID removed from system | Process no longer visible in `pidin` |

```
┌─────────────────────────────────────────────────────────────────────┐
│              EXTERNAL CLEANUP: ALWAYS RUNS                             │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  PROCESS OWNED RESOURCES — ALL AUTOMATICALLY CLEANED UP        │   │
│  │                                                             │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │   │
│  │  │   MEMORY    │  │    IPC      │  │   SYSTEM RESOURCES  │  │   │
│  │  │             │  │             │  │                     │  │   │
│  │  │ • Stack     │  │ • Channels  │  │ • Timers            │  │   │
│  │  │ • Heap      │  │ • Connections│  │ • Interrupts        │  │   │
│  │  │ • Code      │  │ • Pulses    │  │ • Signal handlers   │  │   │
│  │  │ • Data      │  │ • Messages  │  │ • File locks        │  │   │
│  │  │ • Thread    │  │             │  │ • Mutexes (process) │  │   │
│  │  │   stacks    │  │             │  │ • Condvars (process)│  │   │
│  │  │ • Shared    │  │             │  │                     │  │   │
│  │  │   memory    │  │             │  │                     │  │   │
│  │  │   mappings  │  │             │  │                     │  │   │
│  │  │ • Hardware  │  │             │  │                     │  │   │
│  │  │   mappings  │  │             │  │                     │  │   │
│  │  └─────────────┘  └─────────────┘  └─────────────────────┘  │   │
│  │                                                             │   │
│  │  ┌─────────────┐  ┌─────────────────────────────────────────┐  │   │
│  │  │   FILES     │  │   THREADS                             │  │   │
│  │  │             │  │                                       │  │   │
│  │  │ • File      │  │ • All threads terminated              │  │   │
│  │  │   descriptors│  │ • Thread control blocks freed         │  │   │
│  │  │   closed    │  │ • Thread stacks released              │  │   │
│  │  │ • Sockets   │  │ • Thread-local storage freed          │  │   │
│  │  │   closed    │  │                                       │  │   │
│  │  │ • Pipes     │  │                                       │  │   │
│  │  │   closed    │  │                                       │  │   │
│  │  └─────────────┘  └─────────────────────────────────────────┘  │   │
│  │                                                             │   │
│  │  MEMORY UNMAPPING IS THE KEY MECHANISM:                     │   │
│  │  • MMU removes all page table entries for process             │   │
│  │  • Physical RAM freed for reuse by other processes            │   │
│  │  • No memory leaks possible — OS guarantees complete cleanup  │   │
│  │                                                             │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  THIS HAPPENS FOR ALL TERMINATION:                                   │
│  ✓ Normal exit (exit(), main() returns)                             │
│  ✓ Thread exit (last thread exits)                                    │
│  ✓ Crash (SIGSEGV, SIGFPE, SIGILL)                                  │
│  ✓ Signal kill (SIGKILL, SIGTERM without handler)                   │
│  ✓ abort()                                                          │
│  ✓ Cancellation of last thread                                      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## The Exception: Named Resources Survive Process Death

There is one important exception to complete cleanup: **resources with path names in the filesystem namespace survive process termination**. These are persistent objects that exist independently of the creating process.

| Named Resource | Path Example | Behavior on Creator Exit |
|---------------|-------------|--------------------------|
| **Regular files** | `/data/log.txt` | Persist; data remains on storage |
| **Shared memory objects** | `/dev/shmem/my_buffer` | Persist; mappings removed but object remains |
| **Named semaphores** | `/dev/sem/my_semaphore` | Persist; semaphore state retained |
| **POSIX message queues** | `/dev/mqueue/my_queue` | Persist; queued messages retained |
| **Named pipes (FIFOs)** | `/tmp/my_pipe` | Persist; pipe file remains |

```
┌─────────────────────────────────────────────────────────────────────┐
│              NAMED RESOURCES: SURVIVE PROCESS DEATH                    │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  PROCESS A (creator)                                         │   │
│  │                                                              │   │
│  │  fd = open("/data/config.txt", O_CREAT | O_RDWR, 0666);   │   │
│  │  write(fd, "settings", 8);                                   │   │
│  │                                                              │   │
│  │  shm_fd = shm_open("/my_shm", O_CREAT | O_RDWR, 0666);      │   │
│  │  ftruncate(shm_fd, 4096);                                    │   │
│  │                                                              │   │
│  │  sem = sem_open("/my_sem", O_CREAT, 0666, 1);               │   │
│  │                                                              │   │
│  │  // Process A crashes or exits...                            │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              │ Process A terminates                   │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  NAMED RESOURCES PERSIST IN FILESYSTEM NAMESPACE               │   │
│  │                                                              │   │
│  │  /data/config.txt  ←── File exists with "settings" data      │   │
│  │  /dev/shmem/my_shm  ←── Shared memory object still exists      │   │
│  │  /dev/sem/my_sem  ←── Named semaphore still exists            │   │
│  │                                                              │   │
│  │  Process B (later) can open and use these:                     │   │
│  │  fd = open("/data/config.txt", O_RDONLY);                    │   │
│  │  shm_fd = shm_open("/my_shm", O_RDWR, 0);                    │   │
│  │  sem = sem_open("/my_sem", 0);                                 │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  WHY THIS MATTERS:                                                   │
│  • Files are data — expected to persist across process lifetimes     │
│  • Shared memory objects enable persistence for IPC buffers            │
│  • Named semaphores enable cross-process synchronization             │
│  • Message queues enable asynchronous communication survivors          │
│  • These must be explicitly unlinked (removed) when no longer needed │
│                                                                      │
│  CLEANUP RESPONSIBILITY:                                             │
│  • Creating process should unlink if resource is temporary             │
│  • System integrator may configure cleanup scripts                     │
│  • Boot scripts may clean /dev/shmem/ and /dev/sem/ on startup         │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Managing Persistent Named Resources

To prevent accumulation of stale named resources, processes should explicitly remove them when no longer needed:

| Function | Removes | When to Call |
|----------|---------|------------|
| `unlink(path)` | Regular files | File no longer needed |
| `shm_unlink(name)` | Shared memory objects | Shared memory no longer needed |
| `sem_unlink(name)` | Named semaphores | Semaphore no longer needed |
| `mq_unlink(name)` | POSIX message queues | Message queue no longer needed |

If a process crashes before calling these, the named resources remain. System design should account for this:
- Boot-time cleanup scripts
- Periodic maintenance processes
- Resource managers that track and garbage-collect stale objects

---

## Design Implications

| Scenario | Recommendation |
|----------|---------------|
| **Critical data persistence** | Do not rely on stdio buffer flush at exit; use `fflush()` or `fsync()` explicitly |
| **Peer notification of shutdown** | Use heartbeat/keepalive mechanism, not just `atexit()` |
| **Resource locks in distributed systems** | Use lock timeouts or watchdog processes; do not rely on process cleanup |
| **Temporary named resources** | Always `unlink()` before exit; design for crash cleanup |
| **C++ RAII for critical resources** | Good for normal exit; add external watchdog for crash cases |
| **Safety-critical systems** | Design all components as crash-safe; assume `atexit()` never runs |

---

## Summary

| Termination Type | Internal Cleanup | External Cleanup | Named Resources |
|-----------------|------------------|------------------|-----------------|
| **Normal exit** (`exit()`, `main()` returns) | Yes — `atexit()`, flush, destructors | Yes — complete | Persist |
| **Last thread exits** | No — abnormal | Yes — complete | Persist |
| **Thread cancelled/aborted** | No — abnormal | Yes — complete | Persist |
| **Crash** (`SIGSEGV`, `SIGFPE`) | No — abnormal | Yes — complete | Persist |
| **Signal kill** (`SIGKILL`, unhandled `SIGTERM`) | No — abnormal | Yes — complete | Persist |
| **`abort()`** | No — abnormal | Yes — complete | Persist |

The QNX guarantee: **external cleanup is always complete**. Memory, IPC, timers, interrupts, and file descriptors are always reclaimed. The application guarantee you must provide: **design for cases where internal cleanup never runs**. Critical state persistence, peer notification, and resource coordination must use mechanisms that survive or detect process failure, not just graceful shutdown paths.

---


*Happy coding!* 🚀
