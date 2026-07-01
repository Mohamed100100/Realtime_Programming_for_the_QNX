# QNX IPC Method Selection Guide

> **Companion to:** QNX IPC Methods Detailed Comparison, QNX IPC Methods Comparison Overview

This document provides a **practical decision framework** for choosing the right IPC mechanism in QNX Neutrino. It covers the key considerations that drive IPC selection in real-world embedded systems design.

---

## 1. The Decision Framework

Choosing an IPC mechanism in QNX requires evaluating six key considerations:

```
┌─────────────────────────────────────────────────────────┐
│           QNX IPC Selection Decision Tree               │
│                                                         │
│  Start: What are my application requirements?           │
│                                                         │
│  ┌─────────────────┐  ┌─────────────────┐               │
│  │ 1. POSIX req?   │  │ 2. Data volume? │               │
│  │    (portability)│  │    (copy cost)  │               │
│  └────────┬────────┘  └────────┬────────┘               │
│           │                    │                        │
│  ┌────────▼────────┐  ┌────────▼────────┐               │
│  │ 3. Need direct  │  │ 4. Buffering    │               │
│  │    response?    │  │    control?     │               │
│  │    (blocking)   │  │    (memory/safety)│             │
│  └────────┬────────┘  └────────┬────────┘               │
│           │                    │                        │
│  ┌────────▼────────┐  ┌────────▼────────┐               │
│  │ 5. Cross-network?│  │ 6. Hybrid mix?  │              │
│  │    (remote)     │  │    (combine)    │               │
│  └─────────────────┘  └─────────────────┘               │
│                                                         │
│  Each consideration narrows the field of suitable IPC   │
│  mechanisms. The final choice often combines multiple   │
│  methods for different parts of the system.             │
└─────────────────────────────────────────────────────────┘
```

---

## 2. Consideration 1: POSIX Compliance

**Question:** Does your application need to be POSIX compliant?

### When POSIX is Required

| Scenario                   | Reason                              | Suitable IPC    |
| -------------------------- | ----------------------------------- | --------------- |
| **Cross-platform code**    | Must run on Linux, BSD, macOS       | POSIX APIs only |
| **Shared source codebase** | Same code used on multiple OSes     | POSIX APIs only |
| **Open source adoption**   | Integrating third-party POSIX code  | POSIX APIs only |
| **Customer mandate**       | Contract specifies POSIX compliance | POSIX APIs only |
| **Regulatory requirement** | Standards body requires POSIX       | POSIX APIs only |

### POSIX vs QNX-Native Trade-offs

```
┌─────────────────────────────────────────────────────────┐
│              POSIX Compliance Spectrum                  │
│                                                         │
│  STRICT POSIX ────────────────────────► QNX-NATIVE      │
│                                                         │
│  • Signals          • POSIX MQ       • QNX Messages     │
│  • Pipes            • Shared Memory  • QNX Pulses       │
│  • TCP/IP Sockets   • File Descriptors                  │
│                                                         │
│  Portability:    HIGH ◄─────────────► LOW               │
│  Performance:    LOW  ◄─────────────► HIGH              │
│  Real-time:      WEAK ◄─────────────► STRONG            │
│  Safety cert:    RARE ◄─────────────► AVAILABLE         │
│                                                         │
│  Recommendation: Use QNX-native for new designs.        │
│  Use POSIX only when portability is a hard requirement. │
└─────────────────────────────────────────────────────────┘
```

### POSIX-Compliant Options

| IPC Method               | POSIX Standard | Portability | Performance | Notes                 |
| ------------------------ | -------------- | ----------- | ----------- | --------------------- |
| **Signals**              | POSIX.1        | Universal   | Low         | Simple notifications  |
| **Pipes**                | POSIX.1        | Universal   | Low         | Stream-based          |
| **Shared Memory**        | POSIX.1b       | Universal   | High        | Zero copy             |
| **POSIX Message Queues** | POSIX.1b       | Universal   | Medium      | Priority ordering     |
| **TCP/IP Sockets**       | POSIX.1        | Universal   | Medium      | Network capable       |
| **File Descriptors**     | POSIX.1        | Universal   | High        | Via resource managers |

