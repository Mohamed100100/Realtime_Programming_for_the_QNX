# QNX Resource Managers — Overview

---

## What is a Resource Manager?

A **resource manager** is a QNX-specific process that extends the operating system by registering names in the pathname space and providing a POSIX interface for clients.

```
Resource Manager Concept
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │                    Resource Manager                         │
   │                                                             │
   │   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐     │
   │   │   open()    │    │   read()    │    │   write()   │     │
   │   │   handler   │    │   handler   │    │   handler   │     │
   │   └──────┬──────┘    └──────┬──────┘    └──────┬──────┘     │
   │          │                  │                  │            │
   │          └──────────────────┼──────────────────┘            │
   │                             │                               │
   │                        ┌────┴────┐                          │
   │                        │ Channel │  ← receives messages     │
   │                        │   ID    │                          │
   │                        └────┬────┘                          │
   │                             │                               │
   └─────────────────────────────┼───────────────────────────────┘
                                 │
                                 ▼
                         Registered in pathname space
                         (e.g., /dev/ser1, /tmp/queue)


   Clients use standard POSIX calls:
   ─────────────────────────────────
   fd = open("/dev/mydevice", O_RDWR);   ← connect
   read(fd, buf, sizeof(buf));             ← I/O
   write(fd, data, len);                   ← I/O
   close(fd);                              ← disconnect
```

### Resource Manager Examples

```
Hardware Resource Managers          Software Resource Managers
══════════════════════════          ════════════════════════════

   devc-ser8250    ← serial port        queue manager    ← message queue
   devb-eide       ← disk/FS            logging system   ← log service
   devc-con        ← console            /tmp/shmem       ← shared memory
   can-xxx         ← CAN controller
   i2c-xxx         ← I2C controller
```

---

## Pathname Space

The **pathname space** is a mapping from slash-delimited names to resource manager processes. The process manager maintains this mapping in a **prefix tree**.

```
Pathname Space Mapping
═══════════════════════════════════════════════════════════════════

   Pathname Prefix          Resource Manager Process
   ───────────────          ─────────────────────────

   /                        →  devb-eide (filesystem)
   │                        →  procnto-smp-instr (process manager)
   │
   ├── /dev/con1            →  devc-con (console driver)
   │
   ├── /dev/ser1            →  devc-ser8250 (serial driver)
   │
   ├── /dev/ser2            →  devc-ser8250 (same process, different handle)
   │
   ├── /tmp/queue           →  my_queue_mgr (custom resource manager)
   │
   └── /home/bill/...       →  devb-eide (descendant of /)


Prefix Tree (maintained by process manager)
════════════════════════════════════════════

                    ┌─────────┐
                    │    /    │  ← maps to: devb-eide, procnto
                    └────┬────┘
                         │
           ┌─────────────┼─────────────┐
           │             │             │
           ▼             ▼             ▼
        ┌──────┐     ┌──────┐     ┌──────┐
        │ dev  │     │ home │     │ tmp  │
        └──┬───┘     └──┬───┘     └──┬───┘
           │            │            │
      ┌────┴────┐       │            ▼
      │         │       │         ┌──────┐
      ▼         ▼       │         │queue │  ← my_queue_mgr
   ┌─────┐   ┌─────┐    │         └──────┘
   │con1 │   │ser1 │    │
   │con2 │   │ser2 │    │
   └─────┘   └─────┘    │
   devc-con  devc-ser    │
                         │
                         ▼
                      ┌──────┐
                      │ bill │  ← descendant of / (devb-eide)
                      └──┬───┘
                         │
                         ▼
                      ┌──────┐
                      │spud  │
                      │.dat  │
                      └──────┘
```

### Viewing Registrations: /proc/mount

