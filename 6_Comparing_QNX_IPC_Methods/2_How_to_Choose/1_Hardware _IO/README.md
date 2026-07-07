# QNX Neutrino Hardware I/O Programming Guide

> **Companion to:** QNX IPC Method Selection Guide, QNX IPC Methods Detailed Comparison

This document provides a comprehensive guide to hardware I/O programming in QNX Neutrino. It covers memory-mapped I/O, DMA buffer allocation, control register access on AArch64 and x86 platforms, and the role of `procnto` and system pages in hardware abstraction.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Memory-Mapped I/O with `mmap()`](#2-memory-mapped-io-with-mmap)
3. [DMA Buffer Allocation](#3-dma-buffer-allocation)
4. [Getting Physical Addresses](#4-getting-physical-addresses)
5. [Accessing Control Registers](#5-accessing-control-registers)
6. [x86 I/O Port Access](#6-x86-io-port-access)
7. [How `procnto` Knows About Hardware](#7-how-procnto-knows-about-hardware)
8. [Complete Code Examples](#8-complete-code-examples)
9. [Quick Reference](#9-quick-reference)

---

## 1. Overview

In QNX Neutrino, processes run in virtual address space. To interact with hardware devices, you must map physical memory (device registers, frame buffers, DMA regions) into your process's virtual address space. The kernel (`procnto`) manages this mapping using information provided by the startup code via **system pages**.

```
┌─────────────────────────────────────────────────────────────┐
│                    Hardware I/O Access Flow                 │
│                                                             │
│   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│   │   Startup    │───▶│  System Page │───▶│   procnto    │  │
│   │   Program    │    │  (hardware   │    │  (kernel)    │  │
│   │              │    │   info)      │    │              │  │
│   └──────────────┘    └──────────────┘    └──────┬───────┘  │
│                                                  │          │
│                                                  ▼          │
│   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│   │   Process    │◀───│    mmap()    │◀───│  Page Table  │  │
│   │  (virtual    │    │              │    │   Mapping    │  │
│   │   address)   │    └──────────────┘    └──────────────┘  │ 
│   └──────────────┘                                          │
│                                                             │
│   Access methods:                                           │
│   • Memory-mapped I/O (AArch64, most modern platforms)      │
│   • I/O port instructions (x86 legacy)                      │
│   • DMA with physically contiguous memory                   │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Memory-Mapped I/O with `mmap()`

To access memory on a hardware device (e.g., video frame buffers, device registers), you map the physical address into your process's virtual address space using `mmap()`.

### 2.1 Mapping Device Memory

```c
#include <sys/mman.h>
#include <stdint.h>

/* Map video frame buffer at physical address 0xA0000 */
void *map_device_memory(void) {
    void *addr;
    size_t len = 65536;  /* 64 KB */

    addr = mmap(
        NULL,                          /* Let kernel choose virtual address */
        len,                           /* Size to map */
        PROT_READ | PROT_WRITE,        /* Read/Write access */
        MAP_PHYS | MAP_SHARED,         /* Physical memory, shared mapping */
        NOFD,                          /* No file descriptor for physical memory */
        0xA0000                        /* Physical address offset */
    );

    if (addr == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    return addr;
}
```

### 2.2 Key Flags Explained

| Flag           | Purpose                                     | When to Use                       |
| -------------- | ------------------------------------------- | --------------------------------- |
| `MAP_PHYS`     | Indicates physical memory mapping           | Always for hardware access        |
| `MAP_SHARED`   | Shared mapping (visible to other processes) | Device memory, frame buffers      |
| `MAP_ANON`     | Anonymous allocation (no backing file)      | DMA buffer allocation             |
| `PROT_NOCACHE` | Disable CPU caching                         | Device registers, uncached memory |
| `NOFD`         | No file descriptor required                 | With `MAP_PHYS` or `MAP_ANON`     |

### 2.3 Protection Bits

| Protection     | Description                                     |
| -------------- | ----------------------------------------------- |
| `PROT_READ`    | Read access allowed                             |
| `PROT_WRITE`   | Write access allowed                            |
| `PROT_EXEC`    | Execute access allowed                          |
| `PROT_NOCACHE` | Disable caching (critical for device registers) |

> **Note:** For device registers, always use `PROT_NOCACHE` to prevent the CPU from caching stale values.

---

## 3. DMA Buffer Allocation

DMA (Direct Memory Access) operations require **physically contiguous memory**. In QNX, you allocate this using `mmap()` with `MAP_PHYS | MAP_ANON`.

### 3.1 Allocating a DMA Buffer

```c
#include <sys/mman.h>
#include <stdint.h>

/* Allocate 256 KB of physically contiguous memory for DMA */
void *allocate_dma_buffer(size_t *allocated_size) {
    void *addr;
    size_t len = 262144;  /* 256 KB */

    addr = mmap(
        NULL,
        len,
        PROT_READ | PROT_WRITE | PROT_NOCACHE,  /* NOCACHE for DMA consistency */
        MAP_PHYS | MAP_ANON,                      /* Physical + Anonymous */
        NOFD,
        0                                         /* Offset = 0 for new allocation */
    );

    if (addr == MAP_FAILED) {
        perror("mmap DMA buffer");
        return NULL;
    }

    *allocated_size = len;
    return addr;
}
```

### 3.2 DMA Buffer Constraints

| Constraint                 | Flag                   | Description                       |
| -------------------------- | ---------------------- | --------------------------------- |
| Below 16 MB                | `MAP_BELOW16M`         | Required for legacy ISA DMA       |
| No 64 KB boundary crossing | `MAP_NOX64K`           | Required for some DMA controllers |
| Physically contiguous      | `MAP_PHYS \| MAP_ANON` | Essential for DMA operations      |

```c
/* Example: ISA DMA buffer below 16 MB and not crossing 64K boundary */
void *allocate_isa_dma_buffer(size_t len) {
    return mmap(
        NULL,
        len,
        PROT_READ | PROT_WRITE | PROT_NOCACHE,
        MAP_PHYS | MAP_ANON | MAP_BELOW16M | MAP_NOX64K,
        NOFD,
        0
    );
}
```

---

## 4. Getting Physical Addresses

After allocating memory (especially DMA buffers), you often need the **physical address** to program into hardware registers.

### 4.1 Using `mem_offset()` / `mem_offset64()`

```c
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>

/* Get physical address from virtual pointer */
int get_physical_address(void *vaddr, size_t len, uint64_t *paddr) {
    off_t offset;
    int fd;
    size_t contig_len;

    /* mem_offset64 for 64-bit physical addresses */
    if (mem_offset64(vaddr, NOFD, len, &offset, &contig_len) == -1) {
        perror("mem_offset64");
        return -1;
    }

    *paddr = (uint64_t)offset;
    printf("Virtual: %p -> Physical: 0x%llx (contiguous: %zu bytes)\n",
           vaddr, (unsigned long long)*paddr, contig_len);
    return 0;
}
```

### 4.2 Physical Address Flow

```
┌──────────────────────────────────────────────────────────────┐
│              Virtual to Physical Address Resolution          │
│                                                              │
│   Process Virtual Address                                    │
│        │                                                     │
│        ▼                                                     │
│   ┌─────────────┐                                            │
│   │  mem_offset │───▶ Physical Offset (from mmap)            │
│   │   (or       │                                            │
│   │mem_offset64)|                                            │
│   └─────────────┘                                            │
│        │                                                     │
│        ▼                                                     │
│   Physical Address                                           │
│        │                                                     │
│        ▼                                                     │
│   Program into DMA controller / device register              │
│                                                              │
│   Example:                                                   │
│   • Virtual ptr from mmap(MAP_PHYS|MAP_ANON)                 │
│   • Call mem_offset64() to get physical address              │
│   • Write physical address to device's DMA register          │
└──────────────────────────────────────────────────────────────┘
```

---

## 5. Accessing Control Registers

### 5.1 AArch64 (ARM 64-bit) — Memory-Mapped Registers

On AArch64 platforms, hardware registers are mapped into memory just like RAM. You access them using pointer dereferences with **`volatile`** to prevent compiler optimization.

```c
#include <stdint.h>
#include <sys/mman.h>

/* Example: Map and access a device register on AArch64 */
#define DEVICE_BASE_PHYS    0xFE200000   /* Example: Raspberry Pi GPIO */
#define REGISTER_OFFSET     0x00

volatile uint32_t *map_device_registers(uint64_t phys_base, size_t size) {
    void *vaddr = mmap(
        NULL,
        size,
        PROT_READ | PROT_WRITE | PROT_NOCACHE,
        MAP_PHYS | MAP_SHARED,
        NOFD,
        phys_base
    );

    if (vaddr == MAP_FAILED) {
        perror("mmap device registers");
        return NULL;
    }

    /* Cast to volatile pointer to prevent optimization */
    return (volatile uint32_t *)vaddr;
}

/* Read a 32-bit register */
uint32_t read_reg32(volatile uint32_t *base, uint32_t offset) {
    return base[offset / sizeof(uint32_t)];
}

/* Write a 32-bit register */
void write_reg32(volatile uint32_t *base, uint32_t offset, uint32_t value) {
    base[offset / sizeof(uint32_t)] = value;
}
```

### 5.2 Why `volatile` is Critical

```c
/* WITHOUT volatile - Compiler may optimize away reads! */
uint32_t status = *reg;          /* Read once */
while (status & 0x01) {          /* Compiler may cache 'status' */
    /* Infinite loop if hardware changes the register */
}

/* WITH volatile - Each read goes to hardware */
volatile uint32_t *reg = ...;
while (*reg & 0x01) {            /* Fresh read every iteration */
    /* Correctly polls hardware */
}
```

| Without `volatile`                         | With `volatile`                      |
| ------------------------------------------ | ------------------------------------ |
| Compiler caches register value             | Every access hits hardware           |
| May miss status changes                    | Sees real-time hardware state        |
| Optimization removes "redundant" reads     | Preserves all read/write operations  |
| **Bug:** Infinite loops, missed interrupts | **Correct:** Proper hardware polling |

---

## 6. x86 I/O Port Access

Some x86 devices still use a separate **I/O address space** with special `IN`/`OUT` instructions, distinct from memory-mapped I/O.

### 6.1 Requesting I/O Privileges

Before accessing I/O ports, your thread must be granted I/O privilege level:

```c
#include <sys/neutrino.h>
#include <stdio.h>

/* Request I/O privileges for the calling thread */
int request_io_privileges(void) {
    /* Method 1: Legacy (still widely used) */
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("ThreadCtl _NTO_TCTL_IO");
        return -1;
    }

    /* Method 2: Modern - specify I/O level (QNX 7.0+) */
    if (ThreadCtl(_NTO_TCTL_IO_LEVEL, (void *)_NTO_IO_LEVEL_1) == -1) {
        perror("ThreadCtl _NTO_TCTL_IO_LEVEL");
        return -1;
    }

    printf("I/O privileges granted\n");
    return 0;
}
```

### 6.2 I/O Port Access Functions

QNX provides portable wrapper functions in `<hw/inout.h>`:

```c
#include <hw/inout.h>
#include <stdint.h>

/* Read operations */
uint8_t  in8(uintptr_t port);    /* Read 8 bits  from port */
uint16_t in16(uintptr_t port);   /* Read 16 bits from port */
uint32_t in32(uintptr_t port);   /* Read 32 bits from port */

/* Write operations */
void out8(uintptr_t port, uint8_t val);     /* Write 8 bits  to port */
void out16(uintptr_t port, uint16_t val);   /* Write 16 bits to port */
void out32(uintptr_t port, uint32_t val);   /* Write 32 bits to port */

/* String/block operations */
void in8s(uintptr_t port, uint8_t *buff, size_t count);
void out8s(uintptr_t port, const uint8_t *buff, size_t count);
```

### 6.3 Complete x86 I/O Port Example

```c
#include <stdio.h>
#include <stdlib.h>
#include <hw/inout.h>
#include <sys/neutrino.h>

#define SERIAL_PORT_BASE    0x3F8   /* COM1 base address */

int main(int argc, char *argv[]) {
    uint8_t lsr;  /* Line Status Register */

    /* Step 1: Request I/O privileges */
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("ThreadCtl");
        return EXIT_FAILURE;
    }

    /* Step 2: Read Line Status Register (offset 5) */
    lsr = in8(SERIAL_PORT_BASE + 5);
    printf("Line Status Register: 0x%02X\n", lsr);

    /* Step 3: Check if transmitter holding register is empty */
    if (lsr & 0x20) {
        printf("Transmitter ready - can send data\n");

        /* Step 4: Write data to transmit buffer (offset 0) */
        out8(SERIAL_PORT_BASE + 0, 'A');
        printf("Sent 'A' to serial port\n");
    }

    return EXIT_SUCCESS;
}
```

### 6.4 x86 I/O Port Access Flow

```
┌──────────────────────────────────────────────────────────────┐
│              x86 I/O Port Access Architecture                │
│                                                              │
│   CPU                                                        │
│    │                                                         │
│    │  Memory Address Space        I/O Address Space          │
│    │  ┌──────────────────┐        ┌──────────────────┐       │
│    │  │   RAM / ROM      │        │ Device Registers │       │
│    │  │   0x0000-0xFFFF  │        │   0x0000-0xFFFF  │       │
│    │  └──────────────────┘        └──────────────────┘       │
│    │         ▲                           ▲                   │
│    └─────────┼───────────────────────────┘                   │
│              │                                               │
│         MOV instructions              IN/OUT instructions    │
│         (memory-mapped)             (port-mapped)            │
│                                                              │
│   AArch64: Only memory-mapped I/O (no separate I/O space)    │
│   x86:     Both memory-mapped AND port-mapped I/O            │
└──────────────────────────────────────────────────────────────┘
```

---

## 7. How `procnto` Knows About Hardware

### 7.1 The System Page

The **startup program** (e.g., `startup-bios`, `startup-xxx`) runs before the kernel and discovers hardware information:

- CPU type and features
- Memory layout (RAM regions, reserved areas)
- Interrupt controllers
- Timer hardware
- baseCache configuration
- I/O device locations

This information is stored in the **system page** — a data structure that `procnto` reads during initialization.

```
┌──────────────────────────────────────────────────────────────┐
│              QNX Boot Sequence & Hardware Discovery          │
│                                                              │
│   1. IPL (Initial Program Loader)                            │
│      └─▶ Loads startup program from boot media               │
│                                                              │
│   2. Startup Program                                         │
│      ├─▶ Probes hardware (CPU, memory, interrupts)           │
│      ├─▶ Configures MMU page tables                          │
│      ├─▶ Creates SYSTEM PAGE with hardware info              │
│      └─▶ Starts procnto (microkernel)                        │
│                                                              │
│   3. procnto (Microkernel)                                   │
│      ├─▶ Reads system page for hardware configuration        │
│      ├─▶ Sets up kernel data structures                      │
│      ├─▶ Configures memory management                        │
│      └─▶ Starts processes (drivers, applications)            │
│                                                              │
│   System Page Contents:                                      │
│   • syspage_entry::system_private  - Kernel private data     │
│   • syspage_entry::asinfo         - Address space info       │
│   • syspage_entry::qtime          - Timer info               │
│   • syspage_entry::intrinfo       - Interrupt controller info│
│   • syspage_entry::cacheattr      - Cache attributes         │
│   • syspage_entry::cpuinfo        - CPU information          │
│   • syspage_entry::spare          - Platform-specific        │
└──────────────────────────────────────────────────────────────┘
```

### 7.2 Accessing System Page Information

```c
#include <sys/syspage.h>
#include <stdio.h>

void print_system_info(void) {
    struct syspage_entry *sp = _syspage_ptr;

    printf("System Page Info:\n");
    printf("  Num CPUs: %d\n", sp->num_cpu);
    printf("  Total RAM: %llu bytes\n",
           (unsigned long long)sp->system_private.x86.pg_size);

    /* Timer information */
    printf("  Timer interrupt: %d\n", sp->qtime.intr);
    printf("  Clock cycles per second: %llu\n",
           (unsigned long long)sp->qtime.cycles_per_sec);
}
```

### 7.3 Why procnto Needs This

`procnto` uses the system page to:

| Purpose                    | System Page Section           |
| -------------------------- | ----------------------------- |
| Set up memory management   | `asinfo` (address space info) |
| Configure timers           | `qtime`                       |
| Route interrupts           | `intrinfo`                    |
| Set cache policies         | `cacheattr`                   |
| Know about non-RAM regions | `asinfo` with different flags |
| Hardware access flags      | Platform-specific entries     |

> **Key Point:** The startup code tells `procnto` which memory regions are RAM and which are hardware devices, so the kernel can set appropriate access flags (cached vs. uncached, etc.).

---

## 8. Complete Code Examples

### 8.1 Example 1: Frame Buffer Mapping (Video Memory)

```c
/* frame_buffer.c - Map VGA frame buffer */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>

#define VGA_PHYS_ADDR   0xA0000
#define VGA_SIZE        0x10000   /* 64 KB */

int main(void) {
    uint8_t *fb;

    /* Map VGA frame buffer */
    fb = mmap(NULL, VGA_SIZE,
              PROT_READ | PROT_WRITE,
              MAP_PHYS | MAP_SHARED,
              NOFD, VGA_PHYS_ADDR);

    if (fb == MAP_FAILED) {
        perror("mmap VGA");
        return EXIT_FAILURE;
    }

    /* Write a pixel (example: mode 13h, 320x200, 256 colors) */
    fb[0] = 15;   /* White pixel at (0,0) */
    fb[1] = 4;    /* Red pixel at (1,0) */

    printf("Frame buffer mapped at %p\n", (void *)fb);

    munmap(fb, VGA_SIZE);
    return EXIT_SUCCESS;
}
```

### 8.2 Example 2: DMA Buffer with Physical Address

```c
/* dma_buffer.c - Allocate DMA buffer and get physical address */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>

#define DMA_BUFFER_SIZE  (256 * 1024)  /* 256 KB */

int main(void) {
    void *vaddr;
    uint64_t paddr;
    off_t offset;
    size_t contig_len;

    /* Allocate physically contiguous DMA buffer */
    vaddr = mmap(NULL, DMA_BUFFER_SIZE,
                 PROT_READ | PROT_WRITE | PROT_NOCACHE,
                 MAP_PHYS | MAP_ANON,
                 NOFD, 0);

    if (vaddr == MAP_FAILED) {
        perror("mmap DMA");
        return EXIT_FAILURE;
    }

    /* Get physical address */
    if (mem_offset64(vaddr, NOFD, DMA_BUFFER_SIZE,
                      &offset, &contig_len) == -1) {
        perror("mem_offset64");
        munmap(vaddr, DMA_BUFFER_SIZE);
        return EXIT_FAILURE;
    }

    paddr = (uint64_t)offset;

    printf("DMA Buffer:\n");
    printf("  Virtual:  %p\n", vaddr);
    printf("  Physical: 0x%016llX\n", (unsigned long long)paddr);
    printf("  Size:     %zu bytes\n", DMA_BUFFER_SIZE);
    printf("  Contiguous: %zu bytes\n", contig_len);

    /* Program this physical address into your DMA controller */
    /* write_dma_register(DMA_ADDR_REG, paddr); */

    munmap(vaddr, DMA_BUFFER_SIZE);
    return EXIT_SUCCESS;
}
```

### 8.3 Example 3: AArch64 Device Register Access

```c
/* aarch64_device.c - Access memory-mapped device registers */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>

/* Example: Generic device base address */
#define DEVICE_PHYS_BASE    0xFE000000
#define DEVICE_SIZE         0x1000
#define REG_STATUS          0x00
#define REG_CONTROL         0x04
#define REG_DATA            0x08

int main(void) {
    volatile uint32_t *regs;
    uint32_t status;

    /* Map device registers */
    regs = mmap(NULL, DEVICE_SIZE,
                PROT_READ | PROT_WRITE | PROT_NOCACHE,
                MAP_PHYS | MAP_SHARED,
                NOFD, DEVICE_PHYS_BASE);

    if (regs == MAP_FAILED) {
        perror("mmap device");
        return EXIT_FAILURE;
    }

    printf("Device registers mapped at %p\n", (void *)regs);

    /* Read status register */
    status = regs[REG_STATUS / 4];
    printf("Status: 0x%08X\n", status);

    /* Poll until device is ready */
    while ((regs[REG_STATUS / 4] & 0x01) == 0) {
        usleep(1000);  /* 1 ms */
    }

    /* Write control and data */
    regs[REG_CONTROL / 4] = 0x01;  /* Start operation */
    regs[REG_DATA / 4] = 0xDEADBEEF;

    /* Wait for completion */
    while ((regs[REG_STATUS / 4] & 0x02) == 0) {
        usleep(1000);
    }

    printf("Operation complete!\n");

    munmap((void *)regs, DEVICE_SIZE);
    return EXIT_SUCCESS;
}
```

### 8.4 Example 4: x86 I/O Port with Interrupt

```c
/* x86_io_interrupt.c - x86 I/O port + interrupt handling */
#include <stdio.h>
#include <stdlib.h>
#include <hw/inout.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <stdint.h>

#define TIMER_PORT_CTRL     0x43
#define TIMER_PORT_DATA0    0x40
#define TIMER_IRQ           0       /* IRQ0 = timer */

volatile unsigned counter = 0;
struct sigevent event;

const struct sigevent *timer_isr(void *area, int id) {
    if (++counter == 100) {
        counter = 0;
        return &event;  /* Wake up thread every 100 ticks */
    }
    return NULL;
}

int main(void) {
    int id;

    /* Request I/O privileges */
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("ThreadCtl");
        return EXIT_FAILURE;
    }

    /* Setup interrupt event */
    event.sigev_notify = SIGEV_INTR;

    /* Attach to timer interrupt */
    id = InterruptAttach(
        SYSPAGE_ENTRY(qtime)->intr,  /* Get timer IRQ from system page */
        timer_isr,
        NULL, 0, 0
    );

    if (id == -1) {
        perror("InterruptAttach");
        return EXIT_FAILURE;
    }

    printf("Timer ISR attached. Waiting for interrupts...\n");

    for (int i = 0; i < 10; i++) {
        InterruptWait(0, NULL);
        printf("100 timer ticks occurred\n");
    }

    InterruptDetach(id);
    return EXIT_SUCCESS;
}
```

---

## 9. Quick Reference

### 9.1 mmap() Flag Combinations

| Use Case             | Flags                     | fd                                | offset           |
| -------------------- | ------------------------- | --------------------------------- | ---------------- |
| Map device memory    | `MAP_PHYS \| MAP_SHARED`  | `NOFD`                            | Physical address |
| Allocate DMA buffer  | `MAP_PHYS \| MAP_ANON`    | `NOFD`                            | `0`              |
| Shared memory object | `MAP_SHARED`              | File descriptor from `shm_open()` | `0`              |
| Private allocation   | `MAP_PRIVATE \| MAP_ANON` | `NOFD`                            | `0`              |

### 9.2 Function Quick Reference

| Function             | Header             | Purpose                           |
| -------------------- | ------------------ | --------------------------------- |
| `mmap()`             | `<sys/mman.h>`     | Map memory (device, DMA, shared)  |
| `munmap()`           | `<sys/mman.h>`     | Unmap memory                      |
| `mem_offset64()`     | `<sys/mman.h>`     | Get physical address from virtual |
| `ThreadCtl()`        | `<sys/neutrino.h>` | Request I/O privileges            |
| `in8/in16/in32()`    | `<hw/inout.h>`     | Read from x86 I/O port            |
| `out8/out16/out32()` | `<hw/inout.h>`     | Write to x86 I/O port             |
| `mmap_device_io()`   | `<sys/mman.h>`     | Map I/O port range (modern QNX)   |
| `InterruptAttach()`  | `<sys/neutrino.h>` | Attach ISR to hardware interrupt  |
| `InterruptWait()`    | `<sys/neutrino.h>` | Wait for interrupt notification   |

### 9.3 Platform Differences

| Feature               | AArch64                         | x86/x86_64                   |
| --------------------- | ------------------------------- | ---------------------------- |
| Register access       | Memory-mapped (`volatile *ptr`) | Memory-mapped OR I/O ports   |
| I/O port instructions | Not available                   | `IN`/`OUT` instructions      |
| I/O privilege         | Not applicable                  | `ThreadCtl(_NTO_TCTL_IO, 0)` |
| Cache control         | `PROT_NOCACHE`                  | `PROT_NOCACHE`               |
| DMA                   | `mmap(MAP_PHYS\|MAP_ANON)`      | `mmap(MAP_PHYS\|MAP_ANON)`   |

### 9.4 Common Pitfalls

| Pitfall                | Solution                                             |
| ---------------------- | ---------------------------------------------------- |
| Forgetting `volatile`  | Always declare hardware pointers as `volatile`       |
| Missing I/O privileges | Call `ThreadCtl(_NTO_TCTL_IO, 0)` before port access |
| Cache coherency issues | Use `PROT_NOCACHE` for device registers and DMA      |
| Wrong physical address | Verify with `mem_offset64()`                         |
| Non-contiguous DMA     | Always use `MAP_PHYS \| MAP_ANON`                    |
| Forgetting `NOFD`      | Required when using `MAP_PHYS` or `MAP_ANON`         |

---

## Related Documentation

- **QNX IPC Method Selection Guide** — Choosing IPC mechanisms for QNX systems
- **QNX IPC Methods Detailed Comparison** — Full API comparison of all IPC methods
- **QNX Official:** [mmap() Reference](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/m/mmap.html)
- **QNX Official:** [ThreadCtl() Reference](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/t/threadctl.html)
- **QNX Official:** [Hardware I/O](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.prog/topic/hardware_IO.html)
- **QNX From The Board Up #18** — I/O Address Space (devblog.qnx.com)
