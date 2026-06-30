# QNX Inter-Process Communication — Shared Memory

> **Companion to:** QNX Event Delivery & sigevent (Part 5), Deadlock Avoidance (Part 2), Server Designs (Part 3), Multi-Part Messages (Part 4)

This document covers **shared memory** in QNX Neutrino — the highest-bandwidth IPC mechanism for sharing data between processes. Unlike message passing, shared memory eliminates data copies by making the same physical memory visible to multiple processes' address spaces.

---

## 1. Shared Memory Concepts

### What is Shared Memory?

Shared memory is a mechanism where two or more processes are given access to the same underlying physical memory. The OS makes a region of system RAM visible in multiple process address spaces simultaneously.

```
┌─────────────────────────────────────────────────────────┐
│              Shared Memory Architecture                 │
│                                                         │
│   Process A Address Space    Process B Address Space    │
│   ┌──────────────────┐      ┌──────────────────┐        │
│   │      Code        │      │      Code        │        │
│   ├──────────────────┤      ├──────────────────┤        │
│   │      Data        │      │      Data        │    	  │
│   ├──────────────────┤      ├──────────────────┤     	  │
│   │                  │      │                  │        │
│   │  ┌────────────┐  │      │  ┌────────────┐  │        │
│   │  │  Shared    │◀─┼──────┼──┤  Shared    │  │        │
│   │  │  Memory    │  │      │  │  Memory    │  │        │
│   │  │  Region    │  │      │  │  Region    │  │        │
│   │  │  (4KB)     │  │      │  │  (4KB)     │  │        │
│   │  └────────────┘  │      │  └────────────┘  │        │
│   │       ▲          │      │       ▲          │        │
│   └───────┼──────────┘      └───────┼──────────┘        │
│           │                         │                   │
│           └────────┬────────────────┘                   │
│                    │                                    │
│              ┌─────┴─────┐                              │
│              │ Physical  │                              │
│              │  Memory   │                              │
│              │  (4KB)    │                              │
│              └───────────┘                              │
│                                                         │
│   Both processes see the SAME physical memory.          │
│   Writes by A are immediately visible to B.             │
│   No kernel copies. No MsgSend/MsgReceive overhead.     │
└─────────────────────────────────────────────────────────┘
```

**Key characteristics:**

