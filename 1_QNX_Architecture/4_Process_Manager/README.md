
# QNX Process Manager

## Overview

The **Process Manager** is one of the two core components of **procnto** (the other being the Neutrino microkernel). While both share the same kernel address space, they are fundamentally different in behavior and purpose. The Process Manager is the bridge between user applications and system resources — handling process lifecycle, memory management, path namespace resolution, and system state notifications. Unlike the microkernel which is reached via direct kernel calls, the Process Manager is accessed by applications through message passing.

---

## Relationship with procnto

| Component | Name Origin | Access Method | Primary Role |
|-----------|-------------|---------------|--------------|
| **Process Manager** | `proc` | Message passing | Process lifecycle, memory, namespace |
| **Neutrino Microkernel** | `nto` | Kernel calls (direct function calls) | Scheduling, IPC, interrupts, synchronization |

When you see `procnto` in an Image File System (IFS), it represents the combined entity of both components running as the first process in the system (PID 1).

**Key Distinction:** Applications talk to the Process Manager through messages, even when those messages are hidden beneath standard C library APIs like file open operations.

---

## Core Responsibilities

### 1. Process Lifecycle Management

The Process Manager handles all aspects of process creation and termination.

**Process Creation Methods:**

| Method | Description | Example Scenario |
|--------|-------------|----------------|
| **Spawn** | Create new process with explicit parameters | Launching a new application with specific memory limits and security attributes |
| **Fork** | Create child process as copy of parent | A shell spawning a command — child inherits parent's environment |
| **Exec** | Replace current process with new program | A login shell replacing itself with the user's chosen application |
| **Load from Disk/Flash** | Load executable from storage | Booting an application from a QNX Image File System (IFS) |

**Example — Boot Sequence Process Creation:**

1. System boots → procnto starts as PID 1
2. Process Manager creates initial system processes:
   - Console driver (`devc-con`) for terminal I/O
   - Disk driver (`devb-eide`) for storage access
   - Network manager (`io-sock`) for network stack
3. Each process receives its own virtual address space
4. Process Manager assigns memory blocks and establishes protection boundaries

**Example — Application Launch:**

A user requests to start a media player application:
- Shell sends message to Process Manager to spawn the process
- Process Manager allocates new process structure and initial thread
- Memory space is created and protected from other processes
- Executable is loaded from disk (via disk driver) into the new address space
- Initial thread is created at specified priority
- Process begins execution independently

---

### 2. Memory Management

The Process Manager is responsible for all memory allocation and virtual address space management.

#### Virtual Address Space Model

Every process has a **unique virtual address space** — its private view of the system memory. This is not the entire physical memory, but only what that process is allowed to access.

**What Exists in a Process's Virtual Address Space:**

| Component | Source | Description |
|-----------|--------|-------------|
| **Code Segment** | Loaded from executable | The program instructions |
| **Data Segment** | Loaded from executable / allocated at runtime | Global and static variables |
| **Stack** | Automatically allocated | Function call frames and local variables |
| **Heap** | Dynamically allocated | Memory obtained via allocation requests |
| **Shared Memory Blocks** | Explicitly mapped | Memory shared between multiple processes |
| **Memory-Mapped Hardware** | Explicitly mapped | Physical hardware registers mapped into process space |

**Example — Process Memory Layout:**

Consider a typical application process:

- **Code Region:** Contains the executable instructions, loaded from disk at process creation
- **Data Region:** Contains initialized and uninitialized global variables
- **Stack Region:** Grows downward as functions are called; contains return addresses and local variables
- **Heap Region:** Grows upward as dynamic memory is allocated; managed by C library but backed by Process Manager blocks
- **Shared Library Mappings:** Code and data for shared libraries mapped into the address space

**Example — Memory Fragmentation Handling:**

A system has been running for some time and physical memory is fragmented:
- Process A requests a 10MB contiguous block
- Physical memory has no single 10MB free region, but has multiple smaller free blocks
- Process Manager allocates multiple physical blocks
- Maps them into Process A's virtual address space as one contiguous 10MB region
- Process A sees a single continuous block; physical layout is invisible and irrelevant

**Example — Hardware Memory Mapping:**

An embedded system needs to access a hardware device:
- Hardware registers sit at physical address `0xE000_0000`
- Application cannot directly access physical addresses (security/protection)
- Application requests Process Manager to map this region
- Process Manager allocates virtual addresses in the process's space
- Sets up Memory Management Unit (MMU) translation from virtual to physical address
- Application receives a pointer to the virtual address — hardware appears to be in its own memory space

#### Shared Memory Management

The Process Manager coordinates memory sharing between processes.

**Example — Producer-Consumer Shared Buffer:**

A video capture application and a video encoder need to share frame data:
- Process Manager allocates a physical memory block for frame buffers
- Maps this block into the capture process's virtual address space at address `0x1000_0000`
- Maps the **same physical block** into the encoder process's virtual address space at address `0x2000_0000`
- Both processes access identical physical memory through different virtual addresses
- No data copying required — zero-copy data transfer

**Example — Shared Configuration Space:**

