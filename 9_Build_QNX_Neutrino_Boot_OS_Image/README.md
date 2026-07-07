# QNX Boot Sequence, Buildfiles, and OS Images

---

## Boot Sequence Overview

```
QNX Boot Sequence
═══════════════════════════════════════════════════════════════════

   Step 1          Step 2              Step 3           Step 4
   ──────          ──────              ──────           ──────

   ┌─────┐        ┌─────────┐         ┌─────────┐     ┌──────────┐
   │ CPU │   ──►  │  IPL    │   ──►   │ Startup │ ──► │ procnto  │
   │runs │        │  Code   │         │  Code   │     │(kernel + │
   │first│        │         │         │         │     │process   │
   └─────┘        └─────────┘         └─────────┘     │ manager) │
                                                      └────┬─────┘
                                                           │
                                                           ▼
                                                      ┌──────────┐
                                                      │  Boot    │
                                                      │  Script  │
                                                      │  Runs    │
                                                      └──────────┘
                                                           │
                                                           ▼
                                                      ┌──────────┐
                                                      │ Drivers  │
                                                      │ Servers  │
                                                      │ Clients  │
                                                      └──────────┘


Step 1: CPU runs first
──────────────────────
   What happens next depends on target board:

   ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
   │  Virtual Machine │    │   x86 with UEFI  │    │  Embedded Board │
   │  (VMware/QEMU)   │    │                  │    │                 │
   │                  │    │                  │    │                 │
   │  BIOS or virtual │    │  UEFI firmware   │    │  ROM monitor    │
   │  BIOS extension  │    │  runs first      │    │  (e.g., U-Boot) │
   └─────────────────┘    └─────────────────┘    └─────────────────┘

   Or: IPL code on Flash (no ROM monitor)


Step 2: IPL (Initial Program Loader)
────────────────────────────────────
   • Comes with QNX Board Support Package (BSP)
   • Does chip select setup
   • Sets up RAM
   • Jumps to startup code

   NOTE: Many possible routes to startup. Depends on board.


Step 3: Startup Code
─────────────────────
   • Sets up hardware
   • Hardware discovery
   • Prepares environment for procnto
   • Board-specific


Step 4: procnto
───────────────
   • Process Manager + Microkernel combined
   • Runs the boot script
   • Boot script starts drivers, servers, clients


Step 5: System Running
──────────────────────
   • All programs from boot script are running
   • System is fully booted
```

---

## Secure Boot

```
Secure Boot Chain of Trust
═══════════════════════════════════════════════════════════════════

   Hardware Secure Environment
   ───────────────────────────
   ┌─────────────────────────────────────────────────────────────┐
   │  EEPROM / Hardware Store                                    │
   │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │
   │  │ Hash of     │  │ Hash of     │  │ Hash of     │       │
   │  │ ROM Monitor │  │ IPL Code    │  │ OS Image    │       │
   │  │ (stored)    │  │ (stored)    │  │ (stored)    │       │
   │  └─────────────┘  └─────────────┘  └─────────────┘       │
   └─────────────────────────────────────────────────────────────┘
            │                 │                 │
            ▼                 ▼                 ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Boot Verification Chain                                    │
   │                                                             │
   │  CPU ──► Secure Env ──► ROM Monitor ──► IPL ──► Image      │
   │            │                │            │         │        │
   │            │                │            │         │        │
   │            ▼                ▼            ▼         ▼        │
   │         Compute          Compare      Compare    Compare   │
   │         hash             with stored  with stored with stored│
   │         at boot          hash         hash       hash       │
   │                                                             │
   │  If ANY hash mismatch → BOOT ABORTED (tampered!)           │
   └─────────────────────────────────────────────────────────────┘


QNX Trusted Disk (QTD)
══════════════════════
   ┌─────────────────────────────────────────────────────────────┐
   │  Flash File System Layer                                    │
   │                                                             │
   │  ┌─────────────────────────────────────────────────────┐   │
   │  │  QTD (QNX Trusted Disk)                              │   │
   │  │  • Verifies binary files on flash filesystem        │   │
   │  │  • Ensures executables not tampered with           │   │
   │  │  • Extends chain of trust to runtime files         │   │
   │  └─────────────────────────────────────────────────────┘   │
   │                          │                                  │
   │                          ▼                                  │
   │  ┌─────────────────────────────────────────────────────┐   │
   │  │  Verified binaries: drivers, servers, apps         │   │
   │  └─────────────────────────────────────────────────────┘   │
   └─────────────────────────────────────────────────────────────┘

   Chain of trust: Hardware ──► ROM ──► IPL ──► Image ──► Flash Files
```