### QNX-Native Options (Non-POSIX)

| IPC Method                | Portability | Performance | Real-time | Safety Cert |
| ------------------------- | ----------- | ----------- | --------- | ----------- |
| **QNX Messages**          | QNX only    | High        | Excellent | Available   |
| **QNX Pulses**            | QNX only    | Very High   | Excellent | Available   |
| **QNX Events (sigevent)** | QNX only    | High        | Excellent | Available   |

**Key insight:** QNX-native mechanisms are **safety-certifiable**; many POSIX mechanisms are not.

---

## 3. Consideration 2: Data Volume

**Question:** How much data is being moved?

### Data Volume Spectrum

```
┌─────────────────────────────────────────────────────────┐
│              Data Volume vs IPC Method                  │
│                                                         │
│  Tiny        Small        Medium        Large      Huge │
│  (< 8B)     (< 1KB)     (< 64KB)    (< 1MB)     (> 1MB) │
│    │          │            │            │            │  │
│    ▼          ▼            ▼            ▼            ▼  │
│  Pulse    Message      Message      Shared      Shared  │
│           (small)      (IOV)       Memory      Memory + │
│                                   + Control    Zero-copy│
│                                                         │
│  Copy cost: 0      1 kernel     1 kernel         0      │
│             │      copy         copy (gather)  (after   │
│             │      │            │             setup)    │
│             └──────┴────────────┴─────────────────┘     │
└─────────────────────────────────────────────────────────┘
```

### Choosing by Data Size

| Data Size            | Recommended                     | Why                                | Avoid                      |
| -------------------- | ------------------------------- | ---------------------------------- | -------------------------- |
| **Tiny (< 8 bytes)** | Pulse                           | Fits in 8-byte payload; no copy    | MsgSend (overhead)         |
| **Small (< 1KB)**    | MsgSend                         | Single kernel copy; built-in sync  | Shared Memory (setup cost) |
| **Medium (< 64KB)**  | MsgSend or MsgSendv             | Efficient copy; IOV for multi-part | Pipes (2 copies)           |
| **Large (< 1MB)**    | Shared Memory + MsgSend control | Zero copy; message for sync        | MsgSend (copy overhead)    |
| **Huge (> 1MB)**     | Shared Memory + MsgSend control | Zero copy essential                | Any copy-based method      |

### The Copy Cost Equation

```
┌──────────────────────────────────────────────────────────┐
│              Understanding Copy Costs                    │
│                                                          │
│  Method              │ Copies │ Context Switches │ Total │
│  ────────────────────┼────────┼──────────────────┼───────│
│  QNX MsgSend         │   1    │       2          │ Low   │
│  QNX MsgSendv (IOV)  │   1    │       2          │ Low   │
│  QNX Pulse           │   0    │       1          │ Min   │
│  Shared Memory       │   0    │       0*         │ Min   │
│  Pipe                │   2    │       4+         │ High  │
│  POSIX MQ            │   2    │       4+         │ High  │
│  TCP/IP (local)      │   2+   │       6+         │ High  │
│                                                          │
│  * After initial mmap setup; sync may require CS         │
└──────────────────────────────────────────────────────────┘
```

**Rule of thumb:**

- **< 1KB:** Use QNX Messages (copy cost negligible)
- **1KB - 100KB:** Use QNX Messages or MsgSendv (still efficient)
- **> 100KB:** Use Shared Memory + message-based synchronization

---

## 4. Consideration 3: Response Model

**Question:** Do you need a direct response? Can you afford to block?

### Blocking vs Non-Blocking Decision