Multiple applications read system configuration:
- Configuration database stored in shared memory block
- Process Manager maps block into each application that opens the configuration
- All applications see consistent, up-to-date values
- One application updates a value; others see change immediately

#### Kernel vs. User Address Space Separation

The microkernel maintains its own address space separate from user processes.

**Address Space Layout:**

| Region | Boundary | Contents |
|--------|----------|----------|
| **User Space** | Below 512 GB (architecture-dependent) | Application memory, shared memory, mapped hardware |
| **Kernel Space** | Above 512 GB | Microkernel code, data, kernel objects, procnto components |

**Example — Kernel Call Efficiency:**

An application makes a kernel call (e.g., message send):
- Application runs in user space with user privileges
- Kernel call transitions to kernel privilege level
- Kernel address space becomes visible (already mapped, no MMU switch needed)
- Kernel call executes using kernel stack
- Return to user space restores original privileges and stack
- **No MMU reconfiguration required** — faster than full context switch

**Example — Process Context Switch:**

A scheduler switches from Process A to Process B:
- Both are user processes
- MMU must be reconfigured to map Process B's physical pages
- Full address space switch occurs
- More expensive than kernel call but necessary for isolation

---

### 3. Path Namespace Management

This is one of QNX's most distinctive features. The Process Manager controls the entire path namespace, integrating devices, files, and resources into a unified directory tree.

#### Traditional vs. QNX Namespace

**Traditional Operating Systems (Linux, Windows):**
- Filesystem mount points bind storage devices to directory tree
- Device access through special system calls or device files
- Device drivers live in kernel space
- Namespace is relatively static after boot

**QNX Namespace:**
- Everything is accessed through path names
- Device drivers are user-space processes (resource managers)
- Process Manager dynamically resolves paths to appropriate resource managers
- Namespace is highly dynamic — resource managers can register and unregister at any time

**Example — Empty Root at Boot:**

Immediately after procnto starts:
- Namespace contains only `/` (the root)
- Nothing else exists yet
- No devices, no files, no directories

**Example — Proc Filesystem Registration:**

Process Manager creates `/proc` as one of its first actions:
- `/proc` is a pseudo-filesystem — not backed by disk storage
- When a user lists `/proc`, the Process Manager intercepts the request
- Forwards to the internal resource manager that handles process information
- Resource manager returns list of running process IDs as "files"
- User sees directories named with process IDs (e.g., `/proc/1`, `/proc/1024`)
- Reading `/proc/1024/as` shows the address space of process 1024

**Example — Shared Memory View (`/dev/shmem`):**

The Process Manager creates `/dev/shmem`:
- Lists all shared memory objects in the system
- Each shared memory block appears as a named "file"
- Applications can open these to attach to existing shared memory
- Process Manager handles the mapping when an application opens `/dev/shmem/my_buffer`

**Example — Semaphore View (`/dev/sem`):**

The Process Manager creates `/dev/sem`:
- Lists all named semaphores in the system
- Applications can create or open semaphores by path name
- Process Manager coordinates semaphore creation and access control

---

### 4. Path Resolution and Resource Manager Integration

The Process Manager resolves path names by matching them against registered resource managers, always choosing the most specific (most granular) match.

**Example — Namespace Resolution Scenario:**

Consider this registered resource manager tree:

```
/                          (procnto - Process Manager itself)
├── /proc                  (procnto - process information)
├── /dev/
│   ├── /dev/con1          (devc-con - console driver)
│   ├── /dev/ser1          (devc-ser8250 - serial driver)
│   └── /dev/ser2          (devc-ser8250 - serial driver)
└── /etc/                  (devb-eide - disk driver, fallback)
```

**Scenario 1 — Accessing Console Device:**

An application opens `/dev/con1`:
1. Process Manager receives the open request
2. Examines registered path prefixes
3. Finds exact match: `devc-con` registered for `/dev/con1`
4. Redirects request to `devc-con` process
5. Console driver handles the open, read, write operations
6. Application reads keyboard input and writes screen output through this path

**Scenario 2 — Accessing Serial Port:**

An application opens `/dev/ser1`:
1. Process Manager receives the request
2. Finds exact match: `devc-ser8250` registered for `/dev/ser1`
3. Redirects to serial driver process
4. Serial driver configures baud rate, data bits, stop bits
5. Application communicates with external device through serial port

**Scenario 3 — Accessing Disk File:**

An application opens `/etc/passwd`:
1. Process Manager receives the request
2. Checks for exact matches — none found for `/etc/passwd`
3. Checks for prefix matches:
   - `/dev/con1` — no, `/etc` doesn't start with `/dev/con1`
   - `/dev/ser1` — no
   - `/dev/ser2` — no
   - `/proc` — no
   - `/` (procnto root) — yes, but not most specific
4. Falls back to longest matching prefix: `/` handled by `devb-eide` (disk driver)
5. Redirects to disk driver
6. Disk driver reads filesystem structure, locates file on disk
7. Returns file handle to application

**Scenario 4 — Accessing Root Directory:**

A user runs `ls /`:
1. Process Manager handles the request (procnto registered for `/`)
2. Returns combined view of all registered path prefixes
3. User sees: `proc`, `dev`, `etc` (and any other registered paths)
4. This is a unified view — actually served by multiple resource managers

