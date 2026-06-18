
# QNX System Library

## Overview

The QNX System Library is the interface layer between application code and the underlying QNX operating system. It contains two categories of functions: **QNX-specific functions** designed for the microkernel architecture, and **standard POSIX/Unix functions** that are portable across operating systems. Understanding how these functions map to kernel calls, and when to use each type, is essential for writing efficient, maintainable, and portable QNX applications.

---

## Two Types of Library Functions

The QNX library is organized around two distinct categories:

| Category | Purpose | Examples |
|----------|---------|----------|
| **Standard POSIX/Unix Functions** | Portable across operating systems; familiar to developers | `timer_settime()`, `fork()`, `read()`, `open()` |
| **QNX-Specific Functions** | Direct access to QNX microkernel features; non-portable | `TimerSettime()`, `MsgSend()`, `ThreadCtl()` |

The standard functions are essentially wrappers — they provide a familiar interface and internally convert to QNX kernel calls. The QNX-specific functions give direct access to kernel features but tie your code to the QNX platform.

---

## Kernel Call Naming Convention

QNX kernel calls follow a specific naming pattern that distinguishes them from standard library functions:

| Library Type | Naming Style | Example |
|-------------|-------------|---------|
| Standard POSIX | Snake case (lowercase with underscores) | `timer_settime()` |
| QNX Kernel Call | Camel case with leading capital | `TimerSettime()` |

The capitalization is the visual cue. When you see a function with a leading capital letter and camel case formatting, you're looking at a direct kernel call.

