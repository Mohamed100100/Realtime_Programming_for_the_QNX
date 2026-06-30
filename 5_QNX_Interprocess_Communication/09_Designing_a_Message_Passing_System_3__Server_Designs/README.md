# QNX Message Passing System — Part 3: Server Designs 

> **Companion to:** QNX Multi-Part Messages (IOVs, MsgSendv, MsgRead, MsgWrite)

This document covers the third part of designing a message passing system in QNX Neutrino, focusing on **server architecture patterns** and **deadlock avoidance strategies**.

---

## Table of Contents

1. [Server Designs](#1-server-designs)
   - [Single-Threaded Server](#single-threaded-server)
   - [Multi-Threaded Server](#multi-threaded-server)
   - [Thread Pool Architecture](#thread-pool-architecture)
   - [Delayed Reply Pattern](#delayed-reply-pattern)
2. [Deadlock Avoidance](#2-deadlock-avoidance)
   - [The Deadlock Problem](#the-deadlock-problem)
   - [Send Hierarchy](#send-hierarchy)
   - [Pulses for Upward Notification](#pulses-for-upward-notification)
   - [Deferred Reply Pattern](#deferred-reply-pattern)
   - [Practical Rules](#practical-rules)
3. [Priority Inheritance in Servers](#3-priority-inheritance-in-servers)
4. [Complete Examples](#4-complete-examples)
5. [Key Takeaways](#5-key-takeaways)

---

## 1. Server Designs

### Single-Threaded Server

A typical server, if it can keep up, can handle multiple clients while remaining single-threaded. The server receives a message from the first client, handles it, replies, then moves to the next client.

```
┌─────────────────────────────────────────┐
│         Single-Threaded Server          │
│                                         │
│  Client 1 ──MsgSend──▶ MsgReceive()     │
│              ▲          (process)       │
│              └───────── MsgReply()      │
│                                         │
│  Client 2 ──MsgSend──▶ MsgReceive()     │
│              ▲          (process)       │
│              └───────── MsgReply()      │
│                                         │
│  Client 3 ──MsgSend──▶ MsgReceive()     │
│              ▲          (process)       │
│              └───────── MsgReply()      │
└─────────────────────────────────────────┘
```

**Advantages:**

- Easier to design and write
- No internal data protection needed (no mutexes)
- No thread synchronization complexity
- Deterministic execution order

**When it works:**

- All client requests are short and uniform
- Server can return to `MsgReceive()` fast enough
- No client has a hard real-time deadline shorter than the longest request

```c
// Single-threaded server example
#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>

#define MSG_SIZE 256

int main(void) {
    int chid;
    int rcvid;
    char msg[MSG_SIZE];
    struct _msg_info info;

    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        exit(EXIT_FAILURE);
    }

    printf("Single-threaded server started, channel=%d\n", chid);

    for (;;) {
        // Block waiting for any client message
        rcvid = MsgReceive(chid, msg, sizeof(msg), &info);
        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            // Handle pulse (e.g., _PULSE_CODE_UNBLOCK)
            printf("Received pulse code=%d value=%d\n",
                   msg[0], msg[1]);
            continue;
        }

        printf("Received message from pid=%d, tid=%d, nbytes=%d\n",
               info.pid, info.tid, info.nbytes);

        // Process the message...
        // (In a real server, dispatch based on msg type)

        // Reply to unblock the client
        MsgReply(rcvid, EOK, "ack", 4);
    }

    return 0;
}
```

---

### Multi-Threaded Server

When some requests take longer than others, or when clients have tight real-time deadlines, a single-threaded server becomes a bottleneck.

**The Problem:**

```
Timeline:
────┬──────────────────────────────────────────────────────▶
    │
    │ Client 1 (low priority) sends long request
    │ ┌────────────────────────────────────┐
    │ │ Server processing Client 1...      │
    │ └────────────────────────────────────┘
    │
    │    Client 3 (high priority, short deadline) sends request
    │    ╳ Server CANNOT receive — still busy with Client 1
    │
    │         Client 3 MISSES DEADLINE!
```

Even with priority inheritance, if the actual processing time is too long, higher-priority clients miss their deadlines.

**Solution: Thread Pool**

```
┌─────────────────────────────────────────────────────────┐
│              Multi-Threaded Server                      │
│                                                         │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐     │
│  │ Thread 1│  │ Thread 2│  │ Thread 3│  │ Thread N│     │
│  │(blocked)│  │(blocked)│  │(blocked)│  │(blocked)│     │
│  │MsgRecv()│  │MsgRecv()│  │MsgRecv()│  │MsgRecv()│     │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘     │
│       └────────────┴────────────┴────────────┘          │
│                        │                                │
│                   Same Channel                          │
│                        │                                │
│  Client 1 ──MsgSend───▶│ (Thread 3 picked, runs at      │
│                        │  Client 1's priority)          │
│  Client 3 ──MsgSend───▶│ (Thread 1 picked, runs at      │
│                        │  Client 3's higher priority)   │
│                        │  → May preempt Thread 3        │
│  Client 5 ──MsgSend───▶│ (Thread 2 picked, may run      │
│                        │  on different core in parallel)│
└─────────────────────────────────────────────────────────┘
```

**Architecture:**

- Pool of interchangeable worker threads
- All threads blocked on `MsgReceive()` on the **same channel**
- Kernel picks any available thread when a message arrives
- Priority inheritance is automatic — thread runs at client's priority

```c
// Multi-threaded server with thread pool
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/neutrino.h>

#define NUM_WORKER_THREADS 4
#define MSG_SIZE 256

static int g_chid;  // Shared channel ID

void *worker_thread(void *arg) {
    int thread_id = *(int *)arg;
    int rcvid;
    char msg[MSG_SIZE];
    struct _msg_info info;

    printf("Worker thread %d started, waiting on channel %d\n",
           thread_id, g_chid);

    for (;;) {
        // All threads block here on the SAME channel
        rcvid = MsgReceive(g_chid, msg, sizeof(msg), &info);
        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            // Pulse handling
            printf("Thread %d: pulse code=%d\n", thread_id, msg[0]);
            continue;
        }

        printf("Thread %d: handling client pid=%d priority=%d\n",
               thread_id, info.pid, info.priority);

        // Simulate work (in real code, dispatch by message type)
        // Thread automatically runs at client's inherited priority

        MsgReply(rcvid, EOK, "done", 5);
    }

    return NULL;
}

int main(void) {
    pthread_t threads[NUM_WORKER_THREADS];
    int thread_ids[NUM_WORKER_THREADS];

    g_chid = ChannelCreate(0);
    if (g_chid == -1) {
        perror("ChannelCreate");
        exit(EXIT_FAILURE);
    }

    printf("Multi-threaded server starting with %d workers\n",
           NUM_WORKER_THREADS);

    // Create worker threads
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        thread_ids[i] = i + 1;
        if (pthread_create(&threads[i], NULL,
                           worker_thread, &thread_ids[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    // Main thread can also participate or just wait
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
```

**Why multiple threads on the same channel?**

| Approach                                  | Pros                                                         | Cons                                                         |
| ----------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| **Thread pool on same channel**           | Automatic priority inheritance; no internal dispatch overhead; kernel picks thread | Requires thread-safe shared state                            |
| Single receive thread + internal dispatch | More control over dispatching                                | Extra thread-to-thread overhead; must manually propagate priority |

**Benefits of the thread pool approach:**

- **Reduced latency:** High-priority clients get served immediately by available threads
- **Improved throughput:** Parallel execution on multi-core systems
- **Automatic priority inheritance:** No manual priority passing needed
- **Simpler design:** Threads are interchangeable workers

---

### Delayed Reply Pattern

A server does **not** need to reply immediately. It can store the client's receive ID and reply later when data becomes available.

**Use Cases:**

- Queue servers (wait until data is available)
- Hardware drivers (wait for interrupt/data)
- Input-driven systems (keyboard, serial, etc.)

```
┌─────────────────────────────────────────────────────────┐
│                  Delayed Reply Pattern                  │
│                                                         │
│  Step 1: Client requests data from queue 3              │
│  ┌─────────┐          ┌────────────────┐                │
│  │ Client  │─MsgSend─▶│   Server       │                │
│  │         │◀─────────│ "No data yet"  │                │
│  │ REPLY   │  (blocks)│ Store rcvid    │                │
│  │ BLOCKED │          │ Go to wait list│                │
│  └─────────┘          └────────────────┘                │
│                                                         │
│  Step 2: Another client adds data to queue 3            │
│  ┌─────────┐          ┌─────────────┐                   │
│  │ Client 2│─MsgSend─▶│   Server    │                   │
│  │         │◀─Reply── │ "Added OK"  │                   │
│  └─────────┘          │ Server checks wait list         │
│                       │ → Finds Client 1 waiting        │
│                       └─────────────┘                   │
│                                                         │
│  Step 3: Server does delayed reply to Client 1          │
│  ┌─────────┐          ┌─────────────┐                   │
│  │ Client 1│◀─MsgReply│"Here's data"│                   │
│  │ unblocks│          │ (using stored rcvid)            │
│  └─────────┘          └─────────────┘                   │
└─────────────────────────────────────────────────────────┘
```

**Real-world example: /dev/pty (pseudo-terminal driver)**

```
┌─────────┐                    ┌─────────────┐
│  Shell  │──MsgSend("give me  │  devc-pty   │
│         │   typed input")──▶ │   Server    │
│         │                    │             │
│ REPLY   │◀────────────────── │"No input yet"│
│ BLOCKED │  (server stores    │ Store rcvid │
│         │   rcvid, goes to   │ MsgReceive()│
│         │   MsgReceive())    │ (RECEIVE    │
│         │                    │  BLOCKED)   │
└─────────┘                    └─────────────┘

         ... time passes, user types something ...

┌─────────┐                    ┌─────────────┐
│  Shell  │◀──MsgReply(rcvid,  │  devc-pty   │
│ unblocks│    "typed chars")  │ (hardware   │
│         │                    │  interrupt  │
│         │                    │  triggers reply)│
└─────────┘                    └─────────────┘
```

```c
// Simplified delayed reply server (queue example)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>

#define MAX_WAITING_CLIENTS 16
#define NUM_QUEUES 4
#define QUEUE_SIZE 64

// Message types
#define MSG_GET_DATA    1
#define MSG_PUT_DATA    2

struct request_msg {
    int type;       // MSG_GET_DATA or MSG_PUT_DATA
    int queue_id;   // Which queue (0-3)
    int nbytes;     // For PUT: how many bytes
    char data[];    // Variable-length data for PUT
};

// Waiting client entry
struct waiting_client {
    int rcvid;
    int queue_id;
    int valid;      // 1 if entry is in use
};

// Queue data structure
struct queue {
    char buffer[QUEUE_SIZE];
    int head;
    int tail;
    int count;
};

static struct waiting_client waiters[MAX_WAITING_CLIENTS];
static struct queues[NUM_QUEUES];

void init_queues(void) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        queues[i].head = 0;
        queues[i].tail = 0;
        queues[i].count = 0;
    }
    memset(waiters, 0, sizeof(waiters));
}

int queue_put(int qid, const char *data, int len) {
    if (qid < 0 || qid >= NUM_QUEUES) return -1;
    struct queue *q = &queues[qid];
    if (q->count + len > QUEUE_SIZE) return -1; // Full

    for (int i = 0; i < len; i++) {
        q->buffer[q->tail] = data[i];
        q->tail = (q->tail + 1) % QUEUE_SIZE;
        q->count++;
    }
    return len;
}

int queue_get(int qid, char *data, int max_len) {
    if (qid < 0 || qid >= NUM_QUEUES) return -1;
    struct queue *q = &queues[qid];
    if (q->count == 0) return 0; // Empty

    int len = (max_len < q->count) ? max_len : q->count;
    for (int i = 0; i < len; i++) {
        data[i] = q->buffer[q->head];
        q->head = (q->head + 1) % QUEUE_SIZE;
        q->count--;
    }
    return len;
}

// Check if any waiting client can be satisfied
void check_waiters(void) {
    for (int i = 0; i < MAX_WAITING_CLIENTS; i++) {
        if (!waiters[i].valid) continue;

        int qid = waiters[i].queue_id;
        if (queues[qid].count > 0) {
            char data[QUEUE_SIZE];
            int len = queue_get(qid, data, sizeof(data));

            // Delayed reply!
            MsgReply(waiters[i].rcvid, len, data, len);
            waiters[i].valid = 0;
            printf("Delayed reply to rcvid=%d with %d bytes from queue %d\n",
                   waiters[i].rcvid, len, qid);
        }
    }
}

int add_waiter(int rcvid, int queue_id) {
    for (int i = 0; i < MAX_WAITING_CLIENTS; i++) {
        if (!waiters[i].valid) {
            waiters[i].rcvid = rcvid;
            waiters[i].queue_id = queue_id;
            waiters[i].valid = 1;
            return 0;
        }
    }
    return -1; // No slots
}

int main(void) {
    int chid;
    int rcvid;
    struct request_msg msg;
    struct _msg_info info;

    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        exit(EXIT_FAILURE);
    }

    init_queues();
    printf("Delayed-reply server started, channel=%d\n", chid);

    for (;;) {
        rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);
        if (rcvid == -1) {
            perror("MsgReceive");
            continue;
        }

        if (rcvid == 0) {
            // Pulse
            continue;
        }

        // Read full message including variable data if needed
        int header_size = sizeof(struct request_msg);
        int data_bytes = info.nbytes - header_size;

        switch (msg.type) {
            case MSG_GET_DATA: {
                char data[QUEUE_SIZE];
                int len = queue_get(msg.queue_id, data, sizeof(data));

                if (len > 0) {
                    // Data available — reply immediately
                    MsgReply(rcvid, len, data, len);
                } else {
                    // No data — store rcvid for delayed reply
                    if (add_waiter(rcvid, msg.queue_id) == -1) {
                        MsgError(rcvid, EAGAIN);
                    }
                    // Do NOT reply — client stays REPLY BLOCKED
                    printf("Client rcvid=%d waiting on queue %d\n",
                           rcvid, msg.queue_id);
                }
                break;
            }

            case MSG_PUT_DATA: {
                // Read variable-length data from client
                char data[QUEUE_SIZE];
                int to_read = (data_bytes < QUEUE_SIZE) ? data_bytes : QUEUE_SIZE;
                int n = MsgRead(rcvid, data, to_read, header_size);

                int put_len = queue_put(msg.queue_id, data, n);
                MsgReply(rcvid, put_len, NULL, 0);

                // Check if any waiting client can now be satisfied
                check_waiters();
                break;
            }

            default:
                MsgError(rcvid, ENOSYS);
                break;
        }
    }

    return 0;
}
```

**Key points about delayed reply:**

- The client remains **REPLY BLOCKED** until the server replies
- The server stores the `rcvid` and can reply at any later time
- Multiple clients can be waiting simultaneously
- The server can still receive new messages while clients are waiting
- This is the foundation of input-driven architectures in QNX
