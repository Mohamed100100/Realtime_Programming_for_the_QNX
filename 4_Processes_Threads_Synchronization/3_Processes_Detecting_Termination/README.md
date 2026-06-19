
# Detecting Process and Thread Termination in QNX

## Overview

This module covers three mechanisms for detecting when processes terminate in QNX. Understanding these patterns is essential for building robust systems that respond to failures, manage child processes, and maintain reliable client-server relationships. The three approaches cover different architectural relationships: parent-child, client-server, and arbitrary process monitoring.

| Mechanism | Relationship | POSIX Standard | Use Case |
|-----------|-------------|----------------|----------|
| **SIGCHLD + wait()** | Parent / Child | Yes (POSIX) | Launchers, process supervisors, shell programs |
| **Message passing notifications** | Client / Server | No (QNX-specific) | Resource managers, service daemons, IPC-based systems |
| **Death pulses** | Any process | No (QNX-specific) | System monitors, watchdogs, process managers |

---

## Method 1: Detecting Child Process Death (Parent/Child Relationship)

When a parent process creates child processes using `fork()`, `posix_spawn()`, or the `spawn()` family, it can detect when those children terminate. This is the only POSIX-standard mechanism among the three approaches.

### How It Works

When a child process dies, the kernel sends the parent a **SIGCHLD** signal. Unlike most signals whose default action is to terminate the process, SIGCHLD's default action is to be ignored — the parent does not die. However, the parent can establish a handler or use the `wait()` family of functions to learn about the child's fate.

The `wait()`, `waitpid()`, `waitid()`, and `wait3()`/`wait4()` functions block the calling thread until a child process terminates, then return information about how and why the child died.

### The Zombie Process Mechanism

If a child dies while the parent is not currently blocked in a `wait()` call, the child becomes a **zombie process**. This is not a running process — it consumes no CPU, its memory is freed, its file descriptors are closed, and its timers are deleted. However, its process table entry remains because it stores the exit information the parent will need.

| Zombie State | Description |
|-------------|-------------|
| **CPU usage** | None — not scheduled |
| **Memory** | Freed — all allocations released |
| **File descriptors** | Closed — all open files closed |
| **Timers** | Deleted — all timers removed |
| **Process table entry** | **Retained** — stores exit status and PID |
| **Purpose** | Preserve information for parent's future `wait()` call |

When the parent eventually calls `wait()`, the kernel returns the stored information and the zombie process is finally removed from the system.

### Preventing Zombie Processes

If a parent creates child processes but does not care about their termination status, zombie accumulation wastes process table entries. The parent can prevent zombies entirely by telling the kernel to ignore SIGCHLD:

```
signal(SIGCHLD, SIG_IGN);
```

With this setting, child processes that die are cleaned up completely — no process table entry is retained, and no zombie is created. The parent never needs to call `wait()`.

### Typical Usage Pattern: Launcher Process

A common pattern is a launcher that starts multiple child processes, then monitors their health:

```
┌─────────────────────────────────────────────────────────────────────┐
│              LAUNCHER PROCESS WITH CHILD MONITORING                 │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    PARENT (LAUNCHER)                        │    │
│  │                                                             │    │
│  │  // Spawn multiple child processes                          │    │
│  │  posix_spawn(&pid1, "motor_control", ...);                  │    │
│  │  posix_spawn(&pid2, "sensor_reader", ...);                  │    │
│  │  posix_spawn(&pid3, "data_logger", ...);                    │    │
│  │                                                             │    │
│  │  // Enter monitoring loop                                   │    │
│  │  while (running) {                                          │    │
│  │      int status;                                            │    │
│  │      pid_t dead_pid = wait(&status);        ← BLOCKS HERE   │    │
│  │                                                             │    │
│  │      // Returns when ANY child dies                         │    │
│  │      if (WIFEXITED(status)) {                               │    │
│  │          printf("Child %d exited normally with status %d\n",│    │
│  │                 dead_pid, WEXITSTATUS(status));             │    │
│  │      } else if (WIFSIGNALED(status)) {                      │    │
│  │          printf("Child %d killed by signal %d\n",           │    │
│  │                 dead_pid, WTERMSIG(status));                │    │
│  │          // RESTART LOGIC: respawn critical child           │    │
│  │          if (dead_pid == pid1) {                            │    │
│  │              posix_spawn(&pid1, "motor_control", ...);      │    │
│  │          }                                                  │    │
│  │      }                                                      │    │
│  │  }                                                          │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                      │
│                              │ creates                              │
│                              ▼                                      │
│         ┌──────────────────────────────────────────┐                │
│         │         CHILD PROCESSES                  │                │
│         │  ┌─────────┐  ┌─────────┐  ┌─────────┐   │                │
│         │  │ motor   │  │ sensor  │  │ logger  │   │                │
│         │  │ control │  │ reader  │  │         │   │                │
│         │  │ (PID 1) │  │ (PID 2) │  │ (PID 3) │   │                │
│         │  └─────────┘  └─────────┘  └─────────┘   │                │
│         │                                          │                │
│         │  If motor_control dies → parent notified │                │
│         │  via SIGCHLD, wait() returns PID 1       │                │
│         │  Parent respawns motor_control           │                │
│         └──────────────────────────────────────────┘                │
│                                                                     │
│  SCENARIO: sensor_reader dies unexpectedly                          │
│                                                                     │
│  1. Kernel sends SIGCHLD to parent                                  │
│  2. Parent's wait() unblocks, returns PID of sensor_reader          │
│  3. Parent checks status — discovers signal termination             │
│  4. Parent logs failure, optionally respawns sensor_reader          │
│  5. If parent not in wait() → sensor_reader becomes zombie briefly  │
│     until parent calls wait()                                       │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Method 2: Client/Server Relationship Notifications

QNX message passing provides a mechanism for processes to detect when their communication partners terminate. This applies to both directions: a server can detect when a client dies, and a client can detect when a server dies.

### How It Works

In QNX message passing, a server creates a **channel** and clients connect to it. This establishes a relationship tracked by the kernel. When either party dies, the kernel can notify the surviving party that the relationship has been broken.

This is not about detecting arbitrary process death — it is about detecting the **loss of a specific communication relationship**. If a client had multiple connections to different servers, each server would independently detect the client's death through its own channel connection.

### Server Detects Client Death

A server that receives requests from clients can be notified when a client that was sending it messages dies. This is particularly important for resource managers that need to clean up per-client state.

### Client Detects Server Death

Conversely, a client waiting for a reply from a server can be notified if that server dies before responding. This prevents the client from blocking forever on a reply that will never arrive.

### Practical Significance

This mechanism is automatic in QNX message passing. When a process dies, the kernel cleans up its channels and connections. Any blocked senders or receivers receive an error indication (typically `EBADF`, `ESRCH`, or `EPIPE` depending on the exact timing and state). Well-written servers and clients check for these errors and handle partner death appropriately.

```
┌─────────────────────────────────────────────────────────────────────┐
│              CLIENT/SERVER DEATH DETECTION                          │
│                                                                     │
│  ┌─────────────────┐                    ┌─────────────────┐         │
│  │    CLIENT       │                    │     SERVER      │         │
│  │                 │◄──────────────────►│                 │         │
│  │  coid =         │    Connection      │  chid =         │         │
│  │  ConnectAttach( │◄──────────────────►│  ChannelCreate( │         │
│  │  server_pid,    │    established     │  ... )          │         │
│  │  ... )          │                    │                 │         │
│  │                 │                    │                 │         │
│  │  MsgSend(coid,  │──────────────────► │  MsgReceive(chid, ...)    │   
│  │  &request, ...) │   request          │                 │         |
│  │                 │◄───────────────────│  MsgReply(rcvid, ...)     │
│  │  &reply, ...)   │   reply            │                 │         │
│  └─────────────────┘                    └─────────────────┘         │
│         │                                      │                    │
│         │                                      │                    │
│         ▼ CLIENT DIES                          ▼ SERVER DIES        │
│         │                                      │                    │
│  Kernel detects channel/connection cleanup      Kernel detects      │
│  Server's next MsgReceive returns error        Client's blocked     │
│  or server receives disconnect pulse           MsgSend returns error│
│                                                                     │
│  SERVER RESPONSE:                        CLIENT RESPONSE:           │
│  • Clean up per-client resources         • Detect server failure    │
│  • Release locks held by dead client     • Retry connection or      │
│  • Free allocated buffers                 • Report failure upstream │
│  • Log client disconnect                 • Fail gracefully          │
│                                                                     │
│  This is automatic kernel behavior — no explicit registration needed│
│  for basic relationship death detection.                            │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Method 3: Death Pulses (Arbitrary Process Monitoring)

The most flexible mechanism is **death pulses**, which allow any process to receive notification when **any** process in the system dies — not just children or communication partners. This is QNX-specific and particularly useful for system monitors, watchdogs, and process management daemons.

### How It Works

A process registers for death notifications by calling `procmgr_event_notify()`. This is a QNX-specific function (not POSIX) that requests the kernel to send a notification when a specific type of system event occurs.

To receive death notifications:

1. Call `procmgr_event_notify()` with the event type `PROCMGR_EVENT_PROCESS_DEATH`
2. Specify how you want to be notified — typically via a **pulse** (the most convenient and common choice)
3. The kernel sends a pulse to your process whenever any process dies
4. The pulse payload contains the **PID of the process that died**

### Why Pulses Are Preferred

While signals are supported, **pulses** are the recommended notification mechanism because:

