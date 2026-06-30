# QNX Multi-Part Messages — IOVs, MsgSendv(), MsgRead(), and MsgWrite()

## Overview

This section covers **multi-part messaging** in QNX — the mechanism for sending and receiving messages composed of multiple, non-contiguous buffers without requiring intermediate copies. This is essential for efficient scatter/gather operations, variable-length protocols, and large data transfers.

---

## 1. The Problem: Assembling Large Messages

### The Naive Approach

Suppose a client has three separate buffers to send as one logical message:

| Buffer    | Size        |
| --------- | ----------- |
| Buffer 1  | 750 KB      |
| Buffer 2  | 500 KB      |
| Buffer 3  | 1000 KB     |
| **Total** | **2250 KB** |

With `MsgSend()`, you can only pass **one** buffer pointer and length:

```c
// BAD: Must allocate and copy everything into one buffer
char *big_buffer = malloc(2250 * 1024);  // 2.25 MB allocation
memcpy(big_buffer,              buf1, 750 * 1024);
memcpy(big_buffer + 750*1024,   buf2, 500 * 1024);
memcpy(big_buffer + 1250*1024,  buf3, 1000 * 1024);

MsgSend(coid, big_buffer, 2250 * 1024, reply, reply_size);
free(big_buffer);
```

**Problems with this approach:**

| Problem                  | Cost                                                    |
| ------------------------ | ------------------------------------------------------- |
| **Double memory usage**  | 2.25 MB original + 2.25 MB assembled = **4.5 MB total** |
| **Extra copy on client** | `memcpy()` x3 to assemble buffer                        |
| **Extra copy by kernel** | Kernel copies assembled buffer to server                |
| **Latency**              | Assembly time + copy time                               |

### The Better Way: Multi-Part Messaging

Instead of copying data into a contiguous buffer, describe the scattered buffers with an **IOV (Input/Output Vector)** and let the kernel assemble during copy:

```
Client Buffers (scattered in memory)          Kernel Assembles During Copy
┌─────────────┐                               ┌─────────────────────────────┐
│  Buffer 1   │──┐                            │  [Header] [Buf1][Buf2][Buf3]│
│  750 KB     │  │  IOV[0] = {ptr, 750KB}     │       Logical contiguous    │
└─────────────┘  │  IOV[1] = {ptr, 500KB}     │         message to server   │
┌─────────────┐  │  IOV[2] = {ptr, 1000KB}    └─────────────────────────────┘
│  Buffer 2   │──┘
│  500 KB     │
└─────────────┘
┌─────────────┐
│  Buffer 3   │
│  1000 KB    │
└─────────────┘
```

**Advantages:**

- **No client-side assembly copy** — kernel gathers during transmission
- **No extra memory allocation** — uses existing buffers
- **Single kernel copy** — directly from client buffers to server buffer

---

## 2. The IOV Structure

### Definition

An IOV is an array of `{pointer, length}` pairs describing scattered data:

```c
#include <sys/types.h>

struct iovec {
    void   *iov_base;    // Pointer to buffer
    size_t  iov_len;     // Length of buffer in bytes
};
```

### SETIOV Macro

QNX provides a convenience macro to populate an IOV entry:

```c
#include <sys/neutrino.h>

SETIOV(iov_entry, buffer_ptr, buffer_length);
// Equivalent to:
// iov_entry.iov_base = buffer_ptr;
// iov_entry.iov_len  = buffer_length;
```

> **SETIOV does NOT copy data** — it only sets up pointers. The copy happens when the IOV is passed to `MsgSendv()` or similar.

### Setting Up an IOV Array

```c
#include <sys/neutrino.h>

char buf1[750 * 1024];   // 750 KB
char buf2[500 * 1024];   // 500 KB
char buf3[1000 * 1024];  // 1000 KB

struct iovec siov[3];    // Send IOV array

// Set up pointers to the three buffers
SETIOV(&siov[0], buf1, sizeof(buf1));
SETIOV(&siov[1], buf2, sizeof(buf2));
SETIOV(&siov[2], buf3, sizeof(buf3));

// Total logical message size = 750 + 500 + 1000 = 2250 KB
```

---

## 3. MsgSendv() — Vector-Based Message Send

### API

