# QNX Resource Manager — Handling read() and write()

---

## Overview

This guide explains how to implement `read()` and `write()` handlers in a QNX resource manager. We use the example resource manager (`/dev/example`) as the target device.

```
Client → Resource Manager Flow
═══════════════════════════════════════════════════════════════════

   Client (cat /dev/example)          Resource Manager
   ─────────────────────────          ─────────────────
      │                                    │
      │  open("/dev/example", O_RDONLY)    │
      │───────────────────────────────────►│  _IO_CONNECT_OPEN
      │                                    │  • check permissions
      │◄───────────────────────────────────│  • create OCB
      │  fd returned                       │  • return success
      │                                    │
      │  read(fd, buf, 4096)               │
      │───────────────────────────────────►│  _IO_READ
      │                                    │  • check read permission
      │◄───────────────────────────────────│  • return data (or 0 = EOF)
      │  data (or 0 bytes)                 │
      │                                    │
      │  read(fd, buf, 4096)               │
      │───────────────────────────────────►│  _IO_READ
      │◄───────────────────────────────────│  • return 0 bytes (EOF)
      │  0 bytes ← EOF                     │
      │                                    │
      │  close(fd)                         │
      │───────────────────────────────────►│  _IO_CLOSE
      │◄───────────────────────────────────│  • cleanup OCB
      │                                    │
```

---

## Client Perspective: cat /dev/example

```
What "cat" does internally
═══════════════════════════════════════════════════════════════════

   cat /dev/example
   ────────────────

   ┌─────────────────────────────────────────────────────────────┐
   │  fd = open("/dev/example", O_RDONLY);                      │
   │                                                             │
   │  while (1) {                                                │
   │      n = read(fd, buf, sizeof(buf));                       │
   │      if (n == 0) break;      ← EOF: exit loop              │
   │      if (n == -1) { ... }    ← error handling               │
   │      write(stdout, buf, n);                                 │
   │  }                                                          │
   │                                                             │
   │  close(fd);                                                 │
   └─────────────────────────────────────────────────────────────┘


   With example resource manager (returns 0 bytes):
   ─────────────────────────────────────────────────

   open()  → success, fd returned
   read()  → returns 0 bytes (EOF immediately)
   close() → cleanup

   Result: cat outputs nothing and exits immediately
```

---

## Handler Prototypes

```
Read and Write Handler Signatures
═══════════════════════════════════════════════════════════════════

   int io_read(
       resmgr_context_t *ctp,     ← context pointer
       io_read_t        *msg,     ← read message (union)
       iofunc_ocb_t     *ocb      ← open control block
   );

   int io_write(
       resmgr_context_t *ctp,     ← context pointer
       io_write_t       *msg,     ← write message (union)
       iofunc_ocb_t     *ocb      ← open control block
   );


   Common Parameters:
   ──────────────────
   ┌─────────────────────────────────────────────────────────────┐
   │  ctp  → context pointer (contains rcvid, client info, etc.)│
   │  msg  → message union (contains _io_read or _io_write)     │
   │  ocb  → per-open state (offset, flags, attr pointer)       │
   └─────────────────────────────────────────────────────────────┘
```

---

## The Context Pointer (ctp)

```
resmgr_context_t Structure
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  ctp (context pointer)                                      │
   │                                                             │
   │  • rcvid         ← receive ID (who sent the message)       │
   │  • info          ← client info (pid, tid, scoid, etc.)     │
   │  • msg           ← pointer to receive buffer               │
   │  • msg_max_size  ← size of receive buffer                  │
   │  • offset        ← message offset (advanced topics)        │
   │  • size          ← message size (advanced topics)          │
   │                                                             │
   │  Used for:                                                  │
   │  • MsgReply(ctp->rcvid, ...)  ← reply to client            │
   │  • ctp->info.pid              ← get client process ID      │
   │  • ctp->info.tid              ← get client thread ID       │
   └─────────────────────────────────────────────────────────────┘
```

---

## The Open Control Block (OCB)