```
Inspecting Pathname Registrations
═══════════════════════════════════════════════════════════════════

   $ cd /proc/mount
   $ ls /proc/mount

   Each entry shows: 0,pid,chid,handle,flags

   Example: /proc/mount/dev/con1
   ───────────────────────────────
   $ ls /proc/mount/dev/con1
   0,28676,1,0,0
     │    │  │ │ │
     │    │  │ │ └── flags (0 = no special flags)
     │    │  │ └── handle index (0 = first registration)
     │    │  └── channel ID (1)
     │    └── process ID (28676)
     └── always 0 (no longer used)

   $ pidin -p 28676
   → devc-con   (console driver process)


   Example: /proc/mount/dev/con2
   ───────────────────────────────
   $ ls /proc/mount/dev/con2
   0,28676,1,1,0
     │    │  │ │ │
     │    │  │ │ └── flags
     │    │  │ └── handle index (1 = second registration)
     │    │  └── channel ID (1)
     │    └── process ID (28676)
     └── always 0

   Same process, same channel, different handle (different device)


   Multiple registrations at /
   ────────────────────────────
   $ ls /proc/mount/
   → Multiple entries for "/" with different PIDs and flags
   → Each represents a different resource manager handling root paths
```

---

## Pathname Resolution

When a client calls `open()`, the process manager resolves the pathname to find the best-matching resource manager.

### Longest Match Wins

```
Pathname Resolution: open("/dev/ser1")
═══════════════════════════════════════════════════════════════════

   Try candidates in order of longest match:

   Candidate 1: /dev/ser1     ← LONGEST MATCH ✓
   Candidate 2: /dev          ← no registration
   Candidate 3: /             ← fallback

   Result: Sent to devc-ser8250


Pathname Resolution: open("/dev/ser")
═══════════════════════════════════════════════════════════════════

   Try candidates:

   Candidate 1: /dev/ser      ← NOT a whole word match for /dev/ser1
   │                           "ser" != "ser1"
   │
   Candidate 2: /dev         ← no registration
   │
   Candidate 3: /            ← FALLBACK ✓

   Result: Sent to filesystem (devb-eide) — likely fails


Pathname Resolution: open("/home/bill/spud.dat")
═══════════════════════════════════════════════════════════════════

   Try candidates:

   Candidate 1: /home/bill/spud.dat  ← no exact match
   Candidate 2: /home/bill          ← no registration
   Candidate 3: /home               ← no registration
   Candidate 4: /                   ← MATCH ✓ (multiple handlers)

   Result: Try each "/" handler in order:
           1. procnto → "file not found" → try next
           2. devb-eide → checks disk:
              /home exists? → yes
              /home/bill exists? → yes
              /home/bill/spud.dat exists? → yes → SUCCESS
```

### Resolution with Fallback

```
Fallback Chain for open("/home/bill/spud.dat")
═══════════════════════════════════════════════════════════════════

   Client                    Process Manager              Resource Managers
   ──────                    ───────────────              ─────────────────
      │                            │                            │
      │  open("/home/bill/...")    │                            │
      │────────────────────────────►│                            │
      │                            │                            │
      │                            │  Resolve pathname          │
      │                            │  Find best matches         │
      │                            │                            │
      │◄────────────────────────────│  Return list: [procnto,    │
      │     [pid1, pid2, ...]      │             devb-eide]     │
      │                            │                            │
      │                            │                            │
      │  Try 1: ConnectAttach(pid1)  │                            │
      │  MsgSend: _IO_CONNECT_OPEN │                            │
      │────────────────────────────────────────────────────────►│
      │                            │                         procnto
      │◄─────────────────────────────────────────────────────────│
      │    ENONENT (file not found)│                            │
      │                            │                            │
      │  Try 2: ConnectAttach(pid2)│                            │
      │  MsgSend: _IO_CONNECT_OPEN │                            │
      │────────────────────────────────────────────────────────►│
      │                            │                        devb-eide
      │◄─────────────────────────────────────────────────────────│
      │    EOK (success)           │                            │
      │                            │                            │
      │  open() returns fd         │                            │
      │                            │                            │

   Rules:
   ─────
   • ENONENT from handler → try next handler in list
   • Any other error (EPERM, EIO) → stop, return error to client
   • All handlers exhausted → return final ENONENT to client
```

---

## Open Flow