| Property            | Behavior                                                     |
| ------------------- | ------------------------------------------------------------ |
| **Speed**           | Fastest IPC — no kernel copies after setup                   |
| **Visibility**      | Writes immediately visible to all sharing processes          |
| **Contiguity**      | Memory may or may not be physically contiguous (normally doesn't matter) |
| **Page Size**       | Allocations rounded up to page size (4KB on all current QNX implementations) |
| **Lifetime**        | Independent of any single process — survives until explicitly unlinked |
| **Synchronization** | Required — no built-in synchronization                       |

### Shared Memory vs Message Passing

| Aspect                | Message Passing                              | Shared Memory                                  |
| --------------------- | -------------------------------------------- | ---------------------------------------------- |
| **Copy overhead**     | One kernel copy per MsgSend                  | Zero copies after setup                        |
| **Synchronization**   | Built-in (MsgSend blocks, MsgReply unblocks) | Manual (mutexes, semaphores, etc.)             |
| **Data size**         | Best for small-to-medium messages            | Best for large data blocks                     |
| **Complexity**        | Simpler, structured                          | More complex, requires sync                    |
| **Security**          | Implicit (kernel validates)                  | Explicit (must design access control)          |
| **Failure isolation** | Strong (process death doesn't corrupt)       | Weak (bad pointer crashes everyone)            |
| **Typical use**       | Control, metadata, small data                | Bulk data, images, video frames, large buffers |

**The hybrid approach:** Use message passing for control/synchronization and shared memory for bulk data transfer.

### Memory Visibility Guarantees

```
┌──────────────────────────────────────────────────────────┐
│           Shared Memory Visibility Model                 │
│                                                          │
│  Process A writes:                                       │
│    shared_ptr->value = 42;                               │
│                                                          │
│  Memory barrier (implicit or explicit)                   │
│                                                          │
│  Process B reads:                                        │
│    int x = shared_ptr->value;  // x == 42                │
│                                                          │
│  Without synchronization:                                │
│    B might read stale data (cache coherency issues)      │
│                                                          │
│  With synchronization (mutex):                           │
│    pthread_mutex_lock(&shared_mutex);  // Memory barrier │
│    shared_ptr->value = 42;                               │
│    pthread_mutex_unlock(&shared_mutex); // Flush cache   │
│                                                          │
│    pthread_mutex_lock(&shared_mutex);  // Acquire cache  │
│    int x = shared_ptr->value;  // Guaranteed fresh       │
│    pthread_mutex_unlock(&shared_mutex);                  │
└──────────────────────────────────────────────────────────┘
```

---

## 2. POSIX Shared Memory (Named)

The standard POSIX approach uses `shm_open()` to create or open a named shared memory object, `ftruncate()` to allocate memory, and `mmap()` to obtain a pointer.

### shm_open() — Creating Shared Memory

```c
#include <sys/mman.h>
#include <fcntl.h>

int shm_open(const char *name, int oflag, mode_t mode);
```

| Parameter | Description                                                |
| --------- | ---------------------------------------------------------- |
| `name`    | Pathname-style name for the shared memory object           |
| `oflag`   | Open flags: `O_RDONLY`, `O_RDWR`, `O_CREAT`, `O_EXCL`      |
| `mode`    | Permissions (when `O_CREAT` is used): `0644`, `0600`, etc. |

**Return value:** File descriptor (like a regular file), or `-1` on error.

### Naming Rules

POSIX specifies strict rules for shared memory names:

| Rule                 | POSIX Says                             | QNX Behavior                           |
| -------------------- | -------------------------------------- | -------------------------------------- |
| **No leading slash** | Behavior is **undefined**              | QNX prepends current working directory |
| **Embedded slashes** | Behavior is **implementation-defined** | QNX treats them as subdirectories      |
| **Leading slash**    | Required for portability               | **Always use this**                    |

**Best practice:** Always start with `/` for absolute, predictable names.

```c
// GOOD: Absolute name, works from any directory
int fd = shm_open("/myapp/video_buffer", O_RDWR | O_CREAT, 0600);

// BAD: Relative name, depends on cwd
int fd = shm_open("myapp_buffer", O_RDWR | O_CREAT, 0600);
// QNX prepends cwd: /current/dir/myapp_buffer
// Different if started from different directories!

// GOOD: Hierarchical naming avoids collisions
int fd = shm_open("/company/product/feature/data", ...);
```

### O_CREAT and O_EXCL

```c
// Creator: O_CREAT | O_EXCL — fail if already exists
int fd = shm_open("/myshm", O_RDWR | O_CREAT | O_EXCL, 0600);
if (fd == -1) {
    if (errno == EEXIST) {
        // Someone else created it first
        // Re-open without O_CREAT | O_EXCL
        fd = shm_open("/myshm", O_RDWR, 0);
    } else {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
}
```

| Flag               | Meaning                                          |
| ------------------ | ------------------------------------------------ |
| `O_CREAT`          | Create if doesn't exist                          |
| `O_EXCL`           | Fail if already exists (atomic "create or fail") |
| `O_CREAT | O_EXCL` | "I am the creator" — only first caller succeeds  |

**Use case:** Multiple processes from a shared library all try to create. Only one succeeds, the rest open the existing object.

### ftruncate() — Allocating Memory

`shm_open()` creates an **empty** object (size 0). `ftruncate()` allocates the actual memory:

```c
#include <unistd.h>

int ftruncate(int fd, off_t length);
```

```c
// Allocate 16KB of shared memory
size_t size = 16 * 1024;  // 16KB
int ret = ftruncate(fd, size);
if (ret == -1) {
    perror("ftruncate");
    exit(EXIT_FAILURE);
}
```

**Page size rounding:**

```
Requested: 21KB  →  Allocated: 24KB (rounded up to 3 x 4KB pages)
Requested: 4KB   →  Allocated: 4KB  (exact page)
Requested: 100B  →  Allocated: 4KB  (minimum one page)
```

> **QNX page size:** 4KB on all current implementations. Use `sysconf(_SC_PAGESIZE)` to query at runtime.

```c
long page_size = sysconf(_SC_PAGESIZE);
size_t alloc_size = ((requested_size + page_size - 1) / page_size) * page_size;
```

**Zero-filled:** All newly allocated memory is zero-initialized by the OS.

### mmap() — Mapping Memory

`mmap()` maps the shared memory object into the process's address space, returning a pointer:

```c
#include <sys/mman.h>

void *mmap(void *addr, size_t len, int prot, int flags,
           int fd, off_t off);
```

| Parameter | Description                                            |
| --------- | ------------------------------------------------------ |
| `addr`    | Preferred address (usually `NULL` — let kernel choose) |
| `len`     | Number of bytes to map                                 |
| `prot`    | Protection: `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`     |
| `flags`   | `MAP_SHARED` (required for IPC) or `MAP_PRIVATE`       |
| `fd`      | File descriptor from `shm_open()`                      |
| `off`     | Offset within object (usually `0`)                     |

**Critical flags:**

| Flag          | Meaning                        | Use for IPC?            |
| ------------- | ------------------------------ | ----------------------- |
| `MAP_SHARED`  | Changes visible to all mappers | **Yes** — required      |
| `MAP_PRIVATE` | Copy-on-write, private copy    | **No** — breaks sharing |

```c
// Map the entire shared memory object
void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
if (ptr == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
}

// Now 'ptr' is a pointer to shared memory!
// Writes through ptr are visible to all other processes mapping the same object.
```

**Permission checking:** `mmap()` prot must be compatible with `shm_open()` mode:

```c
// Opened read-only, map for write → FAILS
int fd = shm_open("/myshm", O_RDONLY, 0);
void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
// Returns MAP_FAILED, errno = EACCES
```

### Access Permissions

```c
// Creator: read/write for owner only
int fd = shm_open("/myshm", O_RDWR | O_CREAT, 0600);
// 0600 = S_IRUSR | S_IWUSR
// Owner: read + write
// Group: no access
// Others: no access

// Creator: read/write for owner, read for group
int fd = shm_open("/myshm", O_RDWR | O_CREAT, 0640);
// 0640 = S_IRUSR | S_IWUSR | S_IRGRP

// Creator: read/write for all (use with caution!)
int fd = shm_open("/myshm", O_RDWR | O_CREAT, 0666);
```

| Mode   | Owner | Group | Other |
| ------ | ----- | ----- | ----- |
| `0600` | rw    | —     | —     |
| `0640` | rw    | r     | —     |
| `0644` | rw    | r     | r     |
| `0666` | rw    | rw    | rw    |

### Complete Creator Example

```c
/*
 * shm_creator.c
 * Creates and initializes a named shared memory object.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SHM_NAME        "/myapp_data"
#define SHM_SIZE        (4 * 1024)      // 4KB (one page)

// Data structure stored in shared memory
struct shared_data {
    int initialized;        // Flag: 1 if creator has initialized
    int counter;           // Shared counter
    char message[256];     // Shared message buffer
    // Add more fields as needed
};

int main(void) {
    int fd;
    struct shared_data *data;

    // Step 1: Create shared memory object
    fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        if (errno == EEXIST) {
            printf("Shared memory already exists. Removing and recreating...\n");
            shm_unlink(SHM_NAME);
            fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, 0600);
        }
        if (fd == -1) {
            perror("shm_open");
            exit(EXIT_FAILURE);
        }
    }

    printf("[Creator] Created shared memory: %s\n", SHM_NAME);

    // Step 2: Allocate memory (set size)
    if (ftruncate(fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }
    printf("[Creator] Allocated %d bytes\n", SHM_SIZE);

    // Step 3: Map into address space
    data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        shm_unlink(SHM_NAME);
        exit(EXIT_FAILURE);
    }
    printf("[Creator] Mapped at address %p\n", (void *)data);

    // Step 4: Close file descriptor (no longer needed after mmap)
    close(fd);
    printf("[Creator] Closed file descriptor\n");

    // Step 5: Initialize shared data (zero-filled by OS, but set explicit values)
    memset(data, 0, SHM_SIZE);
    data->initialized = 1;
    data->counter = 0;
    strcpy(data->message, "Hello from creator!");

    printf("[Creator] Initialized shared data:\n");
    printf("  initialized = %d\n", data->initialized);
    printf("  counter = %d\n", data->counter);
    printf("  message = '%s'\n", data->message);

    // Step 6: Keep running so other processes can access
    printf("[Creator] Press Enter to exit and cleanup...\n");
    getchar();

    // Step 7: Cleanup
    munmap(data, SHM_SIZE);
    shm_unlink(SHM_NAME);
    printf("[Creator] Cleaned up shared memory\n");

    return 0;
}
```

### Complete Accessor Example

```c
/*
 * shm_accessor.c
 * Opens and accesses an existing named shared memory object.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SHM_NAME        "/myapp_data"
#define SHM_SIZE        (4 * 1024)

struct shared_data {
    int initialized;
    int counter;
    char message[256];
};

int main(void) {
    int fd;
    struct shared_data *data;
    int iterations = 0;

    // Step 1: Open existing shared memory (no O_CREAT!)
    fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (fd == -1) {
        perror("shm_open (is the creator running?)");
        exit(EXIT_FAILURE);
    }

    printf("[Accessor] Opened shared memory: %s\n", SHM_NAME);

    // Step 2: Map into address space
    data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Step 3: Close file descriptor (not needed after mmap)
    close(fd);

    // Step 4: Verify initialization
    if (!data->initialized) {
        fprintf(stderr, "[Accessor] Shared memory not initialized!\n");
        munmap(data, SHM_SIZE);
        exit(EXIT_FAILURE);
    }

    printf("[Accessor] Connected to shared memory at %p\n", (void *)data);
    printf("[Accessor] Initial counter = %d\n", data->counter);
    printf("[Accessor] Initial message = '%s'\n", data->message);

    // Step 5: Read and modify shared data
    for (int i = 0; i < 5; i++) {
        // Read current counter
        int current = data->counter;
        printf("[Accessor] Read counter = %d\n", current);

        // Increment counter
        data->counter = current + 1;
        printf("[Accessor] Wrote counter = %d\n", data->counter);

        // Update message
        snprintf(data->message, sizeof(data->message),
                 "Updated by accessor, iteration %d", i);
        printf("[Accessor] Updated message = '%s'\n", data->message);

        sleep(1);
    }

    printf("[Accessor] Final counter = %d\n", data->counter);

    // Step 6: Cleanup (munmap only — don't shm_unlink!)
    munmap(data, SHM_SIZE);
    printf("[Accessor] Unmapped shared memory\n");

    return 0;
}
```

---

## 3. Reference Counting & Lifetime

### What Keeps Memory Alive?

The QNX process manager maintains a **reference count** for each shared memory object. The object exists as long as any reference exists:

| Reference Type      | Created By                  | Removed By                 |
| ------------------- | --------------------------- | -------------------------- |
| **Name**            | `shm_open()` with `O_CREAT` | `shm_unlink()`             |
| **File descriptor** | `shm_open()`                | `close()`                  |
| **Memory mapping**  | `mmap()`                    | `munmap()` or process exit |

### shm_unlink() — Removing the Name

```c
#include <sys/mman.h>

int shm_unlink(const char *name);
```

**Important:** `shm_unlink()` removes the **name** from the pathname space, but the memory persists until all mappings and file descriptors are closed.

```c
// Remove the name — but memory stays alive if mappings exist
shm_unlink("/myshm");
// Other processes can still access via existing mappings
// New processes CANNOT open it (name is gone)
```

### Automatic Cleanup on Process Death

When a process exits or crashes:

- All its file descriptors are **automatically closed**
- All its memory mappings are **automatically unmapped**
- The **name is NOT automatically removed** — must call `shm_unlink()` explicitly

### Reference Count Walkthrough

```
┌─────────────────────────────────────────────────────────┐
│           Reference Count Lifecycle Example             │
│                                                         │
│  Step 1: Creator creates shared memory                  │
│  ┌─────────┐                                            │
│  │ Creator │  shm_open(O_CREAT) → fd1                   │
│  │         │  ftruncate(16K) → memory allocated         │
│  │         │  mmap() → mapping1                         │
│  │         │  close(fd1) → fd removed                   │
│  │         │  References: name(1) + mapping1(1) = 2     │
│  └─────────┘                                            │
│                                                         │
│  Step 2: Accessor opens shared memory                   │
│  ┌─────────┐                                            │
│  │Accessor │  shm_open() → fd2                          │
│  │         │  mmap() → mapping2                         │
│  │         │  close(fd2) → fd removed                   │
│  │         │  References: name(1) + mapping1(1) +       │
│  │         │            mapping2(1) = 3                 │
│  └─────────┘                                            │
│                                                         │
│  Step 3: Accessor crashes                               │
│  ┌─────────┐                                            │
│  │Accessor │  Process dies → mapping2 auto-removed      │
│  │ (dead)  │  References: name(1) + mapping1(1) = 2     │
│  └─────────┘                                            │
│                                                         │
│  Step 4: Creator unmaps and unlinks                     │
│  ┌─────────┐                                            │
│  │ Creator │  munmap(mapping1) → mapping removed        │
│  │         │  shm_unlink() → name removed               │
│  │         │  References: 0 → MEMORY FREED              │
│  └─────────┘                                            │
│                                                         │
│  Alternative: Creator unlinks before unmapping          │
│  ┌─────────┐                                            │
│  │ Creator │  shm_unlink() → name removed               │
│  │         │  References: mapping1(1) = 1               │
│  │         │  Memory still accessible!                  │
│  │         │  munmap(mapping1) → MEMORY FREED           │
│  └─────────┘                                            │
│                                                         │
│  Note: Memory can survive name removal (orphaned memory)│
└─────────────────────────────────────────────────────────┘
```

### /dev/shm — Debug Interface

QNX exposes all named shared memory objects via `/dev/shm/`:

```bash
# List all shared memory objects
$ ls /dev/shm/
myapp_data   video_buffer   sensor_ring

# Examine contents (for debugging)
$ cat /dev/shm/myapp_data | hexdump -C

# Remove a shared memory object manually
$ rm /dev/shm/myapp_data
# Equivalent to: shm_unlink("/myapp_data")

# Check sizes
$ ls -la /dev/shm/
-rw------- 1 user group 4096 Jan 15 10:30 myapp_data
-rw------- 1 user group 65536 Jan 15 10:31 video_buffer
```

> **Note:** `/dev/shm/` contents are **volatile** — lost on reboot. Unlike files on disk, shared memory is RAM-only.

---

## 4. Anonymous Shared Memory

### SHM_ANON

For scenarios where you don't want a globally visible name (security, avoiding collisions), use **anonymous shared memory**:

```c
int fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
```

| Property   | Behavior                                                     |
| ---------- | ------------------------------------------------------------ |
| `SHM_ANON` | Special constant instead of pathname                         |
| Visibility | Not accessible by name — only via handle or inherited fd     |
| Uniqueness | Each `shm_open(SHM_ANON)` creates a **new, distinct** object |
| Use case   | Server-client private shared memory, security-sensitive data |

### Handles: shm_create_handle()

To share anonymous memory between processes, the creator generates a **handle** that the client can use:

```c
#include <sys/mman.h>

int shm_create_handle(int fd,
                      pid_t pid,          // Target process allowed to use handle
                      int permissions,    // Max permissions for client (O_RDONLY, O_RDWR)
                      unsigned *handle,   // Output: the handle value
                      unsigned flags);    // Flags (usually 0)
```

| Parameter     | Description                                      |
| ------------- | ------------------------------------------------ |
| `fd`          | File descriptor from `shm_open(SHM_ANON)`        |
| `pid`         | Process ID of the client who can use this handle |
| `permissions` | Maximum access: `O_RDONLY` or `O_RDWR`           |
| `handle`      | Output: the handle value to send to client       |
| `flags`       | Additional options (usually `0`)                 |

```c
// Server creates anonymous memory and handle for client
int fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
ftruncate(fd, 65536);  // 64KB

unsigned handle;
int ret = shm_create_handle(fd, client_pid, O_RDWR, &handle, 0);
if (ret == -1) {
    perror("shm_create_handle");
    exit(EXIT_FAILURE);
}

// Send 'handle' to client via message passing
MsgSend(coid_to_client, &handle, sizeof(handle), ...);
```

### shm_open_handle() / shm_open_handle_pid()

The client uses the handle to access the shared memory:

```c
#include <sys/mman.h>

// Option 1: Simple open (less secure)
int fd = shm_open_handle(handle, O_RDWR);

// Option 2: Verify the handle came from expected server (more secure)
int fd = shm_open_handle_pid(handle, O_RDWR, expected_server_pid);
```

| Function                | Security            | Use Case                                  |
| ----------------------- | ------------------- | ----------------------------------------- |
| `shm_open_handle()`     | Basic               | Trusted environment                       |
| `shm_open_handle_pid()` | Verifies origin PID | Prevents man-in-the-middle handle attacks |

```c
// Client receives handle from server, then maps it
unsigned handle;
MsgReceive(chid, &handle, sizeof(handle), &info);

// Verify handle came from the expected server
int fd = shm_open_handle_pid(handle, O_RDWR, server_pid);
if (fd == -1) {
    perror("shm_open_handle_pid");
    exit(EXIT_FAILURE);
}

// Map the shared memory
void *ptr = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
close(fd);  // No longer needed

// Now 'ptr' points to shared memory with the server
```

### Complete Anonymous Example

```c
/*
 * anonymous_shm.c
 * Demonstrates anonymous shared memory with handle-based sharing.
 * Compile: qcc -o anonymous_shm anonymous_shm.c
 * Run:     ./anonymous_shm server  (Terminal 1)
 *          ./anonymous_shm client <pid> <chid> (Terminal 2)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/siginfo.h>

#define SHM_SIZE        (16 * 1024)  // 16KB
#define MSG_GET_HANDLE  1
#define MSG_DATA_READY  2

struct shm_msg {
    int type;
    unsigned handle;
    size_t size;
};

struct shared_buffer {
    int ready;           // Flag: data is ready
    int sequence;        // Sequence number
    char data[SHM_SIZE - 8];  // Actual data payload
};

/* ==================== SERVER ==================== */

void run_server(void) {
    int chid = ChannelCreate(0);
    int rcvid;
    struct shm_msg msg;
    struct _msg_info info;
    int fd;
    struct shared_buffer *buf;
    unsigned handle;
    int sequence = 0;

    printf("[Server] PID=%d CHID=%d\n", getpid(), chid);

    // Create anonymous shared memory
    fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
    if (fd == -1) {
        perror("shm_open SHM_ANON");
        exit(EXIT_FAILURE);
    }

    ftruncate(fd, SHM_SIZE);
    buf = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    memset(buf, 0, SHM_SIZE);
    printf("[Server] Anonymous SHM created at %p\n", (void *)buf);

    for (;;) {
        rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);
        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) continue;

        switch (msg.type) {
            case MSG_GET_HANDLE: {
                // Create handle for this specific client
                int ret = shm_create_handle(fd, info.pid, O_RDWR, &handle, 0);
                if (ret == -1) {
                    perror("shm_create_handle");
                    MsgError(rcvid, EIO);
                    break;
                }

                struct shm_msg reply = {
                    .type = MSG_GET_HANDLE,
                    .handle = handle,
                    .size = SHM_SIZE
                };
                MsgReply(rcvid, EOK, &reply, sizeof(reply));
                printf("[Server] Sent handle=%u to pid=%d\n", handle, info.pid);
                break;
            }

            case MSG_DATA_READY: {
                // Client says data is ready in shared memory
                printf("[Server] Client says data ready, seq=%d\n", buf->sequence);
                printf("[Server] Data: '%.50s...'\n", buf->data);
                MsgReply(rcvid, EOK, NULL, 0);
                break;
            }

            default:
                MsgError(rcvid, ENOSYS);
                break;
        }
    }
}

/* ==================== CLIENT ==================== */

void run_client(int server_pid, int server_chid) {
    int coid;
    struct shm_msg req, reply;
    int fd;
    struct shared_buffer *buf;

    // Connect to server
    coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach");
        exit(EXIT_FAILURE);
    }

    // Request handle from server
    req.type = MSG_GET_HANDLE;
    int status = MsgSend(coid, &req, sizeof(req), &reply, sizeof(reply));
    if (status == -1 || reply.type != MSG_GET_HANDLE) {
        perror("MsgSend");
        exit(EXIT_FAILURE);
    }

    printf("[Client] Received handle=%u, size=%zu\n", reply.handle, reply.size);

    // Open handle with PID verification (secure)
    fd = shm_open_handle_pid(reply.handle, O_RDWR, server_pid);
    if (fd == -1) {
        perror("shm_open_handle_pid");
        exit(EXIT_FAILURE);
    }

    // Map the shared memory
    buf = mmap(NULL, reply.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    close(fd);

    printf("[Client] Mapped SHM at %p\n", (void *)buf);

    // Write data to shared memory
    for (int i = 0; i < 3; i++) {
        buf->sequence = i + 1;
        snprintf(buf->data, sizeof(buf->data),
                 "Hello from client, message #%d", i + 1);
        buf->ready = 1;

        printf("[Client] Wrote seq=%d to shared memory\n", buf->sequence);

        // Notify server via message (synchronization)
        req.type = MSG_DATA_READY;
        MsgSend(coid, &req, sizeof(req), NULL, 0);

        sleep(2);
    }

    munmap(buf, reply.size);
    printf("[Client] Done\n");
}

/* ==================== MAIN ==================== */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s server\n", argv[0]);
        printf("  %s client <pid> <chid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "server") == 0) {
        run_server();
    } else if (strcmp(argv[1], "client") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s client <pid> <chid>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
        run_client(atoi(argv[2]), atoi(argv[3]));
    }

    return 0;
}
```

---

## 5. Synchronization in Shared Memory

### The Synchronization Problem

Shared memory provides no built-in synchronization. If multiple processes read/write simultaneously, data races occur:

```c
// UNSAFE: Both processes increment without synchronization
// Process A                          Process B
// --------                           --------
// int x = data->counter;  // reads 5
//                                    int x = data->counter;  // reads 5
// data->counter = x + 1;  // writes 6
//                                    data->counter = x + 1;  // writes 6
//
// Expected: 7, Actual: 6 (lost update!)
```

### Process-Shared Synchronization Objects

POSIX synchronization primitives can be placed in shared memory and configured for **inter-process** use:

| Primitive              | Setup                              | Process-Shared Flag      |
| ---------------------- | ---------------------------------- | ------------------------ |
| **Mutex**              | `pthread_mutexattr_setpshared()`   | `PTHREAD_PROCESS_SHARED` |
| **Condition Variable** | `pthread_condattr_setpshared()`    | `PTHREAD_PROCESS_SHARED` |
| **RW Lock**            | `pthread_rwlockattr_setpshared()`  | `PTHREAD_PROCESS_SHARED` |
| **Semaphore**          | `sem_init()` parameter             | `pshared != 0`           |
| **Barrier**            | `pthread_barrierattr_setpshared()` | `PTHREAD_PROCESS_SHARED` |

### PTHREAD_PROCESS_SHARED

```c
#include <pthread.h>

// Shared memory structure with embedded mutex
struct shared_data {
    pthread_mutex_t mutex;
    int counter;
    char buffer[1024];
};

void init_shared_mutex(struct shared_data *data) {
    pthread_mutexattr_t attr;

    // Initialize mutex attributes
    pthread_mutexattr_init(&attr);

    // CRITICAL: Set process-shared flag
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    // Initialize mutex in shared memory
    pthread_mutex_init(&data->mutex, &attr);

    pthread_mutexattr_destroy(&attr);
}

// Process A
void process_a_work(struct shared_data *data) {
    pthread_mutex_lock(&data->mutex);
    data->counter++;
    pthread_mutex_unlock(&data->mutex);
}

// Process B
void process_b_work(struct shared_data *data) {
    pthread_mutex_lock(&data->mutex);
    printf("Counter = %d\n", data->counter);
    pthread_mutex_unlock(&data->mutex);
}
```

### sem_init() with pshared

```c
#include <semaphore.h>

struct shared_data {
    sem_t semaphore;  // Unnamed semaphore in shared memory
    int resource;
};

void init_shared_semaphore(struct shared_data *data) {
    // pshared = 1: semaphore is shared between processes
    sem_init(&data->semaphore, 1, 1);  // pshared=1, initial value=1
}

// Process A
void process_a(struct shared_data *data) {
    sem_wait(&data->semaphore);    // P (decrement)
    data->resource = 42;
    sem_post(&data->semaphore);    // V (increment)
}
```

### Robust Mutexes

**The problem:** If a process dies while holding a mutex, other processes deadlock forever.

```
┌─────────────────────────────────────────────────────────┐
│              The Orphaned Mutex Problem                  │
│                                                          │
│  Process A                    Process B                │
│  ─────────                    ─────────                │
│  pthread_mutex_lock(&mutex);                            │
│  // CRASH! Segfault                                          │
│  // Process A dies with mutex locked                     │
│                               pthread_mutex_lock(&mutex);│
│                               // BLOCKS FOREVER!         │
│                               // Mutex is never unlocked│
│                                                          │
│  Result: Process B deadlocks                             │
└─────────────────────────────────────────────────────────┘
```

**Solution: Robust Mutexes**

Robust mutexes detect when the owner dies and notify the next locker:

```c
#include <pthread.h>

void init_robust_shared_mutex(pthread_mutex_t *mutex) {
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);

    // Set process-shared
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    // Set ROBUST — detects owner death
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}
```

### EOWNERDEAD and ENOTRECOVERABLE

| Return Code       | Meaning                                 | Action Required                                       |
| ----------------- | --------------------------------------- | ----------------------------------------------------- |
| `EOWNERDEAD`      | Previous owner died while holding mutex | Recover data state, call `pthread_mutex_consistent()` |
| `ENOTRECOVERABLE` | Mutex state is unrecoverable            | Reinitialize mutex or restart                         |

```c
// Process B trying to lock after Process A died
int ret = pthread_mutex_lock(&shared_mutex);

if (ret == 0) {
    // Normal lock acquired
    use_data();
    pthread_mutex_unlock(&shared_mutex);

} else if (ret == EOWNERDEAD) {
    // Previous owner died!
    printf("WARNING: Previous owner died with mutex locked\n");

    // Attempt to recover data to known state
    if (can_recover_data()) {
        recover_data_to_known_state();

        // Mark mutex as consistent before unlocking
        pthread_mutex_consistent(&shared_mutex);
        pthread_mutex_unlock(&shared_mutex);

    } else {
        // Cannot recover — unlock without consistent()
        pthread_mutex_unlock(&shared_mutex);

        // Mutex is now ENOTRECOVERABLE
        // All future locks will fail with ENOTRECOVERABLE
        fprintf(stderr, "FATAL: Cannot recover mutex state\n");
        exit(EXIT_FAILURE);
    }

} else if (ret == ENOTRECOVERABLE) {
    // Mutex was previously marked unrecoverable
    fprintf(stderr, "FATAL: Mutex is unrecoverable\n");
    exit(EXIT_FAILURE);

} else {
    perror("pthread_mutex_lock");
    exit(EXIT_FAILURE);
}
```

### Complete Robust Mutex Example

```c
/*
 * robust_mutex.c
 * Demonstrates robust mutexes in shared memory with
 * process death recovery.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>

#define SHM_NAME    "/robust_mutex_demo"
#define SHM_SIZE    4096

struct shared_state {
    pthread_mutex_t mutex;
    int counter;
    int data_valid;
    char data[256];
};

void init_shared_state(struct shared_state *state) {
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&state->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    state->counter = 0;
    state->data_valid = 0;
    memset(state->data, 0, sizeof(state->data));
}

void safe_increment(struct shared_state *state) {
    int ret = pthread_mutex_lock(&state->mutex);

    if (ret == EOWNERDEAD) {
        printf("[PID %d] Detected dead owner! Recovering...\n", getpid());

        // Recover: reset counter to known state
        state->counter = 0;
        state->data_valid = 0;
        strcpy(state->data, "RECOVERED");

        pthread_mutex_consistent(&state->mutex);
        printf("[PID %d] Mutex marked consistent\n", getpid());

    } else if (ret == ENOTRECOVERABLE) {
        fprintf(stderr, "[PID %d] Mutex unrecoverable!\n", getpid());
        exit(EXIT_FAILURE);
    } else if (ret != 0) {
        fprintf(stderr, "[PID %d] Lock error: %d\n", getpid(), ret);
        exit(EXIT_FAILURE);
    }

    // Critical section
    state->counter++;
    printf("[PID %d] Counter = %d\n", getpid(), state->counter);

    pthread_mutex_unlock(&state->mutex);
}

int main(int argc, char *argv[]) {
    int fd;
    struct shared_state *state;
    int is_creator = 0;

    // Try to create
    fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        if (errno == EEXIST) {
            // Open existing
            fd = shm_open(SHM_NAME, O_RDWR, 0);
        } else {
            perror("shm_open");
            exit(EXIT_FAILURE);
        }
    } else {
        is_creator = 1;
        ftruncate(fd, SHM_SIZE);
    }

    state = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (is_creator) {
        init_shared_state(state);
        printf("[Creator PID %d] Initialized shared state\n", getpid());
    }

    // Fork a child that will crash with mutex held
    pid_t child = fork();
    if (child == 0) {
        // Child: lock mutex, then crash
        printf("[Child PID %d] Locking mutex...\n", getpid());
        pthread_mutex_lock(&state->mutex);
        state->counter = 999;
        printf("[Child PID %d] Mutex locked, counter = 999\n", getpid());
        printf("[Child PID %d] CRASHING!\n", getpid());
        abort();  // Simulate crash
    }

    // Parent: wait for child to die, then recover
    sleep(2);  // Let child lock and crash
    printf("[Parent PID %d] Child died, attempting to lock...\n", getpid());
    safe_increment(state);

    // Cleanup
    if (is_creator) {
        pthread_mutex_destroy(&state->mutex);
        munmap(state, SHM_SIZE);
        shm_unlink(SHM_NAME);
    }

    return 0;
}
```

**Expected output:**

```
[Creator PID 1234] Initialized shared state
[Child PID 1235] Locking mutex...
[Child PID 1235] Mutex locked, counter = 999
[Child PID 1235] CRASHING!
[Parent PID 1234] Child died, attempting to lock...
[Parent PID 1234] Detected dead owner! Recovering...
[Parent PID 1234] Mutex marked consistent
[Parent PID 1234] Counter = 1
```

---

## 6. Hybrid IPC: Messages + Shared Memory

### The Pattern

The most efficient QNX designs combine **message passing** (for control and synchronization) with **shared memory** (for bulk data):

```
┌─────────────────────────────────────────────────────────┐
│              Hybrid IPC Architecture                    │
│                                                         │
│   ┌─────────┐                    ┌─────────┐            │
│   │ Client  │                    │ Server  │            │
│   │         │                    │         │            │
│   │  ┌─────┐│                    │┌─────┐  │            │
│   │  │Data ││                    ││Data │  │            │
│   │  │Buf  ││◀──────Shared─────▶ ││Buf  │  │            │
│   │  └─────┘│      Memory        │└─────┘  │            │
│   │         │                    │         │            │
│   │ MsgSend │───────▶            │ MsgReceive│          │
│   │ "Display│                    │"Display │            │
│   │  image 5│                    │ image 5"│            │
│   │         │◀───────            │ MsgReply│            │
│   │ "Done"  │                    │ "Done"  │            │
│   └─────────┘                    └─────────┘            │
│                                                         │
│   Control Flow: MsgSend/MsgReceive/MsgReply             │
│   Data Flow: Shared Memory (zero copy)                  │
│   Synchronization: Implicit in message passing          │
└─────────────────────────────────────────────────────────┘
```

### When to Use

| Scenario                          | Approach                                     |
| --------------------------------- | -------------------------------------------- |
| Small data (< 1KB)                | Message passing only — simpler               |
| Large data (> 1KB, images, video) | Shared memory + message control              |
| Frequent updates                  | Shared memory + mutex synchronization        |
| One-shot transfers                | Message passing                              |
| Multiple consumers                | Shared memory + semaphores                   |
| Real-time constraints             | Shared memory + message sync for determinism |

### Complete Hybrid Example

```c
/*
 * hybrid_ipc.c
 * Graphics server using hybrid IPC:
 * - Messages for control (which image to display)
 * - Shared memory for image data (zero copy)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/neutrino.h>

#define SHM_SIZE        (256 * 1024)   // 256KB for image data
#define MAX_IMAGES      16
#define MSG_DISPLAY     1

// Image metadata in shared memory
struct image_slot {
    int valid;           // 1 if image data is valid
    int width;
    int height;
    size_t data_size;
    char data[SHM_SIZE / MAX_IMAGES - 16];
};

struct shared_images {
    struct image_slot slots[MAX_IMAGES];
};

struct display_request {
    int type;
    int slot_index;      // Which image slot to display
};

/* ==================== SERVER ==================== */

void run_server(void) {
    int chid = ChannelCreate(0);
    int rcvid;
    struct display_request req;
    struct _msg_info info;
    int fd;
    struct shared_images *images;

    printf("[Server] PID=%d CHID=%d\n", getpid(), chid);

    // Create shared memory for images
    fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
    ftruncate(fd, sizeof(struct shared_images));
    images = mmap(NULL, sizeof(struct shared_images),
                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memset(images, 0, sizeof(struct shared_images));
    close(fd);

    printf("[Server] Shared images at %p\n", (void *)images);

    // Create handle for clients (simplified: any client)
    unsigned handle;
    shm_create_handle(fd, 0, O_RDWR, &handle, 0);  // 0 = any process

    for (;;) {
        rcvid = MsgReceive(chid, &req, sizeof(req), &info);
        if (rcvid == 0) continue;

        if (req.type == MSG_DISPLAY) {
            if (req.slot_index < 0 || req.slot_index >= MAX_IMAGES) {
                MsgError(rcvid, EINVAL);
                continue;
            }

            struct image_slot *slot = &images->slots[req.slot_index];
            if (!slot->valid) {
                MsgError(rcvid, ENOENT);
                continue;
            }

            printf("[Server] Displaying image from slot %d\n", req.slot_index);
            printf("[Server]   Dimensions: %dx%d\n", slot->width, slot->height);
            printf("[Server]   Data size: %zu bytes\n", slot->data_size);
            printf("[Server]   First 32 bytes: %.32s...\n", slot->data);

            // Simulate display processing
            usleep(100000);  // 100ms

            MsgReply(rcvid, EOK, NULL, 0);
        }
    }
}

/* ==================== CLIENT ==================== */

void run_client(int server_pid, int server_chid) {
    int coid;
    struct display_request req;

    coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);

    // In a real system, client would receive handle and map shared memory
    // For this demo, we just send display requests

    for (int i = 0; i < 3; i++) {
        req.type = MSG_DISPLAY;
        req.slot_index = i;

        printf("[Client] Requesting display of slot %d\n", i);
        int status = MsgSend(coid, &req, sizeof(req), NULL, 0);
        if (status == -1) {
            perror("MsgSend");
        } else {
            printf("[Client] Display complete\n");
        }

        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s server | client <pid> <chid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "server") == 0) {
        run_server();
    } else if (strcmp(argv[1], "client") == 0) {
        run_client(atoi(argv[2]), atoi(argv[3]));
    }

    return 0;
}
```

---

## 7. Common Problems & Solutions

### Pathname Collisions

**Problem:** Multiple developers choose the same shared memory name.

```c
// Company A's component
int fd = shm_open("/myshm", ...);  // Oops!

// Company B's component (same name!)
int fd = shm_open("/myshm", ...);  // Collides!
```

**Solutions:**

```c
// 1. Hierarchical naming
shm_open("/company_a/product/feature/buffer", ...);
shm_open("/company_b/subsystem/data", ...);

// 2. Use anonymous shared memory + handles
shm_open(SHM_ANON, ...);  // No name collision possible

// 3. Include process-specific identifiers
char name[64];
snprintf(name, sizeof(name), "/myapp_%d_buffer", getpid());
shm_open(name, ...);
```

### Security

**Problem:** Unix permissions (0600, 0644) are coarse-grained.

| Approach                            | Security Level | Use Case                               |
| ----------------------------------- | -------------- | -------------------------------------- |
| `0600` permissions                  | Basic          | Single-user systems                    |
| `0400` (read-only for clients)      | Better         | Server writes, clients read            |
| Anonymous + handles                 | Good           | Server controls exactly who can access |
| Anonymous + `shm_open_handle_pid()` | Best           | Verify handle origin                   |

```c
// Server creates, client can only read
int fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
ftruncate(fd, size);

// Give client read-only access
shm_create_handle(fd, client_pid, O_RDONLY, &handle, 0);

// Client verifies and opens
int fd = shm_open_handle_pid(handle, O_RDONLY, server_pid);
```

### Orphaned Mutexes

**Problem:** Process dies with mutex locked.

| Solution        | How                                                          |
| --------------- | ------------------------------------------------------------ |
| Robust mutexes  | `pthread_mutexattr_setrobust()` + `pthread_mutex_consistent()` |
| Semaphores      | `sem_post()` on process cleanup (harder to guarantee)        |
| Message passing | Use `MsgSend`/`MsgReply` for sync instead of mutexes         |
| Watchdog        | Monitor process health, force recovery                       |

### Page Alignment

**Problem:** Sub-page mappings are aligned to page boundaries.

```c
// Request: map 2KB at offset 6KB
void *ptr = mmap(NULL, 2048, PROT_READ, MAP_SHARED, fd, 6144);
// Actual mapping: starts at 4KB (page boundary), size 8KB
// ptr points to offset 6KB within the mapping
// ptr[-2048] is valid (part of mapped page) but not what you wanted
```

**Solution:** Always align offsets and sizes to page boundaries:

```c
long page_size = sysconf(_SC_PAGESIZE);
off_t aligned_offset = (offset / page_size) * page_size;
size_t aligned_size = ((offset - aligned_offset + size + page_size - 1)
                      / page_size) * page_size;

void *ptr = mmap(NULL, aligned_size, PROT_READ, MAP_SHARED, fd, aligned_offset);
char *actual_data = (char *)ptr + (offset - aligned_offset);
```

---

## 8. Key Takeaways

### Core Principles

| Principle                       | Summary                                  |
| ------------------------------- | ---------------------------------------- |
| **shm_open creates a name**     | Like creating a file in `/dev/shm/`      |
| **ftruncate allocates memory**  | Rounded up to 4KB page size              |
| **mmap gives you a pointer**    | `MAP_SHARED` is required for IPC         |
| **close(fd) after mmap**        | File descriptor no longer needed         |
| **shm_unlink removes the name** | Memory survives until all mappings close |
| **Zero-initialized**            | New allocations are cleared by OS        |

### Named vs Anonymous Shared Memory

| Aspect         | Named (`/myshm`)           | Anonymous (`SHM_ANON`)    |
| -------------- | -------------------------- | ------------------------- |
| Visibility     | Global via `/dev/shm/`     | Private, no name          |
| Discovery      | By name                    | Via handle only           |
| Security       | File permissions           | Handle + PID verification |
| Collision risk | Yes                        | No                        |
| Use case       | Multiple unknown consumers | Server-client pairs       |

### Setup Steps

**Creator:**

```
shm_open(O_CREAT | O_EXCL) → ftruncate() → mmap(MAP_SHARED) → close(fd)
```

**Accessor:**

```
shm_open(no O_CREAT) → mmap(MAP_SHARED) → close(fd)
```

**Cleanup:**

```
munmap() → shm_unlink() [creator only]
```

### Synchronization Checklist

- [ ] Use `PTHREAD_PROCESS_SHARED` for all sync objects in shared memory
- [ ] Use robust mutexes for critical data (`pthread_mutexattr_setrobust`)
- [ ] Check `EOWNERDEAD` and call `pthread_mutex_consistent()`
- [ ] Consider semaphores for simple resource counting
- [ ] For simple cases, use message passing instead of manual sync

### Hybrid IPC Decision Tree

```
Data size?
│
├─ < 1KB → Use MsgSend/MsgReply only
│
└─ > 1KB → Continue...
    │
    ├─ One-shot transfer? → MsgSend with IOVs (MsgSendv)
    │
    └─ Frequent access / multiple updates? → Shared memory + sync
        │
        ├─ Server controls access? → MsgSend for control, SHM for data
        │
        └─ Peer-to-peer? → SHM + robust mutexes or semaphores
```

### Quick Reference Table

| Task                 | Function                                                     |
| -------------------- | ------------------------------------------------------------ |
| Create named SHM     | `shm_open(name, O_CREAT \| O_RDWR, 0600)`                    |
| Allocate memory      | `ftruncate(fd, size)`                                        |
| Map to pointer       | `mmap(NULL, size, PROT_READ \| PROT_WRITE, MAP_SHARED, fd, 0)` |
| Open existing        | `shm_open(name, O_RDWR, 0)`                                  |
| Remove name          | `shm_unlink(name)`                                           |
| Unmap                | `munmap(ptr, size)`                                          |
| Create anonymous     | `shm_open(SHM_ANON, O_CREAT \| O_RDWR, 0600)`                |
| Create handle        | `shm_create_handle(fd, pid, perms, &handle, 0)`              |
| Open handle          | `shm_open_handle_pid(handle, perms, server_pid)`             |
| Process-shared mutex | `pthread_mutexattr_setpshared(attr, PTHREAD_PROCESS_SHARED)` |
| Robust mutex         | `pthread_mutexattr_setrobust(attr, PTHREAD_MUTEX_ROBUST)`    |
| Recover mutex        | `pthread_mutex_consistent(&mutex)`                           |

---

## Related Documentation

- **Part 1:** QNX Message Passing Fundamentals (MsgSend, MsgReceive, MsgReply)
- **Part 2:** QNX Deadlock Avoidance — Send Hierarchy & Pulses
- **Part 3:** QNX Server Designs — Thread Pools, Priority Inheritance, Delayed Reply
- **Part 4:** QNX Multi-Part Messages — IOVs, MsgSendv, MsgRead, MsgWrite
- **Part 5:** QNX Event Delivery & sigevent Architecture
- **QNX Official:** [Shared Memory](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/shared_memory.html)
- **QNX Official:** [`shm_open()`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/s/shm_open.html)
- **QNX Official:** [`mmap()`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/m/mmap.html)

---

*This document synthesizes material from QNX Neutrino documentation, training materials, and practical embedded systems design patterns.*
