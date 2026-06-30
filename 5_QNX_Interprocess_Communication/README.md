# QNX Inter-Process Communication (IPC) — Overview

Inter-Process Communication (IPC) is the mechanism by which two or more processes exchange information, data, control signals, or event notifications. At its core, IPC enables a thread in one process to send a message to a thread in another process, allowing coordinated operation across the system.

QNX provides two essential categories of IPC mechanisms:

---

## 1. QNX Core IPC (Native & Fundamental)

These are unique to QNX and form the foundational layer upon which the entire QNX ecosystem operates. They are tightly integrated with the QNX microkernel architecture and offer high performance and deterministic behavior.

| Mechanism | Description |
|-----------|-------------|
| **QNX Messaging** | Synchronous, kernel-managed message passing between threads across process boundaries. The primary IPC method in QNX. |
| **QNX Pulses** | Lightweight, asynchronous notifications sent from the kernel or other processes. Used for event signaling without blocking. |
| **Shared Memory** | High-speed data sharing by mapping the same physical memory into multiple process address spaces. |

> **Focus of this module:** We will concentrate on these QNX-native IPC mechanisms, as they are central to understanding QNX system design and performance characteristics.

---

## 2. POSIX-Compliant IPC APIs (Portable)

These APIs follow the POSIX standard, ensuring portability across Unix-like systems. In QNX, they are typically implemented as services or resource managers that run on top of the core QNX IPC layer.

| Mechanism | Description | QNX Dependency |
|-----------|-------------|----------------|
| **Signals** | Asynchronous notifications sent to processes to indicate events or interrupts. | Kernel support |
| **Shared Memory Pipes** | Unidirectional byte-stream communication between processes. | Requires the `pipe` process |
| **POSIX Message Queues** | Named message queues for structured message passing between processes. | Requires the `mqueue` process |
| **TCP/IP Sockets** | Network-based, full-duplex communication over TCP/IP protocols. | Requires `io-sock` |

---

## Key Takeaway

While POSIX APIs offer cross-platform compatibility, **QNX Core IPC** (messaging, pulses, and native shared memory) provides the lowest latency, most deterministic, and most tightly integrated communication path within the QNX environment. Understanding these native mechanisms is essential for building efficient, real-time applications on QNX.

---

*This module focuses primarily on QNX Core IPC: Messaging, Pulses, and Shared Memory.*