```
Complete open() Flow
═══════════════════════════════════════════════════════════════════

   Phase 1: Pathname Resolution (via process manager)
   ──────────────────────────────────────────────────

   Client                          Process Manager
   ──────                          ───────────────
      │                                 │
      │  MsgSend: resolve pathname      │
      │  "What handles /dev/ser1?"    │
      │────────────────────────────────►│
      │                                 │
      │◄────────────────────────────────│
      │  Reply: [pid, chid, handle]   │
      │                                 │


   Phase 2: Connect to Resource Manager
   ────────────────────────────────────

   Client                          Resource Manager
   ──────                          ─────────────────
      │                                 │
      │  ConnectAttach(pid, chid)       │
      │  Creates file descriptor        │
      │                                 │
      │  MsgSend: _IO_CONNECT_OPEN      │
      │  "Can I open this device?"      │
      │────────────────────────────────►│
      │                                 │
      │◄────────────────────────────────│
      │  Reply: EOK or error            │
      │                                 │


   Phase 3: Direct I/O (process manager no longer involved)
   ─────────────────────────────────────────────────────────

   Client                          Resource Manager
   ──────                          ─────────────────
      │                                 │
      │  MsgSend: _IO_READ              │
      │  "Give me data"                 │
      │────────────────────────────────►│
      │                                 │
      │◄────────────────────────────────│
      │  Reply: data + status           │
      │                                 │
      │                                 │
      │  MsgSend: _IO_WRITE             │
      │  "Here's data"                  │
      │────────────────────────────────►│
      │                                 │
      │◄────────────────────────────────│
      │  Reply: bytes written           │
      │                                 │
      │                                 │
      │  MsgSend: _IO_CLOSE             │
      │  "I'm done"                     │
      │────────────────────────────────►│
      │                                 │
      │◄────────────────────────────────│
      │  Reply: cleanup done            │
      │                                 │


   Symbolic Link Handling
   ──────────────────────

   If resource manager replies: "This is a symlink to /other/path"
   → Client library goes BACK to process manager
   → Resolves new pathname from the beginning
   → May loop multiple times for nested symlinks
```

---

## Message Types

Resource managers handle three categories of messages:

```
Message Categories
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │                    Resource Manager Messages                  │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │  1. CONNECT MESSAGES    (pathname-based)                    │
   │     ─────────────────                                       │
   │     • _IO_CONNECT_OPEN     ← open(), fopen()                │
   │     • _IO_CONNECT_UNLINK   ← unlink()                       │
   │     • _IO_CONNECT_RENAME   ← rename()                       │
   │                                                             │
   │     Purpose: Establish connection, create OCB               │
   │                                                             │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │  2. I/O MESSAGES        (file descriptor-based)            │
   │     ────────────────                                          │
   │     • _IO_READ             ← read(), pread(), readdir()     │
   │     • _IO_WRITE            ← write(), pwrite()              │
   │     • _IO_CLOSE            ← close(), fclose()              │
   │     • _IO_STAT             ← stat(), fstat()                │
   │     • _IO_LSEEK            ← lseek()                        │
   │     • _IO_DUP              ← dup(), dup2()                  │
   │     • _IO_TRUNCATE         ← truncate(), ftruncate()        │
   │     • _IO_CHMOD            ← chmod(), fchmod()               │
   │     • _IO_CHOWN            ← chown(), fchown()               │
   │     • _IO_DEVCTL           ← devctl()                       │
   │                                                             │
   │     Purpose: Perform I/O on established connection          │
   │                                                             │
   ├─────────────────────────────────────────────────────────────┤
   │                                                             │
   │  3. OTHER MESSAGES      (out-of-band)                       │
   │     ─────────────────                                       │
   │     • Pulses (timers, interrupts)                           │
   │     • Private messages (worker threads)                     │
   │     • pulse_attach, message_attach handlers                   │
   │                                                             │
   │     Purpose: Internal resource manager operations           │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
```

### Connect Messages Detail