```
┌─────────────────────────────────────────────────────────┐
│              Response Model Decision                    │
│                                                         │
│  Can the sender block waiting for a reply?              │
│                                                         │
│         YES                              NO             │
│          │                                │             │
│          ▼                                ▼             │
│  ┌───────────────┐              ┌────────────────┐      │
│  │  BLOCKING     │              │  NON-BLOCKING  │      │
│  │  (Synchronous)│              │ (Asynchronous) │      │
│  │               │              │                │      │
│  │  • MsgSend    │              │  • MsgSendPulse│      │
│  │  • MsgSendv   │              │  • Signals     │      │
│  │  • File I/O   │              │  • POSIX MQ    │      │
│  │               │              │  • TCP/IP      │      │
│  │  Built-in     │              │  • Shared Mem  │      │
│  │  synchronization│            │  (manual sync) │      │
│  └───────────────┘              └────────────────┘      │
│                                                         │
│  Use blocking when:        Use non-blocking when:       │
│  • Request/response model  • Fire-and-forget            │
│  • Need result back        • Notification only          │
│  • Real-time sync required • Sender must not wait       │
│  • Data consistency needed • High throughput events     │
└─────────────────────────────────────────────────────────┘
```

### When to Block (Synchronous)

| Scenario                        | Mechanism | Why                               |
| ------------------------------- | --------- | --------------------------------- |
| Client needs result from server | `MsgSend` | Natural RPC model                 |
| Real-time control loop          | `MsgSend` | Deterministic timing              |
| Data must be consistent         | `MsgSend` | Server processes, then replies    |
| Resource allocation             | `MsgSend` | Success/failure known immediately |
| Configuration read              | `MsgSend` | Value returned in reply           |

### When Not to Block (Asynchronous)

| Scenario           | Mechanism                  | Why                                 |
| ------------------ | -------------------------- | ----------------------------------- |
| Event notification | `MsgSendPulse`             | Sender continues immediately        |
| Interrupt handling | `MsgSendPulse`             | ISR must not block                  |
| Timer expiry       | `SIGEV_PULSE_INIT`         | Kernel delivers without blocking    |
| Status broadcast   | `MsgSendPulse`             | Multiple listeners, no reply needed |
| Logging            | `MsgSendPulse` or POSIX MQ | Don't delay main execution          |
| Heartbeat          | `MsgSendPulse`             | Quick, no response expected         |

### The Blocking Trap

```
┌─────────────────────────────────────────────────────────┐
│              The Blocking Trap to Avoid                 │
│                                                         │
│  BAD PATTERN:                                           │
│  ┌─────────┐                     ┌─────────┐            │
│  │ Client  │─MsgSend("do work")─▶│ Server  │            │
│  │ (blocks)│                     │ (slow)  │            │
│  │         │◀─────MsgReply────── │         │            │
│  │         │   (takes 5 seconds) │         │            │
│  └─────────┘                     └─────────┘            │
│                                                         │
│  Problem: Client is blocked for 5 seconds!              │
│  Can't do anything else. Misses deadlines.              │
│                                                         │
│  GOOD PATTERN:                                          │
│  ┌─────────┐                         ┌─────────┐        │
│  │ Client  │──MsgSend("start work")─▶│ Server  │        │
│  │         │◀─────"Accepted"──────── │         │        │
│  │ (free)  │                         │ (works) │        │
│  │         │◀──Pulse("done")─────    │         │        │
│  │         │──MsgSend("get result")─▶│         │        │
│  │         │◀─────Result──────────   │         │        │
│  └─────────┘                         └─────────┘        │
│                                                         │
│ Solution: Async start + pulse completion + fetch result │
└─────────────────────────────────────────────────────────┘
```

---

## 5. Consideration 4: Buffering Control

**Question:** Do you need control over the buffering scheme?

### Built-in vs Custom Buffering

| IPC Method               | Built-in Buffering            | Custom Buffering Possible | Memory Control |
| ------------------------ | ----------------------------- | ------------------------- | -------------- |
| **QNX Messages**         | Channel pulse queue           | Yes (server design)       | High           |
| **QNX Pulses**           | Channel pulse queue (limited) | No                        | Medium         |
| **POSIX Message Queues** | Kernel-managed queue          | Limited (queue size)      | Low            |
| **Pipes**                | Kernel-managed buffer         | Limited (buffer size)     | Low            |
| **TCP/IP Sockets**       | Kernel + stack buffers        | Limited                   | Low            |
| **Shared Memory**        | None (manual)                 | **Full control**          | **Highest**    |
| **Signals**              | None                          | No                        | N/A            |

