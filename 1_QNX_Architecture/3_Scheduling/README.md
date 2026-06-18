
# QNX Scheduling

## Overview

This section covers how the QNX operating system performs thread scheduling. The key principle to remember is that **QNX schedules threads, not processes**. Threads are the fundamental execution entities that compete for CPU time, while processes are merely resource containers that own memory, file descriptors, and other system resources.

---

## Thread States

### Blocked State
A thread is **blocked** when it is waiting for something to happen. Most blocking operations are self-initiated — the thread voluntarily calls into the kernel to wait for an event.

**Examples of blocked states:**

| Blocked State | Description | Example Scenario |
|-------------|-------------|----------------|
| **Receive Blocked** | Waiting to receive a message | A server thread calls `MsgReceive()` and waits for client requests |
| **Send Blocked** | Waiting to send a message | A client thread calls `MsgSend()` but the server's message buffer is full |
| **Reply Blocked** | Waiting for a reply after sending a message | A client sends a request and waits for the server to process and reply |
| **Mutex Blocked** | Waiting to acquire a mutex lock | Thread A holds a mutex; Thread B tries to lock it and blocks |
| **Condition Variable Blocked** | Waiting for a condition variable to be signaled | Consumer thread waits for producer to signal "data available" |
| **Semaphore Blocked** | Waiting for a semaphore to be posted | Thread waits for resource pool semaphore to indicate availability |
| **Stopped** | Externally stopped by another thread | A debugger stops a thread to inspect its state |

**Important:** From a scheduling perspective, blocked threads are completely ignored. The scheduler never considers them for execution on any core.

### Runnable State
A thread is **runnable** when it is capable of using the CPU right now. Runnable threads are actively trying to execute code, call functions, run loops, read or write hardware registers, or perform other computational work.

**Runnable sub-states:**

| Sub-State | Description | Example |
|-----------|-------------|---------|
| **Running** | Actually executing on a CPU core right now | Thread A is currently executing on Core 0 |
| **Ready** | Wants to execute but another thread is using the CPU instead | Thread B is ready at priority 20, but Thread C (priority 25) is running |

At any given moment (ignoring brief thread-switching intervals), there is exactly **one running thread per CPU core** in the system. The number of ready threads is essentially unlimited — any thread that is not currently running or blocked could be ready.

### Dead State
A thread enters the **dead** state when it has terminated but has not yet been fully cleaned up by the system. A dead thread can never run again.

**Example:** A thread completes its work and exits. Its resources are still being reclaimed by the process manager, so it remains in the dead state briefly before disappearing entirely.

---

## Priority System

### Priority Range
QNX uses a priority range from **0 (lowest)** to **255 (highest)**.

### Reserved Priorities

| Priority | Reserved For | Description |
|----------|-------------|-------------|
| **0** | Idle Threads | One idle thread per core; runs when no other thread is available |
| **254** | In-Kernel Interrupt Service Threads | Timer handling and user-created kernel threads |
| **255** | Inter-Processor Interrupt (IPI) Threads | Highest priority for kernel core-to-core communication |

### User-Space Priority Range
Priorities **1 through 253** are available for user-space threads. Within this range, the scheduling rule is simple and strict:

> **Higher priority always preempts lower priority.**

**Example Scenarios:**

| Scenario | Result |
|----------|--------|
| Priority 21 thread vs. Priority 10 thread | Priority 21 runs 100% of the time when both want the CPU |
| Priority 200 thread vs. Priority 10 thread | Priority 200 runs 100% of the time — the gap size doesn't matter |
| Priority 11 thread vs. Priority 10 thread | Priority 11 preempts priority 10 completely |

This is **not** fair-share scheduling. There is no percentage allocation. If a higher-priority thread wants to run, it always runs instead of lower-priority threads.

---

## Fundamental Scheduling Principle

### Preemptive Priority-Based Scheduling

The core scheduling rule is straightforward:

> A higher-priority thread runs instead of a lower-priority thread.

**How CPU sharing works in practice:**

Since higher-priority threads always preempt lower-priority ones, CPU sharing is achieved not by time-slicing at the same priority level, but by architecting threads to spend **most of their time blocked**.

**Example — Typical Thread Structure:**

A well-designed thread follows this pattern:

1. **Block** waiting for work (message, pulse, timer, interrupt, signal)
2. **Receive notification** that work is available
3. **Execute the work** quickly
4. **Block again** waiting for the next notification

**Concrete Example — Sensor Data Processing Thread:**

- A thread waits for a timer pulse every 10 milliseconds (blocked state)
- Timer fires → thread unblocks → reads sensor data from hardware → processes data → sends results via message to another thread
- Thread immediately blocks again waiting for next timer pulse
- While blocked, lower-priority threads get their chance to run

**Concrete Example — Server Thread:**

- A server thread calls `MsgReceive()` and blocks waiting for client requests
- Client sends message → server unblocks → processes request → sends reply → blocks again on `MsgReceive()`
- Between requests, the server consumes no CPU time, allowing other threads to run

---

## Multicore and SMP Support

### Multicore Systems
QNX is designed for systems with one or more CPU cores that share underlying hardware resources:

| Shared Resource | Description |
|-----------------|-------------|
| System RAM | All cores access the same physical memory |
| System Bus | Cores share data pathways to memory and devices |
| Hardware Devices | Network cards, disk controllers, etc. are shared |
| File Systems | Storage is accessible from all cores |

### Core Proximity and Cache Hierarchy
Modern multicore systems have complex physical layouts:

**Example — Cache Clustering:**
- An 8-core system might have:
  - Cores 0, 1, 2, 3 share Level 3 cache
  - Cores 4, 5, 6, 7 share a separate Level 3 cache
- Cache coherency between Core 1 and Core 2 is faster than between Core 1 and Core 6

**Example — big.LITTLE Architecture:**
- Some cores are **high-performance** (fast clock, high power consumption)
- Some cores are **low-performance** (slower clock, very low power)
- A thread doing heavy calculations should run on high-performance cores
- A background monitoring thread can run on low-performance cores

**Example — Core-Specific Hardware:**
- Certain cores have special manufacturer-specific registers
- A thread that needs to read performance counters from Core 0 must run on Core 0

### SMP (Symmetrical Multiprocessing)
SMP is a special case of multicore where all processors are identical with no hardware distinctions between cores.

- QNX kernel names reflect this: `procnto-smp`
- By default, QNX treats all multicore systems as SMP for scheduling purposes
- The kernel sees all cores as interchangeable

---

## Cluster-Based Scheduling

### The Problem with Simple Scheduling Approaches

**Global Shared Queue Approach:**
- All ready threads in one system-wide queue
- Scheduler picks highest-priority thread for any available core
- **Problem:** With 80 ready threads and core affinity restrictions, worst-case search time grows with system load
- **Result:** Scheduling cost becomes unpredictable — unacceptable for real-time systems

**Per-Core Ready Queue Approach:**
- Each core has its own ready queue
- **Problem:** Load balancing suffers — a thread ready on Core 0's queue won't run on Core 1 even if Core 1 is idle
- **Result:** Cores become underutilized while threads wait unnecessarily

### QNX Solution: Cluster-Based Scheduling

A **cluster** is a set of related CPU cores that defines where a thread is allowed to run.

**Key Properties:**
- Every thread belongs to **exactly one cluster**
- Ready threads are tracked on a **per-cluster basis**
- Clusters are **hardware-specific** and defined in the Board Support Package (BSP) startup code
- Once configured, scheduling cost is **fixed and predictable**

**Default Clusters (Always Present):**

| Cluster | Cores Included | Purpose |
|---------|---------------|---------|
| **Global Cluster** | All cores | Fallback to system-wide scheduling |
| **Per-Core Clusters** | Core 0 only, Core 1 only, etc. | Individual core binding |

