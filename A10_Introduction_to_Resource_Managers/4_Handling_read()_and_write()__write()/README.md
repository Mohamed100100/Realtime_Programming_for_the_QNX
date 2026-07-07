# QNX Resource Manager — Handling write()

---

## Overview

This guide explains how to implement the `write()` handler in a QNX resource manager, including handling the `_IO_WRITE` message, replying to the client, and dealing with large writes that may not fit in the receive buffer.

```
write() Flow
═══════════════════════════════════════════════════════════════════

   Client                              Resource Manager
   ──────                              ─────────────────
      │                                    │
      │  write(fd, buf, nbytes)            │
      │───────────────────────────────────►│  _IO_WRITE message
      │  header + data                     │  • verify permissions
      │                                    │  • check xtype
      │◄───────────────────────────────────│  • get data (msgget)
      │  nbytes written (or error)         │  • process data
      │                                    │  • reply with status
      │                                    │

   Return value: number of bytes successfully written
   (or -1 with errno set on error)
```

---

## Client-Side write()

```
POSIX write() Semantics
═══════════════════════════════════════════════════════════════════

   ssize_t write(int fd, const void *buf, size_t nbytes);

   Parameters:
   ───────────
   fd      → file descriptor to write to
   buf     → pointer to data buffer
   nbytes  → number of bytes to write

   Return Values:
   ──────────────
   > 0    → number of bytes successfully written
   = 0    → wrote 0 bytes (valid if have write permission)
   = -1   → error, errno set

   Special Cases:
   ──────────────
   • write(fd, buf, 0) → return 0 if write permission OK
   • Device full (e.g., video frame buffer) → return error
   • File-like devices → typically grow to accommodate data


   No EOF Concept for write()
   ──────────────────────────

   read()                    write()
   ─────                    ─────
   Returns 0 = EOF          No EOF concept
   │                        │
   │                        Files grow to fit data
   │                        │
   │                        Devices may return error
   │                        if buffer full
   │                        │
   ▼                        ▼
   End of data              End of capacity
```

---

## The _IO_WRITE Message

```
What the Client Sends
═══════════════════════════════════════════════════════════════════

   Client library (libc) builds and sends:

   ┌─────────────────────────────────────────────────────────────┐
   │  MsgSendv() with 2-part IOV:                                │
   │                                                             │
   │  iov[0] → header (struct _io_write)                        │
   │  iov[1] → data (client's buf)                              │
   │                                                             │
   │  struct _io_write {                                         │
   │      uint16_t type;      ← _IO_WRITE (2 bytes)             │
   │      uint16_t combine;                                    │
   │      uint32_t nbytes;    ← bytes client wants to write     │
   │      uint32_t xtype;     ← _IO_XTYPE_NONE                  │
   │      uint64_t offset;    ← file position                   │
   │      ...                                                   │
   │  }                                                          │
   │                                                             │
   │  Total: header + data sent as one message                  │
   └─────────────────────────────────────────────────────────────┘


   Client Library Code (simplified):
   ─────────────────────────────────

   struct _io_write header;
   iov_t iov[2];

   header.type   = _IO_WRITE;
   header.nbytes = nbytes;      /* how many bytes to write */
   header.xtype  = _IO_XTYPE_NONE;

   SETIOV(&iov[0], &header, sizeof(header));   /* part 1: header */
   SETIOV(&iov[1], buf, nbytes);                /* part 2: data */

   status = MsgSendv(coid, iov, 2, reply_buf, reply_len);
   return status;   /* = bytes written or error */
```

### Accessing in Handler

```
Handler Receives:
═══════════════════════════════════════════════════════════════════

   io_write_t is a UNION containing struct _io_write:

   msg->i.nbytes    ← how many bytes client wants to write
   msg->i.xtype     ← expected: _IO_XTYPE_NONE
   msg->i.offset    ← file position (for regular files)
   msg->i.type      ← _IO_WRITE


   Data Location:
   ──────────────

   Receive Buffer Layout:
   ┌─────────────────┬─────────────────────────────────────┐
   │  io_write_t     │  Client Data                        │
   │  (header)       │  (may be partial or complete)       │
   │                 │                                     │
   │  msg points     │  msg + 1 points here                │
   │  here           │  (after header)                     │
   └─────────────────┴─────────────────────────────────────┘
        ↑                  ↑
        │                  │
      msg              msg + 1
                     (or (char*)msg + sizeof(io_write_t))
```

