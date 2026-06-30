# QNX Message Passing System — Part 2: Deadlock Avoidance

> **Companion to:** QNX Server Designs & Delayed Reply Patterns (Part 3)

This document covers the second part of designing a message passing system in QNX Neutrino, focusing exclusively on **deadlock avoidance** through proper architectural design patterns.

---

## 1. The Deadlock Problem

### What is Deadlock in Message Passing?

In QNX Neutrino, `MsgSend()` is a **blocking, synchronous** operation. When Process A sends a message to Process B, Process A blocks (enters the **SEND BLOCKED** state) and waits for Process B to:

1. Receive the message via `MsgReceive()`
2. Process the message
3. Reply via `MsgReply()`

**Deadlock occurs when two or more processes are each waiting for the other to receive/reply, creating a circular dependency that can never resolve.**

```
┌─────────────────────────────────────────────────────────┐
│              The Classic Two-Process Deadlock           │
│                                                         │
│  Process A wants to send data to Process B              │
│  Process B wants to send data to Process A              │
│                                                         │
│  ┌─────────┐                    ┌─────────┐             │
│  │Process A│──MsgSend──────────▶│Process B│             │
│  │         │  "Here's data"     │         │             │
│  │         │                    │         │             │
│  │ SEND    │◀────────────────── │ SEND    │             │
│  │ BLOCKED │  Process B tries   │ BLOCKED │             │
│  │         │  to MsgSend to A   │         │             │
│  │         │  (but A is blocked │         │             │
│  │         │   waiting for B!)  │         │             │
│  └─────────┘                    └─────────┘             │
│                                                         │
│  Result: Both processes are blocked forever             │
└─────────────────────────────────────────────────────────┘
```

### Why QNX Does Not Detect Deadlocks

QNX Neutrino **does not attempt to detect or break deadlocks automatically**. This is an intentional design decision rooted in real-time system requirements:

| Reason                       | Explanation                                                  |
| ---------------------------- | ------------------------------------------------------------ |
| **Computational Complexity** | Detecting the general case of deadlocks across N threads is approximately **O(N!)** complexity. This factorial growth makes it intractable for systems with 20, 50, or 100+ processes. |
| **Real-Time Constraints**    | Running a deadlock detection algorithm on every blocking operation would introduce unpredictable latency, violating real-time guarantees. |
| **Scalability**              | The overhead does not scale. What works for 5 processes fails catastrophically for 50. |
| **Design-Time Solution**     | Deadlock avoidance must be solved at **design time**, not runtime. The architecture itself must prevent deadlock. |

> **Key Principle:** QNX provides the primitives; the architect provides the discipline.

### The Two-Process Deadlock

The simplest deadlock case is direct mutual sending:

```c
// Process A
int coid_to_b = ConnectAttach(0, pid_b, chid_b, ...);
MsgSend(coid_to_b, &data_to_b, sizeof(data_to_b), &reply, sizeof(reply));
// A blocks here, waiting for B to receive and reply...

// Meanwhile, Process B
int coid_to_a = ConnectAttach(0, pid_a, chid_a, ...);
MsgSend(coid_to_a, &data_to_a, sizeof(data_to_a), &reply, sizeof(reply));
// B blocks here, waiting for A to receive and reply...
// BUT A IS ALREADY BLOCKED SENDING TO B!
```

**State analysis:**

| Process | State        | Waiting For                          |
| ------- | ------------ | ------------------------------------ |
| A       | SEND BLOCKED | B to `MsgReceive()` and `MsgReply()` |
| B       | SEND BLOCKED | A to `MsgReceive()` and `MsgReply()` |

**Neither can make progress. Deadlock.**

### The Multi-Process Deadlock

Deadlock becomes much harder to spot in systems with many processes. Consider this 7-process system:

```
                    ┌───┐
                    │ 1 │
                    └─┬─┘
                      │
          ┌───────────┼───────────┐
          ▼           ▼           ▼
       ┌─────┐    ┌─────┐    ┌─────┐
       │  2  │    │  3  │    │  4  │
       └──┬──┘    └──┬──┘    └──┬──┘
          │          │          │
          ▼          ▼          ▼
       ┌─────┐    ┌─────┐    ┌─────┐
       │  7  │◄───│  6  │    │  5  │
       └─────┘    └──┬──┘    └─────┘
                     │
                     ▼
                  ┌─────┐
                  │  8  │
                  └─────┘

Arrows indicate potential send directions:
1 → 2, 1 → 3, 1 → 4
2 → 7, 3 → 6, 4 → 5, 4 → 6
6 → 7, 6 → 3, 7 → 2, 3 → 7
```

**Can you spot the deadlock cycles?**

<details>
<summary>Click to reveal deadlock cycles</summary>


**Cycle 1:** 1 → 4 → 6 → 7 → 2 → 1

```
1 sends to 4 (blocks)
4 sends to 6 (blocks)
6 sends to 7 (blocks)
7 sends to 2 (blocks)
2 sends to 1 (blocks)  ← CYCLE! Everyone is blocked.
```

**Cycle 2:** 1 → 4 → 6 → 3 → 7 → 2 → 1

```
1 sends to 4 (blocks)
4 sends to 6 (blocks)
6 sends to 3 (blocks)
3 sends to 7 (blocks)
7 sends to 2 (blocks)
2 sends to 1 (blocks)  ← CYCLE! Everyone is blocked.
```

With just **7 processes**, detection requires careful graph analysis. At **50 or 100 processes**, manual detection is practically impossible.

</details>

