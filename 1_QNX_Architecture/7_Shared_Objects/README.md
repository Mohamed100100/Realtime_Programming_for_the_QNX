

# QNX Shared Objects

## Overview

Shared objects are code modules brought into a process at **runtime** rather than being linked into the program at **compilation time**. They enable multiple processes to share a single physical copy of code in memory, reducing overall system memory usage while maintaining process isolation. This concept is very similar to shared libraries in Linux and other Unix systems, though QNX's microkernel architecture gives it some unique characteristics.

Shared objects serve two primary purposes: they can be **implicitly loaded** at process launch based on linker information, or **explicitly loaded** on demand by the running program itself.

---

## Types of Shared Objects

### Implicitly Loaded Shared Libraries

These are linked at build time and automatically loaded when the process starts. The linker records dependency information in the executable file, and the system loader pulls in the required code when creating the process.

Common examples include:

| Shared Object | Purpose |
|-------------|---------|
| `libc.so` | Standard C library (POSIX functions, memory management, I/O) |
| `lib-tcpip.so` | TCP/IP networking stack |
| `libpthread.so` | POSIX threading support |

When you build an application linking against these libraries, the executable contains metadata telling the loader: *"When creating this process, also load these shared objects into its address space."*

### Explicitly Loaded Dynamic Libraries (DLLs)

These are loaded on demand by the running program using dynamic loading APIs. The program decides at runtime which code to load, often based on configuration or plugin architecture.

This is useful for:

- **Plugin systems** — load extensions based on user configuration
- **Optional features** — load heavy features only when needed
- **Hardware abstraction** — load different driver backends for different hardware
- **Version negotiation** — load compatible versions at runtime

---

## Memory Sharing Model

The fundamental benefit of shared objects is **code sharing across processes**. When multiple programs use the same shared library, only one physical copy resides in system memory, mapped into each process's virtual address space.

```
┌─────────────────────────────────────────────────────────────────────┐
│              SHARED OBJECT MEMORY MODEL                               │
│                                                                      │
│  PHYSICAL SYSTEM MEMORY (Single copy of code)                        │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                                                             │    │
│  │           ┌─────────────────────────┐                       │    │
│  │           │    libc.so CODE        │                       │    │
│  │           │  (read-only in RAM)    │                       │    │
│  │           │                         │                       │    │
│  │           │  • printf()            │                       │    │
│  │           │  • malloc()            │                       │    │
│  │           │  • fopen()             │                       │    │
│  │           │  • pthread_create()    │                       │    │
│  │           │  • ...                 │                       │    │
│  │           └─────────────────────────┘                       │    │
│  │                                                             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              │ mapped read-only into each process   │
│                              ▼                                       │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐ │
│  │   PROCESS A     │    │   PROCESS B     │    │   PROCESS C     │ │
│  │                 │    │                 │    │                 │ │
│  │  Virtual Space  │    │  Virtual Space  │    │  Virtual Space  │ │
│  │  ┌───────────┐  │    │  ┌───────────┐  │    │  ┌───────────┐  │ │
│  │  │ libc.so   │◄─┼────┼──┤ libc.so   │◄─┼────┼──┤ libc.so   │◄─┤ │
│  │  │ (read-only│  │    │  │ (read-only│  │    │  │ (read-only│  │ │
│  │  │  mapping) │  │    │  │  mapping) │  │    │  │  mapping) │  │ │
│  │  └───────────┘  │    │  └───────────┘  │    │  └───────────┘  │ │
│  │                 │    │                 │    │                 │ │
│  │  ┌───────────┐  │    │  ┌───────────┐  │    │  ┌───────────┐  │ │
│  │  │ PRIVATE   │  │    │  │ PRIVATE   │  │    │  │ PRIVATE   │  │ │
│  │  │ DATA      │  │    │  │ DATA      │  │    │  │ DATA      │  │ │
│  │  │ (copy)    │  │    │  │ (copy)    │  │    │  │ (copy)    │  │ │
│  │  └───────────┘  │    │  └───────────┘  │    │  └───────────┘  │ │
│  │                 │    │                 │    │                 │ │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘ │
│                                                                      │
│  KEY POINTS:                                                         │
│  • ONE physical copy of code in RAM                                  │
│  • MAPPED read-only into each process (cannot be modified)           │
│  • Global data is PRIVATE to each process (not shared)               │
│  • Memory savings: N processes × 1 copy instead of N copies           │
└─────────────────────────────────────────────────────────────────────┘
```

### Read-Only Code Mapping

The shared code is mapped with **read-only permissions**. This means:

- All processes see the exact same machine instructions
- None can accidentally (or maliciously) modify the shared code
- The MMU enforces this protection at the hardware level
- If a process tries to write to shared code, it receives a segmentation fault

This read-only mapping is crucial for both **security** and **stability**. A bug in one process cannot corrupt the shared library code used by all other processes.

---

## Data Handling in Shared Objects

### Global Data Becomes Process-Private

While code is shared, **global data within a shared object is not shared between processes**. Each process gets its own private copy of any global or static variables defined in the shared library.

This is a critical distinction from shared memory. When five different processes load the same shared library:

- They all share the **same code** (single physical copy)
- They each get **separate copies** of global/static data
- There is **no implied data sharing** between processes

```
┌─────────────────────────────────────────────────────────────────────┐
│           GLOBAL DATA IS PROCESS-PRIVATE                              │
│                                                                      │
│  Shared Object: libnetwork.so                                        │
│  Contains: global variable int connection_count = 0;                  │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  PHYSICAL RAM (one code copy, multiple data copies)         │    │
│  │                                                             │    │
│  │  ┌─────────────────┐    ┌─────────────────┐                │    │
│  │  │   CODE (shared) │    │   DATA (private)│                │    │
│  │  │                 │    │                 │                │    │
│  │  │  libnetwork.so  │    │  Process A copy │ connection_count│    │
│  │  │  (read-only)    │    │  connection_count = 3           │    │
│  │  │                 │    │                 │                │    │
│  │  │  All processes  │    │  Process B copy │ connection_count│    │
│  │  │  point here     │    │  connection_count = 7           │    │
│  │  │                 │    │                 │                │    │
│  │  │                 │    │  Process C copy │ connection_count│    │
│  │  │                 │    │  connection_count = 0           │    │
│  │  └─────────────────┘    └─────────────────┘                │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              │                                       │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐   │
│  │   PROCESS A     │    │   PROCESS B     │    │   PROCESS C     │   │
│  │                 │    │                 │    │                 │   │
│  │  connection_count│   │  connection_count│   │  connection_count│   │
│  │  = 3 (private)  │    │  = 7 (private)  │    │  = 0 (private)  │   │
│  │                 │    │                 │    │                 │   │
│  │  Changes here   │    │  Changes here   │    │  Changes here   │   │
│  │  DON'T affect   │    │  DON'T affect   │    │  DON'T affect   │   │
│  │  other processes│    │  other processes│    │  other processes│   │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘   │
│                                                                      │
│  Each process sees its OWN value of connection_count.                 │
│  Modifications in one process are invisible to others.                 │
│  This is automatic — no special setup required.                        │
└─────────────────────────────────────────────────────────────────────┘
```

### Why This Design?

This process-private data model provides several benefits:

| Benefit | Explanation |
|---------|-------------|
| **Process Isolation** | A bug in one process corrupting its global state won't affect other processes using the same library |
| **Security** | Processes cannot spy on or interfere with each other's library data |
| **Simplicity** | No need for synchronization primitives (mutexes, locks) around global variables in most cases |
| **Predictability** | Each process has its own state; behavior is deterministic |

---

## When You Need Actual Data Sharing

If a shared library loaded into multiple processes needs to **share data**, you must use **explicit shared memory** — just as you would for any other inter-process data sharing scenario.

### Explicit Shared Memory Approach

Shared memory is the QNX mechanism for making data visible across process boundaries. It requires intentional setup:

1. **Create or open a shared memory object** with a name (e.g., `/my_shared_data`)
2. **Map it into each process's address space** using `mmap()`
3. **Place the shared data structures** in that mapped region
4. **Use synchronization primitives** (mutexes, semaphores) to coordinate access

```
┌─────────────────────────────────────────────────────────────────────┐
│           EXPLICIT SHARED MEMORY FOR LIBRARY DATA                     │
│                                                                      │
│  Scenario: Three processes need shared connection statistics          │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  SHARED MEMORY OBJECT: /dev/shmem/network_stats             │    │
│  │                                                             │    │
│  │  ┌─────────────────────────────────────────────────────┐    │    │
│  │  │  Shared Memory Region (mapped into all processes)    │    │    │
│  │  │                                                     │    │    │
│  │  │  struct network_stats {                             │    │    │
│  │  │    int total_connections;      // shared           │    │    │
│  │  │    int active_connections;     // shared           │    │    │
│  │  │    int bytes_transferred;      // shared           │    │    │
│  │  │    pthread_mutex_t lock;       // shared lock      │    │    │
│  │  │  };                                                 │    │    │
│  │  │                                                     │    │    │
│  │  └─────────────────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────────────┘    │
│         ▲                    ▲                    ▲                 │
│         │ mmap()             │ mmap()             │ mmap()            │
│         ▼                    ▼                    ▼                    │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐ │
│  │   PROCESS A     │    │   PROCESS B     │    │   PROCESS C     │ │
│  │                 │    │                 │    │                 │ │
│  │  libnetwork.so  │    │  libnetwork.so  │    │  libnetwork.so  │ │
│  │  (code shared)  │    │  (code shared)  │    │  (code shared)  │ │
│  │                 │    │                 │    │                 │ │
│  │  ┌───────────┐  │    │  ┌───────────┐  │    │  ┌───────────┐  │ │
│  │  │ PRIVATE   │  │    │  │ PRIVATE   │  │    │  │ PRIVATE   │  │ │
│  │  │ DATA      │  │    │  │ DATA      │  │    │  │ DATA      │  │ │
│  │  │ (local)   │  │    │  │ (local)   │  │    │  │ (local)   │  │ │
│  │  └───────────┘  │    │  └───────────┘  │    │  └───────────┘  │ │
│  │                 │    │                 │    │                 │ │
│  │  ┌───────────┐  │    │  ┌───────────┐  │    │  ┌───────────┐  │ │
│  │  │ SHARED    │◄─┼────┼──┤ SHARED    │◄─┼────┼──┤ SHARED    │◄─┤ │
│  │  │ MEMORY    │  │    │  │ MEMORY    │  │    │  │ MEMORY    │  │ │
│  │  │ (pointer) │  │    │  │ (pointer) │  │    │  │ (pointer) │  │ │
│  │  └───────────┘  │    │  └───────────┘  │    │  └───────────┘  │ │
│  │       │          │    │       │          │    │       │          │ │
│  │       └──────────┼────┘       └──────────┼────┘       └──────────┘ │
│  │                  │                       │                          │
│  └──────────────────┼───────────────────────┼──────────────────────────┘ │
│                     │                       │                           │
│                     └───────────────────────┘                           │
│                          All point to SAME physical memory               │
│                                                                      │
│  ACCESS PATTERN:                                                     │
│  1. Lock mutex (pthread_mutex_lock)                                  │
│  2. Read/write shared data                                           │
│  3. Unlock mutex (pthread_mutex_unlock)                              │
│                                                                      │
│  CRITICAL: Must use synchronization! All processes see same data.   │
└─────────────────────────────────────────────────────────────────────┘
```