```
┌─────────────────────────────────────────────────────────────────────┐
│              FUNCTION NAMING CONVENTIONS                               │
│                                                                      │
│  Standard POSIX Call          QNX Kernel Call                        │
│  ───────────────────          ───────────────                        │
│                                                                      │
│  ┌─────────────────┐        ┌─────────────────┐                    │
│  │ timer_settime() │        │ TimerSettime()  │                    │
│  │ snake_case      │        │ CamelCase       │                    │
│  │ lowercase       │        │ Leading capital │                    │
│  │ underscores     │        │ no underscores  │                    │
│  └─────────────────┘        └─────────────────┘                    │
│           │                          │                              │
│           │ calls                    │ direct kernel entry          │
│           ▼                          ▼                              │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │              QNX NEUTRINO MICROKERNEL                        │     │
│  │                                                             │     │
│  │  • Thread scheduling        • IPC mechanisms                │     │
│  │  • Timer management         • Interrupt handling            │     │
│  │  • Synchronization          • Memory management             │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                                                                      │
│  The POSIX call is a THIN LAYER that converts arguments and calls   │
│  the kernel function. The kernel call enters the microkernel        │
│  directly with elevated privileges.                                  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## How Standard Functions Map to Kernel Calls

Most standard library functions in QNX are built as thin wrappers over kernel calls. The wrapper typically performs argument format conversion and then invokes the underlying kernel function.

**Example — Timer Functions:**

The POSIX function `timer_settime()` accepts time values in seconds and nanoseconds (a `struct timespec` with two fields). Before calling the kernel, it converts this representation into the kernel's native 64-bit nanosecond format. Then it calls `TimerSettime()` to enter the microkernel and configure the timer.

This conversion is minimal overhead — just reformatting data — but it provides the familiar POSIX interface that developers recognize from Linux, Unix, and other systems.

**Example — Process Creation:**

The POSIX function `fork()` to create a new process doesn't directly enter the kernel to duplicate the process. Instead, it builds a message describing the fork request and sends that message to the **Process Manager** via `MsgSend()`. The Process Manager (running as part of procnto) handles the actual process creation, memory duplication, and thread setup. The `fork()` wrapper hides all this message-passing complexity from the application developer.

**Example — File Operations:**

The standard `read()` function on a file descriptor doesn't make a kernel call to read bytes. Instead, it constructs a message containing the read request (file descriptor, buffer size, offset) and sends it via `MsgSend()` to the **resource manager** that owns that file descriptor — perhaps a disk filesystem, a serial port driver, or a network filesystem. The resource manager processes the request and replies with the data. The `read()` wrapper handles building the message, sending it, waiting for the reply, and copying the returned data into the user's buffer.

---

## The Thin Wrapper Principle

The layer between standard calls and kernel calls is intentionally thin. It doesn't add heavy logic or complex processing — it primarily handles:

| Wrapper Task | Description | Example |
|-------------|-------------|---------|
| **Argument conversion** | Reformat data structures | Convert `struct timespec` to 64-bit nanoseconds |
| **Multiple calls funneling** | Several POSIX functions map to one kernel call | Various timer functions all call `TimerSettime()` with different setup |
| **Message building** | Construct IPC messages for resource managers | `read()` builds `_IO_READ` message for resource manager |
| **Error code translation** | Convert kernel errors to POSIX errno values | Map QNX-specific error to standard `EAGAIN`, `EINVAL`, etc. |

This thinness is important for performance. The wrapper adds minimal overhead while providing maximum portability and familiarity.

---

## Recommendation: Prefer Standard Portable Calls

When you have a choice between a standard POSIX function and its QNX-specific kernel equivalent, **use the standard call**. This recommendation applies even if you never intend to port your code away from QNX.

### Portability Benefits

Code using standard POSIX calls can run on QNX, Linux, BSD, macOS, and other Unix-like systems without modification. This is valuable for:

- **Early development and testing** on Linux workstations before QNX hardware is available
- **Porting existing code** from other platforms to QNX
- **Future-proofing** against platform changes (even if you believe QNX is the best choice)

### Maintainability Benefits

Even within QNX development teams, standard calls are easier to work with:

- **Code reviews** are faster — reviewers recognize `timer_settime()` immediately; they must look up `TimerSettime()` in documentation
- **Maintenance** is simpler — developers familiar with POSIX don't need QNX-specific training to understand the code
- **Documentation** is richer — POSIX functions have extensive online resources, examples, and community knowledge
- **Collaboration** is smoother — team members from Unix/Linux backgrounds contribute immediately

The QNX-specific kernel calls require specialized knowledge. Every developer reading, maintaining, or reviewing code that uses `ThreadCtl()`, `MsgSendPulse()`, or `InterruptAttach()` must understand QNX microkernel semantics. This creates friction in team environments and increases long-term maintenance costs.

```
┌─────────────────────────────────────────────────────────────────────┐
│              STANDARD CALLS vs. QNX-SPECIFIC CALLS                     │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              PREFER STANDARD POSIX CALLS                      │   │
│  │                                                             │   │
│  │  ┌─────────────────┐    ┌─────────────────┐              │   │
│  │  │ timer_settime() │    │ fork()          │              │   │
│  │  │ read()          │    │ open()          │              │   │
│  │  │ write()         │    │ pthread_create()│              │   │
│  │  │ select()        │    │ nanosleep()     │              │   │
│  │  └─────────────────┘    └─────────────────┘              │   │
│  │                                                             │   │
│  │  Benefits:                                                  │   │
│  │  • Portable across QNX, Linux, BSD, macOS                  │   │
│  │  • Familiar to most developers                             │   │
│  │  • Extensive documentation and examples                    │   │
│  │  • Easier code reviews and maintenance                     │   │
│  │  • Faster onboarding for new team members                  │   │
│  │                                                             │   │
│  │  Thin wrapper converts to kernel calls automatically.       │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              ▲                                       │
│                              │ use when possible                      │
│                              │                                        │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │         USE QNX-SPECIFIC CALLS ONLY WHEN NECESSARY           │   │
│  │                                                             │   │
│  │  ┌─────────────────┐    ┌─────────────────┐              │   │
│  │  │ TimerSettime()  │    │ MsgSend()       │              │   │
│  │  │ ThreadCtl()     │    │ MsgReceive()    │              │   │
│  │  │ InterruptAttach()│   │ ChannelCreate() │              │   │
│  │  │ procmgr_ability()│   │ ConnectAttach() │              │   │
│  │  └─────────────────┘    └─────────────────┘              │   │
│  │                                                             │   │
│  │  Use when:                                                  │   │
│  │  • No POSIX equivalent exists                              │   │
│  │  • Direct kernel control is required                       │   │
│  │  • Performance-critical microkernel operations               │   │
│  │  • Accessing QNX-specific features (clusters, abilities)   │   │
│  │                                                             │   │
│  │  Requires QNX-specific knowledge. Code is NOT portable.     │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Rule of thumb: If a standard call does what you need, use it.       │
│  Only drop to kernel calls when the standard layer doesn't expose   │
│  the functionality you require.                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## How POSIX Functions Use Message Passing

