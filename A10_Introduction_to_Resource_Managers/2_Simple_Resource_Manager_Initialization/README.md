# QNX Resource Manager Initialization — Step-by-Step Guide

---

## Overview

This guide walks through initializing a QNX resource manager step by step. The example resource manager (`example_resmgr`) provides:

```
Example Resource Manager Behavior
═══════════════════════════════════════════════════════════════════

   Client Operation          Result
   ─────────────────         ──────
   read()                    Always returns 0 bytes
                             (no actual hardware to read from)

   write(any_size)           Always succeeds
                             (accepts any amount of data)

   open(), close(), etc.     Normal behavior
```

---

## Initialization Steps Summary

```
Resource Manager Initialization Flow
═══════════════════════════════════════════════════════════════════

   ┌─────────────────────────────────────────────────────────────┐
   │  Step 1: dispatch_create_channel()                          │
   │          Create channel for receiving messages                │
   │          Returns: dpp (dispatch pointer)                    │
   └────────────────────────┬────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Step 2: iofunc_funcs_init()                                │
   │          Initialize connect function table                    │
   │          (open, unlink, etc.)                               │
   └────────────────────────┬────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Step 3: iofunc_funcs_init()                                │
   │          Initialize I/O function table                      │
   │          (read, write, close, etc.)                         │
   └────────────────────────┬────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Step 4: iofunc_attr_init()                                 │
   │          Initialize device attribute structure                │
   │          (permissions, ownership, etc.)                     │
   └────────────────────────┬────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Step 5: secpol_resmgr_attach()                             │
   │          Register /dev/example in pathname space            │
   │          Requires: PROCMGR_AID_PATHSPACE ability            │
   │          ⚠️ Do this LAST after all setup is complete!       │
   └────────────────────────┬────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Step 6: dispatch_context_alloc()                           │
   │          Allocate dispatch context for receive loop         │
   │          Returns: ctp (context pointer)                     │
   └────────────────────────┬────────────────────────────────────┘
                            │
                            ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  Step 7: dispatch_block() + dispatch_handler()            │
   │          Main loop: block for messages, dispatch to handlers  │
   └─────────────────────────────────────────────────────────────┘
```

---

## Step 1: Create Dispatch Structure

Create a channel for the resource manager to receive messages on.

```
Step 1: dispatch_create_channel()
═══════════════════════════════════════════════════════════════════

   Function:
   ─────────
   dispatch_t *dispatch_create_channel(
       int chid,              ← channel ID (-1 = create new)
       unsigned flags         ← flags (e.g., DISPATCH_FLAG_NOLOCK)
   );


   Parameters:
   ───────────
   chid  = -1               ← create a new channel
   flags = DISPATCH_FLAG_NOLOCK  ← disable unnecessary locks


   Returns:
   ────────
   dpp  ← dispatch pointer (used in subsequent calls)


   Code:
   ────
   dispatch_t *dpp;

   dpp = dispatch_create_channel(-1, DISPATCH_FLAG_NOLOCK);
   if (dpp == NULL) {
       perror("dispatch_create_channel");
       exit(EXIT_FAILURE);
   }
```

```
   What it does:
   ─────────────

   ┌─────────────────┐
   │  dispatch_create │
   │  _channel()      │
   │                  │
   │  • Creates channel│
   │  • Returns dpp   │
   │    (dispatch     │
   │     pointer)     │
   └────────┬────────┘
            │
            ▼
   ┌─────────────────┐
   │   Channel       │  ← Resource manager receives
   │   (message      │     messages here
   │   queue)        │
   └─────────────────┘
```

---

## Step 2 & 3: Set Up Handler Tables

Initialize two function tables: one for **connect** messages and one for **I/O** messages.