### Library Design Pattern for Shared Data

A well-designed shared library that needs cross-process data sharing typically:

1. **Provides initialization function** that creates/opens the shared memory object
2. **Encapsulates all shared data access** behind library functions
3. **Handles synchronization internally** so callers don't need to manage locks
4. **Falls back gracefully** if shared memory cannot be established

This way, the application code remains simple — it just calls library functions, and the library internally manages the shared memory and synchronization complexity.

---

## Comparison with Other Unix Systems

QNX's shared object model is very similar to Linux and other Unix systems, with these key characteristics:

| Aspect | QNX | Linux/Unix | Notes |
|--------|-----|-----------|-------|
| Code sharing | Single physical copy, read-only mapped | Same | Standard copy-on-write optimization |
| Global data privacy | Process-private by default | Same | Each process gets its own data copy |
| Explicit shared memory | `mmap()` with `MAP_SHARED` | Same | POSIX-standard mechanism |
| Dynamic loading | `dlopen()`, `dlsym()` | Same | POSIX-standard dynamic loading APIs |
| Load-time linking | `LD_LIBRARY_PATH`, `rpath` | Same | Standard library search paths |

The primary difference lies in QNX's microkernel architecture: shared object loading and mapping are services provided by the **Process Manager** (part of procnto), and the actual code resides in user-space processes rather than being loaded into kernel space. This means:

- Shared library loading is a **message-passing operation** to the Process Manager
- The Process Manager validates permissions and sets up MMU mappings
- No kernel modules or kernel-space code loading is involved
- Failed library loads are recoverable process-level errors, not system crashes

---

## Practical Implications

### Memory Savings

In a typical embedded system with multiple applications:

| Scenario | Without Shared Objects | With Shared Objects |
|----------|----------------------|---------------------|
| 10 processes using C library | 10 copies of libc in RAM | 1 copy of libc in RAM |
| 5 processes using networking | 5 copies of network stack | 1 copy of network stack |
| 3 processes using graphics | 3 copies of graphics library | 1 copy of graphics library |

The savings scale with the number of processes and the size of shared libraries. In memory-constrained embedded systems, this can be the difference between fitting in RAM or requiring swap/virtual memory.

### Update Flexibility

Because shared objects are loaded at runtime, you can update library code without recompiling applications:

1. Replace `libnetwork.so` on disk with a newer version
2. Restart processes that use it (or use dynamic unloading/reloading)
3. Processes automatically pick up the new library code

This is particularly valuable in embedded systems where over-the-air updates are common. You can patch a networking vulnerability by updating the shared library without touching multiple application binaries.

### Debugging Considerations

When debugging a shared library issue:

- Remember that **global data values differ per process** — a breakpoint in Process A shows its private copy, not what Process B sees
- For shared memory regions, use the process manager's `/proc` views to inspect shared mappings
- Use `pidin` or IDE tools to see which shared objects are loaded in each process
- If a shared library crashes, only the affected process is impacted — other processes using the same library continue running

---

## Summary

Shared objects in QNX provide efficient code sharing across processes while maintaining strict data isolation. The key principles are:

| Principle | Description |
|-----------|-------------|
| **Code is shared** | One physical copy in RAM, read-only mapped to all processes |
| **Data is private** | Global/static variables are automatically process-private |
| **Sharing requires explicit action** | Use POSIX shared memory (`mmap()`) for cross-process data |
| **Standard Unix model** | Familiar to Linux/Unix developers; same `dlopen()`/`dlsym()` APIs |
| **Microkernel benefits** | Loading is user-space operation; failures are process-level |

When designing shared libraries for multi-process QNX systems, decide early whether data needs to be shared across processes. If not, rely on the automatic process-private data model. If yes, design explicit shared memory mechanisms with proper synchronization from the start.

---

*Happy learning!* 🚀
