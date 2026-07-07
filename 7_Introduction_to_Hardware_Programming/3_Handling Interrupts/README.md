# QNX Neutrino Hardware Interrupt Handling Guide

This document provides a comprehensive guide to handling hardware interrupts in QNX Neutrino. It covers the interrupt architecture from hardware assertion through the microkernel to the driver, the two primary interrupt handling models (IST and Event), thread notification mechanisms, and the privilege requirements for interrupt operations.

---

## 1. Overview

At the fundamental level, when hardware needs attention, it asserts an interrupt line that propagates through a programmable interrupt controller (PIC) to the CPU. The QNX microkernel traps this interrupt, identifies its source, and unblocks an Interrupt Service Thread (IST) to handle it.

There are two primary ways to handle interrupts:

- **InterruptAttachThread**: Your thread becomes the IST directly
- **InterruptAttachEvent**: The microkernel creates an in-kernel IST that delivers an event to your process

All ISTs are ordinary threads scheduled by priority and algorithm, though the in-kernel IST runs at reserved priority 253.

---

## 2. Interrupt Architecture

### 2.1 The Interrupt Path

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        Hardware Interrupt Path                               │
│                                                                              │
│   Hardware Source                                                            │
│   ─────────────                                                              │
│   • Serial port (TX/RX complete)                                             │
│   • Network card (packet received/transmitted)                               │
│   • Storage controller (DMA transfer complete)                               │
│   • CAN controller (message received)                                        │
│   • Timer (periodic expiry)                                                  │
│        │                                                                     │
│        │ Asserts interrupt line                                              │
│        ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────────┐  │
│   │              Programmable Interrupt Controller (PIC)                  │  │
│   │                                                                       │  │
│   │   • ARM64:  GIC  (Generic Interrupt Controller)                       │  │
│   │   • x86:    APIC (Advanced Programmable Interrupt Controller)         │  │
│   │   • Other:  Platform-specific interrupt controller                    │  │
│   │                                                                       │  │
│   │   The PIC moderates between external devices and the CPU.             │  │
│   │   It applies internal rules:                                          │  │
│   │   • Is the interrupt master-enabled?                                  │  │
│   │   • Priority masking                                                  │  │
│   │   • Target CPU core routing                                           │  │
│   └───────────────────────────────────────────────────────────────────────┘  │
│        │                                                                     │
│        │ Interrupt passes PIC rules                                          │
│        ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────────┐  │
│   │                    QNX Microkernel (procnto)                          │  │
│   │                                                                       │  │
│   │   1. Traps the interrupt on a CPU core                                │  │
│   │   2. Identifies which interrupt occurred                              │  │
│   │   3. Unblocks the registered IST                                      │  │
│   │                                                                       │  │
│   │   Two unblocking paths:                                               │  │
│   │                                                                       │  │
│   │   PATH A ──▶ Driver's own IST (InterruptAttachThread)                 │  │
│   │              • Thread unblocks from InterruptWait()                   │  │
│   │              • Runs in user space at driver's priority                │  │
│   │                                                                       │  │
│   │   PATH B ──▶ In-kernel IST (InterruptAttachEvent)                     │  │
│   │              • Microkernel-only thread at priority 253                │  │
│   │              • Delivers event to driver's thread                      │  │
│   │              • Driver thread unblocks from MsgReceive() or wait       │  │
│   └───────────────────────────────────────────────────────────────────────┘  │
│        │                                                                     │
│        ▼                                                                     │
│   ┌─────────────────────────────────────────────────────────────────────┐    │
│   │                    Driver Process                                   │    │
│   │                                                                     │    │
│   │   • IST talks to hardware (read status, acknowledge interrupt)      │    │
│   │   • May notify other threads (MsgSendPulse, sem_post, etc.)         │    │
│   │   • May interact with clients (resource manager messages)           │    │
│   └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Interrupt Controller Types

| Platform            | Interrupt Controller                              | Notes                           |
| ------------------- | ------------------------------------------------- | ------------------------------- |
| **ARM64 (AArch64)** | GIC (Generic Interrupt Controller)                | GICv2, GICv3, GICv4 variants    |
| **x86/x86_64**      | APIC (Advanced Programmable Interrupt Controller) | Local APIC + I/O APIC           |
| **Other**           | Platform-specific                                 | Vendor-specific implementations |

The startup code configures the interrupt controller and populates the system page with interrupt routing information. The microkernel uses this to know which interrupt numbers correspond to which hardware sources.

---

## 3. Interrupt Handling Models

### 3.1 Model Comparison

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Interrupt Handling Model Decision                        │
│                                                                             │
│   Do you need a dedicated interrupt thread, or can interrupts be            │
│   handled alongside other work (client messages, etc.)?                     │
│                                                                             │
│         DEDICATED IST                              EVENT-BASED              │
│              │                                         │                    │
│              ▼                                         ▼                    │
│   ┌─────────────────────┐                  ┌─────────────────────┐          │
│   │ InterruptAttachThread│                 │ InterruptAttachEvent│          │
│   │                     │                  │                     │          │
│   │ • Lowest latency    │                  │ • More flexible     │          │
│   │ • Simplest setup    │                  │ • Single-threaded   │          │
│   │ • One thread per    │                  │   driver possible   │          │
│   │   interrupt         │                  │ • Pool of threads   │          │
│   │ • No queuing/count  │                  │ • Queued delivery   │          │
│   │ • Thread can only   │                  │ • Higher overhead   │          │
│   │   wait on one       │                  │ • Slightly higher   │          │
│   │   interrupt         │                  │   latency           │          │
│   └─────────────────────┘                  └─────────────────────┘          │
│                                                                             │
│   Use IST when:                           Use Event when:                   │
│   • Real-time response is critical        • Single-threaded driver design   │
│   • Hardware has few interrupts           • Need to handle clients + HW     │
│   • Simplicity is preferred               • Multiple interrupts share thread│
│   • Lowest latency required               • Thread pool architecture        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Core API Functions