**The core insight:** Any **cycle** in the directed send graph is a potential deadlock.

---

## 2. The Solution: Send Hierarchy

### Client-Server Directionality

The fundamental rule to prevent deadlock:

> **The client sends to the server. The server NEVER sends to the client.**

```
┌─────────────────────────────────────────────────────────┐
│              Client-Server Send Direction               │
│                                                         │
│  ┌─────────┐                    ┌─────────┐             │
│  │ Client  │──MsgSend──────────▶│ Server  │             │
│  │         │  "Here's data"     │         │             │
│  │         │                    │         │             │
│  │         │◀──MsgReply─────────│         │             │
│  │         │  "Processed"       │         │             │
│  └─────────┘                    └─────────┘             │
│                                                         │
│  ALLOWED: Client → Server (blocking send)               │
│  FORBIDDEN: Server → Client (blocking send)             │
│  ALLOWED: Server → Client (non-blocking pulse)          │
└─────────────────────────────────────────────────────────┘
```

**How it works:**

1. **Client has data for server:** Client `MsgSend()` to server → Server `MsgReceive()` → Server processes → Server `MsgReply()` → Client unblocks.

2. **Server has data for client:** Server **cannot** `MsgSend()` to client. Instead, server sends a **non-blocking pulse** to client → Client receives pulse → Client `MsgSend()` to server requesting data → Server `MsgReply()` with data → Client unblocks.

```
┌─────────────────────────────────────────────────────────┐
│          Server-to-Client Data Flow (No Deadlock)       │
│                                                         │
│  Step 1: Server notifies client (NON-BLOCKING)          │
│  ┌─────────┐                    ┌─────────┐             │
│  │ Server  │──MsgSendPulse────▶ │ Client  │             │
│  │         │  "Data ready!"     │         │             │
│  │         │  (non-blocking)    │         │             │
│  └─────────┘                    └─────────┘             │
│                                                         │
│  Step 2: Client requests data (BLOCKING, but safe)      │
│  ┌─────────┐                    ┌─────────┐             │
│  │ Client  │──MsgSend──────────▶│ Server  │             │
│  │         │  "Give me data"    │         │             │
│  │         │◀──MsgReply─────────│         │             │
│  │         │  "Here's data"     │         │             │
│  └─────────┘                    └─────────┘             │
│                                                         │
│  Result: No deadlock because pulse is non-blocking      │
└─────────────────────────────────────────────────────────┘
```

### Building the Hierarchy

A **send hierarchy** arranges all processes in a directed acyclic graph (DAG) where sends flow only in one direction (typically downward). Because a DAG has no cycles, deadlock is impossible.

```
                    ┌─────────┐
                    │ Level 4 │
                    │   P1    │  ← UI, Command & Control
                    │ (Top)   │
                    └────┬────┘
                         │
           ┌─────────────┼─────────────┐
           │             │             │
           ▼             ▼             ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │ Level 3 │  │ Level 3 │  │ Level 3 │
      │   P2    │  │   P3    │  │   P4    │  ← Middleware
      └────┬────┘  └────┬────┘  └────┬────┘
             │            │            │
        ┌────┴────┐   ┌───┴───┐   ┌────┴────┐
        ▼         ▼   ▼       ▼   ▼         ▼
     ┌─────┐  ┌─────┐ ┌─────┐ ┌─────┐  ┌─────┐
     │ P5  │  │ P6  │ │ P7  │ │ P8  │  │ P9  │
     │Level│  │Level│ │Level│ │Level│  │Level│
     │  1  │  │  1  │ │  1  │ │  1  │  │  1  │  ← Drivers, I/O
     └─────┘  └─────┘ └─────┘ └─────┘  └─────┘

     RULE: Sends only go DOWN the tree (higher level → lower level)
     RULE: Upward communication uses non-blocking pulses
     RULE: Sideways communication uses non-blocking pulses
```

**Example system hierarchy:**

```
                    ┌─────────────┐
                    │  HMI / UI   │
                    │   Server    │
                    │  (Level 4)  │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        ┌─────────┐   ┌─────────┐  ┌─────────┐
        │Navigation│  │ Vehicle │  │  Media  │
        │  Server  │  │ Control │  │  Server │
        │ (Level 3)│  │(Level 3)│  │(Level 3)│
        └────┬────┘   └────┬────┘  └────┬────┘
             │            │            │
        ┌────┴────┐   ┌───┴───┐   ┌────┴────┐
        ▼         ▼   ▼       ▼   ▼         ▼
     ┌─────┐  ┌─────┐ ┌─────┐ ┌─────┐  ┌─────┐
     │GPS  │  │CAN  │ │Audio│ │Video│  │Input│
     │Drv  │  │Drv  │ │Drv  │ │Drv  │  │Drv  │
     │(L1) │  │(L1) │ │(L1) │ │(L1) │  │(L1) │
     └─────┘  └─────┘ └─────┘ └─────┘  └─────┘
```

**Send rules for this system:**

- HMI sends to Navigation, Vehicle Control, Media (downward)
- Navigation sends to GPS Driver, CAN Driver (downward)
- Vehicle Control sends to CAN Driver, Input Driver (downward)
- Media sends to Audio Driver, Video Driver (downward)
- **GPS Driver NEVER sends to Navigation** — uses pulse instead
- **CAN Driver NEVER sends to Vehicle Control** — uses pulse instead
- **Navigation NEVER sends to Media** — uses pulse instead (sideways)

### Handling Cross-Level Communication

What happens when two processes at the **same level** need to communicate?