**Example — Custom Cluster Configuration:**

A startup configuration might define:
- `cluster0`: Cores 0, 1, 2 (share L3 cache)
- `cluster1`: Cores 0, 3 (special hardware access)

**Cluster Rules:**
- Each cluster must be **unique** — no two clusters can contain the exact same cores
- Any core can be in a **maximum of 8 clusters** (2 defaults + 6 custom)
- Exceeding this limit causes **boot failure**

**Viewing Cluster Configuration:**
Run `pidin syspage=cluster` to display additional clusters configured on a running system.

---

## Scheduling Algorithm

### When a Thread Leaves the Running State

This is the "easy" scheduling case. A thread leaves running when it voluntarily blocks itself.

**Example Scenarios:**

| Trigger | Thread Action | Result |
|---------|-------------|--------|
| Thread calls `MsgReceive()` | "I want to wait for a message" | Thread becomes receive-blocked |
| Thread calls mutex lock | "I want to wait for this mutex" | Thread becomes mutex-blocked |
| Thread calls semaphore wait | "I want to wait for this semaphore" | Thread becomes semaphore-blocked |
| Thread calls `sigwait()` | "I want to wait for a signal" | Thread becomes signal-blocked |
| Thread calls `nanosleep()` | "I want to wait for time to pass" | Thread becomes timer-blocked |

**External Trigger Example:**
- Thread A on Core 1 is running
- Thread B on Core 0 calls `stop` on Thread A's process
- Core 0's kernel sends an IPI to Core 1
- Core 1's IPI handler thread (priority 255) runs
- Core 1's scheduler stops Thread A and marks it as stopped-blocked

### Scheduling Decision Process

When a core needs to choose a new thread to run:

1. **Examine all clusters** the current core belongs to
2. **Find the highest-priority ready thread** in each cluster
3. **Select the most eligible thread** across all clusters:
   - Highest priority wins
   - If same priority, **lowest timestamp** wins (earliest ready time)
4. **Switch to that thread**:
   - If same process: simple thread switch
   - If different process: full context switch (MMU reconfiguration)

**Example — Core 0 Scheduling Decision:**

Cluster memberships for Core 0:
- Global cluster (all cores)
- Core 0 individual cluster
- Custom cluster: Cores 0, 1, 2

Ready threads found:
- Global cluster: Thread X at priority 50, timestamp 1000
- Core 0 cluster: Thread Y at priority 60, timestamp 2000
- Custom cluster: Thread Z at priority 50, timestamp 800

Decision:
- Compare highest priorities: Thread Y (60) vs. Thread X (50) vs. Thread Z (50)
- Thread Y has highest priority → **Thread Y runs on Core 0**

**Example — Tie-Breaking by Timestamp:**

Ready threads found:
- Global cluster: Thread A at priority 40, timestamp 1500
- Custom cluster: Thread B at priority 40, timestamp 1200

Decision:
- Same priority (40) → compare timestamps
- Thread B has lower timestamp (1200 < 1500) → **Thread B runs** (it was ready first)

### When a Thread Becomes Runnable

This is the "hard" scheduling case — a thread transitions from blocked to ready.

**Example — Semaphore Posted:**

- Thread M was waiting on a semaphore (blocked)
- Another thread posts the semaphore
- Thread M becomes ready and is added to its cluster's ready list
- If Thread M is **not** the most eligible in its cluster → no scheduling action needed
- If Thread M **is** the most eligible in its cluster → evaluate preemption:

**Preemption Decision Tree:**

1. **Can Thread M preempt the current thread on this core?**
   - Yes → Local preemption occurs
   - No → Continue to step 2

2. **Is there an idle core in Thread M's cluster?**
   - Yes → Send IPI to that idle core; idle core preempts itself and runs Thread M
   - No → Continue to step 3