| Function                  | Purpose                                          | Model |
| ------------------------- | ------------------------------------------------ | ----- |
| `InterruptAttachThread()` | Calling thread becomes an IST                    | IST   |
| `InterruptAttachEvent()`  | Kernel creates IST, delivers event               | Event |
| `InterruptDetach()`       | Remove interrupt handler                         | Both  |
| `InterruptWait()`         | IST waits for next interrupt                     | IST   |
| `InterruptMask()`         | Mask this interrupt                              | Both  |
| `InterruptUnmask()`       | Unmask this interrupt                            | Both  |
| `InterruptDisable()`      | Disable ALL interrupts on this CPU (rarely used) | Both  |
| `InterruptEnable()`       | Re-enable interrupts on this CPU                 | Both  |

---

## 4. InterruptAttachThread (IST Model)

### 4.1 How It Works

With `InterruptAttachThread()`, your calling thread becomes the Interrupt Service Thread. When the hardware asserts the interrupt, the microkernel unblocks your thread from `InterruptWait()`.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│              InterruptAttachThread Architecture                              │
│                                                                              │
│   Hardware ──▶ PIC ──▶ CPU ──▶ Microkernel ──▶ UNBLOCKS ──▶ Driver Thread    │
│                                                                              │
│   The driver thread:                                                         │
│   1. Calls InterruptAttachThread(irq, handler, area, size, flags)            │
│   2. Enters forever loop calling InterruptWait()                             │
│   3. When unblocked, talks to hardware (read status, acknowledge)            │
│   4. Calls InterruptUnmask() to re-enable the interrupt                      │
│   5. Does any additional work needed                                         │
│   6. Loops back to InterruptWait()                                           │
│                                                                              │
│   The kernel automatically masks the interrupt BEFORE unblocking the thread. │
│   This prevents nested interrupts while the IST is running.                  │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 IST Thread Structure

```c
#include <sys/neutrino.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Interrupt Service Thread for a hardware device.
 * This thread becomes the IST via InterruptAttachThread().
 */
void *interrupt_service_thread(void *arg) {
    int irq = *(int *)arg;          /* The interrupt number to handle */
    int id;                          /* Interrupt ID returned by attach */
    struct sigevent event;
    int rc;

    /* Attach this thread as the IST */
    id = InterruptAttachThread(
        irq,                         /* Interrupt number */
        NULL,                        /* No event (we are the IST) */
        NULL,                        /* No area data */
        0,                           /* Area size */
        0                            /* Flags */
    );

    if (id == -1) {
        perror("InterruptAttachThread");
        return NULL;
    }

    printf("IST attached to IRQ %d, ID=%d\n", irq, id);

    /* Forever loop: wait for interrupts */
    for (;;) {
        /* Wait for the next interrupt */
        rc = InterruptWait(0, NULL);
        if (rc == -1) {
            perror("InterruptWait");
            break;
        }

        /* === TALK TO HARDWARE === */
        /* Read status registers to determine interrupt cause */
        /* Acknowledge the interrupt (tell hardware to stop asserting) */
        /* Do minimal work here - just what's needed for the hardware */

        uint32_t status = read_device_status_register();

        if (status & TX_COMPLETE_FLAG) {
            handle_tx_complete();
        }

        if (status & RX_DATA_AVAILABLE) {
            handle_rx_data();
        }

        /* Acknowledge the interrupt at hardware level */
        write_device_ack_register(status);

        /* === UNMASK INTERRUPT === */
        /* Now safe to receive another interrupt */
        InterruptUnmask(irq, id);

        /* === ADDITIONAL WORK (optional) === */
        /* Any non-time-critical work can go here */
        /* Or notify another thread via MsgSendPulse, etc. */
    }

    /* Detach before exiting */
    InterruptDetach(id);
    return NULL;
}
```

### 4.3 Key Characteristics of IST Model

| Aspect           | Behavior                              |
| ---------------- | ------------------------------------- |
| **Latency**      | Lowest possible — direct unblocking   |
| **Setup**        | Simplest — just attach and wait       |
| **Thread count** | One thread per interrupt source       |
| **Queuing**      | None — interrupts not counted         |
| **Priority**     | Runs at thread's assigned priority    |
| **Flexibility**  | Thread can only wait on ONE interrupt |

> **Important:** If your hardware has multiple interrupts (e.g., separate TX and RX), you need separate IST threads for each. If you manage multiple devices, each with its own interrupts, you need an IST per interrupt.

---

## 5. InterruptAttachEvent (Event Model)

### 5.1 How It Works

With `InterruptAttachEvent()`, you register an event with the microkernel. The kernel creates an in-kernel IST (at reserved priority 253) that handles the direct interrupt unblocking and then delivers your event to a thread in your process.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│              InterruptAttachEvent Architecture                              │
│                                                                             │
│   Hardware ──▶ PIC ──▶ CPU ──▶ Microkernel                                  │
│                                      │                                      │
│                                      ▼                                      │
│                              ┌───────────────┐                              │
│                              │ In-Kernel IST │  ← Priority 253 (reserved)   │
│                              │ (microkernel  │                              │
│                              │  only thread) │                              │
│                              └───────┬───────┘                              │
│                                      │                                      │
│                                      │ Delivers event                       │
│                                      ▼                                      │
│                              ┌───────────────┐                              │
│                              │ Driver Thread │  ← Unblocks from MsgReceive()│
│                              │ (user space)  │    or other wait             │
│                              │               │                              │
│                              │ • Handles pulse/sigevent                     │
│                              │ • Calls InterruptUnmask()                    │
│                              │ • Processes interrupt                        │
│                              └───────────────┘                              │
│                                                                             │
│   The in-kernel IST is purely in-kernel — no user space transition.         │
│   This is still quite fast, but has slightly more overhead than direct IST. │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.2 Event Model with Pulse (Most Common)

The most common event type for `InterruptAttachEvent` is a **pulse**, which allows a single-threaded driver to handle both hardware interrupts and client messages in one `MsgReceive()` loop.