```
Step 2 & 3: Handler Tables
═══════════════════════════════════════════════════════════════════

   Connect Functions Table          I/O Functions Table
   ─────────────────────            ───────────────────

   ┌─────────────────┐            ┌─────────────────┐
   │ iofunc_connect_ │            │ iofunc_funcs_t  │
   │ _funcs_t        │            │ io_funcs        │
   │ connect_funcs   │            │                 │
   │                 │            │                 │
   │ • open          │            │ • read          │
   │ • unlink        │            │ • write         │
   │ • rename        │            │ • close         │
   │ • link          │            │ • lseek         │
   │ • mknod         │            │ • stat          │
   │ • readlink      │            │ • devctl        │
   │ • ...           │            │ • ...           │
   └─────────────────┘            └─────────────────┘


   Initialization:
   ───────────────

   iofunc_funcs_init(               iofunc_funcs_init(
       _RESMGR_CONNECT_NFUNCS,           _RESMGR_IO_NFUNCS,
       &connect_funcs,                   &io_funcs,
       &iofunc_connect_default_funcs     &iofunc_io_default_funcs
   );                                 );

   │         │                        │         │
   │         │                        │         │
   ▼         ▼                        ▼         ▼
   number of                          number of
   connect                            I/O
   functions                          functions


   Override Specific Handlers:
   ─────────────────────────

   connect_funcs.open = io_open;     io_funcs.read = io_read;
                                     io_funcs.write = io_write;

   All other handlers use defaults
```

### Connect Functions Table

```c
/* Step 2: Initialize connect function table */
iofunc_connect_funcs_t connect_funcs;

iofunc_funcs_init(
    _RESMGR_CONNECT_NFUNCS,           /* number of connect functions */
    &connect_funcs,                   /* table to initialize */
    &iofunc_connect_default_funcs     /* default handlers */
);

/* Override specific handlers */
connect_funcs.open = io_open;         /* custom open handler */
/* connect_funcs.unlink = io_unlink;  ← if needed */
/* All others use defaults */
```

### I/O Functions Table

```c
/* Step 3: Initialize I/O function table */
iofunc_funcs_t io_funcs;

iofunc_funcs_init(
    _RESMGR_IO_NFUNCS,                /* number of I/O functions */
    &io_funcs,                        /* table to initialize */
    &iofunc_io_default_funcs            /* default handlers */
);

/* Override specific handlers */
io_funcs.read = io_read;            /* custom read handler */
io_funcs.write = io_write;            /* custom write handler */
/* io_funcs.close = io_close;  ← if needed */
/* All others use defaults */
```

```
   Handler Table Relationship:
   ───────────────────────────

   Connect Functions                I/O Functions
   ───────────────                  ────────────

   open()  ──► returns fd ──►  read(fd, ...)   ──► io_read()
                               write(fd, ...)   ──► io_write()
                               close(fd)        ──► io_close()
                               lseek(fd, ...)   ──► io_lseek()
                               stat(fd, ...)    ──► io_stat()
                               devctl(fd, ...)  ──► io_devctl()
```

---

## Step 4: Initialize Device Attributes

Set up the device attribute structure with permissions and ownership.

```
Step 4: iofunc_attr_init()
═══════════════════════════════════════════════════════════════════

   Function:
   ─────────
   void iofunc_attr_init(
       iofunc_attr_t *attr,     ← attribute structure to fill
       mode_t mode,              ← file mode/permissions
       iofunc_attr_t *dattr,    ← parent dir attributes (NULL = none)
       struct _client_info *info  ← client info (NULL = use defaults)
   );


   Parameters:
   ───────────
   attr  = &ioattr              ← device attributes structure
   mode  = S_IFCHR | 0666       ← character device, rw-rw-rw-
   dattr = NULL                 ← no parent directory attributes
   info  = NULL                 ← use default ownership


   Code:
   ────
   iofunc_attr_t ioattr;

   iofunc_attr_init(
       &ioattr,                   /* attribute structure */
       S_IFCHR | 0666,            /* character device, all rw */
       NULL,                      /* no parent directory */
       NULL                       /* default ownership */
   );
```

```
   Attribute Structure:
   ────────────────────

   ┌─────────────────────────────────────┐
   │  iofunc_attr_t                      │
   │                                     │
   │  • mode      (permissions)          │
   │  • uid       (user ID)              │
   │  • gid       (group ID)             │
   │  • nlink     (link count)             │
   │  • rdev      (device ID)            │
   │  • size      (file size)            │
   │  • flags     (internal flags)         │
   │  • ...                              │
   │                                     │
   │  Can be extended with custom data:  │
   │  struct my_attr {                   │
   │      iofunc_attr_t attr;            │
   │      int my_custom_field;           │
   │      char *my_buffer;               │
   │  };                                 │
   └─────────────────────────────────────┘
```