```
                    ┌─────────┐
                    │   P1    │
                    └────┬────┘
                         │
           ┌─────────────┼─────────────┐
           ▼             ▼             ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P2    │  │   P3    │◄─┼─── P4   │
      └────┬────┘  └────┬────┘  └────┬────┘
           │            │            │
           ▼            ▼            ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P5    │  │   P6    │  │   P7    │
      └─────────┘  └─────────┘  └─────────┘

      Problem: P3 and P4 are at the same level (Level 3)
               They need to communicate.
               Neither can send to the other (sideways = forbidden)
```

**Solution 1: Move one process up or down**

Can P4 move down? No — that breaks P1 → P4.
Can P3 move down? No — that breaks P1 → P3.
Can P4 move up? No — that breaks P4 → P7.
Can P3 move up? No — that breaks P3 → P6.

**Solution 2: Insert a new level**

```
                    ┌─────────┐
                    │   P1    │
                    └────┬────┘
                         │
           ┌─────────────┼─────────────┐
           ▼             ▼             ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P2    │  │   P3    │  │   P4    │
      └────┬────┘  └────┬────┘  └────┬────┘
             │          │            │
             ▼          ▼            ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P5    │  │   P8    │  │   P7    │  ← New Level 2
      └─────────┘  └────┬────┘  └─────────┘
                        │
                        ▼
                   ┌─────────┐
                   │   P6    │  ← Moved down
                   └─────────┘
```

Now P3 sends to P8 (downward), and P8 sends to P6 (downward). The hierarchy is preserved.

> **There is no requirement that the hierarchy must have a fixed number of levels.** Insert levels as needed.

### Adding New Levels

When a new process (P8) needs to send data "up" to an existing process (P2):

```
                    ┌─────────┐
                    │   P1    │
                    └────┬────┘
                         │
           ┌─────────────┼─────────────┐
           ▼             ▼             ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P2    │◄─┼─── P8   │  │   P4    │
      └────┬────┘  └────┬────┘  └────┬────┘
           │            │            │
           ▼            ▼            ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P5    │  │   P6    │  │   P7    │
      └─────────┘  └─────────┘  └─────────┘

      Problem: P8 needs to send UP to P2
               No rearrangement of P8 or P2 works
               P8 cannot move up (breaks P8 → P6)
               P2 cannot move down (breaks P1 → P2)
```

**Solution: Non-blocking communication upward**

```
                    ┌─────────┐
                    │   P1    │
                    └────┬────┘
                         │
           ┌─────────────┼─────────────┐
           ▼             ▼             ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P2    │◄─│   P8    │  │   P4    │
      └────┬────┘  └────┬────┘  └────┬────┘
           │            │            │
           ▼            ▼            ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P5    │  │   P6    │  │   P7    │
      └─────────┘  └─────────┘  └─────────┘

      P8 ──MsgSendPulse──▶ P2  (non-blocking, ALLOWED)
      P2 ──MsgSend───────▶ P8  (blocking, FORBIDDEN)
```

P8 sends a **pulse** to P2 (non-blocking). P2 receives the pulse, then P2 can `MsgSend()` to P8 if it needs to respond (but in this case, P2 would typically not need to send to P8). If P2 needs to send data to P8, P2 pulses P8, and P8 sends to P2 requesting the data.

---

## 3. Non-Blocking Upward Notification

### The Pulse Mechanism

A **pulse** is a tiny, non-blocking message (8 bytes: code + value) that can be sent to a channel without blocking the sender.

```c
#include <sys/neutrino.h>

int MsgSendPulse(int coid,           // Connection to target channel
                 int priority,       // Priority for pulse delivery
                 int code,           // Pulse code (application-defined)
                 int value);         // Pulse value (application-defined)
```

**Key characteristics:**

| Property     | Behavior                                      |
| ------------ | --------------------------------------------- |
| **Blocking** | Never blocks the sender                       |
| **Size**     | Fixed 8 bytes (code + value)                  |
| **Delivery** | Queued in target channel's pulse queue        |
| **Overrun**  | If queue full, oldest pulse may be dropped    |
| **Receive**  | Received via `MsgReceive()` with `rcvid == 0` |

**Pulse receive pattern:**

```c
#include <sys/neutrino.h>

int chid = ChannelCreate(0);
struct _pulse pulse;
struct _msg_info info;

for (;;) {
    int rcvid = MsgReceive(chid, &pulse, sizeof(pulse), &info);

    if (rcvid == 0) {
        // This is a pulse, not a message
        printf("Pulse received: code=%d, value=%d\n",
               pulse.code, pulse.value);

        switch (pulse.code) {
            case PULSE_CODE_DATA_READY:
                // Server has data for us — now we can send to request it
                request_data_from_server();
                break;
            case PULSE_CODE_UNBLOCK:
                // Client was killed while blocked on us
                handle_unblock();
                break;
            // ... other pulse codes
        }
    } else if (rcvid > 0) {
        // Regular message from a client
        handle_message(rcvid, &info);
    } else {
        // Error
        perror("MsgReceive");
    }
}
```

### Pulse vs Signal vs Semaphore

QNX provides several non-blocking notification mechanisms. Here's how they compare:

| Mechanism              | Blocking?  | Size    | Use Case                                | QNX Recommendation           |
| ---------------------- | ---------- | ------- | --------------------------------------- | ---------------------------- |
| **Pulse**              | No         | 8 bytes | Upward notification in hierarchy        | **Preferred**                |
| **Signal**             | No         | Varies  | Unix compatibility, simple notification | Supported but less flexible  |
| **Semaphore**          | No (post)  | N/A     | Resource counting, synchronization      | Good for resource management |
| **Condition Variable** | Yes (wait) | N/A     | Thread synchronization within process   | Not for IPC                  |