```c
#include <sys/neutrino.h>
#include <sys/siginfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PULSE_CODE_INTERRUPT    _PULSE_CODE_MINAVAIL
#define PULSE_CODE_DISCONNECT   _PULSE_CODE_DISCONNECT

/*
 * Single-threaded driver using InterruptAttachEvent with pulse.
 * Handles both hardware interrupts and client messages.
 */
int single_threaded_driver(int irq) {
    int chid;           /* Channel ID */
    int coid;           /* Connection ID (to self) */
    int id;             /* Interrupt ID */
    struct sigevent event;
    struct _pulse pulse;
    int rc;

    /* Step 1: Create a channel for receiving pulses and messages */
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        return -1;
    }

    /* Step 2: Create a connection to ourselves for pulse delivery */
    coid = ConnectAttach(
        0,               /* Node ID (0 = local) */
        0,               /* Process ID (0 = self) */
        chid,            /* Channel ID */
        _NTO_SIDE_CHANNEL,  /* Non-file-descriptor connection */
        0                /* Flags */
    );

    if (coid == -1) {
        perror("ConnectAttach");
        ChannelDestroy(chid);
        return -1;
    }

    /* Step 3: Initialize the pulse event */
    SIGEV_PULSE_INIT(
        &event,          /* Event structure to fill */
        coid,            /* Connection to deliver pulse on */
        SIGEV_PULSE_PRIO_INHERIT,  /* Inherit thread priority */
        PULSE_CODE_INTERRUPT,      /* Custom pulse code */
        0                /* Pulse value */
    );

    /* Step 4: Attach the interrupt with this event */
    id = InterruptAttachEvent(
        irq,             /* Interrupt number */
        &event,          /* Event to deliver */
        0                /* Flags */
    );

    if (id == -1) {
        perror("InterruptAttachEvent");
        ConnectDetach(coid);
        ChannelDestroy(chid);
        return -1;
    }

    printf("Interrupt %d attached with pulse event, ID=%d\n", irq, id);

    /* Step 5: Main event loop — handles BOTH interrupts and client messages */
    for (;;) {
        rc = MsgReceive(chid, &pulse, sizeof(pulse), NULL);
        if (rc == -1) {
            perror("MsgReceive");
            break;
        }

        if (rc == 0) {
            /* === PULSE RECEIVED === */
            switch (pulse.code) {

                case PULSE_CODE_INTERRUPT:
                    /* === HARDWARE INTERRUPT === */
                    /* Talk to hardware */
                    uint32_t status = read_device_status_register();

                    if (status & TX_COMPLETE_FLAG) {
                        handle_tx_complete();
                    }

                    if (status & RX_DATA_AVAILABLE) {
                        handle_rx_data();
                    }

                    /* Acknowledge interrupt */
                    write_device_ack_register(status);

                    /* Unmask to allow next interrupt */
                    InterruptUnmask(irq, id);
                    break;

                case PULSE_CODE_DISCONNECT:
                    /* Client disconnected */
                    handle_client_disconnect(pulse.value);
                    break;

                default:
                    /* Other pulses */
                    handle_other_pulse(pulse.code, pulse.value);
                    break;
            }

        } else {
            /* === MESSAGE RECEIVED (from client) === */
            /* Handle resource manager message */
            handle_client_message(rc, chid);
        }
    }

    /* Cleanup */
    InterruptDetach(id);
    ConnectDetach(coid);
    ChannelDestroy(chid);
    return 0;
}
```

### 5.3 SIGEV_PULSE_INIT Parameters

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    SIGEV_PULSE_INIT Parameters                              │
│                                                                             │
│   SIGEV_PULSE_INIT(                                                         │
│       &event,           ← struct sigevent to initialize                     │
│       coid,             ← Connection ID (to self, _NTO_SIDE_CHANNEL)        │
│       priority,         ← Pulse delivery priority                           │
│       code,             ← Pulse code (use to identify interrupt source)     │
│       value             ← Pulse value (optional data)                       │
│   );                                                                        │
│                                                                             │
│   Priority Choices:                                                         │
│   • SIGEV_PULSE_PRIO_INHERIT  ← Inherit from thread that gets the pulse     │
│   • Explicit priority (e.g., 20, 50)  ← Fixed priority for pulse delivery   │
│                                                                             │
│   Why _NTO_SIDE_CHANNEL?                                                    │
│   • Creates a connection without a file descriptor                          │
│   • No file-descriptor behaviors (no auto-close on fork, etc.)              │
│   • Required for internal process communication                             │
│                                                                             │
│   Pulse Code:                                                               │
│   • Use custom codes (e.g., _PULSE_CODE_MINAVAIL + N) to distinguish        │
│     between multiple interrupt sources and other pulse types                │
│   • Check against PULSE_CODE_DISCONNECT for client disconnects              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.4 When to Use Event Model

| Scenario                       | Why Use Event                                           |
| ------------------------------ | ------------------------------------------------------- |
| **Single-threaded driver**     | One thread handles both hardware and clients            |
| **Multiple interrupt sources** | One thread receives pulses from multiple interrupts     |
| **Thread pool**                | Pool of threads can handle both interrupts and messages |
| **Resource manager**           | Integrate interrupt handling with message passing       |
| **Flexibility needed**         | Can combine with semaphores, signals, or other events   |

---

## 6. Interrupt Control Functions

### 6.1 InterruptMask and InterruptUnmask

These are the primary functions for controlling individual interrupts.

```c
#include <sys/neutrino.h>

/*
 * InterruptMask - Mask (disable) a specific interrupt
 * Prevents the hardware interrupt from reaching the CPU
 */
int InterruptMask(int irq, int id);

/*
 * InterruptUnmask - Unmask (enable) a specific interrupt
 * Allows the hardware interrupt to reach the CPU again
 */
int InterruptUnmask(int irq, int id);
```