### Why Custom Buffering Matters

```
┌─────────────────────────────────────────────────────────┐
│              Custom Buffering Use Cases                 │
│                                                         │
│  1. LOW MEMORY CONDITIONS                               │
│     • Pre-allocate all buffers at startup               │
│     • No dynamic allocation at runtime                  │
│     • Predictable memory footprint                      │
│     • → Shared Memory with custom ring buffer           │
│                                                         │
│  2. SAFETY-CRITICAL SYSTEMS                             │
│     • Must prove no memory leaks                        │
│     • Must prove bounded memory usage                   │
│     • Certifiable solutions required                    │
│     • → QNX Messages + pre-allocated pools              │
│     • → Shared Memory with static structures            │
│                                                         │
│  3. HIGH-THROUGHPUT STREAMING                           │
│     • Double-buffering or ring buffers                  │
│     • Lock-free queues                                  │
│     • Zero-copy between producer/consumer               │
│     • → Shared Memory with custom circular buffer       │
│                                                         │
│  4. DETERMINISTIC LATENCY                               │
│     • No hidden kernel allocations                      │
│     • No surprise queue overflows                       │
│     • Bounded worst-case behavior                       │
│     • → QNX Messages (channel queues are bounded)       │
│     • → Custom shared memory pools                      │
└─────────────────────────────────────────────────────────┘
```

### Safety Certification Considerations

| Requirement                | QNX Native     | POSIX Methods    |
| -------------------------- | -------------- | ---------------- |
| **Deterministic behavior** | ✅ Excellent    | ⚠️ Variable       |
| **Bounded memory**         | ✅ Configurable | ❌ Kernel-managed |
| **Priority inheritance**   | ✅ Built-in     | ❌ Not available  |
| **Safety certification**   | ✅ Available    | ❌ Rare           |
| **Pre-allocated buffers**  | ✅ Yes          | ⚠️ Limited        |

**Recommendation:** For safety-certified systems (automotive, medical, aerospace), prefer **QNX-native mechanisms** with explicit buffer management.

---

## 6. Consideration 5: Network Communication

**Question:** Do you need to communicate across a network?

### Local vs Remote Communication

```
┌─────────────────────────────────────────────────────────┐
│              Local vs Remote IPC Decision               │
│                                                         │
│  Is the peer on the same machine?                       │
│                                                         │
│         YES                              NO             │
│          │                                │             │
│          ▼                                ▼             │
│  ┌───────────────┐              ┌───────────────┐       │
│  │  LOCAL IPC    │              │  REMOTE IPC   │       │
│  │               │              │               │       │
│  │  • QNX Msg    │              │  • TCP/IP     │       │
│  │  • QNX Pulse  │              │  • UDP        │       │
│  │  • Shared Mem │              │  • QUIC       │       │
│  │  • File I/O   │              │  • gRPC       │       │
│  │  • POSIX MQ   │              │               │       │
│  │  • Pipes      │              │  Only choice  │       │
│  │               │              │  for remote!  │       │
│  │  Use QNX      │              │               │       │
│  │  native for   │              │               │       │
│  │  performance  │              │               │       │
│  └───────────────┘              └───────────────┘       │
│                                                         │
│  HYBRID: Service that accepts BOTH local and remote     │
│  ┌─────────┐                                            │
│  │ Service │──QNX Msg──▶ Local clients                  │
│  │         │──TCP/IP───▶ Remote clients                 │
│  └─────────┘                                            │
│  → Use TCP/IP for universal access                      │
│  → Accept performance penalty for remote capability     │
└─────────────────────────────────────────────────────────┘
```

### Network-Capable Methods

| Method             | Local     | Remote      | Performance | Use Case             |
| ------------------ | --------- | ----------- | ----------- | -------------------- |
| **TCP/IP Sockets** | ✅ Slow    | ✅ Yes       | Medium      | Universal access     |
| **UDP Sockets**    | ✅ Medium  | ✅ Yes       | Medium      | Streaming, multicast |
| **QNX Messages**   | ✅ Fast    | ❌ No        | Excellent   | Local only           |
| **QNX Pulses**     | ✅ Fast    | ❌ No        | Excellent   | Local only           |
| **Shared Memory**  | ✅ Fastest | ❌ No        | Excellent   | Local only           |
| **QNET**           | ✅ Fast    | ✅ Yes (QNX) | Good        | QNX-to-QNX remote    |

