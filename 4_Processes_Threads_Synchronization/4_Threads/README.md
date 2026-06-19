
# Thread APIs in QNX

## Overview

QNX provides multiple APIs for creating and managing threads, reflecting its layered architecture where kernel primitives are wrapped by portable standards. This module covers the three API layers available, explains why portable interfaces are preferred, and establishes POSIX threads (pthreads) as the primary focus for this course.

---

## Three Thread API Layers

QNX offers three distinct API families for thread operations, each built upon the layer below:

| API Layer | Functions | Standard | Built On |
|-----------|-----------|----------|----------|
| **Kernel calls** | `ThreadCreate()`, `SyncMutexLock()` | QNX-specific | Direct microkernel entry |
| **POSIX threads** | `pthread_create()`, `pthread_mutex_lock()` | POSIX standard (IEEE Std 1003.1) | Kernel calls + library services |
| **C11 threads** | `thrd_create()`, `mtx_lock()` | ISO C11 standard (ISO/IEC 9899:2011) | POSIX threads or kernel calls |

All three ultimately reach the Neutrino microkernel for scheduling and synchronization, but they differ in portability, completeness, and intended use.

---

## Kernel Calls: The Foundation

Kernel calls like `ThreadCreate()` and `SyncMutexLock()` are the direct interface to the microkernel. They execute with elevated privilege, switching to kernel address space and using kernel stack.

However, these functions are **not intended for direct application use**:

| Aspect | Kernel Call Characteristic |
|--------|---------------------------|
| **Completeness** | Incomplete implementations — they handle only the kernel-side portion |
| **Library services** | No stack allocation, no thread-local storage setup, no cleanup handlers |
| **Portability** | QNX-only — code cannot run on Linux, BSD, or other systems |
| **Maintenance** | Requires deep QNX knowledge; unfamiliar to most developers |
| **Stability** | Interface may change between QNX versions |

**Example: What `ThreadCreate()` does NOT do**

The kernel `ThreadCreate()` function expects the caller to provide a pre-allocated stack and does not set up:
- Thread-local storage (TLS)
- POSIX thread attributes (detach state, cancel state)
- Cleanup handlers for thread exit
- C library per-thread data structures

These are all handled by the POSIX `pthread_create()` wrapper before and after calling the kernel function.

---

## POSIX Threads: The Recommended Standard

The POSIX thread API (`pthread_*`) is the **recommended choice** for QNX development. These functions are:
- **Portable** — Code runs on Linux, BSD, macOS, and many embedded systems
- **Complete** — Handle stack allocation, attribute setup, and cleanup
- **Familiar** — Recognized by most C/C++ developers
- **Maintainable** — Extensive documentation and community knowledge

### How POSIX Wraps Kernel Calls

The relationship between POSIX and kernel functions illustrates the thin-wrapper principle:

