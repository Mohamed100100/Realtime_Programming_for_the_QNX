
# Host-Target Development and Target Connection

## Overview

QNX uses a **cross-development model** where you develop on a powerful host workstation (Windows or Linux running Momentics IDE) and deploy to a target system running QNX Neutrino. The target can be physical hardware — a custom board, evaluation board from a silicon vendor, or an integrated system — or a virtual machine running QNX. This workflow requires establishing a reliable connection between host and target, which is managed through the `qconn` agent on the target and configured within the Momentics IDE.

---

## The Cross-Development Architecture

The fundamental pattern is build on host, run on target. Your host machine has the full development environment: compilers, linkers, debuggers, and the IDE. The target is typically a resource-constrained embedded device that only needs to execute your compiled programs and provide debugging information back to the host.

| Component | Role | Typical Platform |
|-----------|------|----------------|
| **Host** | Development workstation running Momentics IDE | Windows 10/11, Linux desktop |
| **Target** | QNX execution environment for built programs | ARM board, x86 SBC, VM |
| **qconn** | Target-side agent enabling IDE communication | Runs on every QNX target |
| **Network** | Primary connection medium (IP-based) | Ethernet, WiFi, virtual network |

The `qconn` process is essential. It must be running on the target for the IDE to discover, connect, and interact with that target. `qconn` handles file transfers, process launching, debugging coordination, and system information queries on behalf of the IDE.

---

## Two Primary Target Interaction Views

Once connected to a target, Momentics provides two main views for different types of interaction:

### Target Navigator

The **Target Navigator** is your window into the running system. It shows all processes currently executing on the target and provides tools for process management and analysis.

What you can do with Target Navigator:
- See all running processes with their process IDs and thread counts
- Expand individual processes to inspect their threads, states, and priorities
- Kill or signal processes directly from the IDE
- Launch analysis tools against selected processes (memory usage, CPU profiling)
- View process environment variables and command-line arguments
- Inspect thread-specific information for debugging

This view is the default in the **System Information perspective**, which is optimized for target monitoring and profiling.

### Target File System Navigator

The **Target File System Navigator** provides direct access to the target's file system as if it were a local directory tree. It enables file operations without leaving the IDE.

What you can do with File System Navigator:
- Browse the complete directory hierarchy of the target
- Copy files from host to target or target to host
- Edit files directly on the target using the IDE editor
- Create, delete, or rename directories and files
- Compare target files with local versions
- View file permissions and attributes

The file system view reflects exactly what you would see on the target's command line. If you list `/system` in the IDE and list `/system` in a target shell, the contents are identical.

---

## Creating a Target Connection

### Method 1: Connecting to an Existing Target

If you have a running QNX target — physical hardware or an already-configured virtual machine — you connect to it using its network address.

The typical workflow:
1. Start your QNX target and ensure it boots completely
2. Determine the target's IP address using `ifconfig` at the target shell
3. In Momentics, switch to the System Information perspective or use the Target Navigator
4. Create a new target connection and enter the IP address
5. The IDE connects via `qconn` and populates the target information

Once connected, the target appears in your Target Navigator with an expandable tree showing all running processes. You can immediately begin inspecting the system, transferring files, or launching debug sessions.