---

## Replying to write()

```
Reply Structure
═══════════════════════════════════════════════════════════════════

   MsgReply(ctp->rcvid,           /* who to reply to */
            status,               /* bytes written or error */
            NULL,                 /* NO data to return */
            0);                   /* NO data bytes */


   For write(): client does NOT expect data back
   ─────────────────────────────────────────────

   read() reply:                    write() reply:
   ────────────                     ────────────

   MsgReply(rcvid,                 MsgReply(rcvid,
            nbytes,                         nbytes_written,
            data_buf,                       NULL,
            nbytes);                        0);
            ↑                               ↑
            │                               │
      data goes TO client             no data returned


   Helper Macro:
   ─────────────
   _IO_SET_WRITE_NBYTES(ctp, nbytes_written);

   Sets the write nbytes in the reply structure for the library.
```

---

## The write() Handler

```
Basic write() Handler Structure
═══════════════════════════════════════════════════════════════════

   int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {

       /* Step 1: Verify write is allowed */
       if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
           return status;    /* e.g., EPERM if opened read-only */
       }

       /* Step 2: Check xtype */
       if (msg->i.xtype != _IO_XTYPE_NONE) {
           return ENOSYS;    /* unexpected xtype */
       }

       /* Step 3: Process data */
       /* ... get data, do something with it ... */

       /* Step 4: Mark modification time */
       if (msg->i.nbytes > 0) {
           ocb->attr->flags |= IOFUNC_ATTR_MTIME
                             | IOFUNC_ATTR_DIRTY_TIME;
       }

       /* Step 5: Reply */
       _IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);
       MsgReply(ctp->rcvid, EOK, NULL, 0);

       return _RESMGR_NOREPLY;
   }
```

### Example Resource Manager's write() Handler (Current)

```c
/* ============================================================
 * Example Resource Manager write() Handler
 * "Lies" — claims all bytes were written without doing anything
 * ============================================================ */

int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
    int status;

    /* Verify write is allowed */
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;
    }

    /* Check xtype */
    if (msg->i.xtype != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    /* Mark modification time if actually writing data */
    if (msg->i.nbytes > 0) {
        ocb->attr->flags |= IOFUNC_ATTR_MTIME
                          | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* "Lie" — claim we wrote all bytes (doesn't actually process data) */
    _IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);
    MsgReply(ctp->rcvid, EOK, NULL, 0);

    return _RESMGR_NOREPLY;
}
```

### Accessing Data: msg + 1

```
Getting Data Pointer
═══════════════════════════════════════════════════════════════════

   Method 1: msg + 1
   ─────────────────
   void *data = msg + 1;
   /* Points to first byte after io_write_t header */
   /* Note: pointer arithmetic, not byte arithmetic */

   Method 2: Cast to char*
   ───────────────────────
   void *data = (char *)msg + sizeof(io_write_t);
   /* Explicit byte offset */


   ⚠️  WARNING: This only works if ALL data is in receive buffer!
   If client sends more data than receive buffer can hold,
   msg + 1 only points to the FIRST PART of the data.
```

---

## Handling Large Writes

```
The Large Write Problem
═══════════════════════════════════════════════════════════════════

   Client sends 3000 bytes
   Resource manager receive buffer = 1000 bytes

   ┌─────────────────────────────────────────────────────────────┐
   │  Client MsgSendv()                                          │
   │  iov[0] = header (small)                                    │
   │  iov[1] = data (3000 bytes)                                 │
   │                                                             │
   │         │                                                   │
   │         ▼                                                   │
   │  Kernel copies MIN(send_size, receive_size)                │
   │  = MIN(3000, 1000) = 1000 bytes                            │
   │                                                             │
   │         │                                                   │
   │         ▼                                                   │
   │  Resource Manager Receive Buffer                            │
   │  ┌─────────────────┬─────────────────────────────────────┐  │
   │  │  io_write_t     │  First 1000 bytes of data          │  │
   │  │  (header)       │  (rest is NOT here!)               │  │
   │  └─────────────────┴─────────────────────────────────────┘  │
   │                                                             │
   │  msg + 1 only sees first 1000 bytes!                       │
   │  Need to get remaining 2000 bytes somehow.                 │
   └─────────────────────────────────────────────────────────────┘


   Solution: resmgr_msgget()
   ─────────────────────────
   Framework function that:
   1. Copies whatever is already in receive buffer (local memcpy)
   2. Calls MsgRead() kernel call ONLY if more data needed
   3. Returns total bytes retrieved
```

