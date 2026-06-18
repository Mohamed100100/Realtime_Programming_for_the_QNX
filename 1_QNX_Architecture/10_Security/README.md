# QNX Security Overview

## Introduction

This section provides a high-level overview of QNX security concepts for system integrators and embedded designers building production systems. It covers POSIX-style permissions, the critical distinction between development and production security postures, fine-grained capability control through process manager abilities, and automated security policy generation. For detailed implementation guidance, refer to the **Introduction to Security Policies** module and the **Security Developer's Guide** in the QNX online documentation.

---

## POSIX File Permissions

QNX uses standard Unix-style POSIX file permissions that will be immediately familiar to anyone who has worked with Linux or other Unix systems. These permissions control access to files, directories, and devices through three attributes: read, write, and execute. They apply to three categories of users: the file owner, the file's group, and all other users.

When you run `ls -l` in a QNX shell, you see the familiar permission string format. For example, `-rwxr-xr-x` indicates a regular file where the owner has full read, write, and execute permissions; the group has read and execute but not write; and others have read and execute but not write. These same permissions apply to directories and device files in `/dev/`, meaning access to hardware resources can be controlled through standard filesystem permissions.

This POSIX permission model is the foundation of QNX access control, but it is only the beginning. Production systems require much more sophisticated security mechanisms.

---

## The Root Problem in Boot Images

A critical security consideration in QNX is the behavior of the **Image File System (IFS)**. Everything in the IFS runs as **root** (UID 0) by default. This includes the kernel (procnto), startup code, all drivers launched from the boot script, and all applications included in the boot image.

This is excellent for development. As a developer building and debugging a system, you need unrestricted access to hardware, the ability to kill and restart processes, and the freedom to modify system configuration. Running as root eliminates permission barriers that would slow down development.

However, shipping a production system with everything running as root is a severe security risk. A compromised process with root privileges can access any hardware, modify any file, kill any process, and potentially crash the entire system. The principle of least privilege demands that each process receive only the minimum permissions necessary for its function.