3. **Can Thread M preempt a lower-priority thread on another core in its cluster?**
   - Yes → Send IPI to that core; preempt the lowest-priority running thread
   - No → Thread M waits on the ready list

**Example — Full Preemption Scenario:**

- Core 0 running: Thread P at priority 30
- Core 1 running: Thread Q at priority 20
- Core 2 running: Thread R at priority 25 (idle thread)
- Core 3 running: Thread S at priority 15

Thread M becomes ready at priority 35 in cluster containing all cores:

1. Can Thread M preempt Core 0's Thread P (priority 30)? **Yes** (35 > 30)
2. **Result:** Thread M preempts Thread P on Core 0; Thread P becomes ready

**Example — Idle Core Preemption:**

Same setup, but Thread M is at priority 25:

1. Can Thread M preempt Core 0's Thread P (priority 30)? **No** (25 < 30)
2. Is there an idle core? Core 2 is running idle thread (priority 0) → **Yes**
3. **Result:** IPI sent to Core 2; Thread M runs on Core 2; idle thread becomes ready

**Example — Cross-Core Preemption:**

Same setup, Thread M at priority 22 in cluster with Cores 1, 2, 3:

1. Can Thread M preempt Core 0's Thread P? **No** (Core 0 not in cluster)
2. Is there an idle core in cluster? Core 2 is idle → **Yes**
3. **Result:** Thread M runs on Core 2

If Core 2 was running Thread R at priority 24 (not idle):

1. Can Thread M preempt Core 1's Thread Q (priority 20)? **Yes** (22 > 20)
2. **Result:** IPI sent to Core 1; Thread M preempts Thread Q; Thread Q becomes ready

---

## Core Affinity

Core affinity binds a thread to a specific cluster, restricting which cores it can run on.

**Use Cases for Core Affinity:**

| Scenario | Affinity Purpose | Example |
|----------|----------------|---------|
| **Performance Optimization** | Run compute-heavy threads on high-performance cores | Video encoding thread bound to big cores in big.LITTLE system |
| **Safety Segregation** | Isolate safety-critical threads from non-critical | ASIL-D threads on Cores 0-3; QM threads on Cores 4-7 |
| **Hypervisor Isolation** | Separate hypervisor and non-hypervisor workloads | Hypervisor on Cores 0-1; Guest OS on Cores 2-3 |
| **Cache Optimization** | Keep related threads on cores sharing cache | Database worker threads bound to Cores 0,1,2 (shared L3) |
| **Hardware Access** | Access core-specific registers | Performance monitoring thread bound to Core 0 to read Core 0's PMU |

**Example — Cache-Aware Affinity:**

A system has:
- Cores 0, 1, 2, 3 sharing L3 cache
- Cores 4, 5, 6, 7 sharing a different L3 cache

Application with three threads that frequently exchange data:
- Thread 1, 2, 3 all bound to cluster containing Cores 0, 1, 2, 3
- Result: Data stays in shared L3 cache; fast inter-thread communication
- If spread across both cache groups: Cache coherency traffic slower; performance degrades

**Example — Safety-Critical System:**

Automotive system with mixed criticality:
- Cores 0, 1, 2, 3: ASIL-D (highest safety rating) cluster
- Cores 4, 5, 6, 7: QM (quality management, non-safety) cluster

Thread configuration:
- Brake control thread: affinity to ASIL cluster (Cores 0-3), priority 200
- Infotainment thread: affinity to QM cluster (Cores 4-7), priority 10
- Result: Brake control can never be delayed by infotainment, even if infotainment has bugs

**Example — Core-Specific Hardware Access:**

A processor has performance monitoring registers only accessible from Core 0:
- Profiling thread must run on Core 0 to read/write these registers
- Thread bound to Core 0 individual cluster
- Result: Thread always runs on Core 0; can access special registers

---

## Scheduling Algorithms