**Critical Rule:** The kernel automatically **masks** the interrupt BEFORE unblocking your thread. You must call `InterruptUnmask()` AFTER acknowledging the hardware to re-enable the interrupt.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                    Interrupt Mask/Unmask Sequence                            │
│                                                                              │
│   Time ──▶                                                                   │
│                                                                              │
│   Hardware asserts IRQ                                                       │
│        │                                                                     │
│        ▼                                                                     │
│   [KERNEL] Interrupt trapped, source identified                              │
│        │                                                                     │
│        ▼                                                                     │
│   [KERNEL] Interrupt is AUTO-MASKED by kernel                                │
│        │  ← Hardware can assert again, but PIC won't forward to CPU          │
│        ▼                                                                     │
│   [KERNEL] IST unblocked (or event delivered)                                │
│        │                                                                     │
│        ▼                                                                     │
│   [DRIVER] Thread runs — reads status, handles data                          │
│        │                                                                     │
│        ▼                                                                     │
│   [DRIVER] Acknowledge hardware (write to status/ack register)               │
│        │  ← Hardware stops asserting interrupt line                          │
│        ▼                                                                     │
│   [DRIVER] InterruptUnmask(irq, id)                                          │
│        │  ← Now safe to receive next interrupt                               │
│        ▼                                                                     │
│   [DRIVER] Do additional work (optional)                                     │
│        │                                                                     │
│        ▼                                                                     │
│   [DRIVER] InterruptWait() / MsgReceive() — wait for next                    │
│                                                                              │
│   WHY auto-mask?                                                             │
│   • Prevents nested interrupts while IST is running                          │
│   • Prevents interrupt storms if hardware keeps asserting                    │
│   • Gives driver time to acknowledge before next interrupt                   │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 InterruptDisable and InterruptEnable (Rarely Used)

```c
#include <sys/neutrino.h>

/*
 * InterruptDisable - Disable ALL interrupts on the calling CPU core
 * This is a GLOBAL operation affecting the entire system on this core.
 * NOT recommended for normal driver use.
 */
void InterruptDisable(void);

/*
 * InterruptEnable - Re-enable interrupts on the calling CPU core
 * Reverses InterruptDisable().
 * NOT recommended for normal driver use.
 */
void InterruptEnable(void);
```

> **WARNING:** `InterruptDisable()`/`InterruptEnable()` are **NOT recommended** for normal driver development. They:
>
> - Disable ALL interrupts on the CPU core
> - Stop scheduling, timer interrupts, everything
> - Are a global effect that breaks real-time guarantees
> - Should only be used in very specialized low-level scenarios

### 6.3 Attach Flags

| Flag                        | Value | Description                       |
| --------------------------- | ----- | --------------------------------- |
| `_NTO_INTR_FLAGS_NO_UNMASK` | 0x01  | Don't auto-unmask at attach time  |
| `_NTO_INTR_FLAGS_CPU_LOCAL` | 0x02  | Interrupt is CPU-core-local       |
| `_NTO_INTR_FLAGS_TRK_MSK`   | 0x04  | Track mask count                  |
| `_NTO_INTR_FLAGS_END`       | 0x08  | Process at end of interrupt chain |

#### NO_UNMASK Flag

By default, when you attach to an interrupt, the kernel automatically unmasks it. Use `NO_UNMASK` if you need to do setup before handling interrupts:

```c
/* Attach but don't enable interrupts yet */
id = InterruptAttachEvent(irq, &event, _NTO_INTR_FLAGS_NO_UNMASK);

/* Do other initialization... */
initialize_device_hardware();

/* Now explicitly enable the interrupt */
InterruptUnmask(irq, id);
```

#### CPU_LOCAL Flag

For interrupts that are tied to a specific CPU core:

```c
/*
 * CPU-local interrupts require:
 * 1. _NTO_INTR_FLAGS_CPU_LOCAL flag
 * 2. Thread's runmask must be restricted to ONE core
 * 3. Thread must stay on that core for all interrupt operations
 */

/* Restrict thread to CPU core 0 */
uint64_t runmask = 0x01;  /* Only CPU 0 */
ThreadCtl(_NTO_TCTL_RUNMASK, (void *)&runmask);

/* Attach with CPU_LOCAL flag */
id = InterruptAttachThread(irq, NULL, NULL, 0, _NTO_INTR_FLAGS_CPU_LOCAL);
```

> **Important:** The thread handling a CPU-local interrupt must have its runmask set to ONLY that core, and must keep that restriction for the lifetime of the attachment. Any `InterruptUnmask()` or other operation on a different core will fail or behave incorrectly.

---

## 7. Privilege Requirements

### 7.1 Required Privileges

| Operation                 | Required Privilege               | How to Obtain                |
| ------------------------- | -------------------------------- | ---------------------------- |
| `InterruptAttachThread()` | `PROCMGR_AID_INTERRUPT`          | `procmgr_ability()`          |
| `InterruptAttachEvent()`  | `PROCMGR_AID_INTERRUPT`          | `procmgr_ability()`          |
| `InterruptDetach()`       | Must own the interrupt ID        | Automatic (from attach)      |
| `InterruptMask()`         | Must own the interrupt ID        | Automatic (from attach)      |
| `InterruptUnmask()`       | Must own the interrupt ID        | Automatic (from attach)      |
| `InterruptDisable()`      | `PROCMGR_AID_IO` (I/O privilege) | `ThreadCtl(_NTO_TCTL_IO, 0)` |
| `InterruptEnable()`       | `PROCMGR_AID_IO` (I/O privilege) | `ThreadCtl(_NTO_TCTL_IO, 0)` |

### 7.2 Setting Up Privileges