**Why pulses are preferred in QNX:**

- Integrated with the channel architecture
- Received through the same `MsgReceive()` as messages
- Priority inheritance works with pulses
- No separate signal handlers needed
- Clean separation between sync (messages) and async (pulses) communication

### Complete Pulse Example

**Scenario:** A hardware driver (lower level) needs to notify a media server (higher level) that audio data is ready.

```c
/*
 * media_server.c
 * Higher-level server that receives pulses from driver
 * and then requests data via MsgSend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/neutrino.h>

#define PULSE_CODE_AUDIO_READY  1
#define DRIVER_NAME "audio_driver"

int main(void) {
    int chid;       // Our channel to receive pulses
    int coid;       // Connection to driver
    int rcvid;
    struct _pulse pulse;
    struct _msg_info info;
    char audio_buffer[4096];

    // Create channel to receive pulses from driver
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        exit(EXIT_FAILURE);
    }

    printf("[MediaServer] Started, channel=%d\n", chid);
    printf("[MediaServer] Give this chid to the driver for pulses\n");

    // In a real system, you'd register your chid with the driver
    // via a name service or shared config

    for (;;) {
        // Wait for pulse or message
        rcvid = MsgReceive(chid, &pulse, sizeof(pulse), &info);

        if (rcvid == 0) {
            // Pulse received!
            printf("[MediaServer] Pulse: code=%d, value=%d\n",
                   pulse.code, pulse.value);

            if (pulse.code == PULSE_CODE_AUDIO_READY) {
                printf("[MediaServer] Audio ready! Requesting data...\n");

                // Now we send DOWN to the driver to get the data
                // (This is safe — we are higher level, driver is lower level)
                // In real code, you'd look up the driver's coid
                // int coid_to_driver = name_open(DRIVER_NAME, 0);
                // MsgSend(coid_to_driver, &request, sizeof(request),
                //         audio_buffer, sizeof(audio_buffer));
            }
        } else if (rcvid > 0) {
            // Handle direct messages (e.g., client requests)
            printf("[MediaServer] Message from pid=%d\n", info.pid);
            MsgReply(rcvid, EOK, NULL, 0);
        }
    }

    return 0;
}
```

```c
/*
 * audio_driver.c
 * Lower-level driver that pulses the media server when data is ready.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/neutrino.h>

#define PULSE_CODE_AUDIO_READY  1

int main(int argc, char *argv[]) {
    int coid_to_server;
    int server_chid;
    int server_pid;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_pid> <server_chid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_pid = atoi(argv[1]);
    server_chid = atoi(argv[2]);

    // Connect to the media server's channel
    coid_to_server = ConnectAttach(0, server_pid, server_chid,
                                   _NTO_SIDE_CHANNEL, 0);
    if (coid_to_server == -1) {
        perror("ConnectAttach");
        exit(EXIT_FAILURE);
    }

    printf("[AudioDriver] Connected to server pid=%d chid=%d\n",
           server_pid, server_chid);

    // Simulate hardware interrupts generating audio data
    for (int i = 0; i < 10; i++) {
        printf("[AudioDriver] Audio buffer %d ready, pulsing server...\n", i);

        // NON-BLOCKING pulse to higher-level server
        // This is safe — we are lower level, server is higher level
        int status = MsgSendPulse(coid_to_server,
                                  getprio(0),  // Our priority
                                  PULSE_CODE_AUDIO_READY,
                                  i);          // Buffer number
        if (status == -1) {
            perror("MsgSendPulse");
        }

        sleep(2);  // Simulate hardware timing
    }

    printf("[AudioDriver] Done.\n");
    ConnectDetach(coid_to_server);
    return 0;
}
```

**Execution flow:**

```
Step 1: MediaServer starts, creates channel, prints chid
        [MediaServer] Started, channel=5

Step 2: AudioDriver starts, connects to channel 5
        [AudioDriver] Connected to server pid=1234 chid=5

Step 3: AudioDriver generates data, sends pulse (NON-BLOCKING)
        [AudioDriver] Audio buffer 0 ready, pulsing server...

        ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
        │ AudioDriver │──────│  Pulse      │─────▶│ MediaServer │
        │ (Level 1)   │      │ (queued in  │      │ (Level 3)   │
        │             │      │  channel 5) │      │             │
        └─────────────┘      └─────────────┘      └─────────────┘

Step 4: MediaServer receives pulse via MsgReceive
        [MediaServer] Pulse: code=1, value=0
        [MediaServer] Audio ready! Requesting data...

Step 5: MediaServer sends to AudioDriver to get data
        (This is a DOWNWARD send — safe!)

        ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
        │ MediaServer │──────│  MsgSend    │─────▶│ AudioDriver │
        │ (Level 3)   │      │ (blocking)  │      │ (Level 1)   │
        │             │◀─────│  MsgReply   │──────│             │
        └─────────────┘      └─────────────┘      └─────────────┘

Result: No deadlock. Pulse breaks the potential cycle.
```

---

## 4. Send Hierarchy as System Architecture

### Startup Order

The send hierarchy is also a **dependency graph**, which directly informs the system startup order.