- Pulses are queued and delivered through the standard message-passing channel
- A process can receive multiple death notifications without losing them (signals may coalesce)
- Pulses integrate naturally with the QNX event-driven programming model
- The receiving process can block on `MsgReceivePulse()` and handle notifications in its main event loop

### Typical Usage: System Monitor

A system monitor process uses death pulses to track the health of all critical processes in the system:

```
┌────────────────────────────────────────────────────────────────────┐
│              DEATH PULSE MONITORING SYSTEM                         │
│                                                                    │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    MONITOR PROCESS                          │   │
│  │                    (system watchdog)                        │   │
│  │                                                             │   │
│  │  // Create channel for receiving pulses                     │   │
│  │  chid = ChannelCreate(0);                                   │   │
│  │                                                             │   │
│  │  // Register for death notifications                        │   │
│  │  SIGEV_PULSE_INIT(&event, coid,                             │   │
│  │                   SIGEV_PULSE_PRIO_INHERIT,                 │   │
│  │                   MY_PULSE_CODE, 0);                        │   │
│  │  procmgr_event_notify(PROCMGR_EVENT_PROCESS_DEATH, &event); │   │
│  │                                                             │   │
│  │  // Main monitoring loop                                    │   │
│  │  while (running) {                                          │   │
│  │      rcvid = MsgReceivePulse(chid, &pulse, sizeof(pulse),   │   │
│  │                             NULL);                          │   │
│  │                                                             │   │
│  │      if (pulse.code == MY_PULSE_CODE) {                     │   │
│  │          dead_pid = pulse.value.sival_int;                  │   │
│  │                                                             │   │
│  │          // Check if this was a critical process            │   │
│  │          if (is_critical_process(dead_pid)) {               │   │
│  │              log_critical_failure(dead_pid);                │   │
│  │              initiate_recovery(dead_pid);                   │   │
│  │              // e.g., restart service, alert operator,      │   │
│  │              // switch to backup system                     │   │
│  │          } else {                                           │   │
│  │              log_process_exit(dead_pid);                    │   │
│  │          }                                                  │   │
│  │      }                                                      │   │
│  │  }                                                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                     │
│                              │ registers for death notifications   │
│                              ▼                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    KERNEL (procnto)                         │   │
│  │                                                             │   │
│  │  When ANY process dies:                                     │   │
│  │  • motor_control (PID 1234) exits → send pulse to monitor   │   │
│  │  • sensor_reader (PID 1235) crashes → send pulse to monitor │   │
│  │  • data_logger (PID 1236) killed → send pulse to monitor    │   │
│  │  • user_shell (PID 9999) exits → send pulse to monitor      │   │
│  │                                                             │   │
│  │  Pulse payload: { code: MY_PULSE_CODE, value: dead_pid }    │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                    │
│  CAPABILITIES:                                                     │
│  • Monitor ANY process, not just children                          │
│  • Detect crashes, normal exits, signal terminations uniformly     │
│  • Build system health dashboards and automatic recovery           │
│  • Integrate with safety monitors for ASIL-compliant systems       │
│                                                                    │
│  LIMITATIONS:                                                      │
│  • Cannot prevent process death — only notified after it happens   │
│  • Race condition: process may die between check and action        │
│  • Must have appropriate privileges to receive all death notifications │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

---

## Comparison of Three Methods

| Aspect | Parent/Child (wait) | Client/Server (IPC) | Death Pulses |
|--------|---------------------|---------------------|--------------|
| **Relationship required** | Parent created child | Message passing connection | None — any process |
| **POSIX standard** | Yes | No (QNX-specific) | No (QNX-specific) |
| **What is detected** | Child termination | Communication partner death | Any process death |
| **Information provided** | Exit status, signal, PID | Error on operation | PID of dead process |
| **Notification mechanism** | SIGCHLD signal + wait() | Error return from MsgSend/Receive | Pulse (or signal) |
| **Use case** | Launchers, shells, supervisors | Resource managers, IPC services | System monitors, watchdogs |
| **Zombie handling** | Must call wait() or ignore SIGCHLD | Automatic kernel cleanup | Not applicable |
| **Flexibility** | Limited to own children | Limited to connected partners | Universal |

---

## Summary

| Method | Function | When to Use |
|--------|----------|-------------|
| **Parent/Child** | `wait()`, `waitpid()`, `waitid()` | You created the process and need its exit status |
| **Client/Server** | Automatic via message passing | You communicate with the process via IPC channels |
| **Death Pulses** | `procmgr_event_notify()` | You need to monitor arbitrary processes system-wide |

Understanding these three mechanisms allows you to choose the right approach for your architecture. Parent-child detection is standard and sufficient for process launchers. Client-server notifications are automatic and ideal for message-based systems. Death pulses provide maximum flexibility for system-wide monitoring and safety-critical supervision.

---


*Happy coding!* 🚀