```c
#include <sys/procmgr.h>
#include <sys/neutrino.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Setup required privileges for interrupt handling.
 * Call this before any interrupt operations.
 */
int setup_interrupt_privileges(void) {
    int rc;

    /* Enable interrupt privilege */
    rc = procmgr_ability(
        0,  /* Current process */
        PROCMGR_ADN_ROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
        PROCMGR_AID_EOL  /* End of list */
    );

    if (rc == -1) {
        perror("procmgr_ability (interrupt)");
        return -1;
    }

    printf("Interrupt privilege granted\n");

    /*
     * I/O privilege is ONLY needed if using InterruptDisable/Enable.
     * Most drivers do NOT need this.
     */
    /*
    rc = procmgr_ability(
        0,
        PROCMGR_ADN_ROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_IO,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_IO,
        PROCMGR_AID_EOL
    );

    if (rc == -1) {
        perror("procmgr_ability (I/O)");
        return -1;
    }

    rc = ThreadCtl(_NTO_TCTL_IO, 0);
    if (rc == -1) {
        perror("ThreadCtl(_NTO_TCTL_IO)");
        return -1;
    }
    */

    return 0;
}
```

### 7.3 Privilege Summary

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Interrupt Privilege Requirements                         │
│                                                                             │
│   Normal Driver (recommended):                                              │
│   ───────────────────────────                                               │
│   • InterruptAttachThread / InterruptAttachEvent                            │
│     → Needs: PROCMGR_AID_INTERRUPT                                          │
│     → Set via: procmgr_ability()                                            │
│                                                                             │
│   Specialized Low-Level (rarely needed):                                    │
│   ──────────────────────────────────────                                    │
│   • InterruptDisable / InterruptEnable                                      │
│     → Needs: PROCMGR_AID_IO + ThreadCtl(_NTO_TCTL_IO)                       │
│     → Set via: procmgr_ability() + ThreadCtl()                              │
│                                                                             │
│   RECOMMENDATION:                                                           │
│   Most drivers only need PROCMGR_AID_INTERRUPT.                             │
│   Avoid InterruptDisable/Enable — they are global and break real-time.      │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 8. Notification Mechanisms Comparison

When using `InterruptAttachEvent`, you can choose different event notification types. Here's how they compare:

### 8.1 Notification Types

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Interrupt Event Notification Types                       │
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │ 1. PULSE (SIGEV_PULSE)                                              │   │
│   │    • Delivered via MsgReceive() on a channel                        │   │
│   │    • QUEUED — multiple pulses are counted                           │   │
│   │    • Carries data (code + value)                                    │   │
│   │    • Best for: Single-threaded drivers, resource managers           │   │
│   │    • Pros: Can combine HW + client handling in one loop             │   │
│   │    • Cons: Highest overhead, highest latency                        │   │
│   │                                                                     │   │
│   │    Setup: ChannelCreate() → ConnectAttach(_NTO_SIDE_CHANNEL)        │   │
│   │           → SIGEV_PULSE_INIT() → InterruptAttachEvent()             │   │
│   │           → MsgReceive() loop                                       │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│   ┌────────────────────────────────────────────────────────────────────┐    │
│   │ 2. SEMAPHORE (SIGEV_SEM)                                           │    │
│   │    • Delivered via sem_wait() / sem_trywait()                      │    │
│   │    • COUNTED — tracks multiple posts                               │    │
│   │    • Can be used across processes (unusual)                        │    │
│   │    • Best for: Thread pool architectures                           │    │
│   │    • Pros: Counted, can wake threads in other processes            │    │
│   │    • Cons: More complex setup, requires semaphore management       │    │
│   │                                                                    │    │
│   │    Setup: sem_init() → SIGEV_SEM_INIT() → InterruptAttachEvent()   │    │
│   │           → sem_wait() loop                                        │    │
│   └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│   ┌────────────────────────────────────────────────────────────────────┐    │
│   │ 3. SIGNAL (SIGEV_SIGNAL)                                           │    │
│   │    • Delivered via sigwaitinfo() / SignalWaitinfo()                │    │
│   │    • Can carry data (siginfo_t)                                    │    │
│   │    • Can be queued (real-time signals)                             │    │
│   │    • Best for: When you need both interrupt and pthread_kill       │    │
│   │    • Pros: Can combine multiple wakeup sources                     │    │
│   │    • Cons: Signal handler overhead is HIGH — never use handlers!   │    │
│   │                                                                    │    │
│   │   Setup: sigemptyset() → SIGEV_SIGNAL_INIT() → InterruptAttachEvent│    │
│   │           → sigwaitinfo() loop                                     │    │
│   │                                                                    │    │
│   │    ⚠️ NEVER use signal handlers (sigaction) for interrupts!        │    │
│   │       Overhead is awful, latency is awful, code is unsafe.         │    │
│   └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│   ┌────────────────────────────────────────────────────────────────────┐    │
│   │ 4. THREAD NOTIFICATION (SIGEV_THREAD)                              │    │
│   │    • Creates a new thread for each interrupt                       │    │
│   │    • Best for: Very simple handlers                                │    │
│   │    • Pros: Automatic threading                                     │    │
│   │    • Cons: Thread creation overhead per interrupt                  │    │
│   └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 8.2 Performance Comparison

| Mechanism                       | Latency | Overhead | Queued | Best For                                        |
| ------------------------------- | ------- | -------- | ------ | ----------------------------------------------- |
| **IST (InterruptAttachThread)** | Lowest  | Lowest   | No     | Real-time, dedicated interrupt handling         |
| **Pulse (SIGEV_PULSE)**         | High    | High     | Yes    | Single-threaded drivers, resource managers      |
| **Semaphore (SIGEV_SEM)**       | Medium  | Medium   | Yes    | Thread pools, cross-process notification        |
| **Signal (SIGEV_SIGNAL)**       | Medium  | Medium   | Yes    | Multiple wakeup sources (use sigwaitinfo)       |
| **Thread (SIGEV_THREAD)**       | High    | Highest  | No     | Simple handlers (not recommended for high rate) |

