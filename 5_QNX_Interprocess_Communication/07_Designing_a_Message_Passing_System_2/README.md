# QNX Multi-Part Messages — When and How to Use

## Overview

This section covers the practical use cases for multi-part messaging in QNX. Building on the API reference, we explore **when** to use IOVs, `MsgSendv()`, `MsgRead()`, and related functions, with concrete examples for each scenario.

---

## 1. Use Case: Variable-Length Data

### The Problem

Many protocols have a fixed header followed by data of unknown size at compile time:

| Message           | Header Size | Data Size         |
| ----------------- | ----------- | ----------------- |
| `write()`         | 12 bytes    | 1 byte to 200 MB  |
| Network packet    | 20 bytes    | 0 to 65,535 bytes |
| File read request | 8 bytes     | Variable response |

A fixed-size structure is wasteful or limiting:

```c
// BAD: Wastes memory if data is small, fails if data exceeds 256 bytes
struct msg_write {
    uint16_t type;
    uint32_t nbytes;
    char data[256];   // Fixed — what if we need 2MB?
};
```

### The Solution: Header + IOV Data

```c
// GOOD: Separate header from variable-length data
struct msg_header {
    uint16_t type;      // _IO_WRITE
    uint32_t nbytes;    // How much data follows
};

// Client side: 2-part IOV
struct msg_header header = { .type = MSG_TYPE_WRITE, .nbytes = data_size };
struct iovec siov[2];

SETIOV(&siov[0], &header, sizeof(header));   // Part 1: fixed header
SETIOV(&siov[1], data_buffer, data_size);     // Part 2: variable data

MsgSendv(coid, siov, 2, riov, rparts);
```

```
Client Memory Layout (scattered)
┌─────────────┐
│  Header     │  12 bytes  ──┐
│  (fixed)    │              │  IOV[0]  ┐
└─────────────┘              │           │
                             │           │  MsgSendv() gathers
┌─────────────┐              │           │  into single logical
│  Data       │  variable  ──┘  IOV[1]  ─┘  message during copy
│  (variable) │
└─────────────┘
```

### Server Handling

```c
// Server: receive header first, then read variable data
struct msg_header header;
struct _msg_info info;

// Step 1: Receive only the header
int rcvid = MsgReceive(chid, &header, sizeof(header), &info);

// Step 2: Check how much data the client sent
printf("Client wants to write %u bytes
", header.nbytes);

// Step 3: Allocate or find buffers for the data
char *data = malloc(header.nbytes);

// Step 4: Read the data from client (skip the header)
int bytes_read = MsgRead(rcvid, data, header.nbytes, sizeof(header));

// Step 5: Process data...
write_to_hardware(data, bytes_read);

// Step 6: Reply
MsgReply(rcvid, bytes_read, NULL, 0);

free(data);
```

---

## 2. Use Case: Large Data Transfer

### The Problem

Transferring multi-megabyte buffers efficiently:

| Scenario         | Data Size       | Issue                                   |
| ---------------- | --------------- | --------------------------------------- |
| Image processing | 16 MB per frame | Single large buffer copy is expensive   |
| Audio streaming  | 4 MB buffers    | Want to avoid extra allocations         |
| File I/O         | 100 MB+ reads   | Memory pressure from contiguous buffers |

### The Solution: Direct Multi-Part Send

```c
// Client has data in multiple large buffers
#define FRAME_SIZE_1 (4 * 1024 * 1024)   // 4 MB
#define FRAME_SIZE_2 (8 * 1024 * 1024)   // 8 MB
#define FRAME_SIZE_3 (4 * 1024 * 1024)   // 4 MB

char *frame_y  = malloc(FRAME_SIZE_1);   // Y channel
char *frame_uv = malloc(FRAME_SIZE_2);   // UV channels
char *frame_meta = malloc(FRAME_SIZE_3); // Metadata

// Fill buffers with frame data...

// Send all three parts as one message — NO local copy needed!
struct iovec siov[3];
SETIOV(&siov[0], frame_y, FRAME_SIZE_1);
SETIOV(&siov[1], frame_uv, FRAME_SIZE_2);
SETIOV(&siov[2], frame_meta, FRAME_SIZE_3);

struct iovec riov[1];
char reply[64];
SETIOV(&riov[0], reply, sizeof(reply));

int status = MsgSendv(coid, siov, 3, riov, 1);
// Kernel gathers 16MB directly from three buffers during single copy
```