```
                    ┌─────────┐
                    │   P1    │  ← Start LAST (depends on everyone)
                    └────┬────┘
                         │
           ┌─────────────┼─────────────┐
           ▼             ▼             ▼
      ┌─────────┐  ┌─────────┐  ┌─────────┐
      │   P2    │  │   P3    │  │   P4    │  ← Start MIDDLE
      └────┬────┘  └────┬────┘  └────┬────┘
             │            │            │
        ┌────┴────┐   ┌───┴───┐   ┌────┴────┐
        ▼         ▼   ▼       ▼   ▼         ▼
     ┌─────┐  ┌─────┐ ┌─────┐ ┌─────┐  ┌─────┐
     │ P5  │  │ P6  │ │ P7  │ │ P8  │  │ P9  │  ← Start FIRST
     └─────┘  └─────┘ └─────┘ └─────┘  └─────┘
```

**Startup sequence:**

| Phase | Processes          | Reason                                         |
| ----- | ------------------ | ---------------------------------------------- |
| 1     | P5, P6, P7, P8, P9 | Bottom-level drivers and I/O — no dependencies |
| 2     | P2, P3, P4         | Middleware — depends on drivers being ready    |
| 3     | P1                 | Top-level UI/Control — depends on middleware   |

**If P1 (UI) must start as soon as possible:**

Optimize by starting the critical path first:

```
Critical path to P1: P8 → P4 → P1

Startup order:
1. Start P8 (driver)
2. Start P4 (middleware, waits for P8)
3. Start P1 (UI, waits for P4)
4. Start remaining drivers (P5, P6, P7, P9)
5. Start remaining middleware (P2, P3)
```

### Layer Responsibilities

The hierarchy naturally maps to system layers:

| Level                  | Typical Components                      | Role                                                         |
| ---------------------- | --------------------------------------- | ------------------------------------------------------------ |
| **Top (Level 4+)**     | HMI, UI, Command & Control              | Final decision makers; receive user input; issue system-wide commands |
| **Middle (Level 2-3)** | Middleware, Data Fusion, Business Logic | Merge data from multiple drivers; present unified view; split commands into sub-tasks |
| **Bottom (Level 1)**   | Hardware Drivers, I/O, System Loggers   | Interface to physical hardware; raw data acquisition; low-level device control |

**Example: Automotive Infotainment System**

```
Level 4:  HMI Display Manager
          └─ Receives touch input, sends commands to subsystems

Level 3:  Navigation Engine, Media Player, Vehicle Telemetry
          └─ Navigation: fuses GPS + map data + traffic
          └─ Media Player: manages playlists, codecs, audio routing
          └─ Vehicle Telemetry: aggregates CAN bus data

Level 2:  GPS Service, Map Service, Traffic Service, Audio Mixer
          └─ GPS Service: parses NMEA sentences, calculates position
          └─ Map Service: loads tile data, handles search

Level 1:  GPS Driver, CAN Driver, Audio Codec Driver, Display Driver
          └─ GPS Driver: talks to UART/GPS module
          └─ CAN Driver: reads vehicle bus messages
          └─ Audio Codec Driver: configures DAC/ADC
```

### Dependency Graph

The send hierarchy is a living document of system dependencies:

```
P1 depends on: P2, P3, P4
P2 depends on: P7
P3 depends on: P6, P7
P4 depends on: P5, P6
P5 depends on: (none)
P6 depends on: (none)
P7 depends on: (none)

This tells us:
- P5, P6, P7 are leaf nodes (no outgoing sends)
- P1 is the root (no incoming sends from within system)
- P4 is a bottleneck (P1 and P4 both depend on it)
- P7 is heavily used (P2 and P3 both depend on it)
```

**Operational insights:**

- **Restart impact:** If P6 crashes, P3 and P4 are affected. P1 may partially fail.
- **Scaling:** If P7 becomes a bottleneck, consider load-balancing across multiple P7 instances.
- **Monitoring:** The hierarchy shows where to place health-check pulses.

---

## 5. Practical Rules & Checklist

### The Golden Rules

| #    | Rule                                                         | Violation Consequence                 |
| ---- | ------------------------------------------------------------ | ------------------------------------- |
| 1    | **Client sends to server. Server never sends to client.**    | Direct deadlock                       |
| 2    | **All processes arranged in a send hierarchy.**              | Hidden cycles, undetectable deadlocks |
| 3    | **Sends only go DOWN the hierarchy.**                        | Upward blocking creates cycles        |
| 4    | **Upward/sideways communication uses non-blocking pulses.**  | Deadlock on upward data flow          |
| 5    | **No two processes ever send to each other.**                | Immediate 2-process deadlock          |
| 6    | **If same-level processes must communicate, insert a new level or use pulses.** | Sideways deadlock                     |

### Design Checklist

Before compiling your system, verify:

- [ ] Draw the send graph for all processes
- [ ] Confirm there are **no cycles** in the graph
- [ ] Confirm all **blocking sends** go downward
- [ ] Confirm all **upward/sideways** communication uses pulses
- [ ] Verify startup order respects the dependency graph
- [ ] Document which processes are clients and which are servers for each connection
- [ ] Ensure pulse queues are sized appropriately (don't lose critical notifications)
- [ ] Test with `kill -9` on each process to verify no cascade deadlocks

### Common Anti-Patterns

| Anti-Pattern                                   | Why It's Wrong                          | The Fix                                                      |
| ---------------------------------------------- | --------------------------------------- | ------------------------------------------------------------ |
| "Both processes need to exchange data"         | Mutual sends = deadlock                 | One process is always the client; the other always the server. Use pulses for the reverse direction. |
| "My server needs to ask the client a question" | Server sending to client = deadlock     | Restructure: client sends request that includes all needed context; server replies with answer. Or use pulse + client re-request. |
| "I'll just use a timeout"                      | Timeouts mask design flaws; add latency | Fix the hierarchy. Timeouts are for fault tolerance, not deadlock prevention. |
| "Two peers at the same level need to sync"     | Sideways sends = deadlock               | Insert a new level, or use pulses/semaphores for sync.       |
| "The driver needs to send config to the app"   | Upward send = deadlock                  | App requests config from driver (downward). Driver pulses app when config changes. |

---

## 6. Complete Examples

### Example 1: Full Send Hierarchy with Pulse Notification

```c
/*
 * hierarchy_system.c
 * Demonstrates a complete 4-level send hierarchy with pulse-based
 * upward notification. Compile separately for each role.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/neutrino.h>

// Pulse codes
#define PULSE_CODE_REGISTER     1
#define PULSE_CODE_DATA_READY   2
#define PULSE_CODE_SHUTDOWN     3

// Message types
#define MSG_REGISTER_CLIENT     1
#define MSG_REQUEST_DATA        2
#define MSG_SEND_COMMAND        3

// Registration message (client tells server its channel)
struct reg_msg {
    int type;
    int client_pid;
    int client_chid;
};

// Data request/response
struct data_msg {
    int type;
    int request_id;
    char payload[128];
};

/* ============================================================
 * LEVEL 1: HARDWARE DRIVER (Bottom)
 * Receives commands from Level 2
 * Pulses Level 2 when data is ready
 * ============================================================ */

void level1_driver(int level2_pid, int level2_chid) {
    int my_chid = ChannelCreate(0);
    int coid_to_level2;
    int rcvid;
    struct data_msg msg;
    struct _msg_info info;
    int data_counter = 0;

    printf("[L1-Driver] PID=%d CHID=%d Started\n", getpid(), my_chid);
    printf("[L1-Driver] Connect to me: ConnectAttach(0, %d, %d, ...)\n",
           getpid(), my_chid);

    // Connect to Level 2 for pulses (we can pulse UPWARD)
    coid_to_level2 = ConnectAttach(0, level2_pid, level2_chid,
                                   _NTO_SIDE_CHANNEL, 0);
    if (coid_to_level2 == -1) {
        perror("ConnectAttach to L2");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        rcvid = MsgReceive(my_chid, &msg, sizeof(msg), &info);
        if (rcvid == 0) {
            // We shouldn't receive pulses — we're the bottom level
            continue;
        }

        if (rcvid > 0) {
            switch (msg.type) {
                case MSG_SEND_COMMAND: {
                    printf("[L1-Driver] Received command: '%s'\n", msg.payload);

                    // Simulate hardware processing
                    usleep(500000);  // 500ms

                    // Reply to Level 2 (DOWNWARD — safe)
                    snprintf(msg.payload, sizeof(msg.payload),
                             "Command executed, data=%d", ++data_counter);
                    MsgReply(rcvid, EOK, &msg, sizeof(msg));

                    // Now pulse Level 2 that new data is available
                    // (UPWARD — non-blocking, safe)
                    printf("[L1-Driver] Pulsing L2 that data is ready\n");
                    MsgSendPulse(coid_to_level2, getprio(0),
                                 PULSE_CODE_DATA_READY, data_counter);
                    break;
                }

                case MSG_REQUEST_DATA: {
                    snprintf(msg.payload, sizeof(msg.payload),
                             "Sensor reading: %d", ++data_counter);
                    MsgReply(rcvid, EOK, &msg, sizeof(msg));
                    break;
                }

                default:
                    MsgError(rcvid, ENOSYS);
                    break;
            }
        }
    }
}

/* ============================================================
 * LEVEL 2: MIDDLEWARE
 * Receives commands from Level 3
 * Sends commands to Level 1 (DOWNWARD — safe)
 * Receives pulses from Level 1 (UPWARD — non-blocking, safe)
 * Pulses Level 3 when fused data is ready
 * ============================================================ */

void level2_middleware(int level1_pid, int level1_chid,
                       int level3_pid, int level3_chid) {
    int my_chid = ChannelCreate(0);
    int coid_to_level1;
    int coid_to_level3;
    int rcvid;
    struct data_msg msg;
    struct _pulse pulse;
    struct _msg_info info;

    printf("[L2-Middleware] PID=%d CHID=%d Started\n", getpid(), my_chid);

    // Connect DOWNWARD to Level 1
    coid_to_level1 = ConnectAttach(0, level1_pid, level1_chid,
                                   _NTO_SIDE_CHANNEL, 0);
    if (coid_to_level1 == -1) {
        perror("ConnectAttach to L1");
        exit(EXIT_FAILURE);
    }

    // Connect UPWARD to Level 3 for pulses
    coid_to_level3 = ConnectAttach(0, level3_pid, level3_chid,
                                   _NTO_SIDE_CHANNEL, 0);
    if (coid_to_level3 == -1) {
        perror("ConnectAttach to L3");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        rcvid = MsgReceive(my_chid, &msg, sizeof(msg), &info);

        if (rcvid == 0) {
            // Pulse from Level 1 (UPWARD notification)
            pulse = *(struct _pulse *)&msg;
            printf("[L2-Middleware] Pulse from L1: code=%d val=%d\n",
                   pulse.code, pulse.value);

            if (pulse.code == PULSE_CODE_DATA_READY) {
                // Level 1 has new data — request it (DOWNWARD — safe)
                struct data_request { int type; int req_id; };
                struct data_request req = { MSG_REQUEST_DATA, pulse.value };
                struct data_msg response;

                printf("[L2-Middleware] Requesting data from L1...\n");
                MsgSend(coid_to_level1, &req, sizeof(req),
                        &response, sizeof(response));
                printf("[L2-Middleware] Got data: '%s'\n", response.payload);

                // Fuse/process data, then pulse Level 3
                printf("[L2-Middleware] Pulsing L3 that fused data ready\n");
                MsgSendPulse(coid_to_level3, getprio(0),
                             PULSE_CODE_DATA_READY, pulse.value);
            }
            continue;
        }

        if (rcvid > 0) {
            switch (msg.type) {
                case MSG_SEND_COMMAND: {
                    printf("[L2-Middleware] Forwarding command to L1: '%s'\n",
                           msg.payload);

                    // Forward DOWNWARD to Level 1 (safe)
                    struct data_msg response;
                    MsgSend(coid_to_level1, &msg, sizeof(msg),
                            &response, sizeof(response));

                    printf("[L2-Middleware] L1 response: '%s'\n",
                           response.payload);

                    // Reply UPWARD to Level 3 (safe — this is a reply, not a send)
                    MsgReply(rcvid, EOK, &response, sizeof(response));
                    break;
                }

                default:
                    MsgError(rcvid, ENOSYS);
                    break;
            }
        }
    }
}

/* ============================================================
 * LEVEL 3: APPLICATION / UI (Top)
 * Sends commands to Level 2 (DOWNWARD — safe)
 * Receives pulses from Level 2 (UPWARD — non-blocking, safe)
 * Never sends to anyone below (there is no one below)
 * ============================================================ */

void level3_application(int level2_pid, int level2_chid) {
    int my_chid = ChannelCreate(0);
    int coid_to_level2;
    int rcvid;
    struct data_msg msg;
    struct _pulse pulse;
    struct _msg_info info;
    int command_count = 0;

    printf("[L3-App] PID=%d CHID=%d Started\n", getpid(), my_chid);
    printf("[L3-App] Waiting for data from lower levels...\n");

    // Connect DOWNWARD to Level 2
    coid_to_level2 = ConnectAttach(0, level2_pid, level2_chid,
                                   _NTO_SIDE_CHANNEL, 0);
    if (coid_to_level2 == -1) {
        perror("ConnectAttach to L2");
        exit(EXIT_FAILURE);
    }

    // Send initial registration so L2 can pulse us
    // (In a real system, this would be part of a name service)

    for (;;) {
        rcvid = MsgReceive(my_chid, &msg, sizeof(msg), &info);

        if (rcvid == 0) {
            // Pulse from Level 2
            pulse = *(struct _pulse *)&msg;
            printf("[L3-App] Pulse from L2: code=%d val=%d\n",
                   pulse.code, pulse.value);

            if (pulse.code == PULSE_CODE_DATA_READY) {
                printf("[L3-App] New data available! (batch %d)\n",
                       pulse.value);

                // Optionally request more details from L2
                // (DOWNWARD send — safe)
            }
            continue;
        }

        if (rcvid > 0) {
            // Handle any direct messages
            MsgReply(rcvid, EOK, NULL, 0);
        }
    }
}

// Simple test: L3 sends a command every 5 seconds
void level3_test_commands(int level2_pid, int level2_chid) {
    int coid_to_level2 = ConnectAttach(0, level2_pid, level2_chid,
                                       _NTO_SIDE_CHANNEL, 0);
    struct data_msg msg;
    struct data_msg reply;
    int count = 0;

    sleep(2);  // Let everyone start

    for (int i = 0; i < 5; i++) {
        msg.type = MSG_SEND_COMMAND;
        snprintf(msg.payload, sizeof(msg.payload),
                 "Test command #%d", ++count);

        printf("[L3-App] Sending command: '%s'\n", msg.payload);
        MsgSend(coid_to_level2, &msg, sizeof(msg), &reply, sizeof(reply));
        printf("[L3-App] Got reply: '%s'\n", reply.payload);

        sleep(5);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s l1 <l2_pid> <l2_chid>    (start Level 1 driver)\n", argv[0]);
        printf("  %s l2 <l1_pid> <l1_chid> <l3_pid> <l3_chid>  (start Level 2)\n", argv[0]);
        printf("  %s l3 <l2_pid> <l2_chid>    (start Level 3 app)\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "l1") == 0) {
        level1_driver(atoi(argv[2]), atoi(argv[3]));
    } else if (strcmp(argv[1], "l2") == 0) {
        level2_middleware(atoi(argv[2]), atoi(argv[3]),
                          atoi(argv[4]), atoi(argv[5]));
    } else if (strcmp(argv[1], "l3") == 0) {
        if (fork() == 0) {
            // Child: run the command sender
            level3_test_commands(atoi(argv[2]), atoi(argv[3]));
            exit(0);
        }
        // Parent: run the pulse receiver
        level3_application(atoi(argv[2]), atoi(argv[3]));
    }

    return 0;
}
```

**To run the example:**

```bash
# Terminal 1: Start Level 3 (App) first — it creates the channel
./hierarchy l3 0 0  # Prints its PID and CHID
# [L3-App] PID=1234 CHID=5 Started

# Terminal 2: Start Level 2 (Middleware)
./hierarchy l2 0 0 1234 5  # L1 PID/CHID not known yet
# Wait for L1 to start, then restart with correct values

# Terminal 3: Start Level 1 (Driver)
./hierarchy l1 <l2_pid> <l2_chid>
# [L1-Driver] PID=5678 CHID=3 Started

# Restart Level 2 with correct L1 values:
./hierarchy l2 5678 3 1234 5
```

**Expected output:**

```
[L3-App] Sending command: 'Test command #1'
[L2-Middleware] Forwarding command to L1: 'Test command #1'
[L1-Driver] Received command: 'Test command #1'
[L1-Driver] Pulsing L2 that data is ready
[L2-Middleware] Pulse from L1: code=2 val=1
[L2-Middleware] Requesting data from L1...
[L1-Driver] Received request for data
[L2-Middleware] Got data: 'Sensor reading: 1'
[L2-Middleware] Pulsing L3 that fused data ready
[L3-App] Pulse from L2: code=2 val=1
[L3-App] New data available! (batch 1)
[L3-App] Got reply: 'Command executed, data=1'
```

### Example 2: Detecting Cycles in Your Send Graph

```c
/*
 * cycle_detector.c
 * A simple tool to check your send hierarchy for cycles.
 * Feed it a list of "sender -> receiver" pairs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROCS 100

struct edge {
    int from;
    int to;
};

int adj[MAX_PROCS][MAX_PROCS];
int visited[MAX_PROCS];
int rec_stack[MAX_PROCS];
int num_procs = 0;

int has_cycle_from(int v) {
    visited[v] = 1;
    rec_stack[v] = 1;

    for (int i = 0; i < num_procs; i++) {
        if (adj[v][i]) {
            if (!visited[i] && has_cycle_from(i)) {
                return 1;
            } else if (rec_stack[i]) {
                printf("CYCLE DETECTED: %d -> %d (back edge)\n", v, i);
                return 1;
            }
        }
    }

    rec_stack[v] = 0;
    return 0;
}

int main() {
    // Example: 1->2, 2->3, 3->1 (cycle)
    // And 1->4, 4->5 (no cycle)

    memset(adj, 0, sizeof(adj));

    // Add edges (in a real tool, read from config file)
    adj[1][2] = 1;
    adj[2][3] = 1;
    adj[3][1] = 1;  // CYCLE!
    adj[1][4] = 1;
    adj[4][5] = 1;

    num_procs = 6;

    memset(visited, 0, sizeof(visited));
    memset(rec_stack, 0, sizeof(rec_stack));

    printf("Checking send graph for cycles...\n");
    for (int i = 0; i < num_procs; i++) {
        if (!visited[i]) {
            if (has_cycle_from(i)) {
                printf("\nRESULT: DEADLOCK POSSIBLE! Cycle detected.\n");
                return 1;
            }
        }
    }

    printf("\nRESULT: No cycles detected. Hierarchy is safe.\n");
    return 0;
}
```

---

## 7. Key Takeaways

### Core Principles

| Principle                          | Summary                                         |
| ---------------------------------- | ----------------------------------------------- |
| **QNX does not detect deadlocks**  | Avoidance is mandatory at design time           |
| **Deadlock = cycle in send graph** | Any circular send chain is a potential deadlock |
| **Client → Server only**           | Server never blocks sending to client           |
| **Send hierarchy prevents cycles** | DAG structure guarantees no deadlock            |
| **Pulses for upward notification** | Non-blocking breaks potential cycles            |
| **Hierarchy informs startup**      | Bottom-up startup respects dependencies         |

### Decision Tree

```
Does process A need to send to process B?
│
├─ Is A higher level than B in the hierarchy?
│  └─ YES → Use MsgSend() (blocking, safe, downward)
│  └─ NO  → Continue...
│
├─ Is A lower level than B?
│  └─ YES → Use MsgSendPulse() (non-blocking, safe)
│            OR restructure so B sends to A
│  └─ NO  → Continue...
│
├─ Are A and B at the same level?
│  └─ YES → Option 1: Insert new level between them
│            Option 2: Use MsgSendPulse() (non-blocking)
│            Option 3: Restructure hierarchy
│  └─ NO  → This shouldn't happen — fix your hierarchy!
│
└─ Does B need to reply with data?
   └─ YES → B replies to A's MsgSend() (reply is always safe)
   └─ NO  → B uses MsgReply() with status only
```

### Quick Reference

| Situation                                | Correct Approach                                             |
| ---------------------------------------- | ------------------------------------------------------------ |
| Client has data for server               | `MsgSend()` from client to server                            |
| Server has data for client               | `MsgSendPulse()` to client; client then `MsgSend()` to server |
| Server needs to notify client of event   | `MsgSendPulse()` with event code                             |
| Client needs to request data from server | `MsgSend()` with request; server `MsgReply()` with data      |
| Two peers need to sync                   | Insert hierarchy level OR use pulse + re-request             |
| Driver has interrupt data for app        | Driver pulses app; app sends to driver requesting data       |
| App needs to command driver              | `MsgSend()` from app to driver (downward)                    |

### Remember

> **"If you can't draw it without cycles, it will deadlock."**

Always draw your send graph. If you see an arrow going up, it must be a pulse. If you see a cycle, redesign. The hierarchy is not just a deadlock prevention tool — it's your system architecture.

---

## Related Documentation

- **Part 1:** QNX Message Passing Fundamentals (MsgSend, MsgReceive, MsgReply)
- **Part 3:** QNX Server Designs — Thread Pools, Priority Inheritance, Delayed Reply *(companion document)*
- **Part 4:** QNX Multi-Part Messages — IOVs, MsgSendv, MsgRead, MsgWrite
- **QNX Official:** [Message Passing](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/ipc.html)
- **QNX Official:** [Pulses](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_sys_arch/pulses.html)

---

*This document synthesizes material from QNX Neutrino documentation, training materials, and practical embedded systems design patterns.*
