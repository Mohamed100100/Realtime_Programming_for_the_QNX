
# Module 4: Processes, Threads & Synchronization

## Overview

This module covers the core execution model of QNX Neutrino — how programs become running processes, how processes contain threads that execute code, and how multiple threads within a process coordinate access to shared resources through synchronization primitives. Understanding these concepts is fundamental to building responsive, efficient, and correct real-time applications on QNX.

---

## Learning Objectives

By the end of this module, you will understand:

- What a **process** is and what resources it owns
- What a **thread** is and how it provides the actual execution flow
- Why and when to use **multiple threads** within a single process
- How to **create processes** and **create threads** programmatically
- How to **detect process termination** and **detect thread termination**
- How to **synchronize threads** using mutexes, condition variables, and other primitives

---

## Topics Covered

| Topic | Description | Key Concepts |
|-------|-------------|------------|
| **Process Model** | What a process is, what it owns, how it lives in the system | PID, address space, resources, memory, file descriptors |
| **Thread Model** | What a thread is, how it executes code within a process | TID, priority, scheduling, registers, stack |
| **Multithreading Rationale** | Why use multiple threads? Examples and use cases | Responsiveness, parallelism, resource sharing, I/O overlap |
| **Process Creation** | How to spawn new processes from a running program | `spawn()`, `fork()`, `exec()`, `posix_spawn()` |
| **Thread Creation** | How to create threads within a process | `pthread_create()`, thread attributes, priorities |
| **Process Death Detection** | How to know when a child or related process terminates | `wait()`, `waitpid()`, `sigwaitinfo()`, process monitoring |
| **Thread Death Detection** | How to know when a thread completes or exits | `pthread_join()`, detached threads, cancellation |
| **Mutexes** | Mutual exclusion for protecting shared data | `pthread_mutex_lock()`, `pthread_mutex_unlock()`, priority inheritance |
| **Condition Variables** | Waiting for conditions and signaling between threads | `pthread_cond_wait()`, `pthread_cond_signal()`, producer-consumer patterns |
| **Additional Synchronization** | Other primitives for thread coordination | Barriers, semaphores, read-write locks, spinlocks |

---

## Why This Matters

In QNX, the microkernel schedules **threads**, not processes. Every process must have at least one thread to be alive. Threads within the same process share all process resources — memory, file descriptors, timers — which makes communication efficient but requires careful synchronization to prevent data corruption.

Real-time systems demand predictable behavior. Understanding how to structure your application into processes and threads, how to detect failures, and how to synchronize access to shared state is essential for building reliable embedded systems.

---

## Prerequisites

- Module 1: QNX Architecture (understanding of procnto, processes vs. threads)
- Module 2: Momentics Development Basics (IDE, building, running)
- Basic C/C++ programming knowledge

---

*Let's dive into the process and thread model!* 🚀
