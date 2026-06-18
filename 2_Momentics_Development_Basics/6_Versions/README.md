
# QNX SDK Version Management and Environment Configuration

## Overview

This module covers how to verify and manage QNX SDK versions across your host development workstation and target systems. Version mismatches between host and target can cause subtle compatibility issues, build failures, or runtime errors. You will learn how to set up command-line tool environments, check versions on both host and target, configure the IDE to use the correct SDK, and switch between multiple SDKs and workspaces when needed.

---

## Setting Up Command-Line Environment Variables

Before using QNX command-line tools on your host, you must configure the environment variables that tell the system where your QNX Software Development Kit (SDK) is installed and which toolchain to use.

### Windows Host Setup

On Windows, the SDK provides a batch file that sets all necessary environment variables.

Locate the batch file in your SDK installation directory. The typical path structure is:

```
<QNX_SDK_INSTALL_DIR>/
├── qnxsdk-env.bat          ← Run this file
├── host/
│   └── win64/
│       └── x86_64/
│           └── usr/
│               └── bin/      ← Tools become available after running .bat
├── target/
│   └── qnx/
│       └── x86_64/
│           └── lib/          ← Target libraries
└── ...
```

Open a command prompt, navigate to the SDK root directory, and run:

```
C:\> cd <QNX_SDK_INSTALL_DIR>
C:\<QNX_SDK_INSTALL_DIR>> qnxsdk-env.bat
```

After running the batch file, commands like `qcc`, `ntoarmv7-gcc`, `make`, and other QNX toolchain utilities become available in that terminal session.

### Linux Host Setup

On Linux, the SDK provides a shell script that you source into your current shell environment. Use the `source` command or its shorthand dot (`.`) to execute it in the current shell context.

Locate the script in your SDK installation directory:

```
/opt/qnx/qnxsdk-env.sh        ← Typical location
/opt/qnx/host/linux/x86_64/   ← Host tools
/opt/qnx/target/qnx/x86_64/   ← Target libraries
```

Open a terminal and run:

```
$ cd /opt/qnx
$ source qnxsdk-env.sh
```

Or equivalently:

```
$ . /opt/qnx/qnxsdk-env.sh
```

The `source` command is essential here — it runs the script in the current shell rather than spawning a subshell. This ensures the environment variables (PATH, QNX_HOST, QNX_TARGET, etc.) persist in your terminal session.

After sourcing, verify tools are available:

```
$ which qcc
/opt/qnx/host/linux/x86_64/usr/bin/qcc
```

---

## Why Version Matching Matters

Having different QNX versions on your host and target is a common source of problems. The host compiler generates binaries against a specific version of system libraries and headers. If the target runs a different version of those same libraries, incompatibilities can occur.

Potential issues from version mismatches:

| Scenario | Symptom | Cause |
|----------|---------|-------|
| Host SDK newer than target | Runtime crashes or undefined behavior | Binary uses newer library features not present on target |
| Host SDK older than target | Build failures or missing symbols | Headers don't define APIs that target libraries expect |
| Different patch levels | Subtle behavioral differences | Bug fixes or optimizations diverge between host and target |

Always verify that your host SDK version matches your target system version before building or debugging.

---

## Checking Versions on the Target

The target is the QNX system running on your embedded device or virtual machine. Two commands provide version information: `uname` for the system and `use` for individual files.

### System Version with `uname`

The `uname` command displays operating system information. The `-a` flag shows all available details.

```
$ uname -a
QNX my_target 8.0.0 2024/01/15-14:32:00EST x86_64
```

Key information from this output:
- **Operating system**: QNX
- **Node name**: my_target
- **Release version**: 8.0.0 — this is your SDK version
- **Build date and time**: When the OS image was created
- **Architecture**: x86_64 (or armle-v7, aarch64le, etc.)

The release version (8.0.0 in this example) is what you compare against your host SDK version.

### File Version with `use`

The `use` command examines QNX executable and library files to extract embedded version information. The `-i` flag shows the QNX build ID.

```
$ use -i libc.so.6
QNX_BUILDID: 2024/01/15-14:30:00EST
```

This build ID corresponds to the exact build of the C library on your target. Compare this against the same file on your host to verify they come from the same SDK build.

Checking multiple system libraries:

```
$ use -i libc.so.6
$ use -i libsocket.so.3
$ use -i libpthread.so.3
```

All should show matching build IDs if your target system is consistent.

---

## Checking Versions on the Host

After setting up your environment variables, you can verify host-side versions by examining the same files in the SDK's target directory.

### Locating Host-Side Library Files

The SDK installation contains a `target` directory that mirrors the target system's file structure. This is where the host compiler finds headers and libraries when building your applications.

Typical path structure:

```
<QNX_SDK>/
├── host/
│   └── linux/x86_64/         ← Host tools (qcc, make, gdb)
├── target/
│   └── qnx/
│       └── x86_64/           ← Target files for x86_64 builds
│           ├── lib/            ← Target libraries (libc.so.6, etc.)
│           ├── usr/
│           │   └── include/    ← System headers (stdio.h, pthread.h)
│           └── ...
```

Navigate to the target library directory:

```
$ cd $QNX_TARGET/x86_64/lib
```

Or on Windows:

```
C:\> cd %QNX_TARGET%\x86_64\lib
```

### Verifying Host Library Versions

Use the same `use -i` command on host-side library files:

```
$ use -i libc.so.6
QNX_BUILDID: 2024/01/15-14:30:00EST
```

Compare this build ID against what you saw on the target. They must match exactly.

```
┌─────────────────────────────────────────────────────────────────────┐
│              VERSION VERIFICATION WORKFLOW                             │
│                                                                      │
│  TARGET (QNX device or VM)              HOST (Development workstation)  │
│  ────────────────────────               ─────────────────────────────  │
│                                                                      │
│  $ uname -a                             $ source qnxsdk-env.sh       │
│  QNX 8.0.0 ...                          (or run qnxsdk-env.bat)       │
│       ↑                                $ cd $QNX_TARGET/x86_64/lib  │
│       │                                $ use -i libc.so.6             │
│       │                                QNX_BUILDID: 2024/01/15...   │
│       │                                       ↑                     │
│       │                                       │                     │
│       └───────────────────────────────────────┘                     │
│                    Must match!                                        │
│                                                                      │
│  If versions differ:                                                  │
│  • Rebuild target image with matching SDK                             │
│  • Or reinstall host SDK to match target version                      │
│  • Or use IDE SDK switching to select correct version                 │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Configuring SDK Versions in the IDE

The QNX Momentics IDE maintains its own SDK configuration, independent of command-line environment variables. You must ensure the IDE uses the same SDK as your target.

### Checking Current SDK in IDE

1. Open the IDE
2. Select **Window → Preferences** (or **Edit → Preferences** on some platforms)
3. Navigate to **QNX** in the left panel
4. The right panel displays:
   - **Current SDK version** — The active QNX SDK
   - **QNX SDK path** — Root directory of the installed SDK
   - **Host path** — Location of host tools
   - **Target path** — Location of target libraries and headers

```
┌─────────────────────────────────────────────────────────────────────┐
│              IDE PREFERENCES: QNX SDK CONFIGURATION                    │
│                                                                      │
│  Window → Preferences → QNX                                          │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  QNX SDK Preferences                                         │   │
│  │                                                             │   │
│  │  Current SDK: [QNX 8.0.0______________________________]   │   │
│  │                                                             │   │
│  │  SDK Path: /opt/qnx/8.0.0                                    │   │
│  │                                                             │   │
│  │  Host: /opt/qnx/8.0.0/host/linux/x86_64                     │   │
│  │  Target: /opt/qnx/8.0.0/target/qnx/x86_64                     │   │
│  │                                                             │   │
│  │  [Apply]  [Apply and Close]  [Cancel]                       │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  Verify these paths match your target system version.                │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

Verify that the SDK version shown here matches what `uname -a` reported on your target. Also confirm that the target path contains the same library files you checked with `use -i`.

### Platform Path Details

The QNX preferences panel may show platform-specific paths. These indicate where the IDE looks for:
- **Compiler executables** (qcc, gcc, ld)
- **System headers** (stdio.h, pthread.h, sys/neutrino.h)
- **System libraries** (libc.so, libpthread.so)
- **Debugger components** (gdb, pdebug)

If these paths are incorrect, builds will use wrong headers or link against wrong libraries, producing binaries incompatible with your target.

