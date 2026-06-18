
# Module 2: Momentics Development Basics

## Overview

This module introduces the **QNX Momentics IDE** — the integrated development environment for building, running, and debugging QNX applications. You will gain sufficient familiarity with the IDE to complete exercises in the Realtime Programming and Drivers courses. The module covers IDE operation, Eclipse fundamentals (the framework underlying Momentics), host-target development workflows, and the complete edit-compile-run-debug cycle for QNX programs.

---

## Learning Objectives

By the end of this module, you will be able to:

- Navigate and operate the QNX Momentics IDE effectively
- Understand Eclipse framework basics (workspaces, perspectives, views)
- Configure and use host-target development (build on host, run on target)
- Edit, compile, run, and debug QNX applications
- Set up target connections for remote deployment and debugging

---

## What Is QNX Momentics IDE

The QNX Momentics IDE is a customized Eclipse-based development environment specifically designed for QNX Neutrino RTOS development. It provides:

| Feature | Purpose |
|---------|---------|
| **Project management** | Create and organize C/C++ projects for QNX |
| **Code editing** | Syntax highlighting, code completion, navigation |
| **Cross-compilation** | Build ARM, x86, or other target binaries from your host |
| **Target management** | Connect to QNX targets via IP, serial, or JTAG |
| **Remote debugging** | GDB-based debugging with breakpoints, stepping, variable inspection |
| **System profiling** | Analyze CPU usage, memory, and thread behavior |
| **Image building** | Create bootable QNX OS images (IFS) |

---

## Host-Target Development

QNX development follows a **host-target** model:

- **Host**: Your development workstation (Windows, Linux, or QNX) running Momentics IDE
- **Target**: The embedded device or virtual machine running QNX Neutrino

You write and compile code on the host, then deploy and debug on the target via network or serial connection.

```
┌─────────────────────────────────────────────────────────────────────┐
│              HOST-TARGET DEVELOPMENT MODEL                              │
│                                                                      │
│  ┌─────────────────────────┐        ┌─────────────────────────┐   │
│  │      HOST (Momentics)   │        │      TARGET (QNX)       │   │
│  │                         │        │                         │   │
│  │  • Write code           │        │  • Run compiled program   │   │
│  │  • Compile (cross-build)│        │  • Execute in real-time   │   │
│  │  • Link libraries       │───►   │  • Debug with GDB agent   │   │
│  │  • Package for deploy   │  deploy│  • Return results/logs    │   │
│  │                         │        │                         │   │
│  │  IDE provides:          │        │  qconn agent provides:    │   │
│  │  - Editor, compiler     │        │  - Process launch         │   │
│  │  - Debugger frontend    │◄───   │  - Debug stub (pdebug)    │   │
│  │  - Target navigator     │  debug │  - File transfer          │   │
│  │                         │        │                         │   │
│  └─────────────────────────┘        └─────────────────────────┘   │
│                                                                      │
│  Connection: TCP/IP network, serial port, or JTAG debug interface      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Eclipse Framework Basics

Momentics is built on the **Eclipse** open-source platform. Key Eclipse concepts:

| Concept | Description |
|---------|-------------|
| **Workspace** | Directory containing all your projects, settings, and metadata |
| **Perspective** | A window layout optimized for a task (C/C++, Debug, QNX System Information) |
| **View** | A panel showing specific information (Project Explorer, Console, Variables, Breakpoints) |
| **Editor** | Where you write and edit source code |
| **Project** | A collection of source files, build settings, and configurations |

**Common Perspectives in Momentics:**

- **C/C++ Perspective**: Code editing, project building, navigation
- **Debug Perspective**: Breakpoints, stepping, variable inspection, call stack
- **QNX System Perspective**: Target connections, process listing, memory inspection

---

## Module Topics

| Topic | What You Will Learn |
|-------|---------------------|
| **IDE Basics** | Launching Momentics, creating workspaces, navigating perspectives |
| **Project Creation** | New QNX C/C++ project, selecting target platform (ARM, x86), build configurations |
| **Code Editing** | Editor features, code completion, syntax highlighting, navigation |
| **Building** | Compiling for target, managing build variants (debug/release), handling errors |
| **Target Connection** | Adding target IP, testing connection, browsing target filesystem |
| **Running** | Deploying binary to target, launching remotely, viewing console output |
| **Debugging** | Setting breakpoints, single-stepping, inspecting variables, watching expressions |
| **Target Navigator** | Viewing processes, threads, memory usage on the running target |

---

## Prerequisites

Before starting this module, ensure you have:

- QNX Momentics IDE installed (from QNX Software Development Platform)
- A QNX target available (hardware board, virtual machine, or QNX emulator)
- Network connectivity between host and target (or serial connection configured)
- Target running `qconn` agent for IDE communication

---

## Summary

The QNX Momentics IDE is your primary tool for embedded QNX development. It combines Eclipse's familiar interface with QNX-specific features for cross-compilation, remote deployment, and real-time debugging. Mastering the host-target workflow — building on your development workstation and running on the actual QNX target — is essential for all subsequent development work.

---


*Happy coding!* 🚀