```
iofunc_ocb_t Structure
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  ocb (Open Control Block) — ONE per open()                  │
   │                                                             │
   │  • offset        ← current file position                    │
   │  • attr          ← pointer to device attribute structure    │
   │  • flags         ← open flags (O_RDONLY, O_RDWR, etc.)    │
   │  • count         ← reference count                          │
   │  • ...           ← other internal fields                    │
   │                                                             │
   │  Created by: iofunc_open_default() during open handling    │
   │                                                             │
   │  Access device attributes: ocb->attr                        │
   │  → video frame buffers, interrupt IDs, timer IDs, etc.     │
   └─────────────────────────────────────────────────────────────┘


   Per-Open vs Per-Device Data
   ───────────────────────────

   Per-Device (attr)              Per-Open (ocb)
   ─────────────────              ──────────────

   ┌─────────────────┐            ┌─────────────────┐
   │ Device Attr     │            │ OCB #1          │
   │                 │            │ • offset = 0    │
   │ • frame buffer  │            │ • attr ────────┐│
   │ • interrupt ID  │            └────────────────│┘
   │ • timer ID      │                             │
   │ • input buffer  │            ┌────────────────│┐
   │ • output buffer │            │ OCB #2         ││
   │                 │            │ • offset = 0   ││
   │                 │            │ • attr ────────┘│
   └─────────────────┘            └─────────────────┘

   Example: File position (offset)
   ───────────────────────────────

   Client opens file twice:

   fd1 = open("/dev/example", O_RDONLY);  → OCB #1, offset = 0
   fd2 = open("/dev/example", O_RDONLY);  → OCB #2, offset = 0

   read(fd1, buf, 100);  → OCB #1, offset = 100
   read(fd1, buf, 100);  → OCB #1, offset = 200
   read(fd2, buf, 100);  → OCB #2, offset = 100  (independent!)
```

---

## Handling read()

### Client-Side read() Behavior

```
POSIX read() Semantics
═══════════════════════════════════════════════════════════════════

   ssize_t read(int fd, void *buf, size_t nbytes);

   Return Values:
   ──────────────
   > 0    → number of bytes read (1 to nbytes)
   = 0    → end of file (EOF)
   = -1   → error, errno set

   Special case: read(fd, buf, 0)
   ───────────────────────────────
   • Returns 0 if read permission OK
   • Returns -1, errno = EPERM if no read permission
```

### The _IO_READ Message

```
What the Client Sends (struct _io_read)
═══════════════════════════════════════════════════════════════════

   Client library (libc) builds and sends:

   ┌─────────────────────────────────────┐
   │  struct _io_read                    │
   │  (sent by read() library function)  │
   │                                     │
   │  • type    = _IO_READ  (2 bytes)   │
   │  • combine = 0                       │
   │  • nbytes  = bytes wanted           │
   │  • xtype   = _IO_XTYPE_NONE         │
   │  • ...                               │
   └─────────────────────────────────────┘
            │
            │  MsgSend to resource manager
            ▼


   Accessing in Handler:
   ─────────────────────
   io_read_t is a UNION containing struct _io_read:

   msg->i.nbytes    ← how many bytes client wants
   msg->i.xtype     ← expected: _IO_XTYPE_NONE
   msg->i.type      ← _IO_READ
```

### Replying to read()

```
Replying with Data
═══════════════════════════════════════════════════════════════════

   Correct reply:
   ──────────────
   MsgReply(ctp->rcvid,           /* who to reply to */
            status,               /* return value for read() */
            buffer,               /* data to send back */
            nbytes);              /* how many bytes of data */


   Parameters explained:
   ─────────────────────

   ctp->rcvid  →  Who to reply to (from context structure)
   status      →  What read() returns (byte count or error)
   buffer      →  Pointer to data buffer
   nbytes      →  Number of bytes to copy to client


   Status vs nbytes:
   ─────────────────

   ┌─────────────────────────────────────────────────────────────┐
   │  status parameter  =  what MsgSend() returns               │
   │                       =  what read() returns               │
   │                                                             │
   │  nbytes parameter  =  how many bytes kernel copies         │
   │                       to client's buffer                    │
   │                                                             │
   │  For read(): status == nbytes (same value, different roles)│
   │                                                             │
   │  status = return value (byte count or error code)          │
   │  nbytes = actual data size to transfer                     │
   └─────────────────────────────────────────────────────────────┘


   Example replies:
   ────────────────

   Success with data:
   MsgReply(ctp->rcvid, 20, buf, 20);
   → read() returns 20, client gets 20 bytes

   End of file:
   MsgReply(ctp->rcvid, 0, NULL, 0);
   → read() returns 0, client gets 0 bytes (EOF)

   Error:
   return EPERM;   ← from handler
   → Library calls MsgError(ctp->rcvid, EPERM)
   → read() returns -1, errno = EPERM
```