---

## Step 5: Attach to Pathname Space

Register the resource manager in the pathname space so clients can find it.

```
Step 5: secpol_resmgr_attach()
═══════════════════════════════════════════════════════════════════

   ⚠️  CRITICAL: Call this LAST after all setup is complete!
      Once attached, clients can see and use your resource manager.


   Function:
   ─────────
   int secpol_resmgr_attach(
       secpol_handle_t *handle,    ← security policy handle (NULL)
       dispatch_t *dpp,            ← dispatch pointer (from Step 1)
       resmgr_attr_t *attr,         ← resource manager attributes (NULL)
       const char *path,            ← pathname to register (e.g., "/dev/example")
       enum _file_type file_type,   ← file type (_FTYPE_ANY)
       unsigned flags,              ← control flags (0)
       const resmgr_connect_funcs_t *connect_funcs,  ← connect table
       const resmgr_io_funcs_t *io_funcs,            ← I/O table
       iofunc_attr_t *ioattr        ← device attributes (from Step 4)
   );


   Parameters:
   ───────────
   handle        = NULL           ← no security policy handle
   dpp           = dpp            ← from dispatch_create_channel()
   attr          = NULL           ← no extra resource manager attrs
   path          = "/dev/example" ← pathname to register
   file_type     = _FTYPE_ANY     ← any file type
   flags         = 0              ← no special flags
   connect_funcs = &connect_funcs ← connect handler table
   io_funcs      = &io_funcs      ← I/O handler table
   ioattr        = &ioattr        ← device attributes


   Returns:
   ────────
   id  ← resource manager ID (use for detaching later)


   Requires Ability:
   ─────────────────
   PROCMGR_AID_PATHSPACE
   (permission to register in pathname space)


   Security Policy Updates:
   ──────────────────────
   • May update attributes based on security policy
   • Can set UID, GID, permissions, ACLs
   • System integrator controls resource manager security
   • perms_set parameter indicates if updates occurred
```

```
   What secpol_resmgr_attach Does:
   ───────────────────────────────

   Before attach:                      After attach:
   ─────────────                       ────────────

   Pathname Space                      Pathname Space
   ┌─────────────┐                    ┌─────────────┐
   │      /      │                    │      /      │
   │    /dev     │                    │    /dev     │
   │   /dev/ser1 │                    │   /dev/ser1 │
   │   /dev/con1 │                    │   /dev/con1 │
   │             │                    │   /dev/exam │◄── NEW!
   │             │                    │     ple     │
   └─────────────┘                    └─────────────┘
                                           │
                                           ▼
                                    ┌─────────────┐
                                    │ Your ResMgr │
                                    │  (visible!) │
                                    └─────────────┘

   Clients can now:
   open("/dev/example", ...)
```

### Code

```c
/* Step 5: Attach to pathname space */
resmgr_attr_t resmgr_attr;
bool perms_set;

memset(&resmgr_attr, 0, sizeof(resmgr_attr));

/* ⚠️ All hardware setup, buffer allocation, etc. must be done BEFORE this! */
if (secpol_resmgr_attach(
        NULL,                    /* secpol_handle */
        dpp,                     /* dispatch pointer */
        &resmgr_attr,            /* resource manager attributes */
        "/dev/example",          /* pathname */
        _FTYPE_ANY,              /* file type */
        0,                       /* flags */
        &connect_funcs,          /* connect functions */
        &io_funcs,               /* I/O functions */
        &ioattr,                 /* device attributes */
        &perms_set               /* permissions updated? */
    ) == -1) {
    perror("secpol_resmgr_attach");
    exit(EXIT_FAILURE);
}

/* Check if security policy updated attributes */
if (perms_set) {
    printf("Security policy updated device attributes\n");
}
```

### Important Notes

