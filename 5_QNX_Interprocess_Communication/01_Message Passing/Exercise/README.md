# QNX Message Passing Exercise

## Overview

This exercise demonstrates QNX native message passing IPC with a **checksum server**. The client sends a string to the server, the server calculates the checksum (sum of all bytes), and replies back with the result.

---

## Files

| File       | Description                                                  |
| ---------- | ------------------------------------------------------------ |
| `server.c` | Checksum server — receives strings, calculates checksum, replies |
| `client.c` | Client — sends a string, receives checksum reply             |

---

## Build

```bash
# Compile the server
qcc -o server server.c

# Compile the client
qcc -o client client.c
```

---

## Run

### Step 1: Start the Server

```bash
./server
```

**Expected Output:**

```
========================================
  Checksum Server Started
  PID: 12345
  Channel ID: 1
========================================
Waiting for client messages...
```

> **Note down the PID and Channel ID!**

### Step 2: Run the Client

```bash
./client <server_pid> <server_chid> "Your message here"
```

**Example:**

```bash
./client 12345 1 "Hello World"
```

**Expected Output:**

```
========================================
  Checksum Client
  Connected to Server PID: 12345, Channel: 1
  Connection ID (coid): 1073741825
========================================
Sending message: "Hello World"

MsgSend() returned status: 0
Server replied with checksum: 1052

Client finished successfully!
```

### Step 3: Server Output

```
Received message from PID 12346, TID 1
Client message: "Hello World"
Calculated checksum: 1052
Reply sent successfully!
----------------------------------------
```

---

## Exercise Questions

### Q1: What states did the client and server transition to?

**Answer:**

- **Client:** `RUNNABLE` → `REPLY BLOCKED` → `RUNNABLE`
- **Server:** `RECEIVE BLOCKED` → `RUNNING` → `RECEIVE BLOCKED`

The client skips `SEND BLOCKED` because the server was already waiting in `MsgReceive()`.

**Verify with pidin:**

```bash
# In another terminal, while client is blocked waiting for reply:
pidin
```

Look for:

```
  pid  tid name               prio STATE          Blocked
12345    1 server               10 RECEIVE        1
12346    1 client               10 REPLY          12345
```

---

### Q2: Did the client ever become "SEND blocked"?

**Answer:** No, under normal conditions the client does NOT become `SEND BLOCKED`.

The server is already in `RECEIVE BLOCKED` state when the client calls `MsgSend()`, so the message is immediately received and the client goes directly to `REPLY BLOCKED`.

**To witness SEND BLOCKED:**

1. Add a `sleep(5)` in the server **before** `MsgReceive()`
2. Run the server
3. Run the client immediately
4. In another terminal, run `pidin` — you'll see the client in `SEND BLOCKED` state

```bash
pidin
# Output:
# 12346    1 client               10 SEND           12345
```

---

### Q3: What happens if you remove MsgReply()?

**To test:**

1. Comment out the `MsgReply()` call in `server.c`
2. Rebuild and run the server
3. Run the client

**Result:**

- The client will **hang forever** in `REPLY BLOCKED` state
- `MsgSend()` never returns
- The server continues its loop and receives the next message (if any)

**Verify with pidin:**

```bash
pidin
# Output:
# 12346    1 client               10 REPLY          12345
```

The client remains `REPLY BLOCKED` indefinitely because the server never replied.

---

### Q4: What happens if MsgReceive() returns failure?

**Answer:** If `MsgReceive()` returns `-1`:

- The server prints the error via `perror()`
- The server continues to the next loop iteration
- The client remains in whatever blocked state it was in

**If server was RECEIVE BLOCKED and MsgReceive() fails:**

- This is unusual — typically means the channel was destroyed or a signal interrupted the call
- The client would remain `SEND BLOCKED` or `REPLY BLOCKED` depending on timing

**To simulate:**

```c
// In server.c, temporarily change:
// rcvid = MsgReceive(chid, recv_buffer, sizeof(recv_buffer), &info);
// to use an invalid channel ID:
rcvid = MsgReceive(9999, recv_buffer, sizeof(recv_buffer), &info);
```

---

### Q5: Where does the extra value in "MsgSend return" come from?

**Answer:** The value printed by `MsgSend() returned status: X` comes from the **status parameter** of `MsgReply()`.

In `server.c`:

```c
MsgReply(rcvid, EOK, &checksum, sizeof(checksum));
//           ^^^^
//           This is the status returned to MsgSend()
```

In `client.c`:

```c
status = MsgSend(coid, send_buffer, ... , &reply_checksum, ...);
printf("MsgSend() returned status: %d\n", status);  // Prints EOK (0)
```

The `status` from `MsgReply()` is returned by `MsgSend()` to indicate success/failure of the message passing operation itself.

---

## TODO Locations

### server.c TODOs

| TODO   | Location | What to Fill                                                 |
| ------ | -------- | ------------------------------------------------------------ |
| TODO 1 | Line 28  | `chid = ChannelCreate(0);`                                   |
| TODO 2 | Line 43  | `rcvid = MsgReceive(chid, recv_buffer, sizeof(recv_buffer), &info);` |
| TODO 3 | Line 62  | `MsgReply(rcvid, EOK, &checksum, sizeof(checksum));`         |

### client.c TODOs

| TODO   | Location | What to Fill                                                 |
| ------ | -------- | ------------------------------------------------------------ |
| TODO 1 | Line 32  | `coid = ConnectAttach(0, server_pid, server_chid, _NTO_SIDE_CHANNEL, 0);` |
| TODO 2 | Line 52  | `status = MsgSend(coid, send_buffer, strlen(send_buffer) + 1, &reply_checksum, sizeof(reply_checksum));` |
| TODO 3 | Line 58  | `if (status == -1) { ... }`                                  |
| TODO 4 | Line 65  | `ConnectDetach(coid);`                                       |

---

## Checksum Algorithm

The checksum is calculated as the sum of all byte values in the string:

```c
unsigned int calculate_checksum(const char *str) {
    unsigned int checksum = 0;
    while (*str) {
        checksum += (unsigned char)(*str);
        str++;
    }
    return checksum;
}
```

**Example:**

- `"Hello"` → `72 + 101 + 108 + 108 + 111 = 500`
- `"ABC"` → `65 + 66 + 67 = 198`

---

## Architecture

```
┌─────────────────┐                    ┌─────────────────┐
│   CLIENT        │                    │   SERVER        │
│                 │                    │                 │
│  ConnectAttach()│───▶ coid ─────────▶│  ChannelCreate()│
│  (to chid)      │                    │  chid = 1       │
│                 │                    │                 │
│  MsgSend()      │───▶ "Hello" ──────▶│  MsgReceive()   │
│  (blocks)       │                    │  calculate_sum()│
│                 │◀─── checksum ───────│  MsgReply()     │
│  (unblocks)     │                    │                 │
│  print result   │                    │  loop back      │
└─────────────────┘                    └─────────────────┘
```

---

## Troubleshooting

| Problem                | Cause                                    | Solution                                        |
| ---------------------- | ---------------------------------------- | ----------------------------------------------- |
| `ConnectAttach failed` | Wrong PID or Channel ID                  | Check server output for correct values          |
| Client hangs           | Server not running or MsgReply() missing | Start server first; ensure MsgReply() is called |
| `MsgSend failed`       | Server died or channel destroyed         | Restart server; check with `pidin`              |
| Wrong checksum         | Message encoding issue                   | Ensure null terminator is included in send size |