### Handling xtype

```
Checking xtype
═══════════════════════════════════════════════════════════════════

   Expected: msg->i.xtype == _IO_XTYPE_NONE

   If NOT _IO_XTYPE_NONE:
   → Return ENOSYS ("not supported")
   → Client gets read() = -1, errno = ENOSYS

   Code:
   ────
   if (msg->i.xtype != _IO_XTYPE_NONE) {
       return ENOSYS;    /* unexpected xtype */
   }
```

### Access Time Handling

```
Marking Access Time for Update
═══════════════════════════════════════════════════════════════════

   Don't make kernel call on every read (performance hit).
   Instead, mark for lazy update:

   if (msg->i.nbytes > 0) {
       ocb->attr->flags |= IOFUNC_ATTR_ATIME
                         | IOFUNC_ATTR_DIRTY_TIME;
   }

   • IOFUNC_ATTR_ATIME       → access time needs updating
   • IOFUNC_ATTR_DIRTY_TIME  → time fields are dirty

   When client calls stat():
   → Default stat handler checks this flag
   → Makes kernel call THEN to get current time
   → Updates access time in attr structure

   POSIX allows this lazy update.
```

---

## Handling write()

### Client-Side write() Behavior

```
POSIX write() Semantics
═══════════════════════════════════════════════════════════════════

   ssize_t write(int fd, const void *buf, size_t nbytes);

   Return Values:
   ──────────────
   > 0    → number of bytes written
   = -1   → error, errno set

   The _IO_WRITE Message:
   ──────────────────────

   ┌─────────────────────────────────────┐
   │  struct _io_write                   │
   │  (sent by write() library function) │
   │                                     │
   │  • type    = _IO_WRITE (2 bytes)   │
   │  • combine = 0                      │
   │  • nbytes  = bytes to write         │
   │  • xtype   = _IO_XTYPE_NONE         │
   │  • offset  = file position          │
   │  • ...                              │
   └─────────────────────────────────────┘

   Access: msg->i.nbytes    ← bytes client wants to write
           msg->i.offset    ← where to write (for regular files)
           msg->i.xtype     ← expected: _IO_XTYPE_NONE
```

### Replying to write()

```
Replying to write()
═══════════════════════════════════════════════════════════════════

   MsgReply(ctp->rcvid,           /* who to reply to */
            status,               /* bytes written or error */
            NULL,                 /* no data to return */
            0);                   /* no data bytes */

   Or use helper macro:
   _IO_SET_WRITE_NBYTES(ctp, nbytes_written);


   Example:
   ────────
   /* Verify and process write */
   if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
       return status;
   }

   /* Get data from client if not in receive buffer */
   resmgr_msgread(ctp, buf, msg->i.nbytes, sizeof(msg->i));

   /* Process data... */

   /* Reply with bytes accepted */
   _IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);
   MsgReply(ctp->rcvid, EOK, NULL, 0);
```

### Write Verification

```
iofunc_write_verify()
═══════════════════════════════════════════════════════════════════

   Always call at start of write handler:

   int status = iofunc_write_verify(ctp, msg, ocb, NULL);
   if (status != EOK) {
       return status;    /* return error code */
   }

   Checks:
   ──────
   • Opened for write? (not read-only)
   • Valid message parameters
   • Other standard validations
```

---

## Complete read() Handler Example