```
Connect Messages (sys/iomsg.h)
═══════════════════════════════════════════════════════════════════

   Generated by pathname-based POSIX functions:

   ┌─────────────────────┬────────────────────────────┐
   │  POSIX Function     │  Connect Subtype           │
   ├─────────────────────┼────────────────────────────┤
   │  open()             │  _IO_CONNECT_OPEN          │
   │  unlink()           │  _IO_CONNECT_UNLINK        │
   │  rename()           │  _IO_CONNECT_RENAME        │
   │  link()             │  _IO_CONNECT_LINK          │
   │  mknod()            │  _IO_CONNECT_MKNOD         │
   │  readlink()         │  _IO_CONNECT_READLINK      │
   └─────────────────────┴────────────────────────────┘

   Simple resource managers usually only handle OPEN.
   File system resource managers handle UNLINK, RENAME, etc.
```

### I/O Messages Detail

```
I/O Messages (sys/iomsg.h)
═══════════════════════════════════════════════════════════════════

   Generated by file descriptor-based POSIX functions:

   ┌─────────────────────┬────────────────────────────┐
   │  POSIX Function     │  I/O Message Type          │
   ├─────────────────────┼────────────────────────────┤
   │  read()             │  _IO_READ                  │
   │  pread()            │  _IO_READ (with offset)   │
   │  readdir()          │  _IO_READ (directory)    │
   │  write()            │  _IO_WRITE                 │
   │  pwrite()           │  _IO_WRITE (with offset)  │
   │  close()            │  _IO_CLOSE                 │
   │  dup()/dup2()       │  _IO_DUP                   │
   │  lseek()            │  _IO_LSEEK                 │
   │  fstat()            │  _IO_STAT                  │
   │  ftruncate()        │  _IO_TRUNCATE              │
   │  fchmod()           │  _IO_CHMOD                 │
   │  fchown()           │  _IO_CHOWN                 │
   │  devctl()           │  _IO_DEVCTL                │
   └─────────────────────┴────────────────────────────┘

   All defined in <sys/iomsg.h>
```

---

## Resource Manager Framework

QNX provides a **resource manager framework** in libc to simplify writing resource managers.

```
Framework Benefits
═══════════════════════════════════════════════════════════════════

   Without Framework                          With Framework
   ───────────────                            ──────────────

   Manual message parsing                     Automatic message dispatch
   Custom main loop                           Framework main loop
   Handle all message types                   Default handlers provided
   from scratch                               Override only what you need


   ┌─────────────────────────────────────────────────────────────┐
   │              Resource Manager Framework                     │
   │                                                             │
   │  Provides:                                                  │
   │  • Supporting data structures                               │
   │  • Helper functions                                         │
   │  • Default handlers for ALL message types                   │
   │  • Automatic message dispatch                               │
   │                                                             │
   │  You override only the handlers you need:                 │
   │                                                             │
   │    Simple input device:  open + read                        │
   │    I/O device:           open + read + write               │
   │    Configurable device:  open + read + write + devctl      │
   │                                                             │
   └─────────────────────────────────────────────────────────────┘
```

### Framework Main Loop

```
Resource Manager Main Loop (using framework)
═══════════════════════════════════════════════════════════════════

   ┌─────────────────┐
   │  resmgr_attach()│  ← register pathname prefix
   │  (e.g., /dev/mydev)│
   └────────┬────────┘
            │
            ▼
   ┌─────────────────┐
   │  forever loop   │
   │                 │
   │  resmgr_block() │  ← framework function (calls MsgReceive)
   │       │         │
   │       ▼         │
   │  resmgr_handler()│  ← framework function (parses message)
   │       │         │
   │       ▼         │
   │  Dispatch to    │
   │  your callback  │  ← open_handler, read_handler, etc.
   │  or default     │
   │                 │
   └─────────────────┘
```

---

## Handler Pattern