---

## resmgr_msgget()

```
resmgr_msgget() Function
═══════════════════════════════════════════════════════════════════

   int resmgr_msgget(
       resmgr_context_t *ctp,    ← context pointer
       void *buf,                 ← destination buffer
       int nbytes,                ← bytes to get
       int offset                 ← offset into message (skip header)
   );

   Returns: number of bytes actually retrieved


   Parameters:
   ───────────
   ctp     → context pointer (contains rcvid for MsgRead)
   buf     → where to put the data
   nbytes  → how many bytes to get (usually msg->i.nbytes)
   offset  → where to start (usually sizeof(io_write_t))


   How It Works:
   ─────────────

   ┌─────────────────────────────────────────────────────────────┐
   │  resmgr_msgget(ctp, buf, nbytes, offset)                   │
   │                                                             │
   │  Step 1: Check what's already in receive buffer            │
   │          ┌─────────────────────────────────────────────┐   │
   │          │  Receive Buffer                               │   │
   │          │  ┌──────────┬─────────────────────────────┐ │   │
   │          │  │ Header   │ Data (partial)             │ │   │
   │          │  │          │ ← already here              │ │   │
   │          │  └──────────┴─────────────────────────────┘ │   │
   │          └─────────────────────────────────────────────┘   │
   │                                                             │
   │  Step 2: Copy from receive buffer to destination (memcpy)  │
   │          → Fast, no kernel call needed                      │
   │                                                             │
   │  Step 3: If more data needed                                │
   │          → Call MsgRead() kernel call to get rest          │
   │          → Only when necessary!                            │
   │                                                             │
   │  Step 4: Return total bytes copied                         │
   └─────────────────────────────────────────────────────────────┘


   Optimization:
   ─────────────
   • Copies locally first (fast memcpy)
   • Calls MsgRead() ONLY if data exceeds receive buffer
   • Avoids unnecessary kernel calls for small writes
```

### resmgr_msggetv()

```
Vector Version
═══════════════════════════════════════════════════════════════════

   int resmgr_msggetv(
       resmgr_context_t *ctp,
       iov_t *dst_iov,          ← destination IOV array
       int dst_parts,           ← number of destination parts
       int offset               ← offset into message
   );

   Use when you want to scatter data into multiple buffers:
   • Multiple cache buffers
   • Ring buffer segments
   • Separate header/data areas
```

---

## Complete write() Handler Examples

### Example 1: Simple write() with resmgr_msgget()

```c
/* ============================================================
 * write() Handler — Using resmgr_msgget()
 * Accepts data, stores in malloc'd buffer, processes it
 * ============================================================ */

int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
    int status;
    int nbytes;
    char *buf;

    /* Step 1: Verify write is allowed */
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;
    }

    /* Step 2: Check xtype */
    if (msg->i.xtype != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    /* Step 3: Allocate buffer for data */
    nbytes = msg->i.nbytes;
    if (nbytes > 0) {
        buf = malloc(nbytes);
        if (buf == NULL) {
            return ENOMEM;
        }

        /* Step 4: Get all data (handles large writes automatically) */
        int bytes_got = resmgr_msgget(ctp, buf, nbytes,
                                      sizeof(io_write_t));
        if (bytes_got == -1) {
            free(buf);
            return errno;
        }

        /* Step 5: Process the data */
        /* Example: log it, send to hardware, store in ring buffer, etc. */
        process_data(buf, bytes_got);

        free(buf);
    }

    /* Step 6: Mark modification time */
    if (nbytes > 0) {
        ocb->attr->flags |= IOFUNC_ATTR_MTIME
                          | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* Step 7: Reply */
    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    MsgReply(ctp->rcvid, EOK, NULL, 0);

    return _RESMGR_NOREPLY;
}
```

### Example 2: write() with Pre-allocated Ring Buffer