### Server Handling with Scatter

```c
// Server receives into pre-allocated cache buffers
struct iovec riov[4];
char header[64];
char cache0[4 * 1024 * 1024];
char cache1[4 * 1024 * 1024];
char cache2[4 * 1024 * 1024];

SETIOV(&riov[0], header, sizeof(header));
SETIOV(&riov[1], cache0, sizeof(cache0));
SETIOV(&riov[2], cache1, sizeof(cache1));
SETIOV(&riov[3], cache2, sizeof(cache2));

int rcvid = MsgReceivev(chid, riov, 4, &info);
// Kernel scatters incoming data across all four buffers
```

---

## 3. Use Case: Scatter/Gather — Data in Multiple Locations

### The Problem

Data is naturally scattered across different memory regions:

| Source          | Location        | Size     |
| --------------- | --------------- | -------- |
| Protocol header | Stack variable  | 12 bytes |
| User payload    | Heap buffer     | 8 KB     |
| CRC/checksum    | Computed buffer | 4 bytes  |
| Trailer/footer  | Static buffer   | 8 bytes  |

### The Solution: Gather from Multiple Sources

```c
// Data scattered across different memory areas
struct protocol_header hdr = {
    .type = MSG_TYPE_DATA,
    .seq_num = next_seq++,
    .timestamp = time(NULL)
};

char *payload = get_user_payload();     // From heap
uint32_t crc = compute_crc(payload, payload_len);
static char trailer[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };

// Gather all parts into one message — NO memcpy needed!
struct iovec siov[4];
SETIOV(&siov[0], &hdr, sizeof(hdr));           // Stack
SETIOV(&siov[1], payload, payload_len);          // Heap
SETIOV(&siov[2], &crc, sizeof(crc));           // Stack
SETIOV(&siov[3], trailer, sizeof(trailer));    // Static

MsgSendv(coid, siov, 4, riov, rparts);
```

```
Memory Layout (Gather Operation)
┌─────────────────┐
│  Stack: Header  │ ──┐
│  (12 bytes)     │   │
└─────────────────┘   │
                      │  IOV[0]
┌─────────────────┐   │
│  Heap: Payload  │ ──┼──┐
│  (8 KB)         │   │  │
└─────────────────┘   │  │
                      │  │  MsgSendv() gathers
┌─────────────────┐   │  │  all parts during
│  Stack: CRC     │ ──┘  │  kernel copy
│  (4 bytes)      │      │
└─────────────────┘      │
                         │
┌─────────────────┐      │
│  Static: Trailer│ ─────┘
│  (8 bytes)      │
└─────────────────┘
```

---

## 4. Use Case: Protocol Headers Without Copying

### The Problem

Standard I/O operations need to prepend a header to user data:

```c
// BAD: Must allocate and copy to prepend header
char *combined = malloc(header_size + data_size);
memcpy(combined, &header, header_size);
memcpy(combined + header_size, user_data, data_size);
MsgSend(coid, combined, header_size + data_size, reply, reply_size);
free(combined);
```

### The Solution: IOV Header + Data

```c
// GOOD: Zero-copy header prepending
struct iovec siov[2];

// Header on stack
struct _io_write header = {
    .type = _IO_WRITE,
    .nbytes = user_data_size,
    .xtype = _IO_XTYPE_NONE
};

// User data — already in user's buffer, no copy needed
SETIOV(&siov[0], &header, sizeof(header));
SETIOV(&siov[1], user_data, user_data_size);

// Single kernel copy: header + data gathered directly from source locations
MsgSendv(coid, siov, 2, NULL, 0);
```