```c
#include <sys/neutrino.h>

int MsgSendv(int coid,              // Connection ID
             const struct iovec *siov,  // Send IOV array
             int sparts,            // Number of entries in siov
             const struct iovec *riov,  // Reply IOV array
             int rparts);           // Number of entries in riov
```

**Parameters:**

| Parameter | Description                                             |
| --------- | ------------------------------------------------------- |
| `coid`    | Connection ID to the server                             |
| `siov`    | Array of IOV entries describing send buffers (gather)   |
| `sparts`  | Number of entries in `siov`                             |
| `riov`    | Array of IOV entries describing reply buffers (scatter) |
| `rparts`  | Number of entries in `riov`                             |

**Return Value:**

- **Success:** Status from server's `MsgReply()`
- **Failure:** `-1`, `errno` set

### How the Kernel Copies

```
Client Send Buffers (siov)              Server Receive Buffer
┌─────────┐  IOV[0]                     ┌──────────────────────────┐
│  Part 1 │──────────┐                  │  [Part 1][Part 2][Part 3]│
│  750KB  │          │  Gather          │       Contiguous         │
└─────────┘          └─────────────────▶│      in server space     │
┌─────────┐  IOV[1]  │                  └──────────────────────────┘
│  Part 2 │──────────┘
│  500KB  │			 │
└─────────┘			 │
┌─────────┐  IOV[2]  │
│  Part 3 │			 │
│  1000KB │──────────┘
└─────────┘
```

The kernel copies sequentially through each IOV entry, assembling the logical message.

### Complete Client Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>

#define PART1_SIZE (750 * 1024)
#define PART2_SIZE (500 * 1024)
#define PART3_SIZE (1000 * 1024)