```
┌─────────────────────────────────────────────────────────────────────┐
│              CONNECTING TO AN EXISTING QNX TARGET                        │
│                                                                      │
│  STEP 1: Start the target and get its IP address                      │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  QNX Target Console:                                                 │
│  $ ifconfig                                                          │
│  lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST>                     │
│      inet 127.0.0.1 netmask 0xff000000                               │
│  en0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST>             │
│      inet 192.168.56.103 netmask 0xffffff00 broadcast 192.168.56.255 │
│                                                                      │
│  Note the IP address: 192.168.56.103                                  │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 2: Create target connection in Momentics IDE                    │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  IDE → Window → Perspective → Open Perspective → Other...             │
│  → Select "QNX System Information" → Open                             │
│                                                                      │
│  In Target Navigator:                                                │
│  Right-click → New QNX Target...                                      │
│  → Enter IP: 192.168.56.103                                          │
│  → Finish                                                            │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 3: Verify connection                                            │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  Target Navigator shows:                                             │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  192.168.56.103 [connected]                                  │    │
│  │  ├── procnto (PID 1)                                         │    │
│  │  ├── devc-ser8250 (PID 2)                                    │    │
│  │  ├── devc-con (PID 3)                                        │    │
│  │  ├── io-sock (PID 4)                                         │    │
│  │  ├── pipe (PID 5)                                            │    │
│  │  ├── random (PID 6)                                          │    │
│  │  ├── qconn (PID 7)                                           │    │
│  │  ├── my_application (PID 1024)                               │    │
│  │  └── ...                                                     │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  Connection established. Ready for development, debugging, and      │
│  system analysis.                                                    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Method 2: Creating a Virtual Machine Target

If you do not have physical hardware available, Momentics can create a QNX virtual machine target automatically. This is ideal for learning, experimentation, and early development before hardware is ready.

The IDE uses the `mkqnximage` tool to build a complete QNX virtual machine image, then configures it in your virtualization platform (VirtualBox, VMware, etc.).

Important considerations for VM targets:
- They are created for **experimentation and learning**, not production deployment
- Security configurations may not follow production hardening guidelines
- They provide a quick way to get a running QNX environment without hardware
- The VM platform (x86 or ARM) must match your development target architecture

The creation process:
1. Select New QNX Target → QNX Virtual Machine Target
2. Choose your virtualization platform (VirtualBox, VMware)
3. Select target architecture (x86 or ARM)
4. Leave optional fields blank for automatic configuration
5. The IDE builds the image, configures the VM, and establishes connection

The build process takes several minutes as `mkqnximage` constructs the boot image, file system, and virtual machine configuration. You can monitor progress in the IDE console. Once complete, the target transitions from "not connected" to "connected" automatically.

```
┌─────────────────────────────────────────────────────────────────────┐
│              CREATING A VIRTUAL MACHINE TARGET                         │
│                                                                      │
│  STEP 1: Select VM target creation in IDE                           │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  IDE → New QNX Target → QNX Virtual Machine Target                    │
│                                                                      │
│  Configuration:                                                      │
│  • VM Platform: VirtualBox (or VMware)                              │
│  • Target Architecture: x86 (or ARM, matching your project)             │
│  • Additional Options: leave blank for defaults                       │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 2: IDE builds VM image automatically                           │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  Console output:                                                      │
│  Running mkqnximage...                                               │
│  Creating boot image...                                              │
│  Creating virtual disk...                                            │
│  Configuring VirtualBox VM...                                        │
│  Starting virtual machine...                                          │
│  Waiting for qconn connection...                                      │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 3: Target becomes available                                     │
│  ─────────────────────────────────────────────────────────────         │
│                                                                      │
│  Target Navigator shows:                                             │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  vm1 [connected]  ← status changes from "not connected"       │    │
│  │  ├── procnto (PID 1)                                         │    │
│  │  ├── devc-ser8250 (PID 2)                                    │    │
│  │  └── ...                                                     │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  The VM is fully functional for development and testing.              │
│  Remember: VM targets are for experimentation, not production.        │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## System Information Perspective

The **System Information perspective** is the primary workspace for target interaction. It provides comprehensive views for analyzing system state and is the recommended perspective when working with connected targets.

Key views available in this perspective:
- **Target Navigator** — Process listing and basic process control
- **Process Information** — Detailed process attributes, environment, memory maps
- **Thread Information** — Thread states, priorities, CPU usage per thread
- **Memory Information** — Memory allocation, heap usage, shared memory segments
- **Connection Information** — IPC connections, message passing channels
- **Signal Information** — Pending and blocked signals per process
- **MMap Information** — Memory-mapped regions and their attributes

These views update dynamically as you select different processes in the Target Navigator. Clicking a process in the navigator automatically populates the detail views with that process's specific information.

---

## Target Shell Access

While the IDE provides rich graphical tools, sometimes you need direct command-line access to the target. Momentics supports multiple methods for opening a terminal session.

### Methods for Target Shell Access