```
Client → Resource Manager: Remote Function Call
═══════════════════════════════════════════════════════════════════

   Client Side                     Resource Manager Side
   ──────────                      ─────────────────────

   fd = open("/dev/mydev", O_RDWR);    open_handler()
   │                                    │
   │  MsgSend: _IO_CONNECT_OPEN         │  • permission check
   │───────────────────────────────────►│  • create OCB
   │                                    │  • return success/fail
   │◄───────────────────────────────────│
   │
   │
   read(fd, buf, len);                 read_handler()
   │                                    │
   │  MsgSend: _IO_READ                 │  • check OCB state
   │───────────────────────────────────►│  • get data from hardware/buffer
   │                                    │  • reply with data
   │◄───────────────────────────────────│
   │
   │
   write(fd, data, len);               write_handler()
   │                                    │
   │  MsgSend: _IO_WRITE                │  • check OCB state
   │───────────────────────────────────►│  • copy to output buffer
   │                                    │  • trigger hardware transmit
   │◄───────────────────────────────────│
   │
   │
   close(fd);                          close_handler() / _IO_CLOSE
   │                                    │
   │  MsgSend: _IO_CLOSE                │  • cleanup OCB
   │───────────────────────────────────►│  • free resources
   │                                    │  • reply
   │◄───────────────────────────────────│
   │


   OCB (Open Control Block)
   ─────────────────────────
   ┌─────────────────────────────────────┐
   │  Per-client, per-open state         │
   │                                     │
   │  • file descriptor info               │
   │  • current offset (for read/write)  │
   │  • open flags (O_RDONLY, O_RDWR)   │
   │  • device-specific data             │
   │  • reference count                  │
   └─────────────────────────────────────┘
```

### Data Flow with Hardware

```
Resource Manager with Hardware Layer
═══════════════════════════════════════════════════════════════════

   Client          Resource Manager              Hardware
   ──────          ─────────────────             ────────
      │                    │                       │
      │  read()            │                       │
      │───────────────────►│                       │
      │                    │  read_handler()       │
      │                    │  • check input buffer   │
      │                    │  • buffer has data?     │
      │◄───────────────────│  • copy to client       │
      │   data             │                       │
      │                    │                       │
      │                    │◄──────────────────────│
      │                    │  Interrupt: new data  │
      │                    │  • copy to input buffer │
      │                    │                       │
      │                    │                       │
      │  write()           │                       │
      │───────────────────►│                       │
      │   data             │  write_handler()        │
      │                    │  • copy to output buf   │
      │◄───────────────────│  • trigger HW transmit  │
      │   bytes written    │──────────────────────►│
      │                    │                       │
      │                    │                       │
      │  devctl()          │                       │
      │───────────────────►│                       │
      │   config           │  devctl_handler()       │
      │                    │  • reprogram HW         │
      │◄───────────────────│                       │
      │   status           │                       │
      │                    │                       │
```

### Handling Large Writes

```
Large Write: Client sends 20KB, receive buffer is 2KB
═══════════════════════════════════════════════════════════════════

   Client (20KB write)          Resource Manager
   ───────────────────          ─────────────────
        │                              │
        │  MsgSend: _IO_WRITE          │
        │  + first 2KB in receive buf  │
        │─────────────────────────────►│
        │                              │
        │                              │  "I need more data!"
        │                              │
        │◄─────────────────────────────│  resmgr_msgread()
        │  remaining 18KB              │  (framework helper)
        │                              │
        │                              │  Process all 20KB
        │                              │  • copy to output buffer
        │                              │  • trigger hardware
        │◄─────────────────────────────│
        │  Reply: 20000 bytes written  │
        │                              │


   resmgr_msgread() = framework wrapper around MsgRead()
   ───────────────────────────────────────────────────────
   Gets remaining data from client when receive buffer is too small
```

### Reply Patterns

```
Handler Reply Patterns
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  1. Error Reply                                               │
   │     ───────────                                               │
   │     MsgError(rcvid, EPERM);                                   │
   │     → Client gets -1, errno = EPERM                         │
   │                                                               │
   │     Example: client opened write-only, tries to read        │
   │                                                               │
   ├─────────────────────────────────────────────────────────────┤
   │  2. Success + No Data                                         │
   │     ───────────────────                                       │
   │     MsgReply(rcvid, EOK, NULL, 0);                           │
   │     → Client gets 0 or success status                       │
   │                                                               │
   │     Example: write() completed, no data to return             │
   │                                                               │
   ├─────────────────────────────────────────────────────────────┤
   │  3. Success + Data                                            │
   │     ────────────────                                            │
   │     MsgReply(rcvid, bytes_read, buf, bytes_read);            │
   │     → Client gets data + status                               │
   │                                                               │
   │     Example: read() returns 20 bytes of data                  │
   │                                                               │
   ├─────────────────────────────────────────────────────────────┤
   │  4. Delayed Reply (not covered in intro)                      │
   │     ─────────────────────                                     │
   │     Don't reply immediately — block client                    │
   │     Reply later when data becomes available                   │
   │                                                               │
   │     Example: blocking read with no data yet                   │
   └─────────────────────────────────────────────────────────────┘
```