### FIFO (First In, First Out)
The default and simplest scheduling algorithm.

**Behavior:**
- Thread runs until it voluntarily blocks
- No forced time slicing at the same priority
- Preempted threads return to head of ready list (maintain eligibility)

**Example — FIFO Thread Lifecycle:**

1. Thread A at priority 50 becomes ready
2. Thread A runs on Core 0
3. Higher-priority Thread B (priority 60) becomes ready
4. Thread A preempted → placed at head of priority 50 ready list
5. Thread B runs and eventually blocks
6. Thread A resumes (still at head of priority 50 list) — it was more eligible than any other priority 50 thread

### Round Robin
Similar to FIFO but with time slicing for threads at the same priority.

**Time Slice Behavior:**
- Each thread gets a fixed time quantum (e.g., 4 milliseconds)
- When time slice expires, thread moves from head to tail of its priority's ready list
- Next thread at same priority gets to run

**Example — Single Core Round Robin:**

Three threads: A, B, C all at priority 40, all Round Robin, on one core:

| Time | Action | Running Thread |
|------|--------|----------------|
| 0ms | Thread A starts | A |
| 4ms | A's slice expires → A moves to tail | B |
| 8ms | B's slice expires → B moves to tail | C |
| 12ms | C's slice expires → C moves to tail | A |
| 16ms | A's slice expires | B |
| ... | Pattern continues: A→B→C→A→B→C | ... |

**Example — Multicore Round Robin:**

Same three threads (A, B, C at priority 40, Round Robin) bound to cluster with Cores 1 and 2:

| Time | Core 1 | Core 2 | Event |
|------|--------|--------|-------|
| 0ms | Thread A (head of list) | (idle) | A starts on Core 1 |
| 1ms | Thread A | Thread B | Core 2 becomes available; B is next most eligible |
| 4ms | Thread C | Thread B | A's slice expires; C is next (waited longer than A) |
| 5ms | Thread C | Thread A | B's slice expires; A is next |
| 8ms | Thread B | Thread A | C's slice expires; B is next |
| 9ms | Thread B | Thread C | A's slice expires; C is next |

**Preemption and Time Slices:**
- Time spent **preempted by higher-priority threads does NOT count against the time slice**
- Example: Thread A runs for 2ms, then is preempted by priority 60 thread for 3ms
- When Thread A resumes, it still has 2ms remaining in its slice
- Accounting is precise to nanosecond resolution

### Sporadic Scheduling
A complex algorithm for threads that need guaranteed periodic execution with controlled CPU usage.

**Four Parameters:**

| Parameter | Description | Example Value |
|-----------|-------------|---------------|
| **Priority** | Normal high priority for execution | 50 |
| **Low Priority** | Fallback priority when budget exhausted | 10 |
| **Budget** | Time allowed at normal priority before dropping | 5ms |
| **Replenishment Period** | Time before budget is restored | 20ms |

**Behavior Example:**

A sporadic thread with priority 50, low priority 10, budget 5ms, period 20ms:

1. Thread starts at **priority 50**
2. Runs for 5ms (exhausts budget)
3. Priority drops to **10** (low priority)
4. May or may not get CPU time at priority 10 (other threads may preempt it)
5. After 20ms total period expires
6. Budget replenished → priority returns to **50**
7. Thread preempts lower-priority threads and runs again
8. Cycle repeats

**Preemption and Budget:**
- Time spent **preempted by higher-priority threads does NOT count against the budget**
- Only actual execution time at the normal priority consumes budget

**Use Case Example:**
A logging thread that must periodically flush buffers:
- Needs guaranteed execution every 20ms (priority 50)
- But should not consume more than 5ms of CPU per period
- After 5ms, drops to low priority so critical threads can run
- Budget replenishes every 20ms ensuring periodic access

### High-Priority IST (Interrupt Service Thread)
A special scheduling algorithm for kernel threads running at reserved priorities 254-255.