A core principle of QNX's microkernel design is that operations that would be system calls in a monolithic kernel become **message passing operations** in QNX. The standard library functions handle all this message construction transparently.

### The Pattern: Build Message → Send to Server → Wait for Reply

Most POSIX functions that interact with system resources follow this pattern. The library function constructs a message in the QNX message format, identifies the appropriate server (resource manager or Process Manager), sends the message via `MsgSend()`, and returns the reply to the caller.

**Example — `read()` on a File:**

When an application calls `read(fd, buffer, 1024)`, the library doesn't read from disk directly. Instead:

1. The file descriptor `fd` was created during `open()` and contains the connection information to the resource manager that handles that file
2. The library builds an `_IO_READ` message containing the file descriptor, requested byte count, and current file offset
3. It calls `MsgSend()` to send this message to the resource manager's channel
4. The resource manager receives the message, reads data from its backing store (disk, network, hardware), and places the data in a reply message
5. The library receives the reply, copies the data into the application's `buffer`, and returns the byte count

The application sees a standard `read()` call. Underneath, it's a full client-server message exchange.

**Example — `fork()` to Create a Process:**

When an application calls `fork()`, the library doesn't directly manipulate process tables. Instead:

1. The library builds a message describing the fork request, including the parent's process attributes, memory mappings, and file descriptors
2. It sends this message to the **Process Manager** (part of procnto)
3. The Process Manager creates the new process structure, allocates a new PID, duplicates the address space (using copy-on-write optimization), and creates the initial thread
4. The Process Manager replies with the new PID to the parent, and also arranges for the child process to start executing
5. The library returns the PID to the parent application

The complexity of process creation is entirely handled by the Process Manager. The `fork()` wrapper just packages the request and delivers it.

**Example — `open()` on a Device:**

When an application calls `open("/dev/ser1", O_RDWR)`, the library performs a multi-step message exchange:

1. First, it sends a message to the **Process Manager** asking: *"Which resource manager handles `/dev/ser1`?"*
2. The Process Manager looks up its registered path prefixes and replies with the process ID and channel ID of `devc-ser8250`
3. The library then sends an `_IO_CONNECT` message to that resource manager, including the pathname, open flags, and access mode
4. The resource manager checks permissions, initializes hardware if needed, and allocates internal tracking structures
5. The resource manager replies with a success code and connection details
6. The library creates a file descriptor containing this connection and returns it to the application

All subsequent `read()` and `write()` calls on that file descriptor go directly to the resource manager, bypassing the Process Manager entirely.