---

## Complete Architecture

```
Full Resource Manager Architecture
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────────────┐
   │                         CLIENT PROCESS                               │
   │                                                                      │
   │   fd = open("/dev/ser1", O_RDWR);                                   │
   │   read(fd, buf, 100);                                                │
   │   write(fd, data, 50);                                                │
   │   close(fd);                                                         │
   │                                                                      │
   │   ┌───────────────────────────────────────────────────────────┐   │
   │   │  libc: open(), read(), write(), close()                   │   │
   │   │  • Build _IO_CONNECT / _IO_READ / _IO_WRITE messages      │   │
   │   │  • MsgSend to resource manager                            │   │
   │   └───────────────────────────────────────────────────────────┘   │
   └─────────────────────────────────┬───────────────────────────────────┘
                                     │
                                     │ MsgSend / MsgReceive / MsgReply
                                     │
   ┌─────────────────────────────────┴───────────────────────────────────┐
   │                      RESOURCE MANAGER                                │
   │                                                                      │
   │   ┌───────────────────────────────────────────────────────────┐     │
   │   │  Framework Main Loop                                      │     │
   │   │  • resmgr_block()  → MsgReceive()                       │     │
   │   │  • resmgr_handler() → parse & dispatch                  │     │
   │   └───────────────────────────────────────────────────────────┘     │
   │                              │                                       │
   │           ┌──────────────────┼──────────────────┐                   │
   │           ▼                  ▼                  ▼                   │
   │   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐          │
   │   │ open_handler │    │ read_handler │    │write_handler │          │
   │   │              │    │              │    │              │          │
   │   │ • permission │    │ • check OCB  │    │ • check OCB  │          │
   │   │ • create OCB │    │ • get data   │    │ • copy data  │          │
   │   │ • return fd  │    │ • reply data │    │ • trigger HW │          │
   │   └──────────────┘    └──────────────┘    └──────────────┘          │
   │                              │                                       │
   │                              ▼                                       │
   │   ┌───────────────────────────────────────────────────────────┐     │
   │   │  OCB Table (per-client state)                             │     │
   │   │  ┌─────┐ ┌─────┐ ┌─────┐                                 │     │
   │   │  │OCB 1│ │OCB 2│ │OCB 3│ ...                             │     │
   │   │  │fd=3 │ │fd=4 │ │fd=5 │                                 │     │
   │   │  │off=0│ │off=0│ │off=0│                                 │     │
   │   │  └─────┘ └─────┘ └─────┘                                 │     │
   │   └───────────────────────────────────────────────────────────┘     │
   │                              │                                       │
   │                              ▼                                       │
   │   ┌───────────────────────────────────────────────────────────┐     │
   │   │  Hardware / Business Logic Layer                          │     │
   │   │  • Interrupt handlers                                     │     │
   │   │  • Input/output buffers                                   │     │
   │   │  • Device configuration                                   │     │
   │   └───────────────────────────────────────────────────────────┘     │
   │                              │                                       │
   │                              ▼                                       │
   │                        ┌──────────┐                                 │
   │                        │ Hardware │                                 │
   │                        │ Device   │                                 │
   │                        └──────────┘                                 │
   │                                                                      │
   └─────────────────────────────────────────────────────────────────────┘
```

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     RESOURCE MANAGER QUICK REF                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  DEFINITION:  A process that registers pathnames and handles POSIX         │
│               operations (open, read, write, etc.) via message passing      │
│                                                                             │
│  REGISTRATION: resmgr_attach() → registers prefix in pathname space       │
│                                                                             │
│  PATHNAME RESOLUTION:                                                       │
│    • Longest match wins                                                     │
│    • Whole word match required ("/dev/ser" != "/dev/ser1")                  │
│    • Fallback to "/" handlers on ENONENT                                    │
│                                                                             │
│  TWO-PHASE OPEN:                                                            │
│    Phase 1: Process manager resolves pathname → returns [pid, chid, handle] │
│    Phase 2: Client connects directly to resource manager                      │
│                                                                             │
│  MESSAGE TYPES:                                                             │
│    Connect:  _IO_CONNECT_OPEN, _IO_CONNECT_UNLINK, _IO_CONNECT_RENAME      │
│    I/O:      _IO_READ, _IO_WRITE, _IO_CLOSE, _IO_STAT, _IO_DEVCTL, ...     │
│    Other:    Pulses, private messages (pulse_attach, message_attach)        │
│                                                                             │
│  ALL DEFINED IN:  <sys/iomsg.h>                                             │
│                                                                             │
│  OCB:  Open Control Block — per-client state tracked by resource manager   │
│                                                                             │
│  FRAMEWORK:  Provides default handlers, automatic dispatch, main loop        │
│              Override only the handlers you need                            │
│                                                                             │
│  LARGE DATA:  Use resmgr_msgread() when client data > receive buffer       │
│                                                                             │
│  REPLY PATTERNS:                                                            │
│    • MsgError(rcvid, errno)    → return error                              │
│    • MsgReply(rcvid, status)   → success, no data                          │
│    • MsgReply(rcvid, status, buf, len) → success + data                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Pathname Resolution Examples