**Used By:**
- In-kernel timer handling threads
- IPI handling threads
- User-created kernel threads via `InterruptAttachEvent()`

**Behavior:**
- Scheduled like FIFO
- Special permission to use reserved priorities 254-255
- Essential for kernel internal operations

---

## Scheduling Design Guidelines

### Priority Assignment Strategy

| Thread Type | Recommended Priority | Reasoning |
|-------------|---------------------|-----------|
| **Safety-critical threads** | Highest (e.g., 200-253) | Must never be delayed by lower-priority work |
| **Time-critical threads** | High (e.g., 100-199) | Must meet deadlines; preempt non-time-critical work |
| **CPU-intensive threads** | Lower (e.g., 10-50) | Allow higher-priority threads to preempt when needed |
| **Background tasks** | Lowest (e.g., 1-10) | Fill idle time; never interfere with important work |

**Example — Automotive System Priorities:**

| Component | Priority | Algorithm | Rationale |
|-----------|----------|-----------|-----------|
| Brake control | 250 | FIFO | Safety-critical; must run immediately when needed |
| Engine management | 200 | FIFO | Time-critical; 1ms response requirement |
| ABS sensor processing | 180 | FIFO | Time-critical; derived from brake control |
| Transmission control | 150 | FIFO | Time-critical; 5ms response requirement |
| Instrument cluster | 50 | Round Robin | User-visible; periodic updates |
| Infotainment | 20 | Round Robin | Non-critical; user experience |
| Diagnostics logging | 10 | Sporadic | Periodic but limited CPU usage |

### Thread Design Pattern

**Anti-Pattern — Busy Waiting (Polling):**
- Thread repeatedly checks "Is there work?" without blocking
- Consumes CPU continuously
- Prevents lower-priority threads from running
- Wastes power and degrades system responsiveness

**Correct Pattern — Event-Driven Blocking:**
- Thread blocks waiting for event (message, pulse, timer, interrupt)
- Consumes zero CPU while blocked
- Unblocks only when work arrives
- Processes work quickly
- Blocks again immediately

**Example — Good Design: Message-Driven Server**

1. Server thread calls `MsgReceive()` → blocks (no CPU used)
2. Client sends message → server unblocks
3. Server processes request (brief execution)
4. Server sends reply to client
5. Server calls `MsgReceive()` → blocks again
6. While blocked, other threads use the CPU

**Example — Good Design: Timer-Driven Periodic Task**

1. Thread sets timer for 10ms period
2. Thread calls `MsgReceivePulse()` → blocks waiting for timer
3. Timer fires after 10ms → thread unblocks
4. Thread reads sensor, processes data, sends results
5. Thread calls `MsgReceivePulse()` → blocks again
6. Between periods, CPU is available for other threads

### Multicore Considerations

**When to Use Global Cluster (All Cores):**
- Threads have no special hardware requirements
- Load balancing is more important than cache locality
- System has relatively few threads

**When to Use Custom Clusters:**
- Threads share data frequently (keep in same cache domain)
- Threads access core-specific hardware
- Safety segregation requirements exist
- Power management needs (big.LITTLE architectures)

**When to Use Per-Core Affinity:**
- Maximum cache locality needed
- Hard real-time requirements with minimal jitter
- Avoiding inter-core migration overhead

---

## Summary

| Aspect | Key Principle |
|--------|-------------|
| **Scheduled Entity** | Threads, not processes |
| **Basic Rule** | Higher priority always preempts lower priority |
| **CPU Sharing** | Achieved by blocking when idle, not by time slicing |
| **Multicore** | Cluster-based scheduling for predictable real-time behavior |
| **Affinity** | Bind threads to clusters matching hardware and application needs |
| **Algorithms** | FIFO (default), Round Robin (time slicing), Sporadic (budget control) |
| **Design Goal** | Block when no work; run only when necessary; keep critical threads at high priority |

---



*Happy learning!* 🚀