> **QNET:** QNX's native network protocol allows `MsgSend` across the network transparently. Only works QNX-to-QNX.

### When TCP/IP is the Right Choice

| Scenario              | Why TCP/IP              | Trade-off          |
| --------------------- | ----------------------- | ------------------ |
| Client may be remote  | Only method that works  | Slower locally     |
| Cross-platform client | Client on Linux/Windows | Protocol overhead  |
| Web integration       | HTTP/REST APIs          | Extra stack layers |
| Cloud connectivity    | MQTT, WebSocket         | Network latency    |
| Distributed system    | Multiple machines       | Complexity         |

---

## 7. Consideration 6: Hybrid Approaches

**Question:** Can I use a combination of mechanisms?

### The Hybrid Principle

> **"One style does not fit every application."**
>
> Most real-world QNX systems use **multiple IPC methods** for different parts of the architecture.

### Common Hybrid Patterns

```
┌─────────────────────────────────────────────────────────┐
│              Common Hybrid IPC Patterns                 │
│                                                         │
│  PATTERN 1: Control + Data                              │
│  ┌─────────┐                      ┌─────────┐           │
│  │ Client  │──MsgSend("display")─▶│ Server  │           │
│  │         │◀────"OK"─────────────│         │           │
│  │  ┌─────┐│                      │┌─────┐  │           │
│  │  │ SHM ││◀─────Image data────▶ ││ SHM │  │           │
│  │  └─────┘│                      │└─────┘  │           │
│  └─────────┘                      └─────────┘           │
│  Messages: Control and sync                             │
│  Shared Memory: Bulk data (zero copy)                   │
│                                                         │
│  PATTERN 2: Events + Requests                           │
│  ┌─────────┐                       ┌─────────┐          │
│  │ Driver  │──Pulse("data ready")─▶│ App     │          │
│  │         │                       │         │          │
│  │         │◀──MsgSend("get data")─│         │          │
│  │         │──MsgReply(data)────▶  │         │          │
│  └─────────┘                       └─────────┘          │
│  Pulses: Async notifications                            │
│  Messages: Data retrieval                               │
│                                                         │
│  PATTERN 3: Local + Remote                              │
│  ┌─────────┐                    ┌─────────┐             │
│  │ Local   │──QNX Msg──────────▶│ Service │             │
│  │ Client  │                    │         │             │
│  └─────────┘                    │         │             │
│  ┌─────────┐                    │         │             │
│  │ Remote  │──TCP/IP───────────▶│         │             │
│  │ Client  │                    │         │             │
│  └─────────┘                    └─────────┘             │
│  QNX Messages: Fast local path                          │
│  TCP/IP: Universal remote path                          │
│                                                         │
│  PATTERN 4: Real-time + Background                      │
│  ┌─────────┐                    ┌─────────┐             │
│  │ RT Task │──MsgSend──────────▶│ RT Srv  │             │
│  │         │                    │         │             │
│  │ Bg Task │──POSIX MQ─────────▶│ Bg Srv  │             │
│  │         │                    │         │             │
│  └─────────┘                    └─────────┘             │
│  QNX Messages: Real-time critical path                  │
│  POSIX MQ: Background logging (portable)                │
└─────────────────────────────────────────────────────────┘
```

### Real-World System Example