```c
/* ============================================================
 * write() Handler — Ring Buffer (no malloc per write)
 * ============================================================ */

#define RING_BUF_SIZE  65536

static char ring_buffer[RING_BUF_SIZE];
static int ring_head = 0;
static int ring_tail = 0;

int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
    int status;
    int nbytes;
    int space_available;

    /* Verify and check xtype */
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;
    }
    if (msg->i.xtype != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    nbytes = msg->i.nbytes;

    /* Check available space */
    space_available = RING_BUF_SIZE - (ring_head - ring_tail);
    if (nbytes > space_available) {
        return ENOSPC;    /* no space left */
    }

    /* Get data directly into ring buffer */
    if (nbytes > 0) {
        /* Calculate write position */
        int write_pos = ring_head % RING_BUF_SIZE;

        /* Handle wrap-around */
        if (write_pos + nbytes <= RING_BUF_SIZE) {
            /* Single contiguous write */
            resmgr_msgget(ctp, ring_buffer + write_pos, nbytes,
                          sizeof(io_write_t));
        } else {
            /* Split across end of buffer */
            int first_part = RING_BUF_SIZE - write_pos;
            iov_t iov[2];
            SETIOV(&iov[0], ring_buffer + write_pos, first_part);
            SETIOV(&iov[1], ring_buffer, nbytes - first_part);
            resmgr_msggetv(ctp, iov, 2, sizeof(io_write_t));
        }

        ring_head += nbytes;
    }

    /* Mark modification time */
    if (nbytes > 0) {
        ocb->attr->flags |= IOFUNC_ATTR_MTIME
                          | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* Reply */
    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    MsgReply(ctp->rcvid, EOK, NULL, 0);

    return _RESMGR_NOREPLY;
}
```

### Example 3: write() with Hardware Cache Buffer

```c
/* ============================================================
 * write() Handler — Flash Driver with Hardware Cache
 * ============================================================ */

#define CACHE_SIZE  4096

static char hw_cache[CACHE_SIZE];
static int cache_used = 0;

int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
    int status;
    int nbytes;

    /* Verify and check xtype */
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;
    }
    if (msg->i.xtype != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    nbytes = msg->i.nbytes;

    /* Check if data fits in cache */
    if (nbytes > CACHE_SIZE - cache_used) {
        /* Flush cache to hardware first */
        if (flush_cache_to_flash() != 0) {
            return EIO;
        }
        cache_used = 0;
    }

    /* Still too big? */
    if (nbytes > CACHE_SIZE) {
        return ENOSPC;    /* single write too large for cache */
    }

    /* Get data into hardware cache */
    if (nbytes > 0) {
        resmgr_msgget(ctp, hw_cache + cache_used, nbytes,
                      sizeof(io_write_t));
        cache_used += nbytes;

        /* Mark modification time */
        ocb->attr->flags |= IOFUNC_ATTR_MTIME
                          | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* Reply */
    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    MsgReply(ctp->rcvid, EOK, NULL, 0);

    return _RESMGR_NOREPLY;
}
```

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     WRITE HANDLER QUICK REF                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  HANDLER SIGNATURE:                                                         │
│    int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb)│
│                                                                             │
│  MESSAGE ACCESS:                                                            │
│    • msg->i.nbytes   → bytes client wants to write                         │
│    • msg->i.xtype    → expected: _IO_XTYPE_NONE                            │
│    • msg->i.offset   → file position (for regular files)                   │
│                                                                             │
│  DATA LOCATION:                                                             │
│    • msg + 1         → pointer to data in receive buffer                   │
│    • (char*)msg + sizeof(io_write_t)  → same thing                         │
│                                                                             │
│  ⚠️  msg + 1 only works if data fits in receive buffer!                   │
│                                                                             │
│  FOR LARGE WRITES:                                                          │
│    • resmgr_msgget(ctp, buf, nbytes, sizeof(io_write_t))                  │
│    • resmgr_msggetv(ctp, dst_iov, parts, sizeof(io_write_t))              │
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
│  3. GET DATA:                                                               │
│     Option A (small data, fits buffer):                                     │
│        data = msg + 1;   /* pointer to data */                             │
│                                                                             │
│     Option B (any size, safe):                                              │
│        buf = malloc(msg->i.nbytes);                                        │
│        resmgr_msgget(ctp, buf, msg->i.nbytes, sizeof(io_write_t));        │
│        /* process buf... */                                                 │
│        free(buf);                                                           │
│                                                                             │
│  4. MARK MTIME: if (msg->i.nbytes > 0)                                    │
│                  ocb->attr->flags |= IOFUNC_ATTR_MTIME |                   │
│                                     IOFUNC_ATTR_DIRTY_TIME;                │
│                                                                             │
│  5. REPLY:   _IO_SET_WRITE_NBYTES(ctp, nbytes_written);                   │
│              MsgReply(ctp->rcvid, EOK, NULL, 0);                           │
│                                                                             │
│  6. RETURN:  _RESMGR_NOREPLY                                              │
│                                                                             │
│  ERROR:      return errno_value;  (library calls MsgError for you)        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### resmgr_msgget() vs Direct Access