```c
/* ============================================================
 * Complete read() Handler
 * ============================================================ */

#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <errno.h>
#include <string.h>

/* Example data buffer (in real case: hardware buffer, log, etc.) */
static char device_data[] = "Hello from resource manager!\n";
static int data_len = 30;

int io_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb) {
    int status;
    int nbytes;           /* number of bytes to return */
    int offset;           /* current file position */

    /* ─────────────────────────────────────────────────────── */
    /* Step 1: Verify read is allowed                          */
    /* ─────────────────────────────────────────────────────── */
    if ((status = iofunc_read_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;    /* e.g., EPERM if opened write-only */
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 2: Check xtype is expected value                   */
    /* ─────────────────────────────────────────────────────── */
    if (msg->i.xtype != _IO_XTYPE_NONE) {
        return ENOSYS;    /* unexpected xtype, not supported */
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 3: Calculate how many bytes to return              */
    /* ─────────────────────────────────────────────────────── */
    /* Get current file position from OCB */
    offset = ocb->offset;

    /* Calculate remaining bytes from current position */
    if (offset >= data_len) {
        nbytes = 0;       /* at or past EOF */
    } else {
        nbytes = data_len - offset;
        /* Don't exceed what client asked for */
        if (nbytes > msg->i.nbytes) {
            nbytes = msg->i.nbytes;
        }
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 4: Mark access time for update (if actually reading) */
    /* ─────────────────────────────────────────────────────── */
    if (nbytes > 0) {
        ocb->attr->flags |= IOFUNC_ATTR_ATIME
                          | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 5: Reply with data (or 0 for EOF)                  */
    /* ─────────────────────────────────────────────────────── */
    if (nbytes > 0) {
        /* Reply with data */
        MsgReply(ctp->rcvid,
                 nbytes,                          /* status = byte count */
                 device_data + offset,            /* data pointer */
                 nbytes);                         /* data size */

        /* Update file position in OCB */
        ocb->offset += nbytes;
    } else {
        /* End of file — reply with 0 bytes */
        MsgReply(ctp->rcvid,
                 0,                               /* status = 0 (EOF) */
                 NULL,                            /* no data */
                 0);                              /* no bytes */
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 6: Tell library we already replied                 */
    /* ─────────────────────────────────────────────────────── */
    return _RESMGR_NOREPLY;
}
```

### Simple read() Handler (Example Resource Manager)

```c
/* ============================================================
 * Simple read() Handler — returns 0 bytes (EOF immediately)
 * This is what the example resource manager does by default
 * ============================================================ */

int io_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb) {
    int status;

    /* Verify read is allowed */
    if ((status = iofunc_read_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;
    }

    /* Check xtype */
    if (msg->i.xtype != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    /* Mark access time if client is actually trying to read */
    if (msg->i.nbytes > 0) {
        ocb->attr->flags |= IOFUNC_ATTR_ATIME
                          | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* Reply with 0 bytes = EOF */
    MsgReply(ctp->rcvid, 0, NULL, 0);

    return _RESMGR_NOREPLY;
}
```

### write() Handler Example