```
┌─────────────────────────────────────────────────────────────────────┐
│           DEVELOPMENT vs. PRODUCTION SECURITY POSTURE                  │
│                                                                      │
│  DEVELOPMENT SYSTEM (Acceptable — but only for development)           │
│  ─────────────────────────────────────────────────────────────       │
│                                                                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐       │
│  │ procnto │    │ devc-ser│    │ io-sock │    │ my_app  │       │
│  │  ROOT   │    │  ROOT   │    │  ROOT   │    │  ROOT   │       │
│  │  UID 0  │    │  UID 0  │    │  UID 0  │    │  UID 0  │       │
│  │         │    │         │    │         │    │         │       │
│  │ Can:    │    │ Can:    │    │ Can:    │    │ Can:    │       │
│  │ • All   │    │ • All   │    │ • All   │    │ • All   │       │
│  │ • HW    │    │ • HW    │    │ • HW    │    │ • HW    │       │
│  │ • Kill  │    │ • Kill  │    │ • Kill  │    │ • Kill  │       │
│  │ • Map   │    │ • Map   │    │ • Map   │    │ • Map   │       │
│  └─────────┘    └─────────┘    └─────────┘    └─────────┘       │
│                                                                      │
│  PROBLEM: Any compromised process has FULL SYSTEM ACCESS            │
│  Serial driver bug → attacker can access network stack memory         │
│  Application exploit → attacker can kill safety-critical processes      │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  PRODUCTION SYSTEM (Required — minimal privileges per process)       │
│  ─────────────────────────────────────────────────────────────       │
│                                                                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐       │
│  │ procnto │    │ devc-ser│    │ io-sock │    │ my_app  │       │
│  │  ROOT   │    │  user   │    │  user   │    │  user   │       │
│  │  UID 0  │    │  UID 100│    │  UID 101│    │  UID 102│       │
│  │         │    │         │    │         │    │         │       │
│  │ Can:    │    │ Can:    │    │ Can:    │    │ Can:    │       │
│  │ • All   │    │ • UART  │    │ • Net   │    │ • App   │       │
│  │ • HW    │    │ HW only │    │ HW only │    │ data    │       │
│  │ • Kill  │    │ • No    │    │ • No    │    │ • No    │       │
│  │ • Map   │    │ kill    │    │ kill    │    │ HW      │       │
│  └─────────┘    └─────────┘    └─────────┘    └─────────┘       │
│                                                                      │
│  RESULT: Compromised application cannot touch hardware or other        │
│  processes. Attack surface is minimized. Defense in depth.           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Transitioning from Root to Restricted Execution

Several mechanisms exist to prevent processes from running as root and to establish proper user identities:

### Launcher Programs

A launcher program is a small, trusted utility that starts as root (from the boot script) and then drops privileges before launching the actual application. The launcher can set up necessary resources — opening device files, mapping shared memory, establishing connections — that require root access, then switch to a non-root user ID before executing the target program.

**Example scenario:** A system needs a data logging application that writes to a specific directory and reads from a sensor device. The launcher starts as root, creates the log directory with correct permissions, opens the sensor device file, then switches to UID 100 and launches the logger. The logger runs without root privileges but with access to the pre-opened resources.

### The Login Utility

For systems with interactive access, the `login` command authenticates users and assigns appropriate user IDs and groups. After successful authentication, the user's shell or application runs with the authenticated identity, not as root. This is standard Unix behavior adapted for QNX embedded systems.

### Setuid Executables

The `setuid` bit on an executable allows a program to run with the file owner's privileges rather than the caller's privileges. A carefully crafted `setuid` program can perform privileged operations (like binding to a low-numbered port or accessing a protected device) and then drop privileges for the remainder of execution.

**Caution:** `setuid` programs are a common source of security vulnerabilities. They must be meticulously audited for buffer overflows, race conditions, and logic errors that could allow privilege escalation. In QNX, the security policy framework provides safer alternatives for most use cases.

### C API Calls

Applications can programmatically change their user ID using standard POSIX calls like `setuid()`, `seteuid()`, `setgid()`, and related functions. A well-designed application starts with necessary privileges, performs privileged initialization, then permanently drops to an unprivileged user ID before processing untrusted input or performing complex operations.

---

## The qconn Security Risk

A particularly important security consideration is **qconn**, the agent process that the Momentics IDE uses to communicate with target systems. qconn enables file transfer, process debugging, remote command execution, and other development conveniences. It runs as root by default.

**Never include qconn in a production system.** It is a development tool with no place in deployed devices. A network-accessible qconn instance is equivalent to leaving an unlocked root shell exposed to the network. Attackers who discover qconn can upload malicious files, execute arbitrary commands, debug and modify running processes, and effectively own the system.

The rule is simple: qconn belongs in development images, not production images. Remove it from your production boot scripts and IFS configurations.

---

## From Coarse to Fine-Grained Privileges

Traditional Unix security is binary: you are root, or you are not. Root can do everything; non-root can do very little. This coarse model is insufficient for modern embedded systems where processes need selective access to specific hardware or system functions without receiving full root privileges.

QNX addresses this through **process manager abilities** — a fine-grained capability system that replaces the root/not-root dichotomy with precise, per-process permissions.

### Process Manager Abilities

The `procmgr_ability()` function allows explicit control over dozens of specific system capabilities. Rather than granting a process full root access, you grant it only the specific abilities it requires.

Examples of controllable abilities include:

| Ability Category | Specific Capability | Example Use Case |
|-----------------|---------------------|----------------|
| **Interrupt management** | Configure interrupt handlers | Device driver attaching to hardware interrupts |
| **I/O port access** | Read/write x86 I/O ports | Legacy hardware driver on x86 platforms |
| **Memory mapping** | Map physical memory into process space | Graphics driver mapping frame buffer |
| **Timer access** | Set high-resolution timers | Real-time control loop requiring precise timing |
| **Priority manipulation** | Raise thread priority above own | Priority inheritance in mutex operations |
| **Process management** | Kill or signal other processes | Process supervisor or watchdog application |
| **Trace and debug** | Attach to other processes for debugging | Development diagnostic tool |

Each ability can be granted, denied, or constrained with specific limits. A process might receive the ability to map memory but only within a specific physical address range, or the ability to raise priority but only up to a defined ceiling.

```
┌─────────────────────────────────────────────────────────────────────┐
│           PROCESS MANAGER ABILITIES: FINE-GRAINED CONTROL              │
│                                                                      │
│  COARSE MODEL (Traditional Unix — insufficient for embedded)         │
│  ─────────────────────────────────────────────────────────────       │
│                                                                      │
│  ┌─────────────────┐              ┌─────────────────┐               │
│  │     ROOT        │              │   NON-ROOT      │               │
│  │                 │              │                 │               │
│  │  Can do ALL of: │              │  Can do NONE of:│               │
│  │  • Interrupts   │              │  • Interrupts   │               │
│  │  • I/O ports    │              │  • I/O ports    │               │
│  │  • Memory map   │              │  • Memory map   │               │
│  │  • Timers       │              │  • Timers       │               │
│  │  • Kill procs   │              │  • Kill procs   │               │
│  │  • Trace debug  │              │  • Trace debug  │               │
│  │  • ...          │              │  • ...          │               │
│  └─────────────────┘              └─────────────────┘               │
│                                                                      │
│  PROBLEM: Serial driver needs interrupts + I/O ports.               │
│  With coarse model: must be ROOT → gets ALL abilities → dangerous.    │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  FINE-GRAINED MODEL (QNX procmgr_ability — precise control)          │
│  ─────────────────────────────────────────────────────────────       │
│                                                                      │
│  ┌─────────────────┐              ┌─────────────────┐               │
│  │  SERIAL DRIVER  │              │  USER APP       │               │
│  │  UID: 100       │              │  UID: 101       │               │
│  │                 │              │                 │               │
│  │  Abilities:     │              │  Abilities:     │               │
│  │  ✓ Interrupts   │              │  ✗ Interrupts   │               │
│  │  ✓ I/O ports    │              │  ✗ I/O ports    │               │
│  │  ✗ Memory map   │              │  ✗ Memory map   │               │
│  │  ✗ Timers       │              │  ✗ Timers       │               │
│  │  ✗ Kill procs   │              │  ✗ Kill procs   │               │
│  │  ✗ Trace debug  │              │  ✗ Trace debug  │               │
│  └─────────────────┘              └─────────────────┘               │
│                                                                      │
│  RESULT: Serial driver can do its job (handle interrupts, access     │
│  UART hardware) but CANNOT map arbitrary memory, kill other processes,  │
│  or debug the system. If compromised, damage is contained.            │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  NETWORK STACK EXAMPLE:                                               │
│  ┌─────────────────┐                                                  │
│  │  io-sock        │  UID: 102                                        │
│  │                 │                                                  │
│  │  Abilities:     │                                                  │
│  │  ✓ Interrupts   (for network card IRQs)                          │
│  │  ✓ Memory map   (for DMA buffer rings)                             │
│  │  ✓ Timers       (for TCP timeouts, retransmits)                    │
│  │  ✗ Kill procs   (cannot terminate other processes)                 │
│  │  ✗ Trace debug  (cannot inspect other processes)                     │
│  │  ✗ I/O ports    (doesn't need x86 legacy I/O)                      │
│  └─────────────────┘                                                  │
│                                                                      │
│  Compromised network stack cannot kill safety-critical brake control.   │
│  It cannot map arbitrary physical memory. Damage is limited to network. │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Security Policy Generation with secpolgenerate

Manually assigning process manager abilities to every process is tedious, error-prone, and requires deep knowledge of what each process actually needs. QNX provides an automated tool called **secpolgenerate** that observes system behavior and generates a security policy file based on actual observed requirements.

### How secpolgenerate Works

The tool operates in observation mode. You run it on a system that is exercising all normal functionality — all drivers loaded, all applications running, all typical operations performed. As the system runs, secpolgenerate monitors which abilities each process actually uses.

When a device driver calls `mmap()` to map hardware registers, secpolgenerate records that this driver needs the memory mapping ability. When an application calls `kill()` to signal another process, it records the signal ability. When a process attaches an interrupt handler, it records the interrupt ability. Over time, a complete picture emerges of what each process legitimately requires.

After the observation period, secpolgenerate outputs a policy file — a text file listing each process and the abilities it was observed to use. This becomes the starting point for your production security policy.

```
┌─────────────────────────────────────────────────────────────────────┐
│           SECPOLGENERATE: AUTOMATED POLICY DISCOVERY                   │
│                                                                      │
│  STEP 1: Build minimal system with all applications and drivers        │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  Boot script includes:                                                │
│  • procnto (kernel — always runs as root)                             │
│  • devc-ser8250 (serial driver)                                       │
│  • devb-eide (disk driver)                                            │
│  • io-sock (network stack)                                            │
│  • pci-server (PCI bus manager)                                       │
│  • my_application (main application)                                  │
│  • my_logger (data logging service)                                     │
│                                                                      │
│  All processes currently running as root (development mode).          │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 2: Run secpolgenerate during normal operation                    │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  $ secpolgenerate -o my_policy.pol &                                   │
│                                                                      │
│  System exercises all normal functions:                                │
│  • Serial driver handles console input/output                           │
│  • Disk driver reads/writes files                                      │
│  • Network stack sends/receives packets                                │
│  • Application performs its business logic                              │
│  • Logger writes data to disk                                          │
│                                                                      │
│  secpolgenerate observes and records:                                  │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  OBSERVATION LOG (simplified)                                │    │
│  │                                                             │    │
│  │  devc-ser8250:                                              │    │
│  │    • Used: interrupt attach (UART IRQ)                       │    │
│  │    • Used: I/O port access (x86 UART registers)              │    │
│  │    • Used: priority raise (for interrupt thread)             │    │
│  │    • NOT used: memory map, kill, trace, timer config        │    │
│  │                                                             │    │
│  │  devb-eide:                                                 │    │
│  │    • Used: interrupt attach (disk controller IRQ)            │    │
│  │    • Used: memory map (DMA buffer rings)                     │    │
│  │    • Used: DMA setup (scatter-gather lists)                  │    │
│  │    • NOT used: I/O ports, kill, trace                        │    │
│  │                                                             │    │
│  │  io-sock:                                                   │    │
│  │    • Used: interrupt attach (network card IRQs)              │    │
│  │    • Used: memory map (NIC DMA descriptors)                  │    │
│  │    • Used: timer config (TCP retransmit timers)              │    │
│  │    • NOT used: kill, trace, I/O ports                        │    │
│  │                                                             │    │
│  │  my_application:                                              │    │
│  │    • Used: file read/write (via standard POSIX)              │    │
│  │    • Used: message send (IPC to other processes)             │    │
│  │    • NOT used: ANY privileged abilities                      │    │
│  │                                                             │    │
│  │  my_logger:                                                 │    │
│  │    • Used: file write (log files)                            │    │
│  │    • Used: timer (periodic flush)                            │    │
│  │    • NOT used: interrupts, memory map, I/O ports             │    │
│  │                                                             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 3: Generated policy file (my_policy.pol) — text format         │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  The policy file is a text file that can be edited:                    │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  # Generated by secpolgenerate vX.Y.Z                        │    │
│  │  # Edit as needed for production deployment                  │    │
│  │                                                             │    │
│  │  type "devc-ser8250" {                                      │    │
│  │    ability = { interrupt, io_port, priority };              │    │
│  │    uid = 100;                                               │    │
│  │    gid = 100;                                               │    │
│  │  };                                                         │    │
│  │                                                             │    │
│  │  type "devb-eide" {                                         │    │
│  │    ability = { interrupt, memory_map, dma };              │    │
│  │    uid = 101;                                               │    │
│  │    gid = 101;                                               │    │
│  │  };                                                         │    │
│  │                                                             │    │
│  │  type "io-sock" {                                           │    │
│  │    ability = { interrupt, memory_map, timer };              │    │
│  │    uid = 102;                                               │    │
│  │    gid = 102;                                               │    │
│  │  };                                                         │    │
│  │                                                             │    │
│  │  type "my_application" {                                    │    │
│  │    ability = { };  # No special abilities needed            │    │
│  │    uid = 500;                                               │    │
│  │    gid = 500;                                               │    │
│  │  };                                                         │    │
│  │                                                             │    │
│  │  type "my_logger" {                                         │    │
│  │    ability = { timer };                                     │    │
│  │    uid = 501;                                               │    │
│  │    gid = 501;                                               │    │
│  │  };                                                         │    │
│  │                                                             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 4: System integrator reviews and refines                       │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  The generated policy is a STARTING POINT, not a final answer.        │
│  The system integrator should:                                         │
│                                                                      │
│  • Verify each ability is truly necessary                             │
│  • Remove abilities that were observed but not required               │
│  • Add constraints (e.g., memory map only specific address ranges)     │
│  • Add abilities for rare code paths not exercised during observation   │
│  • Assign appropriate user IDs and group IDs                            │
│  • Test thoroughly in staging before production deployment             │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 5: Policy loaded at boot time                                  │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  The refined policy file is included in the boot image. At startup:    │
│                                                                      │
│  1. procnto loads the policy file                                     │
│  2. Each process is assigned its defined type and abilities             │
│  3. Processes run with restricted privileges from the start           │
│  4. Any attempt to use unassigned abilities fails with an error         │
│                                                                      │
│  Result: Defense in depth. Compromised processes have minimal damage.   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Policy File Integration with POSIX Permissions

The security policy attributes work alongside the traditional POSIX file permissions. The three-octal-digit permission model (read/write/execute for owner/group/other) remains in effect for filesystem access. The security policy adds process-level capability control that operates independently.

A complete security posture combines both:

| Layer | Controls | Example |
|-------|----------|---------|
| **POSIX file permissions** | Who can read/write/execute files and devices | Only group `netdev` can write to `/dev/socket` |
| **Security policy abilities** | What system capabilities a process can use | `io-sock` can attach interrupts but cannot kill processes |
| **User/group identity** | Which processes run as which users | `io-sock` runs as UID 102, not root |

This layered approach means that even if an attacker bypasses one layer, others remain in place. A compromised process running as a non-root user with limited abilities and restricted file access poses minimal threat to the overall system.

---

## Summary

| Concept | Key Point |
|---------|-----------|
| **POSIX permissions** | Standard Unix file permissions for files, directories, and devices |
| **Root in IFS** | Everything in the boot image runs as root — acceptable for development, dangerous for production |
| **Privilege reduction** | Use launcher programs, login, setuid, or C API calls to drop from root to restricted users |
| **qconn danger** | Never include in production — it is a root-equivalent remote access tool for development only |
| **Process manager abilities** | Fine-grained replacement for root/not-root with per-process capability control |
| **procmgr_ability()** | Programmatic API for assigning specific capabilities to processes |
| **secpolgenerate** | Automated tool that observes system behavior and generates a starting policy file |
| **Policy refinement** | Generated policy is a starting point — must be reviewed, edited, and tested by system integrators |
| **Layered security** | POSIX permissions + security policy abilities + user identity work together for defense in depth |

---

*Happy learning!* 🚀