---

## 5. Use Case: Streaming Data from Slow Devices

### The Problem

Server processes data in chunks from a slow source (serial port, sensor, network):

| Device      | Transfer Rate   | Chunk Size        |
| ----------- | --------------- | ----------------- |
| Serial port | 115200 baud     | ~11 KB/s          |
| Sensor      | 100 samples/sec | Small batches     |
| Network     | Variable        | MTU-sized packets |

### The Solution: Read in Chunks, Reuse Buffers

```c
// Client sent: header (12B) + 100KB of data

void handle_slow_device(int rcvid, struct msg_header *header,
                        struct _msg_info *info) {
    int total_bytes = header->nbytes;
    int offset = sizeof(*header);  // Skip header
    int chunk_size = 4096;
    char chunk[4096];
    int bytes_processed = 0;

    printf("Processing %d bytes from slow device...
", total_bytes);

    while (bytes_processed < total_bytes) {
        // Read one chunk from client
        int to_read = (total_bytes - bytes_processed < chunk_size)
                      ? (total_bytes - bytes_processed)
                      : chunk_size;

        int n = MsgRead(rcvid, chunk, to_read, offset);
        if (n <= 0) break;

        // Process this chunk (e.g., write to serial port)
        write_to_serial_port(chunk, n);

        // Wait for hardware to finish before reading next chunk
        wait_for_serial_ready();

        bytes_processed += n;
        offset += n;
    }

    printf("Processed %d bytes
", bytes_processed);
    MsgReply(rcvid, bytes_processed, NULL, 0);
}
```

**Benefits:**

- **No large server buffer needed** — data stays in client memory
- **Flow-controlled** — server reads at hardware pace
- **Memory efficient** — reuse same 4KB buffer for all chunks

---

## 6. Use Case: File System Cache Buffers

### The Problem

File systems receive write requests and need to store data in cache buffers:

```c
// Client sends: header (12B) + 12KB of file data
// Server has 4KB cache pages scattered in memory
```

### The Solution: Scatter into Cache Pages

```c
#define PAGE_SIZE (4 * 1024)
#define NUM_PAGES 16

char cache_pages[NUM_PAGES][PAGE_SIZE];
int page_used[NUM_PAGES] = {0};

void handle_file_write(int rcvid, struct _io_write *header) {
    int data_size = header->nbytes;
    int pages_needed = (data_size + PAGE_SIZE - 1) / PAGE_SIZE;
    struct iovec riov[pages_needed];
    int page_indices[pages_needed];
    int found = 0;

    // Find available cache pages
    for (int i = 0; i < NUM_PAGES && found < pages_needed; i++) {
        if (!page_used[i]) {
            page_indices[found] = i;
            SETIOV(&riov[found], cache_pages[i], PAGE_SIZE);
            page_used[i] = 1;
            found++;
        }
    }

    if (found < pages_needed) {
        MsgError(rcvid, ENOSPC);
        return;
    }

    // Scatter data into cache pages, skipping header
    int bytes_read = MsgReadv(rcvid, riov, found, sizeof(*header));
    printf("Scattered %d bytes into %d cache pages
", bytes_read, found);

    // Mark last page with actual used size
    int last_page_used = data_size % PAGE_SIZE;
    if (last_page_used == 0) last_page_used = PAGE_SIZE;

    // Later: flush dirty pages to disk...

    MsgReply(rcvid, bytes_read, NULL, 0);
}
```

---

## 7. Complete Decision Flowchart