| Method | Description | Use Case |
|--------|-------------|----------|
| **IDE SSH Terminal** | Built-in SSH client within Eclipse | Quick command execution without leaving IDE |
| **Physical Console** | Direct keyboard/monitor or serial connection | When network is unavailable or for bootloader access |
| **Serial Connection** | RS-232 or USB-serial direct link | Low-level debugging, board bring-up |
| **External SSH Client** | Command-line SSH from host terminal | Power users, scripting, automation |

### Configuring SSH Terminal in IDE

The IDE's built-in SSH terminal uses an Eclipse SSH plugin. A common issue is that modern SSH servers reject older key exchange algorithms that the Eclipse plugin uses by default. If you encounter an "algorithm negotiation failed" error, you need to enable legacy algorithm support on the target.

The fix involves editing the SSH server configuration file on the target:
1. Locate the SSH configuration file (typically in `/system/etc/ssh/` or `/etc/ssh/`)
2. Add the line `HostAlgorithms +ssh-rsa` to enable the required algorithm
3. Save the file and reboot the target (or restart the SSH service)
4. Reconnect from the IDE SSH terminal

This configuration change allows the Eclipse SSH plugin to negotiate a compatible connection with the target's SSH server.

```
┌─────────────────────────────────────────────────────────────────────┐
│              SSH TERMINAL CONFIGURATION                                │
│                                                                      │
│  PROBLEM: IDE SSH connection fails with "algorithm negotiation failed" │
│  ────────────────────────────────────────────────────────────────      │
│                                                                      │
│  Cause: Eclipse SSH plugin uses ssh-rsa algorithm                    │
│  Modern SSH servers disable this by default for security reasons     │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  SOLUTION: Enable ssh-rsa on the target SSH server                   │
│  ────────────────────────────────────────────────────────────────      │
│                                                                      │
│  STEP 1: Edit SSH configuration via IDE File System Navigator         │
│                                                                      │
│  Navigate to: /system/etc/ssh/ssh_config (or /etc/ssh/ssh_config)     │
│  Right-click → Edit                                                   │
│                                                                      │
│  Add line: HostAlgorithms +ssh-rsa                                    │
│                                                                      │
│  Save file (IDE prompts to save modified file)                        │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 2: Reboot target to apply changes                               │
│                                                                      │
│  Target console: shutdown                                              │
│  (or reboot command if available)                                     │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  STEP 3: Connect SSH terminal from IDE                                │
│                                                                      │
│  IDE → Right-click target → Open SSH Terminal                         │
│  → Enter credentials (default: root)                                  │
│  → Connection established                                             │
│                                                                      │
│  Alternative: Use external SSH client:                                │
│  $ ssh root@192.168.56.103                                           │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  NOTE: For production systems, use modern SSH clients and disable      │
│  legacy algorithms. The IDE SSH plugin limitation is a development   │
│  convenience issue, not a production security model.                    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## File System Synchronization

A powerful feature of the target connection is the ability to edit files directly on the target through the IDE. When you open a file from the Target File System Navigator, the IDE downloads it to a local buffer, allows editing with full syntax highlighting and code completion, and saves changes back to the target.

This is particularly useful for:
- Modifying configuration files on the target without switching tools
- Editing scripts and build files during system bring-up
- Quick patches and testing without full rebuild cycles
- Comparing target files with your local project versions

The synchronization is bidirectional — you can copy files from host to target (deploying your built application) or from target to host (retrieving logs or generated data).

---

## Summary

| Task | How to Do It | Key View/Tool |
|------|-------------|---------------|
| Connect to existing target | Enter IP address in Target Navigator | Target Navigator |
| Create VM target | New QNX Target → Virtual Machine | mkqnximage (automatic) |
| Browse target processes | Expand target in Target Navigator | Target Navigator |
| Inspect process details | Select process, view detail panes | System Information perspective |
| Transfer files | Drag-and-drop in File System Navigator | Target File System Navigator |
| Edit target files | Double-click file in File System Navigator | IDE Editor |
| Open target shell | Right-click target → SSH Terminal | SSH Terminal |
| Kill target process | Right-click process → Terminate | Target Navigator |

The host-target development model is central to QNX development. Mastering target creation, connection, and the various interaction views enables efficient debugging, testing, and deployment of embedded applications.

---


*Happy coding!* 🚀