### 8.3 Decision Flow

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                    Choosing Your Interrupt Handling Approach                 │
│                                                                              │
│   START: Need to handle hardware interrupts                                  │
│        │                                                                     │
│        ├─ Can you dedicate a thread?                                         │
│        │  ├─ YES → Use InterruptAttachThread (lowest latency)                │
│        │  └─ NO  → Continue...                                               │
│        │                                                                     │
│        ├─ Need to handle clients in same thread?                             │
│        │  ├─ YES → Use InterruptAttachEvent + PULSE                          │
│        │  └─ NO  → Continue...                                               │
│        │                                                                     │
│        ├─ Have a thread pool?                                                │
│        │  ├─ YES → Use InterruptAttachEvent + SEMAPHORE                      │
│        │  └─ NO  → Continue...                                               │
│        │                                                                     │
│        ├─ Need multiple wakeup sources (signals + interrupts)?               │
│        │  ├─ YES → Use InterruptAttachEvent + SIGNAL (sigwaitinfo)           │
│        │  └─ NO  → Use InterruptAttachEvent + PULSE (default)                │
│        │                                                                     │
│        └─ High interrupt rate, real-time critical?                           │
           ├─ YES → Reconsider: can you dedicate a thread?                     │
           └─ NO  → PULSE is fine                                              │
│                                                                              │
│   GENERAL RULE:                                                              │
│   • Use IST (InterruptAttachThread) for real-time, low-latency requirements  │
│   • Use PULSE (InterruptAttachEvent) for flexibility and single-threaded     │
│     driver designs                                                           │
│   • Avoid signal handlers completely                                         │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Complete Code Examples

### 9.1 Example 1: Dedicated IST Driver

```c
/* dedicated_ist_driver.c - Driver with dedicated interrupt service thread */
#include <sys/neutrino.h>
#include <sys/procmgr.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEVICE_IRQ          0x10    /* Example IRQ number */
#define DEVICE_BASE_ADDR    0xFE000000

/* Simulated device registers */
volatile uint32_t *device_regs;
#define REG_STATUS   0
#define REG_DATA     1
#define REG_CONTROL  2
#define TX_COMPLETE  0x01
#define RX_AVAILABLE 0x02

/* Forward declarations */
void *ist_thread(void *arg);
void *server_thread(void *arg);
void handle_client_message(int rcvid);

int main(int argc, char *argv[]) {
    pthread_t ist_tid, server_tid;
    int irq = DEVICE_IRQ;

    /* Setup privileges */
    if (procmgr_ability(0,
            PROCMGR_ADN_ROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
            PROCMGR_ADN_NONROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
            PROCMGR_AID_EOL) == -1) {
        perror("procmgr_ability");
        return EXIT_FAILURE;
    }

    /* Map device registers */
    device_regs = mmap(NULL, 0x1000,
                        PROT_READ | PROT_WRITE | PROT_NOCACHE,
                        MAP_PHYS | MAP_SHARED, NOFD, DEVICE_BASE_ADDR);
    if (device_regs == MAP_FAILED) {
        perror("mmap device");
        return EXIT_FAILURE;
    }

    /* Create IST thread */
    pthread_create(&ist_tid, NULL, ist_thread, &irq);

    /* Create server thread for client messages */
    pthread_create(&server_tid, NULL, server_thread, NULL);

    /* Wait for threads */
    pthread_join(ist_tid, NULL);
    pthread_join(server_tid, NULL);

    munmap((void *)device_regs, 0x1000);
    return EXIT_SUCCESS;
}

/*
 * IST Thread: Dedicated to handling interrupts
 */
void *ist_thread(void *arg) {
    int irq = *(int *)arg;
    int id;
    int rc;

    /* Attach this thread as the IST */
    id = InterruptAttachThread(irq, NULL, NULL, 0, 0);
    if (id == -1) {
        perror("InterruptAttachThread");
        return NULL;
    }

    printf("[IST] Attached to IRQ 0x%x, ID=%d\n", irq, id);

    for (;;) {
        /* Wait for interrupt */
        rc = InterruptWait(0, NULL);
        if (rc == -1) {
            perror("InterruptWait");
            break;
        }

        /* Read and handle device status */
        uint32_t status = device_regs[REG_STATUS];

        if (status & TX_COMPLETE) {
            printf("[IST] TX complete\n");
            /* Notify server thread if needed */
        }

        if (status & RX_AVAILABLE) {
            uint32_t data = device_regs[REG_DATA];
            printf("[IST] RX data: 0x%x\n", data);
            /* Process received data */
        }

        /* Acknowledge interrupt */
        device_regs[REG_STATUS] = status;  /* Write-back clears */

        /* Unmask to allow next interrupt */
        InterruptUnmask(irq, id);
    }

    InterruptDetach(id);
    return NULL;
}

/*
 * Server Thread: Handles client messages
 */
void *server_thread(void *arg) {
    int chid;
    int rcvid;
    char msg[256];

    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        return NULL;
    }

    printf("[SERVER] Ready on channel %d\n", chid);

    for (;;) {
        rcvid = MsgReceive(chid, msg, sizeof(msg), NULL);
        if (rcvid == -1) {
            perror("MsgReceive");
            break;
        }

        if (rcvid == 0) {
            /* Pulse — not expected in this design */
            continue;
        }

        /* Handle client request */
        handle_client_message(rcvid);
    }

    ChannelDestroy(chid);
    return NULL;
}

void handle_client_message(int rcvid) {
    /* Process client request and reply */
    MsgReply(rcvid, 0, NULL, 0);
}
```

### 9.2 Example 2: Single-Threaded Driver with Pulse