int main(int argc, char *argv[]) {
    int coid;
    struct iovec siov[3];
    struct iovec riov[1];
    char reply_buffer[256];
    int status;

    // Assume coid is already connected
    coid = /* ... ConnectAttach or name_open ... */;

    // Allocate or obtain three separate buffers
    char *buf1 = malloc(PART1_SIZE);
    char *buf2 = malloc(PART2_SIZE);
    char *buf3 = malloc(PART3_SIZE);

    // Fill buffers with data...
    memset(buf1, 'A', PART1_SIZE);
    memset(buf2, 'B', PART2_SIZE);
    memset(buf3, 'C', PART3_SIZE);

    // Set up send IOV — gather from three buffers
    SETIOV(&siov[0], buf1, PART1_SIZE);
    SETIOV(&siov[1], buf2, PART2_SIZE);
    SETIOV(&siov[2], buf3, PART3_SIZE);

    // Set up reply IOV — single buffer
    SETIOV(&riov[0], reply_buffer, sizeof(reply_buffer));

    // Send multi-part message
    status = MsgSendv(coid, siov, 3, riov, 1);
    if (status == -1) {
        perror("MsgSendv");
    } else {
        printf("Server replied: %s
", reply_buffer);
    }

    free(buf1); free(buf2); free(buf3);
    return 0;
}
```

---

## 4. MsgReceivev() — Vector-Based Message Receive

### API

```c
#include <sys/neutrino.h>

rcvid_t MsgReceivev(int chid,                  // Channel ID
                    const struct iovec *riov,  // Receive IOV array
                    int rparts,                // Number of entries in riov
                    struct _msg_info *info);   // Client info (optional)
```

**The kernel scatters the incoming message across the receive buffers:**

```
Client Send Buffer (single or multi-part)    Server Receive Buffers (riov)
┌─────────────────────────────────────┐      	┌─────────┐  IOV[0]
│  [Header][Data Part 1][Data Part 2] │─────▶	│ Header  │  12 bytes
│         12KB total                  │      	└─────────┘
└─────────────────────────────────────┘        	┌─────────┐  IOV[1]
                                               	│ Cache 0 │  4KB
                                               	└─────────┘
                                               	┌─────────┐  IOV[2]
                                               	│ Cache 1 │  4KB
                                               	└─────────┘
                                               	┌─────────┐  IOV[3]
                                               	│ Cache 2 │  4KB
                                               	└─────────┘
```

### Server Example with MsgReceivev()

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>

#define HEADER_SIZE 12
#define CACHE_SIZE  (4 * 1024)

int main(void) {
    int chid;
    int rcvid;
    struct iovec riov[4];
    struct _msg_info info;

    char header[HEADER_SIZE];
    char cache0[CACHE_SIZE];
    char cache1[CACHE_SIZE];
    char cache2[CACHE_SIZE];

    chid = ChannelCreate(0);

    // Set up receive IOV — scatter into 4 buffers
    SETIOV(&riov[0], header, HEADER_SIZE);
    SETIOV(&riov[1], cache0, CACHE_SIZE);
    SETIOV(&riov[2], cache1, CACHE_SIZE);
    SETIOV(&riov[3], cache2, CACHE_SIZE);

    rcvid = MsgReceivev(chid, riov, 4, &info);
    if (rcvid > 0) {
        // Header is in header[0..11]
        // Data is scattered across cache0, cache1, cache2
        printf("Received %d bytes total
", info.msglen);
        printf("Header: %.12s
", header);

        MsgReply(rcvid, EOK, NULL, 0);
    }

    return 0;
}
```

---

## 5. The Real-World Pattern: Header + Variable Data

### The Problem with Fixed-Size Buffers

Most protocols have a **fixed-size header** followed by **variable-length data**:

```c
// BAD: Fixed-size buffer wastes space or limits data
struct msg_write {
    uint16_t type;       // Message type
    uint32_t nbytes;     // How much data follows
    char data[256];      // Fixed size — what if we need 2MB?
};
```

**Better approach:** Separate header from data using IOVs.

### write() Library Implementation (Conceptual)

This is how QNX's C library implements `write()` internally:

```c
#include <sys/neutrino.h>

// Header structure for _IO_WRITE message
struct _io_write {
    uint16_t type;       // _IO_WRITE (reserved system type)
    uint16_t combine_len;
    int32_t  nbytes;     // How many bytes of data follow
    int32_t  xtype;
    int32_t  zero;
};

// What write(fd, buf, nbytes) does internally:
int write_impl(int fd, const void *buf, size_t nbytes) {
    struct _io_write header;
    struct iovec siov[2];
    int status;

    // Build the header
    header.type = _IO_WRITE;
    header.nbytes = nbytes;
    // ... fill other fields ...

    // Set up 2-part IOV: header + data
    SETIOV(&siov[0], &header, sizeof(header));   // Part 1: 12-byte header
    SETIOV(&siov[1], buf, nbytes);                // Part 2: variable data

    // Send as one logical message — no local copy needed!
    status = MsgSendv(fd, siov, 2, NULL, 0);     // No reply data for write()

    return status;  // Returns bytes written or error
}
```

**Key insight:** The header is prepended **without** copying the data buffer. The kernel gathers header + data during transmission.

---

## 6. MsgRead() and MsgReadv() — Reading More Data

### The Problem

A server often cannot predict how much data a client will send:

```
Client sends:  [12-byte header][variable data: could be 1B, 12KB, 2MB, 200MB]

Server receives only the header first (12 bytes):
  rcvid = MsgReceive(chid, &header, sizeof(header), &info);
  // Kernel copies only 12 bytes. Client is still REPLY BLOCKED.
  // Remaining data is still in the client's address space.
```

**The client remains REPLY BLOCKED until the server replies.** This means the server can read more data on demand.

### MsgRead() — Read Additional Data from Client

```c
#include <sys/neutrino.h>

int MsgRead(int rcvid,           // Receive ID from MsgReceive()
            void *msg,           // Buffer to copy data into
            int bytes,           // Number of bytes to read
            int offset);         // Offset into client's send buffer
```

**Parameters:**

| Parameter | Description                                                  |
| --------- | ------------------------------------------------------------ |
| `rcvid`   | Receive ID from `MsgReceive()` — identifies which REPLY-blocked client |
| `msg`     | Destination buffer in server's address space                 |
| `bytes`   | Number of bytes to copy                                      |
| `offset`  | Byte offset into the client's send buffer where copying starts |

**Return Value:**

- **Success:** Number of bytes actually copied (may be less than requested)
- **Failure:** `-1`, `errno` set

### MsgReadv() — Vector-Based Read

```c
#include <sys/neutrino.h>

int MsgReadv(int rcvid,              // Receive ID
             const struct iovec *riov,  // Destination IOV array (scatter)
             int rparts,             // Number of entries in riov
             int offset);            // Offset into client's send buffer
```

**The kernel scatters data from the client into multiple server buffers:**

```
Client Send Buffer (still REPLY BLOCKED)
┌─────────────────────────────────────────────────────────┐
│ [Header: 12B] [Data Part 1: 4KB] [Data Part 2: 4KB] ... │
└─────────────────────────────────────────────────────────┘
                ▲
                │  MsgReadv(rcvid, riov, 3, offset=12)
                │  "Skip header, read data starting at byte 12"
                ▼
Server Buffers (scatter)
┌─────────┐  IOV[0]
│ Cache 6 │  4KB
└─────────┘
┌─────────┐  IOV[1]
│ Cache 2 │  4KB
└─────────┘
┌─────────┐  IOV[2]
│ Cache 5 │  4KB
└─────────┘
```

### Complete Server Example: write()-Style Handler

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>

#define HEADER_SIZE     12
#define CACHE_SIZE      (4 * 1024)
#define NUM_CACHES      16

struct _io_write {
    uint16_t type;
    uint16_t combine_len;
    int32_t  nbytes;
    int32_t  xtype;
    int32_t  zero;
};

// Simulated cache buffers
char caches[NUM_CACHES][CACHE_SIZE];
int cache_used[NUM_CACHES] = {0};

// Find available cache buffers (simplified LRU)
int find_free_caches(int needed, int *indices) {
    int found = 0;
    for (int i = 0; i < NUM_CACHES && found < needed; i++) {
        if (!cache_used[i]) {
            indices[found++] = i;
            cache_used[i] = 1;
        }
    }
    return found;
}

void handle_write(int rcvid, struct _io_write *header, struct _msg_info *info) {
    int data_bytes = header->nbytes;
    int num_caches_needed = (data_bytes + CACHE_SIZE - 1) / CACHE_SIZE;
    int cache_indices[num_caches_needed];
    struct iovec riov[num_caches_needed];
    int bytes_read;

    printf("Write request: %d bytes of data
", data_bytes);

    // Find available cache buffers
    int found = find_free_caches(num_caches_needed, cache_indices);
    if (found < num_caches_needed) {
        printf("Not enough cache buffers!
");
        MsgError(rcvid, ENOMEM);
        return;
    }

    // Set up IOVs pointing to cache buffers
    int remaining = data_bytes;
    for (int i = 0; i < found; i++) {
        int chunk = (remaining < CACHE_SIZE) ? remaining : CACHE_SIZE;
        SETIOV(&riov[i], caches[cache_indices[i]], chunk);
        remaining -= chunk;
    }

    // Read data from client, skipping the header (offset = HEADER_SIZE)
    bytes_read = MsgReadv(rcvid, riov, found, HEADER_SIZE);
    printf("Read %d bytes from client into %d cache buffers
", bytes_read, found);

    // Process data (e.g., write to hardware, file, etc.)
    // ...

    // Reply with number of bytes written
    MsgReply(rcvid, bytes_read, NULL, 0);

    // Mark caches as free (in real code, this happens after I/O completes)
    for (int i = 0; i < found; i++) {
        cache_used[cache_indices[i]] = 0;
    }
}

int main(void) {
    int chid;
    int rcvid;
    struct _io_write header;
    struct _msg_info info;

    chid = ChannelCreate(0);
    printf("Server started, channel=%d
", chid);

    for (;;) {
        // Receive ONLY the header first
        rcvid = MsgReceive(chid, &header, sizeof(header), &info);

        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            // Handle pulse
            continue;
        }

        switch (header.type) {
            case _IO_WRITE:
                handle_write(rcvid, &header, &info);
                break;

            default:
                MsgError(rcvid, ENOSYS);
                break;
        }
    }

    return 0;
}
```

---

## 7. MsgWrite() and MsgWritev() — Writing Data Back to Client

### Concept

While `MsgRead()` pulls data **from** the client's send buffer, `MsgWrite()` pushes data **into** the client's reply buffer.

```c
#include <sys/neutrino.h>

int MsgWrite(int rcvid,           // Receive ID
             const void *msg,     // Data to copy to client
             int bytes,           // Number of bytes
             int offset);         // Offset into client's reply buffer

int MsgWritev(int rcvid,                 // Receive ID
              const struct iovec *siov,  // Source IOV array (gather)
              int sparts,                // Number of entries
              int offset);               // Offset into client's reply buffer
```

### Use Cases

| Scenario                             | Approach                                                     |
| ------------------------------------ | ------------------------------------------------------------ |
| **Large reply, processed in chunks** | `MsgWrite()` multiple times, then `MsgReply()` with status only |
| **Insert header before data**        | `MsgWrite()` header at offset 0, then `MsgWrite()` data at offset = header_size |
| **Slow device, streaming reply**     | Write chunk by chunk as data becomes available               |

### MsgWrite() Example

```c
void handle_read_request(int rcvid, struct _msg_info *info) {
    char header[64];
    char data_chunk[4096];
    int total_bytes = 0;
    int offset = 0;

    // Build response header
    snprintf(header, sizeof(header), "DATASTREAM v1.0");

    // Write header to start of client's reply buffer
    MsgWrite(rcvid, header, strlen(header) + 1, offset);
    offset += strlen(header) + 1;

    // Stream data in chunks
    while ((bytes = read_from_hardware(data_chunk, sizeof(data_chunk))) > 0) {
        MsgWrite(rcvid, data_chunk, bytes, offset);
        offset += bytes;
        total_bytes += bytes;
    }

    // Final reply with total count (no additional data)
    MsgReply(rcvid, total_bytes, NULL, 0);
}
```

> **Note:** `MsgWrite()` is less commonly used than `MsgRead()`. Most servers assemble the complete reply and use `MsgReply()` or `MsgReplyv()`.

---

## 8. Complete Data Flow: write() Example

### Step-by-Step: Client Sends 12KB, Server Receives into 3x4KB Caches

```
STEP 1: Client calls MsgSendv() with 2-part IOV
┌──────────────────────────────────────────────────────────────────┐
│ Client Address Space                                             │
│  ┌─────────────┐  IOV[0]                                         │
│  │ _io_write   │  12 bytes (header: type=_IO_WRITE, nbytes=12288)│
│  │ header      │                                                 │
│  └─────────────┘                                                 │
│  ┌─────────────┐  IOV[1]                                         │
│  │ Data        │  12288 bytes                                    │
│  │ (12 KB)     │                                                 │
│  └─────────────┘                                                 │
│                                                                  │
│  MsgSendv(coid, siov, 2, NULL, 0)                                │
│  Client becomes REPLY BLOCKED                                    │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
STEP 2: Server calls MsgReceive() — receives only header
┌─────────────────────────────────────────────────────────────┐
│ Server Address Space                                        │
│  ┌─────────────┐                                            │
│  │ _io_write   │◀── 12 bytes copied from client             │
│  │ header      │    (kernel copies only what fits)          │
│  └─────────────┘                                            │
│                                                             │
│  rcvid = MsgReceive(chid, &header, sizeof(header), &info)   │
│  info.msglen = 12, info.srcmsglen = 12288 + 12 = 12300      │
│  Client STILL REPLY BLOCKED — rest of data available!       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
STEP 3: Server allocates cache buffers and calls MsgReadv()
┌─────────────────────────────────────────────────────────────┐
│ Server Address Space                                        │
│  ┌─────────────┐                                            │
│  │ Cache 6     │  4KB  ◀── IOV[0]                           │
│  └─────────────┘                                            │
│  ┌─────────────┐                                            │
│  │ Cache 2     │  4KB  ◀── IOV[1]                           │
│  └─────────────┘                                            │
│  ┌─────────────┐                                            │
│  │ Cache 5     │  4KB  ◀── IOV[2]                           │
│  └─────────────┘                                            │
│                                                             │
│  MsgReadv(rcvid, riov, 3, offset=12)  // Skip header        │
│  Returns: 12288 bytes copied                                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
STEP 4: Server processes data and replies
┌──────────────────────────────────────────────────────────────┐
│  // Data is now scattered in cache buffers                   │
│  // Process and write to hardware/file...                    │
│                                                              │
│  MsgReply(rcvid, 12288, NULL, 0)  // Status only             │
│                                                              │
│  Client unblocks, MsgSendv() returns 12288                   │
└──────────────────────────────────────────────────────────────┘
```

---

## 9. MsgRead() Return Values and Edge Cases

### Reading Within Bounds

```c
// Client sent: header (12B) + data (12KB) = 12300 bytes total

// Read 8KB starting after header (offset = 12)
int n = MsgRead(rcvid, buffer, 8192, 12);
// n = 8192 (full read, 12KB available after offset 12)

// Read 8KB starting at offset 8KB (within the 12KB data)
n = MsgRead(rcvid, buffer, 8192, 8192);
// n = 4096 (only 4KB remaining: 12288 - 8192 = 4096)
```

### Reading Past End of Data

```c
// Client sent: 12KB data (12300 bytes total including header)

// Read at offset 16KB — past the end of client's buffer
int n = MsgRead(rcvid, buffer, 8192, 16384);
// n = 0  (zero bytes available, NOT an error!)
// This is normal — it means "no more data at that offset"
```

### Key Rules

| Situation                | Result                                           |
| ------------------------ | ------------------------------------------------ |
| Read within bounds       | Returns bytes actually copied (≤ requested)      |
| Read past end            | Returns `0` — **not an error**                   |
| Invalid client pointer   | Returns `-1`, `errno` set — **this is an error** |
| Client already unblocked | Returns `-1`, `errno = ESRCH`                    |

> **MsgRead() is stateless and non-destructive.** You can read the same data multiple times, in any order, as long as the client is REPLY BLOCKED.

---

## 10. The "v" Variants: Complete API Family

Every QNX message passing function has a vector variant:

| Single-Buffer API | Vector API      | Purpose                                                      |
| ----------------- | --------------- | ------------------------------------------------------------ |
| `MsgSend()`       | `MsgSendv()`    | Send message (gather) / receive reply (scatter)              |
| `MsgSendsv()`     | `MsgSendvs()`   | Single send buffer + vector reply, or vector send + single reply |
| `MsgReceive()`    | `MsgReceivev()` | Receive message (scatter into multiple buffers)              |
| `MsgReply()`      | `MsgReplyv()`   | Reply with vector of buffers (gather)                        |
| `MsgRead()`       | `MsgReadv()`    | Read from client into vector of buffers (scatter)            |
| `MsgWrite()`      | `MsgWritev()`   | Write from vector of buffers to client (gather)              |

### When to Use Which

| Pattern                                       | API to Use                                         |
| --------------------------------------------- | -------------------------------------------------- |
| Single buffer send, single buffer reply       | `MsgSend()`                                        |
| Multiple buffers send, single buffer reply    | `MsgSendsv()`                                      |
| Single buffer send, multiple buffers reply    | `MsgSendvs()`                                      |
| Multiple buffers send, multiple buffers reply | `MsgSendv()`                                       |
| Variable-length data, header first            | `MsgReceive()` for header, `MsgReadv()` for data   |
| Large reply, build in pieces                  | `MsgWrite()` chunks, then `MsgReply()` with status |

---

## 11. Key Principles

| Principle                                  | Explanation                                                  |
| ------------------------------------------ | ------------------------------------------------------------ |
| **IOVs avoid extra copies**                | Describe scattered buffers; kernel gathers/scatters during copy |
| **SETIOV just sets pointers**              | No data is copied until `MsgSendv()` / `MsgReadv()` is called |
| **Receive header first, then read data**   | For variable-length protocols, receive fixed header, then `MsgReadv()` the rest |
| **MsgRead() is stateless**                 | Can read data multiple times, in any order, while client is REPLY BLOCKED |
| **MsgRead() past end returns 0**           | Not an error — simply means no data at that offset           |
| **MsgWrite() is for streaming replies**    | Push data to client reply buffer in chunks, then `MsgReply()` with status |
| **Single kernel copy**                     | Data goes directly from client space to server space — no kernel buffering |
| **All Msg*() functions have "v" variants** | Consistent API pattern across the entire message passing family |

---

## 12. Complete Quick Reference Table

### By Category

| Category          | Function                   | Purpose                                        |
| ----------------- | -------------------------- | ---------------------------------------------- |
| **Channel**       | `ChannelCreate()`          | Create channel for receiving                   |
|                   | `ChannelCreatePulsePool()` | Create channel with pre-allocated pulse queue  |
|                   | `ChannelDestroy()`         | Destroy channel                                |
| **Connection**    | `ConnectAttach()`          | Create connection to server                    |
|                   | `ConnectDetach()`          | Detach connection (required for scoid cleanup) |
|                   | `ConnectClientInfo()`      | Get extended client credentials                |
| **Send (Single)** | `MsgSend()`                | Send message, block for reply                  |
|                   | `MsgSendnc()`              | Non-cancelable send                            |
| **Send (Vector)** | `MsgSendv()`               | Multi-part send, multi-part reply              |
|                   | `MsgSendsv()`              | Single send, vector reply                      |
|                   | `MsgSendvs()`              | Vector send, single reply                      |
| **Receive**       | `MsgReceive()`             | Receive message or pulse                       |
|                   | `MsgReceivev()`            | Receive into multiple buffers                  |
|                   | `MsgReceivePulse()`        | Receive only pulses                            |
| **Reply**         | `MsgReply()`               | Reply with single buffer                       |
|                   | `MsgReplyv()`              | Reply with multiple buffers                    |
| **Read Client**   | `MsgRead()`                | Read more data from client                     |
|                   | `MsgReadv()`               | Read into multiple buffers                     |
| **Write Client**  | `MsgWrite()`               | Write to client reply buffer                   |
|                   | `MsgWritev()`              | Write from multiple buffers                    |
| **Pulse**         | `MsgSendPulse()`           | Send non-blocking pulse                        |
|                   | `MsgSendPulsePtr()`        | Send pulse with pointer                        |
|                   | `MsgDeliverEvent()`        | Deliver async event                            |
| **Info**          | `MsgInfo()`                | Get message info (rarely used)                 |
| **Name**          | `name_attach()`            | Register name, create channel                  |
|                   | `name_detach()`            | Unregister name                                |
|                   | `name_open()`              | Look up name, create connection                |
|                   | `name_close()`             | Close name-based connection                    |
| **Error**         | `MsgError()`               | Send error reply                               |

### By Data Direction

| Direction                       | Functions                                                    |
| ------------------------------- | ------------------------------------------------------------ |
| **Client → Server**             | `MsgSend()`, `MsgSendv()`, `MsgSendsv()`, `MsgSendvs()`, `MsgSendnc()` |
| **Server → Client**             | `MsgReply()`, `MsgReplyv()`, `MsgWrite()`, `MsgWritev()`     |
| **Client → Server (on demand)** | `MsgRead()`, `MsgReadv()`                                    |
| **Notification (no reply)**     | `MsgSendPulse()`, `MsgSendPulsePtr()`, `MsgDeliverEvent()`   |
| **Error reply**                 | `MsgError()`                                                 |

### By Blocking Behavior

| Blocking         | Functions                                                    |
| ---------------- | ------------------------------------------------------------ |
| **Blocking**     | `MsgSend()`, `MsgSendv()`, `MsgSendsv()`, `MsgSendvs()`, `MsgReceive()`, `MsgReceivev()` |
| **Non-blocking** | `MsgSendPulse()`, `MsgSendPulsePtr()`, `MsgReply()`, `MsgReplyv()`, `MsgError()`, `MsgRead()`, `MsgReadv()`, `MsgWrite()`, `MsgWritev()` |

## 13. Common Pitfalls

| Pitfall                                          | Problem                                        | Solution                                                |
| ------------------------------------------------ | ---------------------------------------------- | ------------------------------------------------------- |
| **Not checking IOV array size**                  | Buffer overflow if more parts than array slots | Ensure array is large enough for `sparts`/`rparts`      |
| **Forgetting total length check**                | Kernel copies min(send_total, receive_total)   | Verify `info.msglen` vs `info.srcmsglen` for truncation |
| **Assuming MsgRead() reads all requested bytes** | May return less if near end of client buffer   | Check return value, loop if needed                      |
| **Treating MsgRead() return of 0 as error**      | 0 means "no data at offset" — normal           | Only `-1` indicates error                               |
| **Reading after MsgReply()**                     | Client is unblocked, data no longer accessible | Read all needed data before calling `MsgReply()`        |
| **Using MsgWrite() when MsgReply() suffices**    | Unnecessary complexity for most cases          | Use `MsgReply()`/`MsgReplyv()` for typical replies      |
| **Not using IOVs for header+data protocols**     | Wastes memory with fixed-size buffers          | Use 2-part IOV: header + variable data                  |

---

*This module covers multi-part messaging fundamentals. Advanced topics such as combine messages, message offset handling, and zero-copy techniques are covered in subsequent modules.*