---

## OS Image File

```
What is an OS Image?
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  OS Image File                                              │
   │  = A file you create                                        │
   │  = Contains executables and/or data files                 │
   │  = Can be bootable                                          │
   │                                                             │
   │  Contents:                                                  │
   │  ┌─────────────────────────────────────────────────────┐   │
   │  │  Bootstrap info (if bootable)                        │   │
   │  ├─────────────────────────────────────────────────────┤   │
   │  │  File system:                                        │   │
   │  │    • startup code                                    │   │
   │  │    • procnto                                         │   │
   │  │    • boot script                                     │   │
   │  │    • libc.so, libgcc_s.so, ldqnx-64.so             │   │
   │  │    • libsecpol.so                                    │   │
   │  │    • drivers, servers, clients, data files         │   │
   │  └─────────────────────────────────────────────────────┘   │
   └─────────────────────────────────────────────────────────────┘


/proc/boot — In-Memory File System
═══════════════════════════════════

   After boot, image contents appear in /proc/boot:

   ┌─────────────────────────────────────────────────────────────┐
   │  $ ls /proc/boot                                            │
   │                                                             │
   │  devc-ser8250    ← serial driver                           │
   │  devc-con        ← console driver                          │
   │  esh             ← embedded shell                            │
   │  libc.so         ← C library                               │
   │  libgcc_s.so     ← GCC runtime                              │
   │  ldqnx-64.so     ← runtime loader/linker                   │
   │  libsecpol.so    ← security policy library                 │
   │  hello           ← your application                         │
   │  hosts           ← data file                                │
   │  ...                                                      │
   │                                                             │
   │  Properties:                                                │
   │    • Read-only                                              │
   │    • In memory (RAM)                                        │
   │    • Created from image file at boot                        │
   └─────────────────────────────────────────────────────────────┘
```

---

## Creating Images with mkifs

```
mkifs Workflow
═══════════════════════════════════════════════════════════════════

   ┌─────────────┐         ┌─────────────┐         ┌─────────────┐
   │  Buildfile  │         │    mkifs    │         │  OS Image   │
   │  (text file)│   ──►   │  (utility)  │   ──►   │   File      │
   │             │         │             │         │             │
   │  [virtual]  │         │  Reads      │         │  Bootstrap  │
   │  startup=   │         │  buildfile  │         │  info       │
   │  [virtual]  │         │             │         │  +          │
   │  procnto    │         │  Finds all  │         │  File       │
   │             │         │  listed     │         │  system     │
   │  [+script]  │         │  files from │         │             │
   │  boot script│         │  search path│         │             │
   │             │         │             │         │             │
   │  libc.so    │         │  Packs into │         │             │
   │  libgcc_s.so│         │  image file │         │             │
   │  ldqnx-64.so│         │             │         │             │
   │  ...        │         │             │         │             │
   └─────────────┘         └─────────────┘         └─────────────┘
                                │
                                ▼
                         ┌─────────────┐
                         │  Put on     │
                         │  target     │
                         │  (board     │
                         │  specific)  │
                         └─────────────┘


mkifs Command
─────────────
   mkifs <buildfile> <output_image>

   Example:
   $ mkifs my_buildfile my_image.ifs


Image Compression
─────────────────
   [virtual=x86_64,bios +compress]
                    │
                    └── Compress image at build time
                        Startup code uncompresses at boot
```

---

## Buildfile Format