```c
/* single_threaded_driver.c - One thread handles everything */
#include <sys/neutrino.h>
#include <sys/procmgr.h>
#include <sys/siginfo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICE_IRQ              0x10
#define DEVICE_BASE_ADDR        0xFE000000
#define PULSE_CODE_HW_IRQ       (_PULSE_CODE_MINAVAIL + 0)
#define PULSE_CODE_DISCONNECT   _PULSE_CODE_DISCONNECT

volatile uint32_t *device_regs;
#define REG_STATUS   0
#define REG_DATA     1

int setup_hardware(void);
void handle_interrupt(void);
void handle_client_request(int rcvid);

int main(int argc, char *argv[]) {
    int chid, coid, id;
    struct sigevent event;
    struct _pulse pulse;
    int rc;
    int irq = DEVICE_IRQ;

    /* Setup privileges */
    if (procmgr_ability(0,
            PROCMGR_ADN_ROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
            PROCMGR_ADN_NONROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
            PROCMGR_AID_EOL) == -1) {
        perror("procmgr_ability");
        return EXIT_FAILURE;
    }

    /* Setup hardware */
    if (setup_hardware() != 0) {
        return EXIT_FAILURE;
    }

    /* Create channel */
    chid = ChannelCreate(0);
    if (chid == -1) {
        perror("ChannelCreate");
        return EXIT_FAILURE;
    }

    /* Connect to self for pulse delivery */
    coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) {
        perror("ConnectAttach");
        ChannelDestroy(chid);
        return EXIT_FAILURE;
    }

    /* Setup pulse event */
    SIGEV_PULSE_INIT(&event, coid,
                     SIGEV_PULSE_PRIO_INHERIT,
                     PULSE_CODE_HW_IRQ, 0);

    /* Attach interrupt with event */
    id = InterruptAttachEvent(irq, &event, 0);
    if (id == -1) {
        perror("InterruptAttachEvent");
        ConnectDetach(coid);
        ChannelDestroy(chid);
        return EXIT_FAILURE;
    }

    printf("[DRIVER] Single-threaded driver ready. IRQ=0x%x, ID=%d\n", irq, id);

    /* Main loop: handles BOTH interrupts and client messages */
    for (;;) {
        rc = MsgReceive(chid, &pulse, sizeof(pulse), NULL);
        if (rc == -1) {
            perror("MsgReceive");
            break;
        }

        if (rc == 0) {
            /* === PULSE === */
            switch (pulse.code) {
                case PULSE_CODE_HW_IRQ:
                    /* Hardware interrupt */
                    handle_interrupt();
                    InterruptUnmask(irq, id);
                    break;

                case PULSE_CODE_DISCONNECT:
                    printf("[DRIVER] Client disconnected\n");
                    break;

                default:
                    printf("[DRIVER] Unknown pulse code: %d\n", pulse.code);
                    break;
            }
        } else {
            /* === CLIENT MESSAGE === */
            handle_client_request(rc);
        }
    }

    /* Cleanup */
    InterruptDetach(id);
    ConnectDetach(coid);
    ChannelDestroy(chid);
    munmap((void *)device_regs, 0x1000);

    return EXIT_SUCCESS;
}

int setup_hardware(void) {
    device_regs = mmap(NULL, 0x1000,
                       PROT_READ | PROT_WRITE | PROT_NOCACHE,
                       MAP_PHYS | MAP_SHARED, NOFD, DEVICE_BASE_ADDR);
    if (device_regs == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    return 0;
}

void handle_interrupt(void) {
    uint32_t status = device_regs[REG_STATUS];

    if (status & 0x01) {
        printf("[IRQ] TX complete\n");
    }
    if (status & 0x02) {
        printf("[IRQ] RX available: 0x%x\n", device_regs[REG_DATA]);
    }

    /* Acknowledge */
    device_regs[REG_STATUS] = status;
}

void handle_client_request(int rcvid) {
    /* Read message, process, reply */
    char msg[256];
    int nbytes;

    nbytes = MsgRead(rcvid, msg, sizeof(msg), 0);
    if (nbytes > 0) {
        printf("[CLIENT] Received request: %s\n", msg);
    }

    MsgReply(rcvid, 0, "OK", 3);
}
```

### 9.3 Example 3: IST with Event Delivery to Another Thread

```c
/* ist_with_notification.c - IST handles interrupt, notifies worker thread */
#include <sys/neutrino.h>
#include <sys/procmgr.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DEVICE_IRQ      0x10
#define WORKER_PRIORITY 15

/* Shared data between IST and worker */
volatile int work_available = 0;
pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;

void *ist_thread(void *arg);
void *worker_thread(void *arg);

int main(void) {
    pthread_t ist_tid, worker_tid;
    int irq = DEVICE_IRQ;

    /* Setup privileges */
    procmgr_ability(0,
        PROCMGR_ADN_ROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
        PROCMGR_AID_EOL);

    pthread_create(&ist_tid, NULL, ist_thread, &irq);
    pthread_create(&worker_tid, NULL, worker_thread, NULL);

    pthread_join(ist_tid, NULL);
    pthread_join(worker_tid, NULL);

    return EXIT_SUCCESS;
}

void *ist_thread(void *arg) {
    int irq = *(int *)arg;
    int id;

    /* Set high priority for IST */
    struct sched_param param;
    param.sched_priority = 255;  /* Very high priority */
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    id = InterruptAttachThread(irq, NULL, NULL, 0, 0);
    if (id == -1) {
        perror("InterruptAttachThread");
        return NULL;
    }

    printf("[IST] Running at high priority, waiting for IRQ 0x%x\n", irq);

    for (;;) {
        InterruptWait(0, NULL);

        /* Minimal work: just signal that work is available */
        pthread_mutex_lock(&work_mutex);
        work_available = 1;
        pthread_cond_signal(&work_cond);
        pthread_mutex_unlock(&work_mutex);

        /* Acknowledge hardware */
        /* ... write to device ack register ... */

        InterruptUnmask(irq, id);
    }

    InterruptDetach(id);
    return NULL;
}

void *worker_thread(void *arg) {
    struct sched_param param;
    param.sched_priority = WORKER_PRIORITY;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    printf("[WORKER] Running at priority %d\n", WORKER_PRIORITY);

    for (;;) {
        pthread_mutex_lock(&work_mutex);
        while (!work_available) {
            pthread_cond_wait(&work_cond, &work_mutex);
        }
        work_available = 0;
        pthread_mutex_unlock(&work_mutex);

        /* Do the heavy processing here */
        printf("[WORKER] Processing interrupt work...\n");
        /* ... process data, notify clients, etc. ... */
    }

    return NULL;
}
```

### 9.4 Example 4: NO_UNMASK Flag Usage