```
┌─────────────────────────────────────────────────────────┐
│         Automotive Infotainment System IPC Layout       │
│                                                         │
│  HMI (UI Process)                                       │
│     │                                                   │
│     ├──MsgSend──▶ Navigation Server (route calc)        │
│     │              └──MsgReply (route data)             │
│     │                                                   │
│     ├──MsgSend──▶ Media Server (play/pause)             │
│     │              └──MsgReply (status)                 │
│     │                                                   │
│     ├──MsgSend──▶ Vehicle Telemetry (get speed)         │
│     │              └──MsgReply (speed value)            │
│     │                                                   │
│     └──MsgSend──▶ Graphics Server ("render frame 5")    │
│                    └──Shared Memory (frame buffer)      │
│                                                         │
│  GPS Driver                                             │
│     └──Pulse──▶ Navigation Server ("new position")      │
│                                                         │
│  CAN Driver                                             │
│     └──Pulse──▶ Vehicle Telemetry ("new vehicle data")  │
│                                                         │
│  Touchscreen Interrupt                                  │
│     └──Pulse──▶ HMI ("touch event at x,y")              │
│                                                         │
│  Cloud Service                                          │
│     └──TCP/IP──▶ Remote server (traffic updates)        │
│                                                         │
│  IPC Methods Used: 4 different mechanisms!              │
│  • QNX Messages: Control and sync                       │
│  • QNX Pulses: Hardware events                          │
│  • Shared Memory: Graphics frames                       │
│  • TCP/IP: Cloud connectivity                           │
└─────────────────────────────────────────────────────────┘
```

---

## 8. Complete Decision Matrix

### The Master Decision Table

| Requirement              | QNX Msg | Pulse | SHM  | Signal | Pipe | PosMQ | TCP/IP | File I/O |
| ------------------------ | ------- | ----- | ---- | ------ | ---- | ----- | ------ | -------- |
| **POSIX required**       | ❌       | ❌     | ✅    | ✅      | ✅    | ✅     | ✅      | ✅        |
| **Tiny data (< 8B)**     | ⚠️       | ✅     | ❌    | ✅      | ❌    | ⚠️     | ❌      | ❌        |
| **Small data (< 1KB)**   | ✅       | ⚠️     | ❌    | ⚠️      | ⚠️    | ✅     | ⚠️      | ✅        |
| **Large data (> 100KB)** | ❌       | ❌     | ✅    | ❌      | ❌    | ❌     | ⚠️      | ⚠️        |
| **Need response**        | ✅       | ❌     | ❌    | ❌      | ✅    | ✅     | ✅      | ✅        |
| **Fire-and-forget**      | ❌       | ✅     | ⚠️    | ✅      | ✅    | ✅     | ✅      | ❌        |
| **Real-time**            | ✅       | ✅     | ✅*   | ❌      | ❌    | ❌     | ❌      | ✅        |
| **Priority inheritance** | ✅       | ✅     | ❌    | ❌      | ❌    | ❌     | ❌      | ✅        |
| **Safety certifiable**   | ✅       | ✅     | ✅*   | ❌      | ❌    | ❌     | ❌      | ✅        |
| **Custom buffering**     | ✅       | ⚠️     | ✅    | ❌      | ❌    | ❌     | ❌      | ⚠️        |
| **Cross-network**        | ❌       | ❌     | ❌    | ❌      | ❌    | ❌     | ✅      | ❌        |
| **Zero copy**            | ❌       | ✅     | ✅    | ✅      | ❌    | ❌     | ❌      | ❌        |
| **Low latency**          | ✅       | ✅     | ✅    | ⚠️      | ❌    | ❌     | ❌      | ✅        |
| **Porting Unix code**    | ❌       | ❌     | ✅    | ✅      | ✅    | ✅     | ✅      | ✅        |

*With manual real-time synchronization (robust mutexes)

### The Decision Flowchart