```
┌─────────────────────────────────────────┐
│  Do you need to send/receive data?      │
└─────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────┐
│  Is the data in a single contiguous     │
│  buffer?                                │
└─────────────────────────────────────────┘
        │                    │
       YES                  NO
        │                    │
        ▼                    ▼
┌───────────────┐    ┌─────────────────────────┐
│ Use MsgSend() │    │ Is it header + variable │
│ or MsgReceive()│   │ data?                   │
└───────────────┘    └─────────────────────────┘
                            │           │
                           YES          NO
                            │           │
                            ▼           ▼
                    ┌─────────────┐  ┌─────────────────────┐
                    │ Use 2-part  │  │ Is it data scattered│
                    │ IOV: header │  │ across memory?      │
                    │ + data      │  └─────────────────────┘
                    └─────────────┘         │        │
                                           YES       NO
                                            │        │
                                            ▼        ▼
                                    ┌─────────────┐ ┌──────────────┐
                                    │ Use multi-  │ │ Use multi-   │
                                    │ part IOV to │ │ part MsgSendv│
                                    │ gather from │ │ with large   │
                                    │ all sources │ │ buffers      │
                                    └─────────────┘ └──────────────┘
```

---

## 8. Summary: When to Use Each Pattern

| Pattern                        | Use When                                        | API                                     |
| ------------------------------ | ----------------------------------------------- | --------------------------------------- |
| **Single buffer**              | Small, fixed-size messages                      | `MsgSend()`, `MsgReceive()`             |
| **Header + variable data**     | Protocol with unknown data size at compile time | `MsgSendv()` (2-part IOV) + `MsgRead()` |
| **Multiple scattered buffers** | Data naturally in different memory regions      | `MsgSendv()` (multi-part IOV)           |
| **Large data transfer**        | Multi-MB buffers, avoid extra allocations       | `MsgSendv()` or `MsgReceivev()`         |
| **Slow device streaming**      | Process data at hardware pace, reuse buffers    | `MsgRead()` in loop                     |
| **File system cache**          | Scatter data into non-contiguous cache pages    | `MsgReadv()`                            |
| **Reply with multiple parts**  | Response assembled from different sources       | `MsgReplyv()`                           |

---

## 9. Key Principles

| Principle                                            | Explanation                                                  |
| ---------------------------------------------------- | ------------------------------------------------------------ |
| **IOVs eliminate assembly copies**                   | Point to existing buffers instead of `malloc()` + `memcpy()` |
| **Receive header first, read data on demand**        | For variable-length protocols, always receive fixed header, then `MsgRead()` |
| **MsgRead() is non-destructive**                     | Can read same data multiple times, in any order, while client is REPLY BLOCKED |
| **MsgRead() past end returns 0**                     | Not an error — simply no data at that offset                 |
| **Reuse buffers for slow devices**                   | Read chunks sequentially, process at hardware pace, minimize memory |
| **Scatter/gather is zero-copy**                      | Kernel copies directly from source to destination — no intermediate buffers |
| **Always check `info->msglen` vs `info->srcmsglen`** | Detect if message was truncated on first receive             |

---

## 10. Common Pitfalls

| Pitfall                                       | Problem                                      | Solution                                        |
| --------------------------------------------- | -------------------------------------------- | ----------------------------------------------- |
| **Allocating large contiguous buffers**       | Memory fragmentation, allocation failures    | Use IOVs with existing buffers                  |
| **Not checking MsgRead() return**             | May read less than expected near end of data | Loop until all data processed or return is 0    |
| **Using fixed-size struct for variable data** | Wastes memory or limits functionality        | Separate header from data, use IOVs             |
| **Reading after MsgReply()**                  | Client is unblocked, data inaccessible       | Read all needed data before replying            |
| **Forgetting header size in offset**          | MsgRead() starts at wrong position           | Offset = sizeof(header)                         |
| **Not using union with struct _pulse**        | Pulse buffer too small, data lost            | Always include `struct _pulse` in receive union |

---

*This module covers practical usage patterns for multi-part messaging. For API details, see the Complete API Reference.*