```c
/* no_unmask_example.c - Delay interrupt enable until ready */
#include <sys/neutrino.h>
#include <sys/procmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define DEVICE_IRQ  0x10

int main(void) {
    int id;
    int rc;

    /* Setup privileges */
    procmgr_ability(0,
        PROCMGR_ADN_ROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_INTERRUPT,
        PROCMGR_AID_EOL);

    /* Attach with NO_UNMASK — interrupt stays masked */
    id = InterruptAttachThread(DEVICE_IRQ, NULL, NULL, 0,
                               _NTO_INTR_FLAGS_NO_UNMASK);
    if (id == -1) {
        perror("InterruptAttachThread");
        return EXIT_FAILURE;
    }

    printf("Attached with NO_UNMASK. Interrupt is still masked.\n");

    /* Do initialization that must complete before interrupts fire */
    printf("Initializing device hardware...\n");
    sleep(2);  /* Simulated init */
    printf("Device initialized.\n");

    /* Now safe to enable interrupts */
    printf("Enabling interrupts...\n");
    InterruptUnmask(DEVICE_IRQ, id);
    printf("Interrupts enabled. Waiting...\n");

    /* Handle interrupts */
    for (int i = 0; i < 5; i++) {
        rc = InterruptWait(0, NULL);
        if (rc == -1) {
            perror("InterruptWait");
            break;
        }
        printf("Interrupt %d received\n", i + 1);

        /* Acknowledge hardware */
        /* ... */

        InterruptUnmask(DEVICE_IRQ, id);
    }

    InterruptDetach(id);
    return EXIT_SUCCESS;
}
```

---

## 10. Quick Reference

### 10.1 Function Reference

| Function                  | Header             | Purpose                              |
| ------------------------- | ------------------ | ------------------------------------ |
| `InterruptAttachThread()` | `<sys/neutrino.h>` | Thread becomes IST                   |
| `InterruptAttachEvent()`  | `<sys/neutrino.h>` | Kernel IST delivers event            |
| `InterruptDetach()`       | `<sys/neutrino.h>` | Remove handler                       |
| `InterruptWait()`         | `<sys/neutrino.h>` | IST waits for interrupt              |
| `InterruptMask()`         | `<sys/neutrino.h>` | Mask specific interrupt              |
| `InterruptUnmask()`       | `<sys/neutrino.h>` | Unmask specific interrupt            |
| `InterruptDisable()`      | `<sys/neutrino.h>` | Disable ALL interrupts (rarely used) |
| `InterruptEnable()`       | `<sys/neutrino.h>` | Enable ALL interrupts (rarely used)  |
| `SIGEV_PULSE_INIT()`      | `<sys/siginfo.h>`  | Initialize pulse event               |
| `SIGEV_SEM_INIT()`        | `<sys/siginfo.h>`  | Initialize semaphore event           |
| `SIGEV_SIGNAL_INIT()`     | `<sys/siginfo.h>`  | Initialize signal event              |
| `ChannelCreate()`         | `<sys/neutrino.h>` | Create channel for pulses            |
| `ConnectAttach()`         | `<sys/neutrino.h>` | Create connection for pulse delivery |
| `procmgr_ability()`       | `<sys/procmgr.h>`  | Grant process abilities              |
| `ThreadCtl()`             | `<sys/neutrino.h>` | Thread control (I/O privilege)       |

### 10.2 Interrupt Flags

| Flag                        | Hex  | Description                 |
| --------------------------- | ---- | --------------------------- |
| `_NTO_INTR_FLAGS_NO_UNMASK` | 0x01 | Don't auto-unmask at attach |
| `_NTO_INTR_FLAGS_CPU_LOCAL` | 0x02 | CPU-core-local interrupt    |
| `_NTO_INTR_FLAGS_TRK_MSK`   | 0x04 | Track mask count            |
| `_NTO_INTR_FLAGS_END`       | 0x08 | End of interrupt chain      |

### 10.3 Common Pitfalls

| Pitfall                         | Solution                                                     |
| ------------------------------- | ------------------------------------------------------------ |
| Forgetting `InterruptUnmask()`  | Always unmask after acknowledging hardware                   |
| Missing privileges              | Call `procmgr_ability(PROCMGR_AID_INTERRUPT)` before attach  |
| Using signal handlers           | Use `sigwaitinfo()`, never `sigaction()` for interrupts      |
| Not acknowledging hardware      | Write to device ack register before `InterruptUnmask()`      |
| Wrong priority for IST          | Set high FIFO priority for time-critical interrupts          |
| CPU-local without runmask       | Restrict thread to one core with `ThreadCtl(_NTO_TCTL_RUNMASK)` |
| Using `InterruptDisable/Enable` | Avoid — they are global and break real-time                  |
| Forgetting `InterruptDetach()`  | Always detach before exit to free resources                  |

### 10.4 Best Practices

| Practice                          | Why                                                          |
| --------------------------------- | ------------------------------------------------------------ |
| Keep IST work minimal             | Do only what's needed to acknowledge hardware; defer heavy work |
| Use `InterruptUnmask()` after ack | Prevents interrupt storms and nested handling                |
| Set appropriate IST priority      | Use `SCHED_FIFO` with high priority for time-critical devices |
| Consider `NO_UNMASK` for init     | Attach early, enable interrupts only after setup complete    |
| Use pulses for flexibility        | Single-threaded drivers can handle both HW and clients       |
| Never use signal handlers         | Overhead and latency are unacceptable for interrupts         |
| Always check return values        | `InterruptAttach*()` can fail for many reasons               |

---

## Related Documentation

- **QNX Hardware I/O Programming Guide** — Memory mapping, DMA, register access
- **QNX PCI Device Programming Guide** — PCI device enumeration and configuration
- **QNX IPC Method Selection Guide** — Choosing IPC mechanisms for driver design
- **QNX Official:** [Interrupts](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.prog/topic/interrupts.html)
- **QNX Official:** [InterruptAttachThread()](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/i/interruptattachthread.html)
- **QNX Official:** [InterruptAttachEvent()](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/i/interruptattachevent.html)
- **QNX Official:** [procmgr_ability()](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/p/procmgr_ability.html)