```
Buildfile Syntax
═══════════════════════════════════════════════════════════════════

   General format:
   ───────────────
   [attribute] filename [= contents]

   All components optional EXCEPT contents alone (must have filename)


   Examples:
   ─────────
   libc.so                    ← filename only
   /etc/hosts=/path/to/hosts  ← filename = contents (different path)
   [+script] boot_script      ← attribute + filename
   [uid=0] special_file       ← value attribute + filename


   Comments and blank lines:
   ──────────────────────────
   # This is a comment

   libc.so                    ← blank lines allowed


Attribute Types
═══════════════

   Boolean Attributes:
   ───────────────────
   +attribute    ← enable
   -attribute    ← disable

   Examples:
     [+script]      ← mark as boot script
     [-optional]    ← file is required (not optional)


   Value Attributes:
   ─────────────────
   attribute=value

   Examples:
     [uid=0]        ← set user ID to 0 (root)
     [gid=0]        ← set group ID to 0


Attribute Scope
═══════════════

   Per-file attribute:
   ───────────────────
   [uid=0] file1        ← applies only to file1
   [uid=100] file2      ← applies only to file2


   Global attribute (applies to all subsequent files):
   ────────────────────────────────────────────────────
   [+optional]          ← all following files are optional
   file1
   file2
   file3
   [-optional]          ← remaining files are NOT optional
   file4
   file5


Required Components for Bootable Image
════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  [virtual=board_name,bios]   ← bootstrap section            │
   │  startup=startup-board_name                                 │
   │  procnto=procnto-smp-instr                                  │
   │                                                             │
   │  [+script] .script = {                                      │
   │      # boot script commands                                 │
   │  }                                                          │
   │                                                             │
   │  libc.so                                                    │
   │  libgcc_s.so.1                                              │
   │  ldqnx-64.so.2                                              │
   │  libsecpol.so.1                                             │
   │                                                             │
   │  # your programs, drivers, data files...                    │
   └─────────────────────────────────────────────────────────────┘


Essential Libraries
═══════════════════

   ┌─────────────────┬────────────────────────────────────────────┐
   │  File           │  Purpose                                     │
   ├─────────────────┼────────────────────────────────────────────┤
   │  libc.so        │  C library (open, read, write, printf,     │
   │                 │  kernel calls)                               │
   │  libgcc_s.so    │  GCC runtime support                         │
   │  ldqnx-64.so    │  Runtime loader and linker                   │
   │  libsecpol.so   │  Security policy library (required for       │
   │                 │  most QNX resource managers)                 │
   └─────────────────┴────────────────────────────────────────────┘


File Placement in Image
═══════════════════════

   Default location: /proc/boot

   Custom location:
   ────────────────
   /etc/hosts=/path/to/hosts
   │    │      │
   │    │      └── source path on host (where mkifs finds it)
   │    │
   │    └── filename in image
   │
   └── directory in target filesystem

   Result: File appears as /etc/hosts on target


Inline File Contents
════════════════════
   readme = {
       This is the contents of the readme file.
       It will be created in /proc/boot/readme
   }
```

---

## Boot Script