```
┌─────────────────────────────────────────────────────────────────────┐
│  ⚠️  IMPORTANT NOTES ABOUT secpol_resmgr_attach                     │
│                                                                      │
│  1. Call LAST — after all initialization is complete               │
│     • Hardware detected                                              │
│     • Buffers allocated                                              │
│     • All configuration done                                         │
│                                                                      │
│  2. Tables must persist forever                                    │
│     • Library does NOT copy connect_funcs or io_funcs               │
│     • Declare as global or malloc() them                            │
│     • Do NOT use stack-local variables                               │
│                                                                      │
│  3. Requires privilege                                              │
│     • PROCMGR_AID_PATHSPACE ability needed                         │
│                                                                      │
│  4. Security policy may modify attributes                           │
│     • Check perms_set to know if changes occurred                    │
│                                                                      │
│  5. Older API: resmgr_attach()                                       │
│     • secpol_resmgr_attach = resmgr_attach + security policy        │
│     • Same functionality + security enhancements                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Step 6: Allocate Dispatch Context

Allocate the context structure used by the main message loop.

```
Step 6: dispatch_context_alloc()
═══════════════════════════════════════════════════════════════════

   Function:
   ─────────
   dispatch_context_t *dispatch_context_alloc(
       dispatch_t *dpp    ← dispatch pointer
   );


   Returns:
   ────────
   ctp  ← context pointer (points to receive buffer)


   What ctp contains:
   ──────────────────
   • rcvid      ← receive ID of current message
   • info       ← client info
   • msg        ← pointer to receive buffer
   • ...        ← other message metadata


   Code:
   ────
   dispatch_context_t *ctp;

   ctp = dispatch_context_alloc(dpp);
   if (ctp == NULL) {
       perror("dispatch_context_alloc");
       exit(EXIT_FAILURE);
   }
```

```
   Dispatch Context:
   ─────────────────

   ┌─────────────────────────────────────┐
   │  dispatch_context_t (ctp)           │
   │                                     │
   │  • rcvid     ← who sent message     │
   │  • msg       ← pointer to buffer    │
   │  • info      ← client credentials   │
   │  • size      ← message size         │
   │  • ...                               │
   │                                     │
   │  Passed to ALL handler functions    │
   │  as the ctp parameter               │
   └─────────────────────────────────────┘
           │
           ▼
   ┌─────────────────────────────────────┐
   │  Receive Buffer                     │
   │  (holds incoming message data)        │
   └─────────────────────────────────────┘
```

---

## Step 7: Main Message Loop

Enter the main loop to receive and dispatch messages.

```
Step 7: dispatch_block() + dispatch_handler()
═══════════════════════════════════════════════════════════════════

   Main Loop:
   ──────────

   while (1) {
       ctp = dispatch_block(ctp);      ← block waiting for message
       if (ctp == NULL) { ... }         ← error check

       dispatch_handler(ctp);          ← parse & dispatch to handler
   }


   What happens:
   ─────────────

   ┌─────────────────┐
   │ dispatch_block()│
   │                 │
   │ • Calls MsgReceive()
   │ • Blocks until message arrives
   │ • Fills ctp with message info
   │ • Returns ctp
   └────────┬────────┘
            │
            ▼
   ┌─────────────────┐
   │dispatch_handler()│
   │                 │
   │ • Examines message type
   │ • Calls appropriate handler:
   │   - connect function (open, unlink)
   │   - I/O function (read, write, close)
   │   - pulse handler
   │   - default handler
   └─────────────────┘
```

```c
/* Step 7: Main message loop */
while (1) {
    /* Block waiting for a message */
    ctp = dispatch_block(ctp);
    if (ctp == NULL) {
        perror("dispatch_block");
        break;
    }

    /* Dispatch to appropriate handler */
    dispatch_handler(ctp);
}
```

---

## Complete Code Example

```c
/* ============================================================
 * QNX Resource Manager — Complete Initialization Example
 * Registers /dev/example with custom read/write handlers
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/procmgr.h>

/* Custom handler: read always returns 0 bytes */
int io_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb) {
    /* No actual hardware — return 0 bytes */
    return _RESMGR_NPARTS(0);
}

/* Custom handler: write accepts any size */
int io_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb) {
    int status;

    /* Let framework do permission checks and update offset */
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK) {
        return status;
    }

    /* Update access time */
    if (msg->i.nbytes > 0) {
        ocb->attr->flags |= IOFUNC_ATTR_ATIME | IOFUNC_ATTR_DIRTY_TIME;
    }

    /* Return number of "bytes written" (accept everything) */
    _IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);
    return _RESMGR_NPARTS(0);
}