```
┌─────────────────────────────┬─────────────────────────────┬───────────────┐
│  open() Pathname              │  Resolution Result            │  Handler      │
├─────────────────────────────┼─────────────────────────────┼───────────────┤
│  /dev/ser1                    │  Exact match: /dev/ser1       │  devc-ser8250 │
│  /dev/ser                     │  No whole word match          │  fallback to /│
│  /dev/con1                    │  Exact match: /dev/con1       │  devc-con     │
│  /home/bill/file.dat          │  Descendant of /              │  devb-eide    │
│  /tmp/queue                   │  Exact match: /tmp/queue      │  queue_mgr    │
│  /nonexistent                 │  No match, fallback to /      │  ENONENT      │
└─────────────────────────────┴─────────────────────────────┴───────────────┘
```

### Message Type to POSIX Mapping

```
┌─────────────────────┬─────────────────────┬─────────────────────────────────┐
│  POSIX Function     │  Message Type       │  Handler                        │
├─────────────────────┼─────────────────────┼─────────────────────────────────┤
│  open()             │  _IO_CONNECT_OPEN   │  open_handler()                 │
│  read()             │  _IO_READ           │  read_handler()                 │
│  write()            │  _IO_WRITE          │  write_handler()                │
│  close()            │  _IO_CLOSE          │  close_handler() / default      │
│  lseek()            │  _IO_LSEEK          │  lseek_handler()                │
│  fstat()            │  _IO_STAT           │  stat_handler()                   │
│  dup()/dup2()       │  _IO_DUP            │  dup_handler() / default        │
│  devctl()           │  _IO_DEVCTL         │  devctl_handler()               │
│  unlink()           │  _IO_CONNECT_UNLINK │  unlink_handler()               │
│  rename()           │  _IO_CONNECT_RENAME │  rename_handler()               │
└─────────────────────┴─────────────────────┴─────────────────────────────────┘
```

---

> **📌 Key Takeaways:**
>
> - Resource managers extend the OS by registering names in the pathname space
> - Clients use standard POSIX calls (open, read, write) — no special APIs needed
> - Process manager maintains the prefix tree and resolves pathnames
> - Longest match wins; whole word match required
> - After open(), all I/O goes directly to resource manager (no process manager involvement)
> - Three message categories: Connect, I/O, and Other (pulses/private)
> - All message types defined in `<sys/iomsg.h>`
> - Framework provides default handlers — override only what you need
> - OCB tracks per-client, per-open state
> - Use `resmgr_msgread()` for large client data exceeding receive buffer