```
┌─────────────────────────────────────────────────────────────────────┐
│              POSIX pthread_create() vs. Kernel ThreadCreate()          │
│                                                                      │
│  POSIX FUNCTION: pthread_create()                                    │
│  ─────────────────────────────────                                   │
│                                                                      │
│  1. Validate parameters (attr, stack size, etc.)                     │
│  2. Allocate thread stack from process heap or use specified stack     │
│  3. Set up thread-local storage (TLS) area                           │
│  4. Initialize POSIX thread attributes (detach state, cancel type)     │
│  5. Initialize C library per-thread data structures                 │
│  6. Call kernel ThreadCreate() with:                                │
│     • Stack pointer (from step 2)                                    │
│     • Thread function entry point                                    │
│     • Argument pointer                                               │
│     • Priority and scheduling parameters                             │
│  7. Register thread with POSIX thread manager for cleanup              │
│  8. Return thread ID to caller                                       │
│                                                                      │
│  KERNEL FUNCTION: ThreadCreate()                                      │
│  ─────────────────────────────────                                    │
│                                                                      │
│  1. Validate kernel parameters                                       │
│  2. Allocate kernel thread control block (TCB)                       │
│  3. Set up kernel stack for thread context switching                 │
│  4. Initialize scheduling attributes (priority, algorithm, affinity)   │
│  5. Add thread to ready queue for scheduling                         │
│  6. Return thread ID (TID)                                           │
│                                                                      │
│  ═══════════════════════════════════════════════════════════════════  │
│  KEY POINT: pthread_create() does ~8 steps; ThreadCreate() does ~6.   │
│  The POSIX layer adds stack management, TLS, library setup, and       │
│  cleanup registration that the kernel function intentionally omits.    │
│  ═══════════════════════════════════════════════════════════════════  │
│                                                                      │
│  SIMILAR PATTERN FOR MUTEXES:                                        │
│                                                                      │
│  pthread_mutex_lock() → allocate mutex attributes if needed →         │
│                         → SyncMutexLock() kernel call                 │
│                                                                      │
│  The POSIX layer handles priority inheritance setup, error checking,   │
│  and attribute validation before entering the kernel.                  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## C11 Threads: The ISO Standard

C11 introduced a standard thread API (`thrd_*`, `mtx_*`, `cnd_*`) as part of the C language standard. These functions provide similar capabilities to POSIX threads but with a C-standard interface.

| Characteristic | C11 Threads | POSIX Threads |
|---------------|-------------|---------------|
| **Standard** | ISO/IEC 9899:2011 | IEEE Std 1003.1 |
| **Naming** | `thrd_create()`, `mtx_lock()` | `pthread_create()`, `pthread_mutex_lock()` |
| **Pronunciation** | Often awkward ("thrd" = "thread"? "mtx" = "mutex"?) | Well-established terminology |
| **Ecosystem** | Smaller community, less documentation | Mature, extensive resources |
| **QNX support** | Available via C library wrapper | Native, optimized implementation |
| **Recommendation** | Use if strict C-standard compliance required | **Preferred for QNX development** |

The C11 API is functionally equivalent for most use cases but lacks the maturity and ecosystem of POSIX threads. QNX supports both, but this course focuses on POSIX as the practical standard for real-world development.

---

## Why Portable APIs Are Preferred

The recommendation to use POSIX (or C11) over direct kernel calls extends beyond just thread creation. It applies to all QNX system services:

| Criterion | Portable API (POSIX/C11) | Direct Kernel Call |
|-----------|---------------------------|-------------------|
| **Code portability** | Runs on Linux, BSD, QNX, embedded RTOS | QNX-only |
| **Developer familiarity** | Recognized by most C/C++ programmers | Requires QNX-specific training |
| **Code review efficiency** | Reviewers understand immediately | Reviewers must look up documentation |
| **Maintenance cost** | Standard documentation, examples, Stack Overflow | QNX-specific documentation only |
| **Completeness** | Full stack allocation, cleanup, error handling | Kernel-side only; caller must manage rest |
| **Future-proofing** | Stable standard interfaces | May change with QNX versions |

The QNX philosophy mirrors the general system library guidance: use the highest-level abstraction that meets your needs. Drop to kernel calls only when no portable equivalent exists.

---

## Summary

| API | Use When | Example |
|-----|----------|---------|
| **POSIX threads** | **Default choice for all QNX thread development** | `pthread_create()`, `pthread_mutex_lock()` |
| **C11 threads** | Strict ISO C compliance required, no POSIX dependency | `thrd_create()`, `mtx_lock()` |
| **Kernel calls** | Only when no portable API exists (rare) | `ThreadCreate()` — not recommended |

This course focuses on **POSIX thread functions** as the primary teaching interface. All examples, exercises, and explanations use `pthread_*` naming and semantics. The underlying kernel behavior is explained where it aids understanding, but all practical code uses the portable POSIX layer.

---


*Happy coding!* 🚀