int main(int argc, char *argv[]) {
    /* Step 0: Grant pathname space ability */
    if (procmgr_ability(PROCMGR_ADN_ROOT,
                        PROCMGR_AOP_ALLOW | PROCMGR_AID_PATHSPACE,
                        PROCMGR_AOP_DENY  | PROCMGR_AID_EOL) == -1) {
        perror("procmgr_ability");
        return EXIT_FAILURE;
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 1: Create dispatch structure (channel)             */
    /* ─────────────────────────────────────────────────────── */
    dispatch_t *dpp;

    dpp = dispatch_create_channel(-1, DISPATCH_FLAG_NOLOCK);
    if (dpp == NULL) {
        perror("dispatch_create_channel");
        return EXIT_FAILURE;
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 2: Initialize connect function table               */
    /* ─────────────────────────────────────────────────────── */
    resmgr_connect_funcs_t connect_funcs;

    iofunc_funcs_init(
        _RESMGR_CONNECT_NFUNCS,
        &connect_funcs,
        &iofunc_connect_default_funcs
    );
    /* Use default open handler (no override needed for simple case) */

    /* ─────────────────────────────────────────────────────── */
    /* Step 3: Initialize I/O function table                    */
    /* ─────────────────────────────────────────────────────── */
    resmgr_io_funcs_t io_funcs;

    iofunc_funcs_init(
        _RESMGR_IO_NFUNCS,
        &io_funcs,
        &iofunc_io_default_funcs
    );

    /* Override read and write handlers */
    io_funcs.read = io_read;
    io_funcs.write = io_write;
    /* All other I/O handlers use defaults */

    /* ─────────────────────────────────────────────────────── */
    /* Step 4: Initialize device attributes                     */
    /* ─────────────────────────────────────────────────────── */
    iofunc_attr_t ioattr;

    iofunc_attr_init(
        &ioattr,
        S_IFCHR | 0666,          /* Character device, rw-rw-rw- */
        NULL,                     /* No parent directory */
        NULL                      /* Default ownership */
    );

    /* ─────────────────────────────────────────────────────── */
    /* ⚠️  Do ALL hardware setup, buffer allocation, etc. HERE */
    /*     BEFORE calling secpol_resmgr_attach()               */
    /* ─────────────────────────────────────────────────────── */

    /* ─────────────────────────────────────────────────────── */
    /* Step 5: Attach to pathname space                        */
    /* ─────────────────────────────────────────────────────── */
    resmgr_attr_t resmgr_attr;
    bool perms_set;

    memset(&resmgr_attr, 0, sizeof(resmgr_attr));

    if (secpol_resmgr_attach(
            NULL,                    /* secpol_handle */
            dpp,                     /* dispatch pointer */
            &resmgr_attr,            /* resource manager attributes */
            "/dev/example",          /* pathname */
            _FTYPE_ANY,              /* file type */
            0,                       /* flags */
            &connect_funcs,          /* connect functions */
            &io_funcs,               /* I/O functions */
            &ioattr,                 /* device attributes */
            &perms_set               /* permissions updated? */
        ) == -1) {
        perror("secpol_resmgr_attach");
        return EXIT_FAILURE;
    }

    if (perms_set) {
        printf("Security policy updated device attributes\n");
    }

    printf("Resource manager registered at /dev/example\n");

    /* ─────────────────────────────────────────────────────── */
    /* Step 6: Allocate dispatch context                      */
    /* ─────────────────────────────────────────────────────── */
    dispatch_context_t *ctp;

    ctp = dispatch_context_alloc(dpp);
    if (ctp == NULL) {
        perror("dispatch_context_alloc");
        return EXIT_FAILURE;
    }

    /* ─────────────────────────────────────────────────────── */
    /* Step 7: Main message loop                                */
    /* ─────────────────────────────────────────────────────── */
    printf("Resource manager running, waiting for messages...\n");

    while (1) {
        /* Block waiting for a message */
        ctp = dispatch_block(ctp);
        if (ctp == NULL) {
            perror("dispatch_block");
            break;
        }

        /* Dispatch to appropriate handler */
        dispatch_handler(ctp);
    }

    return EXIT_SUCCESS;
}
```

---

## Quick Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                  RESOURCE MANAGER INIT QUICK REF                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  STEP 1: dispatch_create_channel(-1, DISPATCH_FLAG_NOLOCK)                │
│          → Returns: dpp (dispatch pointer)                                  │
│                                                                             │
│  STEP 2: iofunc_funcs_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,        │
│                            &iofunc_connect_default_funcs)                   │
│          → Override: connect_funcs.open = my_open;                        │
│                                                                             │
│  STEP 3: iofunc_funcs_init(_RESMGR_IO_NFUNCS, &io_funcs,                  │
│                            &iofunc_io_default_funcs)                        │
│          → Override: io_funcs.read = my_read;                               │
│                    io_funcs.write = my_write;                               │
│                                                                             │
│  STEP 4: iofunc_attr_init(&ioattr, S_IFCHR | 0666, NULL, NULL)           │
│          → Sets device permissions and type                                 │
│                                                                             │
│  STEP 5: secpol_resmgr_attach(NULL, dpp, NULL, "/dev/example",           │
│                                _FTYPE_ANY, 0, &connect_funcs,             │
│                                &io_funcs, &ioattr, &perms_set)           │
│          → Requires: PROCMGR_AID_PATHSPACE                                │
│          → ⚠️ Call LAST after all setup!                                  │
│          → Tables must be global/malloc'd (library doesn't copy)         │
│                                                                             │
│  STEP 6: dispatch_context_alloc(dpp)                                       │
│          → Returns: ctp (context pointer)                                   │
│                                                                             │
│  STEP 7: while (1) { ctp = dispatch_block(ctp); dispatch_handler(ctp); } │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Function Reference

```
┌─────────────────────────────┬────────────────────────────────────────────────┐
│  Function                   │  Purpose                                       │
├─────────────────────────────┼────────────────────────────────────────────────┤
│  dispatch_create_channel()  │  Create channel, return dispatch pointer       │
│  iofunc_funcs_init()        │  Initialize connect or I/O function table      │
│  iofunc_attr_init()         │  Initialize device attribute structure         │
│  secpol_resmgr_attach()     │  Register pathname, make visible to clients    │
│  dispatch_context_alloc()   │  Allocate context for receive loop             │
│  dispatch_block()           │  Block waiting for message (MsgReceive wrapper)│
│  dispatch_handler()         │  Parse message, dispatch to handler            │
└─────────────────────────────┴────────────────────────────────────────────────┘
```

### Important Rules

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              ⚠️  IMPORTANT RULES                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. secpol_resmgr_attach() is the LAST initialization step                  │
│     • All hardware setup, buffer allocation must be done BEFORE              │
│     • Once attached, clients can immediately use your resource manager    │
│                                                                             │
│  2. Handler tables must persist for the lifetime of the resource manager  │
│     • Declare as global variables                                            │
│     • Or allocate with malloc()                                              │
│     • Library stores pointers, does NOT copy the tables                      │
│                                                                             │
│  3. Requires PROCMGR_AID_PATHSPACE ability                                 │
│     • Must be granted before secpol_resmgr_attach()                          │
│                                                                             │
│  4. secpol_resmgr_attach vs resmgr_attach                                   │
│     • secpol_resmgr_attach = resmgr_attach + security policy                │
│     • secpol version is preferred for secure systems                         │
│                                                                             │
│  5. Security policy may update attributes                                   │
│     • Check perms_set parameter                                              │
│     • May set UID, GID, permissions, ACLs                                    │
│                                                                             │
│  6. Use DISPATCH_FLAG_NOLOCK for single-threaded resource managers          │
│     • Disables unnecessary locking overhead                                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

> **📌 Key Takeaways:**
>
> - **7 steps** to initialize a resource manager: dispatch → connect table → I/O table → attributes → attach → context → loop
> - **secpol_resmgr_attach()** must be called **LAST** — after all setup is complete
> - **Handler tables must persist** — library stores pointers, doesn't copy
> - **Requires PROCMGR_AID_PATHSPACE** ability
> - **dispatch_block() + dispatch_handler()** form the main message loop
> - **ctp (context pointer)** is passed to all handlers, contains message info
> - **Override only what you need** — framework provides sensible defaults
> - **secpol_resmgr_attach** extends resmgr_attach with security policy support