---

## Switching Between Multiple SDKs

Development environments often require working with multiple QNX versions — perhaps maintaining a legacy product on QNX 7.1 while developing a new product on QNX 8.0.

### SDK Switching in IDE

If you have multiple SDKs installed, the IDE can switch between them:

1. **Window → Preferences → QNX**
2. The SDK dropdown shows all installed QNX SDKs
3. Select the desired SDK version
4. Click **Apply** or **Apply and Close**

The IDE will momentarily shut down and automatically restart with the new SDK configuration. All projects, build configurations, and target connections will use the newly selected SDK after restart.

```
┌─────────────────────────────────────────────────────────────────────┐
│              SWITCHING SDKs IN THE IDE                                 │
│                                                                      │
│  Window → Preferences → QNX                                          │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  QNX SDK Preferences                                         │   │
│  │                                                             │   │
│  │  Current SDK: [▼ QNX 8.0.0____________________________]   │   │
│  │               [  QNX 7.1.0____________________________]   │   │
│  │               [  QNX 8.0.0 SP1________________________]   │   │
│  │                                                             │   │
│  │  Select different SDK → Apply → IDE restarts automatically │   │
│  │                                                             │   │
│  │  [Apply]  [Apply and Close]  [Cancel]                       │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ⚠️ IDE will close and reopen. Save all work before switching.     │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

Important considerations when switching SDKs:
- Save all open files and projects before switching — the IDE restart is automatic and may not prompt for unsaved changes
- Build configurations may need updating if paths changed between SDK versions
- Target connections should be verified — the new SDK may expect different target versions
- Projects built with one SDK may need clean rebuilds when switching to another

---

## Switching Workspaces

Eclipse workspaces contain all project files, IDE settings, and metadata. You may need different workspaces for different products, SDK versions, or team configurations.

### Switching Workspaces in IDE

1. Select **File → Switch Workspace**
2. A dialog shows recently used workspaces and allows browsing for new ones
3. Select the desired workspace directory
4. Click **Launch** or **OK**

The IDE shuts down and automatically restarts with the selected workspace. All projects, preferences, and window layouts from that workspace are restored.

```
┌─────────────────────────────────────────────────────────────────────┐
│              SWITCHING WORKSPACES                                      │
│                                                                      │
│  File → Switch Workspace                                             │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Switch Workspace                                            │   │
│  │                                                             │   │
│  │  Recent workspaces:                                          │   │
│  │  /home/dev/projects/qnx8_workspace                           │   │
│  │  /home/dev/projects/legacy_qnx7_workspace                    │   │
│  │  /home/dev/projects/driver_dev_workspace                       │   │
│  │                                                             │   │
│  │  [Browse...]  ← select different directory                   │   │
│  │                                                             │   │
│  │  [Launch]  [Cancel]                                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  IDE restarts with selected workspace. Each workspace can have:      │
│  • Different projects                                               │
│  • Different SDK settings                                           │
│  • Different target connections                                     │
│  • Different window layouts and perspectives                        │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

Workspace organization strategies:

| Strategy | Workspace Contents | Use Case |
|----------|-------------------|----------|
| **Per SDK version** | All projects using QNX 8.0 | Clean separation of incompatible SDKs |
| **Per product** | Projects for one embedded product | Team collaboration, consistent settings |
| **Per role** | Driver projects vs. application projects | Different build configurations, targets |
| **Per course** | Exercise projects grouped together | Learning environments, experimentation |

---

## Summary

| Task | Command/Menu | Platform |
|------|-------------|----------|
| Set up command-line environment | `qnxsdk-env.bat` | Windows |
| Set up command-line environment | `source qnxsdk-env.sh` | Linux |
| Check target system version | `uname -a` | QNX target |
| Check target file version | `use -i <filename>` | QNX target |
| Check host file version | `use -i <filename>` | Host (after env setup) |
| Check IDE SDK version | Window → Preferences → QNX | IDE |
| Switch IDE SDK | Window → Preferences → QNX → Select SDK | IDE |
| Switch workspace | File → Switch Workspace | IDE |

Version consistency between host and target is fundamental to reliable QNX development. Always verify versions before starting development, after SDK updates, or when encountering unexplained build or runtime issues.

---


*Happy coding!* 🚀