**Scenario 5 — Dynamic Driver Loading:**

A USB device is plugged in:
1. USB stack detects new device (mass storage class)
2. USB stack starts new resource manager process for this device
3. Resource manager registers `/dev/hd1` with Process Manager
4. User immediately sees `/dev/hd1` in namespace
5. User can mount or access it without system reboot
6. Device is unplugged — resource manager unregisters, path disappears

---

### 5. System State and Notifications

The Process Manager provides system-wide state information and change notifications.

**Example — Process State Queries:**

A monitoring application needs to track system health:
- Reads `/proc` to enumerate all processes
- Reads `/proc/1024/status` to get thread states, memory usage
- Reads `/proc/1024/fd` to see open file descriptors
- Process Manager generates this information dynamically from internal data structures

**Example — Process Termination Notification:**

A parent process wants to know when a child exits:
- Parent registers for notification with Process Manager
- Child process terminates
- Process Manager cleans up child resources
- Process Manager sends notification (pulse) to parent
- Parent can collect exit status and reap child process

---

## Memory Management Deep Dive

### Virtual to Physical Address Translation

The Process Manager maintains the mapping between virtual addresses (what processes see) and physical addresses (actual RAM/hardware locations).

**Example — Two Processes with Shared Library:**

Process A and Process B both use the C library:
- Physical memory contains one copy of C library code (read-only)
- Process A's virtual space: C library mapped at `0x7F00_0000`
- Process B's virtual space: C library mapped at `0x7F00_0000` (same virtual address, common practice)
- Both virtual addresses translate to same physical pages
- Memory savings: only one physical copy exists
- Protection: read-only prevents either process from corrupting the library

**Example — Copy-on-Write Memory:**

A process forks to create a child:
- Initially, child shares all physical pages with parent (marked read-only)
- Both processes see identical memory contents
- Child attempts to write to a shared data page
- MMU fault occurs (write to read-only page)
- Process Manager copies the physical page
- Gives child its own private copy (now read-write)
- Parent continues using original page
- Only modified pages are copied — efficient memory usage

### Memory Block Allocation

**Example — Large Block Request:**

An image processing application needs 100MB for raw image data:
- Application requests allocation
- Process Manager finds contiguous or non-contiguous physical pages totaling 100MB
- Maps them into virtual address space as contiguous region
- Returns starting virtual address to application
- Application treats it as single array

**Example — Small Frequent Allocations:**

A network server handles many small connections:
- Application makes many small allocation requests (1KB each)
- C library sub-allocates from larger blocks obtained from Process Manager
- Process Manager provides 64KB or 256KB chunks
- C library divides these for small requests
- Reduces system call overhead

---

## Path Namespace Deep Dive

### Dynamic Nature

Unlike traditional systems where the filesystem is largely static after boot, QNX's namespace is highly dynamic.

**Example — Network Filesystem:**

A network filesystem client starts:
- Registers `/net/remote_server/` with Process Manager
- Applications can now access `/net/remote_server/etc/config`
- Process Manager redirects these paths to network client
- Network client translates to remote operations
- Network client stops — path automatically removed

**Example — Temporary Filesystem:**

A RAM disk for temporary files:
- Resource manager starts, registers `/tmp/`
- All temporary file operations go to RAM disk
- Fast access, no disk wear
- On system shutdown or resource manager stop, `/tmp/` disappears

### Security and Access Control

The Process Manager enforces access permissions on all path operations.

**Example — Restricted Process Information:**

A regular user attempts to read `/proc/1/as` (procnto's memory):
- Process Manager checks caller's user ID and capabilities
- Determines caller lacks sufficient privileges
- Returns permission denied error
- User can still list `/proc` and see process IDs (public information)
- But cannot inspect kernel process memory

**Example — Device Access Control:**

An application attempts to open `/dev/ser1` (serial port):
- Process Manager checks if application has hardware access capability
- If not, request denied
- Prevents unauthorized applications from accessing physical devices
- Even though the serial driver is a user-space process, access is controlled

---

## Summary

| Responsibility | Key Function | Unique QNX Characteristic |
|----------------|-------------|--------------------------|
| **Process Lifecycle** | Create, terminate, spawn, fork, exec | All processes are equal — drivers are just processes |
| **Memory Management** | Virtual address spaces, physical allocation, shared memory | Hardware-mapped memory through standard APIs |
| **Path Namespace** | Unified directory tree integrating all resources | Dynamic registration; everything is a path |
| **Resource Resolution** | Match requests to resource managers | Longest-prefix match; most specific wins |
| **System Information** | Process listings, state notifications | `/proc` as pseudo-filesystem |

The Process Manager's design enables QNX's key architectural advantages:
- **Resilience:** Driver failures don't crash the system (drivers are user-space processes)
- **Flexibility:** Namespace grows and shrinks dynamically as resource managers start and stop
- **Scalability:** New hardware supported by starting new resource managers, no kernel changes
- **Transparency:** Applications use standard path operations for files, devices, network resources, and system information

---


*Happy learning!* 🚀