```
┌─────────────────────────────────────────────────────────────────────┐
│           POSIX FUNCTIONS AS MESSAGE PASSING WRAPPERS                  │
│                                                                      │
│  In monolithic kernels:  syscall → kernel handles everything          │
│  In QNX microkernel:     library call → build message → send to server │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              EXAMPLE: read() OPERATION                       │   │
│  │                                                             │   │
│  │  Application calls: read(fd, buffer, 1024)                  │   │
│  │                                                             │   │
│  │         ┌─────────┐                                         │   │
│  │         │  C      │  1. Build _IO_READ message            │   │
│  │         │ Library │     • fd identifier                    │   │
│  │         │         │     • requested bytes (1024)           │   │
│  │         │         │     • current offset                   │   │
│  │         │         │                                         │   │
│  │         │         │  2. Call MsgSend() to resource manager  │   │
│  │         │         │     (connection established at open())   │   │
│  │         │         │                                         │   │
│  │         │         │  3. Wait for reply (blocking call)      │   │
│  │         │         │                                         │   │
│  │         │         │  4. Copy reply data to buffer           │   │
│  │         │         │                                         │   │
│  │         │         │  5. Return bytes read or error          │   │
│  │         └────┬────┘                                         │   │
│  │              │                                              │   │
│  │              ▼ MsgSend()                                   │   │
│  │  ┌─────────────────────────────────────────────────────┐    │   │
│  │  │              RESOURCE MANAGER                        │    │   │
│  │  │  (disk fs, serial driver, network fs, etc.)        │    │   │
│  │  │                                                     │    │   │
│  │  │  Receives _IO_READ message:                         │    │   │
│  │  │  • Read from disk / hardware / network              │    │   │
│  │  │  • Place data in reply message                      │    │   │
│  │  │  • Send reply back to client                         │    │   │
│  │  └─────────────────────────────────────────────────────┘    │   │
│  │              │                                              │   │
│  │              ▼ Reply                                        │   │
│  │         ┌─────────┐                                         │   │
│  │         │  C      │  Returns: bytes read or errno          │   │
│  │         │ Library │                                         │   │
│  │         └────┬────┘                                         │   │
│  │              │                                              │   │
│  │              ▼                                              │   │
│  │         ┌─────────┐                                         │   │
│  │         │Application│  Continues with data in buffer       │   │
│  │         │         │                                         │   │
│  │         └─────────┘                                         │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  The application sees a STANDARD POSIX CALL.                         │
│  Underneath is QNX MESSAGE PASSING to a resource manager.            │
│  The library hides ALL complexity of message construction.            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Strong Recommendation: Never Build Messages Manually

Just as you should prefer standard POSIX calls over direct kernel calls, you should **strongly prefer standard library functions over manually constructing and sending messages**.

### Why Manual Message Building Is Discouraged

The QNX message format is internal and subject to change between versions. The library functions are maintained by QNX and will adapt to any protocol changes. If you manually build messages:

- Your code breaks when QNX updates the message format
- You must track internal implementation details that the library abstracts
- Your code becomes harder to understand and maintain
- You lose the benefit of standardized error handling and edge case management

The library functions handle all the nuances: message size limits, version negotiation, error code translation, retry logic for interrupted operations, and proper cleanup on failure.

### The Parallel Principle

This recommendation mirrors the kernel call guidance:

| Level | Preferred Approach | Discouraged Approach |
|-------|-------------------|----------------------|
| **Highest** | Standard POSIX functions (`read()`, `open()`, `timer_settime()`) | — |
| **Middle** | QNX-specific cover functions (`MsgSend()`, `TimerSettime()`) when no POSIX equivalent exists | — |
| **Lowest** | — | Manually building `_IO_READ`, `_IO_CONNECT`, or other internal messages |

Always work at the highest level of abstraction that meets your needs. Drop to lower levels only when the higher level genuinely cannot do what you require.

---

## When to Use QNX-Specific Calls

Despite the general recommendation for standard calls, there are legitimate scenarios where QNX-specific functions are necessary:

| Scenario | Example | Why Standard Calls Don't Suffice |
|----------|---------|----------------------------------|
| **Direct IPC control** | `MsgSend()`, `MsgReceive()`, `ChannelCreate()` | POSIX doesn't expose message-passing primitives |
| **Thread affinity** | `ThreadCtl(_NTO_TCTL_RUNMASK, ...)` | POSIX threads don't support cluster-based CPU binding |
| **Interrupt handling** | `InterruptAttach()`, `InterruptAttachEvent()` | POSIX signal handling is too coarse for hardware interrupts |
| **Process abilities** | `procmgr_ability()` | POSIX capabilities model differs from QNX's ability system |
| **Kernel object control** | `TimerCreate()` with QNX-specific flags | POSIX timers lack some real-time features |
| **Performance-critical paths** | Direct `MsgSend()` in tight loops | Library wrapper overhead matters in microsecond-scale loops |

Even in these cases, consider whether a higher-level QNX library function exists before using the raw kernel call. For example, the QNX resource manager framework provides higher-level patterns for message handling that are preferable to raw `MsgSend()`/`MsgReceive()` in most driver code.

---

## Summary

The QNX System Library serves as the bridge between portable application code and the QNX microkernel. It provides standard POSIX functions that developers recognize, while internally converting these to QNX kernel calls and message-passing operations.

Key principles to remember:

| Principle | Guidance |
|-----------|----------|
| **Prefer standard POSIX calls** | Use `timer_settime()`, `fork()`, `read()` when possible |
| **Recognize kernel calls by naming** | Leading capital + CamelCase = direct kernel entry (`TimerSettime()`) |
| **Understand the thin wrapper** | Standard calls reformat arguments and call kernel equivalents |
| **Message passing is hidden** | `read()`, `open()`, `fork()` all build and send messages internally |
| **Never build messages manually** | Always use library functions; don't construct raw `_IO_*` messages |
| **Use QNX calls only when necessary** | Direct IPC, interrupt handling, thread affinity, and QNX-specific features |

By following these guidelines, you write code that is portable, maintainable, and aligned with QNX best practices — while still having access to the full power of the microkernel when your application truly needs it.

---

*Happy learning!* 🚀