```
Boot Script Basics
═══════════════════════════════════════════════════════════════════

   Marked with [+script] attribute in buildfile:

   [+script] .script = {
       # commands to run at boot
   }


   Multiple boot scripts:
   ──────────────────────
   • mkifs concatenates ALL [+script] sections into ONE script
   • Concatenated in order encountered in buildfile
   • Can include other buildfiles with [include] attribute


Boot Script Modifiers
══════════════════════

   ┌─────────────┬─────────────────────────────────────────────────┐
   │  Modifier   │  Meaning                                        │
   ├─────────────┼─────────────────────────────────────────────────┤
   │  pri=       │  Set process priority                           │
   │             │  Example: pri=27f (priority 27, FIFO)         │
   │             │           pri=10r (priority 10, RR)             │
   ├─────────────┼─────────────────────────────────────────────────┤
   │  session    │  Create new session, program is session leader  │
   ├─────────────┼─────────────────────────────────────────────────┤
   │  &          │  Run in background (don't wait for completion)  │
   │             │  Example: devc-ser8250 &                        │
   └─────────────┴─────────────────────────────────────────────────┘


Builtin (Internal) Commands
════════════════════════════

   These commands are recognized by mkifs and process manager:
   (No executable file needed — process manager knows how to do them)

   ┌──────────────────┬────────────────────────────────────────────┐
   │  Command         │  Description                               │
   ├──────────────────┼────────────────────────────────────────────┤
   │  procmgr_symlink │  Create symbolic link                      │
   │                  │  Example: procmgr_symlink /proc/boot/      │
   │                  │           ldqnx-64.so.2 /usr/lib/           │
   │                  │           ldqnx-64.so.2                     │
   ├──────────────────┼────────────────────────────────────────────┤
   │  display_msg     │  Display text to debug device              │
   │                  │  Example: display_msg "Booting system..."   │
   ├──────────────────┼────────────────────────────────────────────┤
   │  waitfor         │  Wait for pathname to appear in namespace  │
   │                  │  Example: waitfor /dev/ser1                │
   │                  │  (blocks until driver registers the name)  │
   ├──────────────────┼────────────────────────────────────────────┤
   │  reopen          │  Redirect stdin/stdout/stderr (fd 0,1,2)  │
   │                  │  to specified device                       │
   │                  │  Example: reopen /dev/ser1                 │
   └──────────────────┴────────────────────────────────────────────┘


Boot Script Example (Complex)
══════════════════════════════

   [+script] .script = {
       # Create symlink for runtime loader
       procmgr_symlink /proc/boot/ldqnx-64.so.2 /usr/lib/ldqnx-64.so.2

       # Display boot message to debug device
       display_msg "QNX Neutrino Booting..."

       # Start serial driver (background)
       devc-ser8250 -e -b115200 &

       # Start PTY driver (background)
       devc-pty &

       # Start console driver (background)
       devc-con -n4 &

       # Wait for serial device to be registered
       waitfor /dev/ser1

       # Display message
       display_msg "Serial driver ready"

       # Redirect stdin/stdout/stderr to serial port
       reopen /dev/ser1

       # Run embedded shell at priority 27, FIFO, as session leader
       pri=27f session esh &

       # Redirect to console for next shell
       reopen /dev/con1

       # Run another shell on console
       pri=27f session esh &
   }


Flow of Boot Script Example
═══════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  1. procmgr_symlink                                         │
   │     └── Create /usr/lib/ldqnx-64.so.2 → /proc/boot/...    │
   │                                                             │
   │  2. display_msg "QNX Neutrino Booting..."                  │
   │     └── Print to debug device                               │
   │                                                             │
   │  3. devc-ser8250 &                                          │
   │     └── Start serial driver (background)                    │
   │                                                             │
   │  4. devc-pty &                                              │
   │     └── Start PTY driver (background)                       │
   │                                                             │
   │  5. devc-con &                                              │
   │     └── Start console driver (background)                   │
   │                                                             │
   │  6. waitfor /dev/ser1                                       │
   │     └── BLOCK until serial driver registers /dev/ser1     │
   │                                                             │
   │  7. display_msg "Serial driver ready"                       │
   │                                                             │
   │  8. reopen /dev/ser1                                        │
   │     └── stdin/stdout/stderr → /dev/ser1                     │
   │                                                             │
   │  9. pri=27f session esh &                                   │
   │     └── Run shell (inherits fd 0,1,2 = /dev/ser1)         │
   │                                                             │
   │  10. reopen /dev/con1                                       │
   │      └── stdin/stdout/stderr → /dev/con1                   │
   │                                                             │
   │  11. pri=27f session esh &                                  │
   │      └── Run shell on console                              │
   └─────────────────────────────────────────────────────────────┘


Boot Script Limitations
═══════════════════════

   The boot script language is VERY SIMPLE:
   • No if/else statements
   • No variables
   • No loops
   • No expressions
   • No branching

   For complex logic, use one of these methods:

   Method 1: Korn Shell Script
   ───────────────────────────
   [+script] .script = {
       ksh /proc/boot/my_complex_script.ksh &
   }

   Method 2: Launcher Program (C/C++)
   ────────────────────────────────────
   [+script] .script = {
       /proc/boot/launcher &
   }

   launcher.c:
   ────────────
   int main(void) {
       // Do anything: if/else, loops, variables
       spawn("driver1", ...);
       spawn("server", ...);
       spawn("client", ...);
       // Full C/C++ power
       return 0;
   }
```

---

## mkifs Search Path

```
Default Search Path
═══════════════════════════════════════════════════════════════════

   mkifs searches for files in this order:

   1. ${QNX_TARGET}/${PROCESSOR}/bin
   2. ${QNX_TARGET}/${PROCESSOR}/usr/bin
   3. ${QNX_TARGET}/${PROCESSOR}/sbin


Where Variables Come From
───────────────────────────

   ${PROCESSOR}
   ────────────
   From buildfile: [virtual=x86_64,bios]
                              │
                              └── processor = x86_64

   Set as environment variable by mkifs


   ${QNX_TARGET}
   ─────────────
   • Command line: set by sourcing QNX environment script
   • IDE: automatically set by QNX Momentics/IDE


Custom Search Path
══════════════════

   Override with MKIFS_PATH environment variable:

   Linux:
   ─────
   export MKIFS_PATH=/custom/path1:/custom/path2:${MKIFS_PATH}

   Windows:
   ────────
   set MKIFS_PATH=C:\custom\path1;C:\custom\path2;%MKIFS_PATH%


Per-File Search Path
════════════════════

   In buildfile, use [search] attribute:

   [search=/project/bin:/my_libs] my_program
   │         │           │
   │         │           └── second place to look
   │         └── first place to look
   └── applies to my_program

   [search=${MKIFS_PATH}:/project/bin] my_other_program
   │         │              │
   │         │              └── fallback path
   │         └── use standard search path first
   └── applies to my_other_program
```

---

## Complete Example