```
┌─────────────────────────┬─────────────────────────┬─────────────────────────┐
│      Method             │      When to use        │      Pros / Cons        │
├─────────────────────────┼─────────────────────────┼─────────────────────────┤
│  msg + 1                │  Small writes only      │  Fast, simple           │
│  (direct pointer)       │  (< receive buffer)     │  FAILS for large writes │
├─────────────────────────┼─────────────────────────┼─────────────────────────┤
│  resmgr_msgget()        │  Any size writes        │  Safe, handles all      │
│                         │  (RECOMMENDED)          │  Slightly more overhead │
├─────────────────────────┼─────────────────────────┼─────────────────────────┤
│  resmgr_msggetv()       │  Scatter into multiple  │  Flexible, no copy      │
│                         │  buffers                │  More complex setup     │
└─────────────────────────┴─────────────────────────┴─────────────────────────┘
```

### Return Values

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  FROM HANDLER          │  WHAT HAPPENS          │  CLIENT SEES              │
├─────────────────────────────────────────────────────────────────────────────┤
│  _RESMGR_NOREPLY       │  Library does nothing  │  write() returns          │
│                        │  (you already replied) │  nbytes_written           │
├─────────────────────────────────────────────────────────────────────────────┤
│  errno value (EPERM,   │  Library calls         │  write() returns -1       │
│  ENOSYS, ENOSPC, etc.) │  MsgError(rcvid, err)  │  errno = that value       │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Key Differences: read() vs write()

```
┌─────────────────────┬─────────────────────────┬─────────────────────────┐
│      Aspect         │        read()           │        write()          │
├─────────────────────┼─────────────────────────┼─────────────────────────┤
│  Client sends       │  Header only            │  Header + data          │
│                     │  (no data payload)      │  (data in message)      │
├─────────────────────┼─────────────────────────┼─────────────────────────┤
│  Reply contains     │  Data + status          │  Status only            │
│                     │  (bytes read)           │  (bytes written)        │
├─────────────────────┼─────────────────────────┼─────────────────────────┤
│  EOF concept        │  Yes (return 0)         │  No                     │
│                     │                         │  (file grows or error)  │
├─────────────────────┼─────────────────────────┼─────────────────────────┤
│  Time update        │  Access time (ATIME)    │  Modification time      │
│                     │                         │  (MTIME)                │
├─────────────────────┼─────────────────────────┼─────────────────────────┤
│  Data handling      │  Get from device        │  Get from client        │
│                     │  → reply with it        │  → process it           │
├─────────────────────┼─────────────────────────┼─────────────────────────┤
│  Large data issue   │  No (you control reply) │  Yes (client sends it)  │
│                     │                         │  → use resmgr_msgget()  │
└─────────────────────┴─────────────────────────┴─────────────────────────┘
```

---

> **📌 Key Takeaways:**
>
> - **write() sends header + data** as a 2-part IOV message via `MsgSendv()`
> - **Reply with status only** — no data returned to client
> - **Use `_IO_SET_WRITE_NBYTES(ctp, nbytes)`** to set bytes-written in reply
> - **Always call `iofunc_write_verify()`** first to check permissions
> - **Always check `msg->i.xtype`** — return `ENOSYS` if not `_IO_XTYPE_NONE`
> - **Data follows header** in receive buffer: access via `msg + 1`
> - **For large writes**, `msg + 1` is NOT enough — use `resmgr_msgget()`
> - **`resmgr_msgget()`** copies locally first, calls `MsgRead()` only when needed
> - **Mark modification time** with `IOFUNC_ATTR_MTIME | IOFUNC_ATTR_DIRTY_TIME`
> - **Return `_RESMGR_NOREPLY`** after explicit `MsgReply()`
> - **Return errno value** for errors (library calls `MsgError()` for you)
