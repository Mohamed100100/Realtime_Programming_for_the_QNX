# QNX Pulse Exercise

## Overview

This exercise demonstrates sending **pulses** from a client to a server. The server handles both **pulses** (non-blocking notifications) and **regular messages** (synchronous request/reply) on the same channel.

---

## Files

| File             | Description                                            |
| ---------------- | ------------------------------------------------------ |
| `pulse_server.c` | Server that receives both pulses and checksum messages |
| `pulse_client.c` | Client that sends a pulse, then a checksum message     |
| `Makefile`       | Build file for both programs                           |

---

## Key Concepts Demonstrated

### 1. Union-Based Receive Buffer

The server uses a **union** as its receive buffer to safely handle both messages and pulses:

```c
typedef union {
    struct _pulse       pulse;      // 24 bytes — ensures minimum size
    uint16_t            type;       // Bare type for quick inspection
    struct checksum_msg checksum;   // Checksum message structure
} recv_buf_t;
```

> **Why a union?** The C language guarantees the union size is at least the size of its largest member. Including `struct _pulse` (24 bytes) ensures the buffer is always large enough for any pulse. This prevents the `EFAULT` error and lost pulses.

### 2. The Bare `type` Field Trick

```c
union {
    struct _pulse       pulse;      // pulse.type is always 0 (internal kernel use)
    uint16_t            type;       // Quick access to message type
    struct checksum_msg checksum;   // checksum.type is the message type
};
```

By putting a bare `uint16_t type` at the same offset, the server can inspect the message type **before** deciding which union member to access. This works because:

- Every message structure starts with `uint16_t type` (by protocol design)
- `struct _pulse` also starts with `uint16_t type` (though it's always `_PULSE_TYPE` for pulses)
- The `type` field is at the same memory offset in all union members

### 3. Distinguishing Pulses from Messages

```c
rcvid = MsgReceive(chid, &msg, sizeof(msg), &info);

if (rcvid == 0) {
    // PULSE — do NOT reply!
    printf("Pulse code: %d, value: 0x%lx\n",
           msg.pulse.code, msg.pulse.value.sival_long);
} else if (rcvid > 0) {
    // MESSAGE — must reply with MsgReply() or MsgError()
    switch (msg.type) {
        case MSG_TYPE_CHECKSUM:  ...
    }
}
```

---

## Build

```bash
make
```

Or manually:

```bash
qcc -Wall -g -o pulse_server pulse_server.c
qcc -Wall -g -o pulse_client pulse_client.c
```

---

## Run

### Step 1: Start the Server

```bash
./pulse_server
```

**Expected Output:**

```
========================================
  Pulse Server Started
  PID: 12345
  Channel ID: 1
========================================
Waiting for messages and pulses...
```

> **Note down the PID and Channel ID!**

### Step 2: Run the Client

```bash
./pulse_client <server_pid> <server_chid> "Your message here"
```

**Example:**

```bash
./pulse_client 12345 1 "Hello World"
```

**Expected Client Output:**

```
========================================
  Pulse Client
  Connected to Server PID: 12345, Channel: 1
  Connection ID (coid): 1073741825
========================================

Sending PULSE to server...
  Pulse sent successfully!
    Code: 3
    Value: 0xdeadc0de

Sending CHECKSUM MESSAGE to server...
  Message: "Hello World"

MsgSend() returned status: 0
Server replied with checksum: 1052

Client finished successfully!
```

**Expected Server Output:**

```
[PULSE RECEIVED]
  Pulse code: 3
  Pulse value: 0xdeadc0de (3735929054)
  From scoid: 1
----------------------------------------

[CHECKSUM MESSAGE RECEIVED]
  From PID 12346, TID 1
  Message: "Hello World"
  Calculated checksum: 1052
  Reply sent successfully!
----------------------------------------
```

---

## Architecture

```
CLIENT SIDE                              SERVER SIDE
┌─────────────────┐                      ┌─────────────────┐
│                 │                      │                 │
│  MsgSendPulse() │───▶ PULSE ─────────▶│  MsgReceive()   │
│  code=3         │   (non-blocking)     │  rcvid == 0     │
│  value=0xdeadc0de                      │  print pulse    │
│                 │                      │  NO REPLY       │
│                 │                      │                 │
│  MsgSend()      │───▶ MESSAGE ───────▶│  MsgReceive()   │
│  type=CHECKSUM  │   (blocking)         │  rcvid > 0      │
│  text="Hello"   │                      │  calc checksum  │
│                 │◀─── REPLY ───────────│  MsgReply()     │
│  checksum=1052  │                      │                 │
└─────────────────┘                      └─────────────────┘
```

---

## Code Walkthrough

### pulse_server.c

| Section                | Purpose                                                |
| ---------------------- | ------------------------------------------------------ |
| `struct checksum_msg`  | Protocol-defined message structure                     |
| `recv_buf_t` union     | **Server implementation detail** — safe receive buffer |
| `calculate_checksum()` | Computes sum of all byte values in string              |
| `MsgReceive()` loop    | Blocks waiting for messages and pulses                 |
| `rcvid == 0` check     | Handles pulse — prints code and value, no reply        |
| `switch(msg.type)`     | Handles message — branches on message type             |
| `MsgReply(rcvid, ...)` | Replies to message with checksum                       |

### pulse_client.c

| Section           | Purpose                                                |
| ----------------- | ------------------------------------------------------ |
| `ConnectAttach()` | Establishes connection to server's channel             |
| `MsgSendPulse()`  | Sends non-blocking pulse with code=3, value=0xdeadc0de |
| `MsgSend()`       | Sends blocking checksum message, waits for reply       |
| `ConnectDetach()` | Cleans up the connection                               |

---

## Design Notes

### Why `recv_buf_t` is NOT in a Shared Header

The `recv_buf_t` union is a **server implementation detail**, not part of the client/server protocol:

```
Shared Protocol (msg_def.h)          Server-Only (pulse_server.c)
┌─────────────────────────┐          ┌─────────────────────────┐
│ struct checksum_msg {   │          │ typedef union {         │
│   uint16_t type;        │◄─────────│   struct _pulse pulse;  │
│   char text[512];       │          │   uint16_t type;        │
│ };                      │          │   struct checksum_msg   │
│                         │          │             checksum;   │
│ #define MSG_TYPE_...    │          │ } recv_buf_t;           │
└─────────────────────────┘          └─────────────────────────┘
    Both include                         Only server includes
```

- The **protocol** (message structures, type constants) goes in a shared header
- The **receive buffer union** is specific to how the server receives data
- If the server had multiple source files, it might go in a `serverdefs.h`

---

## Troubleshooting

| Problem                    | Cause                  | Solution                                      |
| -------------------------- | ---------------------- | --------------------------------------------- |
| Server doesn't see pulse   | Buffer too small       | Ensure union includes `struct _pulse`         |
| `MsgSendPulse fails`       | Invalid coid           | Check server PID and channel ID               |
| Client hangs after message | Server didn't reply    | Ensure server calls `MsgReply()` for messages |
| `EFAULT from MsgReceive`   | Buffer < 24 bytes      | Use union with `struct _pulse`                |
| Pulse value prints wrong   | Wrong format specifier | Use `%lx` for hex, `%lu` for decimal          |

---

## Questions to Explore

1. **What happens if you comment out `MsgReply()` in the server?**
   - The client will hang in `REPLY BLOCKED` state after `MsgSend()`
   - Verify with `pidin` — client state shows `REPLY <server_pid>`

2. **What happens if the server replies to a pulse?**
   - `MsgReply(0, ...)` returns `-1` with error — `rcvid == 0` is not a valid reply target

3. **Can you send multiple pulses before the message?**
   - Yes! Pulses are queued. Try sending 5 pulses, then the message.
   - The server will receive all 5 pulses (`rcvid == 0` each time), then the message.

4. **What if you send a pulse with a different code?**
   - The server will still receive it, but the `switch(pulse->code)` won't have a matching case
   - Add more cases to handle different pulse codes!