```
START: Choose IPC for QNX application
│
├─ Is POSIX compliance required?
│  ├─ YES → Use POSIX methods (SHM, Signals, Pipes, MQ, TCP/IP, File I/O)
│  │        └─ Is remote communication needed? → TCP/IP
│  │        └─ Is large data moved? → Shared Memory
│  │        └─ Is it simple notification? → Signals
│  │        └─ Otherwise → File I/O or POSIX MQ
│  │
│  └─ NO → Use QNX native methods (recommended)
│         │
│         ├─ Is it notification only (no response)?
│         │  ├─ YES → Use Pulses
│         │  └─ NO → Continue...
│         │
│         ├─ Is large data (> 100KB) being transferred?
│         │  ├─ YES → Shared Memory + MsgSend for control
│         │  └─ NO → Continue...
│         │
│         ├─ Is real-time response critical?
│         │  ├─ YES → MsgSend (priority inheritance)
│         │  └─ NO → MsgSend or File I/O
│         │
│         ├─ Need custom buffering / safety cert?
│         │  ├─ YES → MsgSend with pre-allocated pools
│         │  └─ NO → MsgSend (standard)
│         │
│         └─ Is this a driver/resource manager?
│            ├─ YES → File Descriptors (POSIX client, QNX server)
│            └─ NO → MsgSend
│
└─ Can I combine multiple methods?
   └─ YES → Design hybrid architecture
      • Messages for control
      • Pulses for events
      • Shared Memory for bulk data
      • TCP/IP for remote access
```

---

## 9. Key Takeaways

### The Six Considerations Summary

| #    | Consideration             | Key Question                       | Drives Choice Toward                              |
| ---- | ------------------------- | ---------------------------------- | ------------------------------------------------- |
| 1    | **POSIX Compliance**      | "Must it run on Linux too?"        | POSIX APIs if yes; QNX native if no               |
| 2    | **Data Volume**           | "How much data per transfer?"      | SHM for large; MsgSend for small; Pulse for tiny  |
| 3    | **Response Model**        | "Can I block for a reply?"         | MsgSend if yes; Pulse if no                       |
| 4    | **Buffering Control**     | "Do I need pre-allocated buffers?" | QNX native + custom pools if yes                  |
| 5    | **Network Communication** | "Is the peer on another machine?"  | TCP/IP if yes; QNX native if no                   |
| 6    | **Hybrid Approach**       | "Can I use multiple methods?"      | **Yes** — design for the right tool per subsystem |

### The Selection Priority List

```
┌─────────────────────────────────────────────────────────┐
│              QNX IPC Selection Priority                 │
│                                                         │
│  1. QNX Messages (MsgSend/MsgReceive/MsgReply)          │
│     → Default choice for local IPC                      │
│     → Best real-time behavior                           │
│     → Built-in sync and priority inheritance            │
│                                                         │
│  2. QNX Pulses (MsgSendPulse)                           │
│     → Notifications, events, interrupts                 │
│     → Non-blocking, no deadlock                         │
│                                                         │
│  3. Shared Memory + QNX Messages                        │
│     → Large data with message-based control             │
│     → Zero copy for bulk transfer                       │
│                                                         │
│  4. File Descriptors (Resource Managers)                │
│     → Device drivers, POSIX-compatible APIs             │
│     → Same performance as QNX messages                  │
│                                                         │
│  5. POSIX Methods (for portability only)                │
│     → Signals, Pipes, POSIX MQ, TCP/IP                  │
│     → Use when code must run on multiple OSes           │
│                                                         │
│  AVOID for performance-critical local IPC:              │
│  • Pipes (slow, extra process)                          │
│  • POSIX Message Queues (slow, extra process)           │
│  • Local TCP/IP (slow, extra process)                   │
└─────────────────────────────────────────────────────────┘
```

### Final Rules

| Rule                                 | Explanation                                           |
| ------------------------------------ | ----------------------------------------------------- |
| **Default to QNX native**            | Unless POSIX is required, use MsgSend/Pulse           |
| **Use the right tool per subsystem** | Don't force one method everywhere                     |
| **Combine for best results**         | Messages + SHM is the canonical pattern               |
| **Measure before optimizing**        | Profile actual data sizes and frequencies             |
| **Consider safety early**            | QNX native is certifiable; POSIX often is not         |
| **Plan for growth**                  | Today's local service may need remote access tomorrow |

---

## Related Documentation

- **QNX IPC Methods Detailed Comparison** — Full API comparison of all methods
- **QNX IPC Methods Comparison Overview** — Quick-reference comparison tables
- **Part 1-6:** Individual deep-dives into Message Passing, Deadlock Avoidance, Server Designs, Multi-Part Messages, Event Delivery, and Shared Memory
- **QNX Official:** [Interprocess Communication](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/ipc.html)