```c
/* ============================================================
 * write() Handler — accepts any size (example resource manager)
 * ============================================================ */

int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
    int status;
    int nbytes;

    /* Verify write is allowed */
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;
    }

    /* Check xtype */
    if (msg->i.xtype != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    /* Get number of bytes client wants to write */
    nbytes = msg->i.nbytes;

    /* Mark modification time */
    if (nbytes > 0) {
        ocb->attr->flags |= IOFUNC_ATTR_MTIME
                          | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* In real case: get data and process it */
    /* resmgr_msgread(ctp, my_buffer, nbytes, sizeof(msg->i)); */
    /* process_data(my_buffer, nbytes); */

    /* Reply: accept all bytes */
    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    MsgReply(ctp->rcvid, EOK, NULL, 0);

    return _RESMGR_NOREPLY;
}
```

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     READ/WRITE HANDLER QUICK REF                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  HANDLER SIGNATURES:                                                        │
│    int io_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb)  │
│    int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb)│
│                                                                             │
│  CTP (Context Pointer):                                                     │
│    • ctp->rcvid      → receive ID (use in MsgReply)                        │
│    • ctp->info       → client info (pid, tid, etc.)                        │
│    • ctp->msg        → receive buffer pointer                              │
│                                                                             │
│  OCB (Open Control Block):                                                  │
│    • ocb->offset     → current file position                               │
│    • ocb->attr       → pointer to device attributes                        │
│    • ocb->flags      → open flags                                          │
│                                                                             │
│  MESSAGE ACCESS:                                                            │
│    • msg->i.nbytes   → bytes client wants to read/write                    │
│    • msg->i.xtype    → expected: _IO_XTYPE_NONE                            │
│    • msg->i.type     → _IO_READ or _IO_WRITE                               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### read() Handler Template

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  int io_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb) {  │
│                                                                             │
│  1. VERIFY:  iofunc_read_verify(ctp, msg, ocb, NULL)                      │
│              if (status != EOK) return status;                              │
│                                                                             │
│  2. CHECK:   if (msg->i.xtype != _IO_XTYPE_NONE) return ENOSYS;           │
│                                                                             │
│  3. GET DATA: from hardware buffer, log, frame buffer, etc.                │
│                                                                             │
│  4. MARK ATIME: if (msg->i.nbytes > 0)                                    │
│                 ocb->attr->flags |= IOFUNC_ATTR_ATIME |                    │
│                                    IOFUNC_ATTR_DIRTY_TIME;                 │
│                                                                             │
│  5. REPLY:   MsgReply(ctp->rcvid, nbytes, buf, nbytes);                   │
│              OR MsgReply(ctp->rcvid, 0, NULL, 0) for EOF                   │
│                                                                             │
│  6. RETURN:  _RESMGR_NOREPLY (we already replied)                         │
│                                                                             │
│  ERROR:      return errno_value;  (library calls MsgError for you)        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### write() Handler Template

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {│
│                                                                             │
│  1. VERIFY:  iofunc_write_verify(ctp, msg, ocb, NULL)                     │
│              if (status != EOK) return status;                              │
│                                                                             │
│  2. CHECK:   if (msg->i.xtype != _IO_XTYPE_NONE) return ENOSYS;           │
│                                                                             │
│  3. GET DATA: resmgr_msgread(ctp, buf, msg->i.nbytes, sizeof(msg->i));    │
│                                                                             │
│  4. MARK MTIME: if (msg->i.nbytes > 0)                                    │
│                  ocb->attr->flags |= IOFUNC_ATTR_MTIME |                   │
│                                     IOFUNC_ATTR_DIRTY_TIME;                │
│                                                                             │
│  5. REPLY:   _IO_SET_WRITE_NBYTES(ctp, nbytes);                           │
│              MsgReply(ctp->rcvid, EOK, NULL, 0);                           │
│                                                                             │
│  6. RETURN:  _RESMGR_NOREPLY                                              │
│                                                                             │
│  ERROR:      return errno_value;                                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Return Values

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  FROM HANDLER          │  WHAT HAPPENS          │  CLIENT SEES              │
├─────────────────────────────────────────────────────────────────────────────┤
│  _RESMGR_NOREPLY       │  Library does nothing  │  Whatever you sent in     │
│                        │  (you already replied) │  MsgReply()               │
├─────────────────────────────────────────────────────────────────────────────┤
│  EOK (or other value)  │  Library calls         │  read()/write() returns   │
│                        │  MsgReply()/MsgError   │  appropriate value        │
├─────────────────────────────────────────────────────────────────────────────┤
│  errno value (EPERM,   │  Library calls         │  read()/write() returns   │
│  ENOSYS, etc.)         │  MsgError(rcvid, err)  │  -1, errno = that value   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

> **📌 Key Takeaways:**
>
> - **read() handler**: verify → check xtype → get data → mark atime → reply with data or 0 (EOF) → return _RESMGR_NOREPLY
> - **write() handler**: verify → check xtype → read data → mark mtime → reply with bytes accepted → return _RESMGR_NOREPLY
> - **ctp->rcvid** is used in MsgReply() to reply to the correct client
> - **status parameter** in MsgReply() = return value of read()/write()
> - **nbytes parameter** in MsgReply() = actual data bytes to transfer
> - **Return errno** from handler for errors (library calls MsgError for you)
> - **ocb->offset** tracks per-open file position
> - **ocb->attr** points to device attributes (frame buffers, interrupt IDs, etc.)
> - **Mark access/modify times** with flags, don't make kernel calls on every I/O
> - **Always check xtype** — return ENOSYS if not _IO_XTYPE_NONE