```
Simple Buildfile Example
═══════════════════════════════════════════════════════════════════

   [virtual=x86_64,bios +compress]
   [virtual=x86_64] .bootstrap = {
       startup=startup-x86_64
       procnto=procnto-smp-instr
   }

   [+script] .script = {
       devc-ser8250 -e -b115200 &
       esh &
   }

   libc.so
   libgcc_s.so.1
   ldqnx-64.so.2
   libsecpol.so.1
   devc-ser8250
   esh
   hello


Build and Deploy
────────────────
   $ mkifs buildfile my_image.ifs

   Then deploy to target (board-specific):
   • Copy to SD card (specific filename required)
   • Flash to onboard memory
   • Network boot (TFTP)
   • etc.
```

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        BOOT SEQUENCE QUICK REF                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ORDER:  CPU → IPL → Startup → procnto → Boot Script → Drivers/Services   │
│                                                                             │
│  IPL:         Initial Program Loader (BSP-provided)                         │
│               Sets up chip select, RAM, jumps to startup                    │
│                                                                             │
│  Startup:     Hardware setup, discovery, prepares for procnto              │
│                                                                             │
│  procnto:     Process Manager + Microkernel                                │
│               Runs boot script                                              │
│                                                                             │
│  Boot Script: Simple command language (no if/loops/variables)              │
│               Use ksh or C program for complex logic                        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                        BUILDFILE QUICK REF                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  FORMAT:      [attribute] filename [= contents]                            │
│                                                                             │
│  COMMENTS:    # comment line                                                │
│  BLANK LINES: Allowed                                                       │
│                                                                             │
│  ATTRIBUTES:                                                                │
│    Boolean:   +script  -optional  +compress                                 │
│    Value:     uid=0  gid=0  search=/path                                    │
│                                                                             │
│  SCOPE:                                                                     │
│    Per-file:  [uid=0] file1                                                 │
│    Global:    [+optional]  ← applies to all following files                   │
│                                                                             │
│  BOOTSTRAP:                                                                 │
│    [virtual=board,bios]                                                     │
│    startup=startup-board_name                                               │
│    procnto=procnto-smp-instr                                                │
│                                                                             │
│  ESSENTIAL FILES:                                                           │
│    libc.so  libgcc_s.so.1  ldqnx-64.so.2  libsecpol.so.1                    │
│                                                                             │
│  CUSTOM PATH:                                                               │
│    /etc/hosts=/path/to/hosts   ← appears as /etc/hosts on target           │
│                                                                             │
│  INLINE:                                                                    │
│    readme = { This is the content }                                         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                        BOOT SCRIPT QUICK REF                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  MARK WITH:   [+script] attribute                                           │
│                                                                             │
│  MODIFIERS:                                                                 │
│    pri=27f      ← priority 27, FIFO scheduling                              │
│    pri=10r      ← priority 10, Round-Robin                                  │
│    session      ← create new session, program is leader                     │
│    &            ← run in background (don't wait)                             │
│                                                                             │
│  BUILTIN COMMANDS:                                                          │
│    procmgr_symlink src dst   ← create symlink                               │
│    display_msg "text"        ← print to debug device                        │
│    waitfor /dev/xxx          ← wait for pathname to appear                  │
│    reopen /dev/xxx           ← redirect stdin/stdout/stderr                 │
│                                                                             │
│  COMPLEX LOGIC:                                                             │
│    • Run ksh script:   ksh /proc/boot/script.ksh &                         │
│    • Run C program:    /proc/boot/launcher &                                 │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                        mkifs QUICK REF                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  COMMAND:     mkifs buildfile output_image                                  │
│                                                                             │
│  SEARCH PATH:                                                               │
│    1. ${QNX_TARGET}/${PROCESSOR}/bin                                      │
│    2. ${QNX_TARGET}/${PROCESSOR}/usr/bin                                  │
│    3. ${QNX_TARGET}/${PROCESSOR}/sbin                                     │
│                                                                             │
│  OVERRIDE:    MKIFS_PATH=/custom/path1:/custom/path2                        │
│                                                                             │
│  PER-FILE:    [search=/path1:/path2] filename                             │
│                                                                             │
│  COMPRESSION: [virtual=board,bios +compress]                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

> **📌 Key Takeaways:**
>
> - Boot sequence: CPU → IPL → Startup → procnto → Boot Script → System running
> - IPL and startup are board-specific (come with BSP)
> - OS image is a file containing bootstrap + filesystem, created by mkifs
> - Image contents appear in /proc/boot (read-only, in-memory)
> - Buildfile format: [attribute] filename [= contents]
> - Boot script is simple — no if/loops/variables; use ksh or C for complex logic
> - Essential libraries: libc.so, libgcc_s.so, ldqnx-64.so, libsecpol.so
> - Builtin commands: procmgr_symlink, display_msg, waitfor, reopen
> - mkifs searches ${QNX_TARGET}/${PROCESSOR}/{bin,usr/bin,sbin} by default
