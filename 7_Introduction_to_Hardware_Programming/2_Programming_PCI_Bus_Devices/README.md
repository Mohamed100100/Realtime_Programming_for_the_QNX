# QNX Neutrino PCI Device Programming Guide

> **Companion to:** QNX Hardware I/O Programming Guide, QNX IPC Method Selection Guide

This document provides a comprehensive guide to programming PCI bus devices in QNX Neutrino. It covers the PCI server architecture, device discovery and attachment, configuration space access, address translation for bus mastering, and the complete API workflow for PCI device management.

---

## 1. Overview

PCI (Peripheral Component Interconnect) is the standard bus architecture for connecting hardware devices to the CPU. In QNX Neutrino, all PCI device access is mediated by the **PCI server** (`pci-server`), which manages device enumeration, resource allocation, and access control.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     QNX PCI Architecture Overview                       │
│                                                                         │
│   ┌─────────────┐                                                       │
│   │  pci-server │  ←── PCI Bus Manager (singleton process)              │
│   │             │                                                       │
│   │  • Enumerates all PCI devices at boot                               │
│   │  • Manages device allocation (exclusive/shared)                     │
│   │  • Handles configuration space access                               │
│   │  • Translates addresses (CPU ↔ PCI)                                 │
│   │  • Manages BARs (Base Address Registers)                            │
│   │  • Routes interrupts                                                │
│   └──────┬──────┘                                                       │
│          │                                                              │
│          │ PCI Bus                                                      │
│          ▼                                                              │
│   ┌─────────────────────────────────────────────────────────────┐       │
│   │                      PCI Device Tree                        │       │
│   │                                                             │       │
│   │   ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐     │       │
│   │   │ Device  │   │ Device  │   │ Device  │   │ Bridge  │     │       │
│   │   │  00:01  │   │  00:02  │   │  00:03  │   │  00:04  │     │       │
│   │   │(NIC)    │   │(GPU)    │   │(USB)    │   │(PCIe→PCI)│    │       │
│   │   └────┬────┘   └────┬────┘   └────┬────┘   └────┬────┘     │       │
│   │        │             │             │             │          │       │
│   │    ┌───┴───┐    ┌───┴───┐    ┌───┴───┐    ┌───┴───┐         │       │
│   │    │ BAR0  │    │ BAR0  │    │ BAR0  │    │  Bus  │         │       │
│   │    │ BAR1  │    │ BAR1  │    │ BAR1  │    │  Sec  │         │       │
│   │    │ BAR2  │    │ ...   │    │ ...   │    │  Sub  │         │       │
│   │    └───────┘    └───────┘    └───────┘    └───────┘         │       │
│   │                                                             │       │
│   │   BDF = Bus:Device.Function (e.g., 00:01.0)                 │       │
│   │   Each device identified by Vendor ID + Device ID           │       │
│   └─────────────────────────────────────────────────────────────┘       │
│                                                                         │
│   Application → pci-server → PCI Configuration Space → Device           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Concepts

| Term                    | Description                                            |
| ----------------------- | ------------------------------------------------------ |
| **BDF**                 | Bus:Device.Function — unique PCI device identifier     |
| **Vendor ID**           | 16-bit manufacturer identifier (e.g., 0x8086 = Intel)  |
| **Device ID**           | 16-bit product identifier assigned by vendor           |
| **BAR**                 | Base Address Register — maps device memory/I/O regions |
| **Configuration Space** | 256/4096-byte register set for device configuration    |
| **Bus Mastering**       | Device-initiated DMA transfers                         |

---

## 2. PCI Server Architecture

### 2.1 Starting the PCI Server

The PCI server must be running before any PCI device access:

```bash
# Start PCI server (typically done in startup script)
pci-server &

# Verify it's running
pidin arg | grep pci-server
```

### 2.2 PCI Server Responsibilities

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PCI Server Responsibilities                          │
│                                                                         │
│   ┌─────────────────┐                                                   │
│   │   pci-server    │                                                   │
│   │                 │                                                   │
│   │  1. ENUMERATION │──▶ Scan all PCI buses at startup                  │
│   │                 │    • Read Vendor ID / Device ID from Config Space │
│   │                 │    • Build device tree (BDF hierarchy)            │
│   │                 │    • Discover bridges and buses                   │
│   │                 │                                                   │
│   │  2. ALLOCATION  │──▶ Manage device ownership                        │
│   │                 │    • Exclusive attach (one process owns device)   │
│   │                 │    • Shared attach (multiple processes can read)  │
│   │                 │                                                   │
│   │  3. TRANSLATION │──▶ CPU ↔ PCI address conversion                   │
│   │                 │    • Virtual → Physical → PCI bus address         │
│   │                 │    • Required for DMA (bus mastering)             │
│   │                 │                                                   │
│   │  4. PROTECTION  │──▶ Access control                                 │
│   │                 │    • Only attached process can access config space│
│   │                 │    • BAR allocation tracking                      │
│   │                 │                                                   │
│   │  5. INTERRUPTS  │──▶ IRQ routing and allocation                     │
│   │                 │    • Read IRQ line from config space              │
│   │                 │    • Provide to attaching process                 │
│   └─────────────────┘                                                   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. PCI Device Discovery

### 3.1 Finding Devices with `pci_device_find()`

The first step is to locate your device on the PCI bus using Vendor ID and Device ID.

```c
#include <hw/pci.h>
#include <stdio.h>
#include <stdlib.h>

/* Example: Find an Intel Ethernet controller (Vendor: 0x8086) */
#define VENDOR_ID_INTEL     0x8086
#define DEVICE_ID_EXAMPLE   0x10D3   /* Example: Intel 82574L */

int find_pci_device(void) {
    pci_bdf_t bdf = PCI_BDF_NONE;
    pci_err_t err;
    unsigned idx = 0;

    /* Search for all matching devices */
    while ((err = pci_device_find(
                VENDOR_ID_INTEL,     /* Vendor ID (0 = wildcard) */
                DEVICE_ID_EXAMPLE,   /* Device ID (0 = wildcard) */
                idx,                 /* Index: 0 = first match */
                &bdf                 /* OUTPUT: BDF of found device */
            )) == PCI_ERR_OK) {

        printf("Found device at BDF: %04x:%02x:%02x.%x\n",
               PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));

        /* Get device info */
        pci_dev_info_t info;
        err = pci_device_read_bdf(bdf, &info);
        if (err == PCI_ERR_OK) {
            printf("  Class: 0x%06x\n", info.Class);
            printf("  Revision: 0x%02x\n", info.Revision);
            printf("  IRQ: %d\n", info.Irq);
        }

        idx++;  /* Look for next matching device */
    }

    if (idx == 0) {
        printf("No matching PCI device found!\n");
        return -1;
    }

    return 0;
}
```

### 3.2 Wildcard Search

```c
/* Find ALL PCI devices on the system */
void enumerate_all_pci_devices(void) {
    pci_bdf_t bdf = PCI_BDF_NONE;
    pci_err_t err;
    unsigned idx = 0;

    printf("PCI Device Enumeration:\n");
    printf("BDF        Vendor   Device   Class     Rev  IRQ\n");
    printf("------------------------------------------------\n");

    while ((err = pci_device_find(0, 0, idx, &bdf)) == PCI_ERR_OK) {
        pci_dev_info_t info;
        if (pci_device_read_bdf(bdf, &info) == PCI_ERR_OK) {
            printf("%04x:%02x:%02x.%x  0x%04x   0x%04x   0x%06x  0x%02x  %d\n",
                   PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf),
                   info.VendorId, info.DeviceId,
                   info.Class, info.Revision, info.Irq);
        }
        idx++;
    }

    printf("Total devices found: %u\n", idx);
}
```

### 3.3 pci_device_find Parameters

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    pci_device_find() Parameters                         │
│                                                                         │
│   pci_err_t pci_device_find(                                            │
│       unsigned vendor_id,    ← 0 = wildcard (match any vendor)          │
│       unsigned device_id,      ← 0 = wildcard (match any device)        │
│       unsigned index,          ← 0 = first match, 1 = second, etc.      │
│       pci_bdf_t *bdf          ← OUTPUT: BDF of found device             │
│   );                                                                    │
│                                                                         │
│   Return Values:                                                        │
│   • PCI_ERR_OK      = Device found, bdf filled in                       │
│   • PCI_ERR_ENODEV  = No device matching criteria                       │
│   • PCI_ERR_EINVAL  = Invalid parameter                                 │
│                                                                         │
│   BDF Extraction Macros:                                                │
│   • PCI_BUS(bdf)    → Bus number (0-255)                                │
│   • PCI_DEV(bdf)    → Device number (0-31)                              │
│   • PCI_FUNC(bdf)   → Function number (0-7)                             │
│                                                                         │
│   Example BDF: 00:1f.2 = Bus 0, Device 31, Function 2                   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 4. PCI Device Attachment

### 4.1 Attaching to a Device

Once you have the BDF, you must attach to the device to gain access rights.

```c
#include <hw/pci.h>
#include <stdio.h>

int attach_pci_device(pci_bdf_t bdf) {
    pci_err_t err;
    pci_attach_flags_t flags;
    pci_devhdl_t handle;

    /* Attachment flags */
    flags = PCI_ATTACH_ALL_PIO |      /* Allow I/O port access */
            PCI_ATTACH_ALL_MEM |      /* Allow memory-mapped access */
            PCI_ATTACH_BUS_MASTER |   /* Allow bus mastering (DMA) */
            PCI_ATTACH_EXCLUSIVE;      /* Exclusive ownership */

    /* Alternative: PCI_ATTACH_SHARED for non-exclusive access */

    err = pci_device_attach(
        bdf,              /* Device to attach to */
        flags,            /* Access permissions requested */
        NULL,             /* Reserved (must be NULL) */
        &handle           /* OUTPUT: Device handle for subsequent calls */
    );

    if (err != PCI_ERR_OK) {
        printf("Failed to attach to device: %s\n", pci_strerror(err));
        return -1;
    }

    printf("Successfully attached to device. Handle: %p\n", (void*)handle);
    return 0;
}
```

### 4.2 Attachment Flags

| Flag                    | Description           | When to Use                        |
| ----------------------- | --------------------- | ---------------------------------- |
| `PCI_ATTACH_EXCLUSIVE`  | Exclusive ownership   | Device driver (prevents conflicts) |
| `PCI_ATTACH_SHARED`     | Shared read access    | Monitoring tools, diagnostics      |
| `PCI_ATTACH_ALL_PIO`    | Grant I/O port access | Devices with I/O BARs              |
| `PCI_ATTACH_ALL_MEM`    | Grant memory access   | Devices with memory BARs           |
| `PCI_ATTACH_BUS_MASTER` | Enable bus mastering  | DMA-capable devices                |
| `PCI_ATTACH_PCI_IRQ`    | Use PCI IRQ routing   | MSI/MSI-X capable devices          |

### 4.3 Attachment Flow

```
┌──────────────────────────────────────────────────────────────────────────┐
│                    PCI Device Attachment Flow                            │
│                                                                          │
│   ┌─────────────┐                                                        │
│   │ Application │                                                        │
│   └──────┬──────┘                                                        │
│          │ pci_device_find(vendor, device, 0, &bdf)                      │
│          ▼                                                               │
│   ┌─────────────┐                                                        │
│   │   Found?    │──NO──▶ Return error / try next index                   │
│   └──────┬──────┘                                                        │
│         YES                                                              │
│          │                                                               │
│          ▼                                                               │
│   ┌─────────────┐                                                        │
│   │ pci_device_attach(bdf, flags, NULL, &handle)                         │
│   └──────┬──────┘                                                        │
│          │                                                               │
│          ▼                                                               │
│   ┌─────────────┐                                                        │
│   │ pci-server  │──▶ Checks if device already attached                   │
│   │             │    Validates flags against device capabilities         │
│   │             │    Allocates resources (if not already done)           │
│   │             │    Returns device handle                               │
│   └──────┬──────┘                                                        │
│          │                                                               │
│          ▼                                                               │
│   ┌─────────────┐                                                        │
│   │   Handle    │──▶ Use in all subsequent PCI API calls                 │
│   │   Valid     │    • pci_device_read_cfg()                             │
│   │             │    • pci_device_write_cfg()                            │
│   │             │    • pci_device_map_as()                               │
│   │             │    • pci_device_read_ba()                              │
│   └─────────────┘                                                        │
│                                                                          │
│   IMPORTANT: Must call pci_device_detach() when done!                    │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 5. PCI Configuration Space Access

### 5.1 Reading Configuration Space

PCI devices have a standard configuration space (256 bytes for PCI, 4096 bytes for PCIe).

```c
#include <hw/pci.h>
#include <stdio.h>
#include <stdint.h>

/* Read configuration space registers */
void read_pci_config(pci_devhdl_t handle) {
    uint32_t val32;
    uint16_t val16;
    uint8_t val8;
    pci_err_t err;

    /* Read 32-bit: Vendor ID + Device ID (offset 0x00) */
    err = pci_device_read_cfg(handle, 0x00, sizeof(uint32_t), &val32);
    if (err == PCI_ERR_OK) {
        printf("Vendor ID: 0x%04x\n", val32 & 0xFFFF);
        printf("Device ID: 0x%04x\n", (val32 >> 16) & 0xFFFF);
    }

    /* Read 16-bit: Command Register (offset 0x04) */
    err = pci_device_read_cfg(handle, 0x04, sizeof(uint16_t), &val16);
    if (err == PCI_ERR_OK) {
        printf("Command Register: 0x%04x\n", val16);
        printf("  I/O Space Enable:    %s\n", (val16 & 0x01) ? "Yes" : "No");
        printf("  Memory Space Enable: %s\n", (val16 & 0x02) ? "Yes" : "No");
        printf("  Bus Master Enable:   %s\n", (val16 & 0x04) ? "Yes" : "No");
    }

    /* Read 8-bit: Revision ID (offset 0x08) */
    err = pci_device_read_cfg(handle, 0x08, sizeof(uint8_t), &val8);
    if (err == PCI_ERR_OK) {
        printf("Revision ID: 0x%02x\n", val8);
    }

    /* Read 8-bit: Interrupt Line (offset 0x3C) */
    err = pci_device_read_cfg(handle, 0x3C, sizeof(uint8_t), &val8);
    if (err == PCI_ERR_OK) {
        printf("Interrupt Line (IRQ): %d\n", val8);
    }

    /* Read 8-bit: Interrupt Pin (offset 0x3D) */
    err = pci_device_read_cfg(handle, 0x3D, sizeof(uint8_t), &val8);
    if (err == PCI_ERR_OK) {
        const char *pins[] = {"None", "INTA#", "INTB#", "INTC#", "INTD#"};
        printf("Interrupt Pin: %s\n",
               (val8 >= 1 && val8 <= 4) ? pins[val8] : "Unknown");
    }
}
```

### 5.2 Writing Configuration Space

```c
#include <hw/pci.h>
#include <stdint.h>

/* Enable bus mastering in the Command Register */
int enable_bus_mastering(pci_devhdl_t handle) {
    uint16_t command;
    pci_err_t err;

    /* Read current command register */
    err = pci_device_read_cfg(handle, 0x04, sizeof(uint16_t), &command);
    if (err != PCI_ERR_OK) {
        return -1;
    }

    /* Set Bus Master bit (bit 2) */
    command |= 0x04;

    /* Write back */
    err = pci_device_write_cfg(handle, 0x04, sizeof(uint16_t), &command);
    if (err != PCI_ERR_OK) {
        return -1;
    }

    return 0;
}

/* Enable Memory Space and I/O Space */
int enable_device_regions(pci_devhdl_t handle) {
    uint16_t command;
    pci_err_t err;

    err = pci_device_read_cfg(handle, 0x04, sizeof(uint16_t), &command);
    if (err != PCI_ERR_OK) return -1;

    command |= 0x03;  /* Bit 0 = I/O Space, Bit 1 = Memory Space */

    err = pci_device_write_cfg(handle, 0x04, sizeof(uint16_t), &command);
    if (err != PCI_ERR_OK) return -1;

    return 0;
}
```

### 5.3 PCI Configuration Space Layout

```
┌──────────────────────────────────────────────────────────────────────────┐
│              PCI Configuration Space (256 bytes / 4K for PCIe)           │
│                                                                          │
│   Offset  │ Register           │ Description                             │
│   ────────┼────────────────────┼─────────────────────────────────────────│
│   0x00    │ Vendor ID          │ Manufacturer identifier                 │
│   0x02    │ Device ID          │ Product identifier                      │
│   0x04    │ Command            │ Device control bits                     │
│   0x06    │ Status             │ Device status bits                      │
│   0x08    │ Revision ID        │ Hardware revision                       │
│   0x09    │ Class Code         │ Device class (24 bits: Base/Sub/Prog)   │
│   0x0C    │ Cache Line Size    │ Cache line size in 32-bit words         │
│   0x0D    │ Latency Timer      │ PCI bus latency                         │
│   0x0E    │ Header Type        │ 0=Normal, 1=Bridge, 2=CardBus           │
│   0x0F    │ BIST               │ Built-in self test                      │
│   0x10    │ BAR0               │ Base Address Register 0                 │
│   0x14    │ BAR1               │ Base Address Register 1                 │
│   0x18    │ BAR2               │ Base Address Register 2                 │
│   0x1C    │ BAR3               │ Base Address Register 3                 │
│   0x20    │ BAR4               │ Base Address Register 4                 │
│   0x24    │ BAR5               │ Base Address Register 5                 │
│   0x28    │ CardBus CIS        │ CardBus CIS pointer                     │
│   0x2C    │ Subsystem Vendor   │ Subsystem vendor ID                     │
│   0x2E    │ Subsystem ID       │ Subsystem device ID                     │
│   0x30    │ ROM BAR            │ Expansion ROM Base Address              │
│   0x3C    │ Interrupt Line     │ IRQ routing (0xFF = none)               │
│   0x3D    │ Interrupt Pin      │ Which INTx# pin (1-4)                   │
│   0x3E    │ Min Grant          │ Minimum bus grant time                  │
│   0x3F    │ Max Latency        │ Maximum latency                         │
│                                                                          │
│   Command Register Bits:                                                 │
│   • Bit 0: I/O Space Enable                                              │
│   • Bit 1: Memory Space Enable                                           │
│   • Bit 2: Bus Master Enable (required for DMA)                          │
│   • Bit 3: Special Cycles                                                │
│   • Bit 4: Memory Write and Invalidate Enable                            │
│   • Bit 5: VGA Palette Snoop                                             │
│   • Bit 6: Parity Error Response                                         │
│   • Bit 7: Wait Cycle Enable                                             │
│   • Bit 8: System Error Enable                                           │
│   • Bit 9: Fast Back-to-Back Enable                                      │
│   • Bit 10: Interrupt Disable                                            │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Address Translation (pci_device_map_as)

### 6.1 Why Address Translation is Needed

When a PCI device performs DMA (bus mastering), it uses **PCI bus addresses**, not CPU virtual addresses. The `pci_device_map_as()` function translates between these address spaces.

```
┌──────────────────────────────────────────────────────────────────────────┐
│              Address Translation for Bus Mastering (DMA)                 │
│                                                                          │
│   CPU Side                          PCI Bus Side                         │
│   ─────────                         ────────────                         │
│                                                                          │
│   Process Virtual Address                                                │
│        │                                                                 │
│        ▼                                                                 │
│   ┌─────────────┐                                                        │
│   │   mmap()    │──▶ Physical Address (via page tables)                  │
│   │  (virtual)  │                                                        │
│   └──────┬──────┘                                                        │
│          │                                                               │
│          ▼                                                               │
│   Physical Address (CPU view)                                            │
│        │                                                                 │
│        │  pci_device_map_as()                                            │
│        ▼                                                                 │
│   PCI Bus Address ──────▶ Write to device's DMA register                 │
│                           (e.g., TxDescAddr, RxBufferAddr)               │
│                                                                          │
│   The PCI device then uses this bus address to read/write memory         │
│   directly without CPU involvement (Direct Memory Access)                │
│                                                                          │
│   Example: Network Card DMA                                              │
│   1. Driver allocates buffer with mmap(MAP_PHYS|MAP_ANON)                │
│   2. Driver gets physical address with mem_offset64()                    │
│   3. Driver converts to PCI bus address with pci_device_map_as()         │
│   4. Driver writes PCI bus address to NIC's Tx descriptor register       │
│   5. NIC DMAs packet data directly from/to that address                  │
└──────────────────────────────────────────────────────────────────────────┘
```

### 6.2 Using pci_device_map_as()

```c
#include <hw/pci.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>

/* Translate CPU physical address to PCI bus address for DMA */
int setup_dma_translation(pci_devhdl_t handle,
                           uint64_t cpu_phys_addr,
                           size_t length,
                           uint64_t *pci_bus_addr) {
    pci_err_t err;
    pci_ba_t ba;
    pci_addr_t cpu_addr;
    pci_addr_t pci_addr;
    pci_asmap_t asmap;

    /* Initialize address space mapping structure */
    memset(&asmap, 0, sizeof(asmap));

    /* Set up CPU address (physical address from mem_offset64) */
    cpu_addr = cpu_phys_addr;

    /* Map CPU address to PCI bus address */
    err = pci_device_map_as(
        handle,              /* Device handle */
        PCI_AS_BUS,          /* Target address space: PCI bus */
        &cpu_addr,           /* INPUT: CPU physical address */
        length,              /* Length of region to map */
        PCI_AS_IO_MEM,       /* Type: Memory (not I/O ports) */
        &pci_addr,           /* OUTPUT: PCI bus address */
        &asmap               /* OUTPUT: Mapping info */
    );

    if (err != PCI_ERR_OK) {
        printf("pci_device_map_as failed: %s\n", pci_strerror(err));
        return -1;
    }

    *pci_bus_addr = (uint64_t)pci_addr;

    printf("Address Translation:\n");
    printf("  CPU Physical:  0x%016llX\n", (unsigned long long)cpu_phys_addr);
    printf("  PCI Bus:     0x%016llX\n", (unsigned long long)*pci_bus_addr);

    return 0;
}
```

### 6.3 Complete DMA Buffer Setup

```c
#include <hw/pci.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DMA_BUFFER_SIZE (64 * 1024)  /* 64 KB DMA buffer */

typedef struct {
    void *vaddr;           /* Virtual address for CPU access */
    uint64_t paddr;        /* Physical address */
    uint64_t pci_addr;     /* PCI bus address for device DMA */
    size_t size;
} dma_buffer_t;

/* Allocate DMA buffer and get both physical and PCI bus addresses */
int alloc_dma_buffer_for_pci(pci_devhdl_t handle, dma_buffer_t *buf) {
    off_t offset;
    size_t contig_len;
    pci_addr_t cpu_addr;
    pci_addr_t pci_addr;
    pci_asmap_t asmap;
    pci_err_t err;

    /* Step 1: Allocate physically contiguous memory */
    buf->vaddr = mmap(NULL, DMA_BUFFER_SIZE,
                      PROT_READ | PROT_WRITE | PROT_NOCACHE,
                      MAP_PHYS | MAP_ANON,
                      NOFD, 0);

    if (buf->vaddr == MAP_FAILED) {
        perror("mmap DMA buffer");
        return -1;
    }

    buf->size = DMA_BUFFER_SIZE;

    /* Step 2: Get CPU physical address */
    if (mem_offset64(buf->vaddr, NOFD, DMA_BUFFER_SIZE,
                     &offset, &contig_len) == -1) {
        perror("mem_offset64");
        munmap(buf->vaddr, DMA_BUFFER_SIZE);
        return -1;
    }
    buf->paddr = (uint64_t)offset;

    /* Step 3: Translate to PCI bus address */
    memset(&asmap, 0, sizeof(asmap));
    cpu_addr = buf->paddr;

    err = pci_device_map_as(
        handle,
        PCI_AS_BUS,
        &cpu_addr,
        DMA_BUFFER_SIZE,
        PCI_AS_IO_MEM,
        &pci_addr,
        &asmap
    );

    if (err != PCI_ERR_OK) {
        printf("pci_device_map_as failed: %s\n", pci_strerror(err));
        munmap(buf->vaddr, DMA_BUFFER_SIZE);
        return -1;
    }

    buf->pci_addr = (uint64_t)pci_addr;

    printf("DMA Buffer Setup Complete:\n");
    printf("  Virtual:     %p\n", buf->vaddr);
    printf("  Physical:    0x%016llX\n", (unsigned long long)buf->paddr);
    printf("  PCI Bus:     0x%016llX\n", (unsigned long long)buf->pci_addr);
    printf("  Size:        %zu bytes\n", buf->size);

    return 0;
}

/* Write PCI bus address to device's DMA descriptor register */
void program_device_dma(pci_devhdl_t handle, uint64_t pci_addr, uint32_t reg_offset) {
    /* Example: Write to a 32-bit DMA address register */
    uint32_t addr_low = (uint32_t)(pci_addr & 0xFFFFFFFF);
    uint32_t addr_high = (uint32_t)((pci_addr >> 32) & 0xFFFFFFFF);

    /* Write low 32 bits */
    pci_device_write_cfg(handle, reg_offset, sizeof(uint32_t), &addr_low);

    /* Write high 32 bits (for 64-bit addressing) */
    pci_device_write_cfg(handle, reg_offset + 4, sizeof(uint32_t), &addr_high);
}
```

---

## 7. Base Address Information (pci_device_read_ba)

### 7.1 Reading Base Address Registers

BARs define the memory and I/O regions that a PCI device uses.

```c
#include <hw/pci.h>
#include <stdio.h>
#include <stdint.h>

/* Read and display all Base Address Registers */
void read_base_addresses(pci_devhdl_t handle) {
    pci_err_t err;
    pci_ba_t ba[6];  /* Up to 6 BARs per device */
    int i;

    /* Read all BARs */
    for (i = 0; i < 6; i++) {
        err = pci_device_read_ba(handle, i, &ba[i]);
        if (err != PCI_ERR_OK) {
            printf("BAR%d: Not present or error\n", i);
            continue;
        }

        printf("BAR%d:\n", i);
        printf("  Type:     %s\n",
               (ba[i].type == PCI_AS_IO) ? "I/O Port" : "Memory");
        printf("  Size:     0x%llx (%llu bytes)\n",
               (unsigned long long)ba[i].size,
               (unsigned long long)ba[i].size);

        if (ba[i].type == PCI_AS_MEM) {
            printf("  Address:  0x%016llX\n",
                   (unsigned long long)ba[i].addr);
            printf("  Prefetch: %s\n",
                   (ba[i].attr & PCI_AS_ATTR_PREFETCH) ? "Yes" : "No");
            printf("  64-bit:   %s\n",
                   (ba[i].attr & PCI_AS_ATTR_64BIT) ? "Yes" : "No");
        } else {
            printf("  Address:  0x%08X\n", (uint32_t)ba[i].addr);
        }
    }
}
```

### 7.2 Mapping BARs into Process Address Space

```c
#include <hw/pci.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>

/* Map a device's memory BAR into process virtual address space */
void *map_device_bar(pci_devhdl_t handle, int bar_index, size_t *mapped_size) {
    pci_ba_t ba;
    pci_err_t err;
    void *vaddr;
    int prot = 0;
    int flags = 0;

    /* Read BAR information */
    err = pci_device_read_ba(handle, bar_index, &ba);
    if (err != PCI_ERR_OK) {
        printf("Failed to read BAR%d: %s\n", bar_index, pci_strerror(err));
        return NULL;
    }

    if (ba.type != PCI_AS_MEM) {
        printf("BAR%d is not a memory BAR\n", bar_index);
        return NULL;
    }

    /* Set protection flags */
    prot = PROT_READ | PROT_WRITE | PROT_NOCACHE;

    /* Map using pci_device_map_as to get proper address */
    /* Then use mmap with MAP_PHYS to map it */
    vaddr = mmap(
        NULL,
        ba.size,
        prot,
        MAP_PHYS | MAP_SHARED,
        NOFD,
        ba.addr  /* Physical address from BAR */
    );

    if (vaddr == MAP_FAILED) {
        perror("mmap BAR");
        return NULL;
    }

    *mapped_size = ba.size;

    printf("BAR%d mapped:\n", bar_index);
    printf("  Physical: 0x%016llX\n", (unsigned long long)ba.addr);
    printf("  Virtual:  %p\n", vaddr);
    printf("  Size:     %llu bytes\n", (unsigned long long)ba.size);

    return vaddr;
}
```

### 7.3 BAR Types and Attributes

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PCI Base Address Register (BAR) Types                │
│                                                                         │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │  32-bit Memory BAR                                              │   │
│   │  ┌────────┬────────────────────────────────────────────┐        │   │
│   │  │Bits 0  │ 0 = Memory Space                           │        │   │
│   │  │Bits 1-2│ Type: 00=32-bit, 01=<1MB, 10=64-bit        │        │   │
│   │  │Bit 3   │ Prefetchable: 1=Yes, 0=No                  │        │   │
│   │  │Bits 4-31│ Base Address (16-byte aligned)            │        │   │
│   │  └────────┴────────────────────────────────────────────┘        │   │
│   │                                                                 │   │
│   │  64-bit Memory BAR (uses 2 consecutive BARs)                    │   │
│   │  ┌────────┬────────────────────────────────────────────┐        │   │
│   │  │ BAR N   │ Lower 32 bits of 64-bit address           │        │   │
│   │  │ BAR N+1 │ Upper 32 bits of 64-bit address           │        │   │
│   │  └────────┴────────────────────────────────────────────┘        │   │
│   │                                                                 │   │
│   │  I/O BAR                                                        │   │
│   │  ┌────────┬────────────────────────────────────────────┐        │   │
│   │  │Bit 0   │ 1 = I/O Space                              │        │   │
│   │  │Bits 1  │ Reserved (0)                               │        │   │
│   │  │Bits 2-31│ Base Address (4-byte aligned)             │        │   │
│   │  └────────┴────────────────────────────────────────────┘        │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│   Determining BAR Size:                                                 │
│   1. Write 0xFFFFFFFF to BAR                                            │
│   2. Read back the BAR value                                            │
│   3. Invert bits and add 1 → size                                       │
│   4. Restore original value                                             │
│                                                                         │
│   Example: Read back 0xFFF00000 → Invert: 0x000FFFFF → +1: 0x00100000   │
│   → Size = 1 MB                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 8. Device Control Operations

### 8.1 Resetting a Device

```c
#include <hw/pci.h>
#include <stdio.h>

/* Reset the PCI device */
int reset_pci_device(pci_devhdl_t handle) {
    pci_err_t err;

    printf("Resetting PCI device...\n");

    err = pci_device_reset(handle);
    if (err != PCI_ERR_OK) {
        printf("Device reset failed: %s\n", pci_strerror(err));
        return -1;
    }

    printf("Device reset complete\n");
    return 0;
}
```

### 8.2 Detaching from a Device

```c
#include <hw/pci.h>

/* Clean up and release PCI device */
void cleanup_pci_device(pci_devhdl_t handle) {
    pci_err_t err;

    if (handle == NULL) {
        return;
    }

    printf("Detaching from PCI device...\n");

    err = pci_device_detach(handle);
    if (err != PCI_ERR_OK) {
        printf("Detach warning: %s\n", pci_strerror(err));
    } else {
        printf("Successfully detached from device\n");
    }
}
```

### 8.3 Device Lifecycle

```
┌──────────────────────────────────────────────────────────────────────────┐
│                    PCI Device Lifecycle                                  │
│                                                                          │
│   ┌───────────┐                                                          │
│   │   START   │                                                          │
│   └─────┬─────┘                                                          │
│         │                                                                │
│         ▼                                                                │
│   ┌───────────────┐                                                      │
│   │ pci_device_find│  ← Search for device by Vendor ID / Device ID       │
│   │   (get BDF)   │                                                      │
│   └───────┬───────┘                                                      │
│           │                                                              │
│           ▼                                                              │
│   ┌───────────────┐                                                      │
│   │ pci_device_attach│ ← Acquire device handle and access rights         │
│   │  (get handle) │                                                      │
│   └───────┬───────┘                                                      │
│           │                                                              │
│           ▼                                                              │
│   ┌────────────────────────────────────────────────────────────────┐     │
│   │                     OPERATIONAL PHASE                          │     │
│   │                                                                │     │
│   │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐     │     │
│   │  │ Read/Write  │  │ Map BARs    │  │ Setup DMA (map_as)  │     │     │
│   │  │ Config Space│  │ (mmap)      │  │ (bus mastering)     │     │     │
│   │  └─────────────┘  └─────────────┘  └─────────────────────┘     │     │
│   │                                                                │     │
│   │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐     │     │
│   │  │ Enable Bus  │  │ Handle      │  │ Reset Device        │     │     │
│   │  │ Mastering   │  │ Interrupts  │  │ (if needed)         │     │     │
│   │  └─────────────┘  └─────────────┘  └─────────────────────┘     │     │
│   │                                                                │     │
│   └────────────────────────────────────────────────────────────────┘     │
│           │                                                              │
│           ▼                                                              │
│   ┌───────────────┐                                                      │
│   │ pci_device_detach│ ← Release device and free resources               │
│   │   (cleanup)   │                                                      │
│   └───────┬───────┘                                                      │
│           │                                                              │
│           ▼                                                              │
│   ┌───────────┐                                                          │
│   │    END    │                                                          │
│   └───────────┘                                                          │
│                                                                          │
│   IMPORTANT: Always detach before process exit!                          │
│   pci-server tracks allocations; leaks can prevent re-attachment.        │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Interrupt Handling

### 9.1 Getting Interrupt Information

```c
#include <hw/pci.h>
#include <sys/neutrino.h>
#include <stdio.h>

/* Get and setup interrupt for PCI device */
int setup_pci_interrupt(pci_devhdl_t handle, pci_bdf_t bdf) {
    pci_err_t err;
    pci_irq_t irq;
    int intr;

    /* Get interrupt information from PCI server */
    err = pci_device_read_irq(handle, &irq);
    if (err != PCI_ERR_OK) {
        printf("Failed to read IRQ info: %s\n", pci_strerror(err));
        return -1;
    }

    printf("PCI Interrupt Information:\n");
    printf("  IRQ: %d\n", irq.irq);
    printf("  Type: %s\n",
           (irq.type == PCI_IRQ_TYPE_LEGACY) ? "Legacy INTx" :
           (irq.type == PCI_IRQ_TYPE_MSI) ? "MSI" :
           (irq.type == PCI_IRQ_TYPE_MSIX) ? "MSI-X" : "Unknown");

    /* Attach to the interrupt */
    intr = InterruptAttach(irq.irq, my_isr, NULL, 0, 0);
    if (intr == -1) {
        perror("InterruptAttach");
        return -1;
    }

    return intr;
}
```

### 9.2 MSI/MSI-X Interrupts

```c
#include <hw/pci.h>
#include <stdio.h>

/* Enable MSI (Message Signaled Interrupts) for better performance */
int enable_msi_interrupts(pci_devhdl_t handle) {
    pci_err_t err;
    pci_irq_t irq;
    int i;

    /* Check if device supports MSI/MSI-X */
    err = pci_device_read_irq(handle, &irq);
    if (err != PCI_ERR_OK) {
        return -1;
    }

    if (irq.caps & PCI_IRQ_CAP_MSI) {
        printf("Device supports MSI\n");

        /* Configure MSI */
        pci_msi_t msi;
        memset(&msi, 0, sizeof(msi));
        msi.nirq = 1;  /* Request 1 MSI vector */

        err = pci_device_cfg_msi(handle, &msi);
        if (err == PCI_ERR_OK) {
            printf("MSI enabled, IRQ: %d\n", msi.irq[0]);
        }
    }

    if (irq.caps & PCI_IRQ_CAP_MSIX) {
        printf("Device supports MSI-X\n");
        /* Similar setup for MSI-X with multiple vectors */
    }

    return 0;
}
```

---

## 10. Complete Code Examples

### 10.1 Example 1: Complete PCI Device Initialization

```c
/* pci_init_example.c - Complete PCI device initialization workflow */
#include <hw/pci.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define VENDOR_ID       0x8086   /* Intel */
#define DEVICE_ID       0x10D3   /* Example device */
#define BAR_INDEX       0        /* Use first memory BAR */

typedef struct {
    pci_devhdl_t handle;
    pci_bdf_t bdf;
    void *bar_vaddr;
    size_t bar_size;
    int irq;
} pci_device_t;

/* Initialize PCI device: find, attach, map BAR, get IRQ */
int pci_device_init(pci_device_t *dev) {
    pci_err_t err;
    pci_attach_flags_t flags;
    pci_ba_t ba;
    pci_irq_t irq;

    memset(dev, 0, sizeof(*dev));

    /* Step 1: Find the device */
    err = pci_device_find(VENDOR_ID, DEVICE_ID, 0, &dev->bdf);
    if (err != PCI_ERR_OK) {
        printf("Device not found: %s\n", pci_strerror(err));
        return -1;
    }

    printf("Found device at BDF: %04x:%02x:%02x.%x\n",
           PCI_BUS(dev->bdf), PCI_DEV(dev->bdf), PCI_FUNC(dev->bdf));

    /* Step 2: Attach to device */
    flags = PCI_ATTACH_ALL_MEM | PCI_ATTACH_BUS_MASTER | PCI_ATTACH_EXCLUSIVE;

    err = pci_device_attach(dev->bdf, flags, NULL, &dev->handle);
    if (err != PCI_ERR_OK) {
        printf("Attach failed: %s\n", pci_strerror(err));
        return -1;
    }

    /* Step 3: Enable bus mastering and memory space */
    uint16_t cmd;
    pci_device_read_cfg(dev->handle, 0x04, sizeof(cmd), &cmd);
    cmd |= 0x06;  /* Bit 1 (Memory) + Bit 2 (Bus Master) */
    pci_device_write_cfg(dev->handle, 0x04, sizeof(cmd), &cmd);

    /* Step 4: Read and map BAR */
    err = pci_device_read_ba(dev->handle, BAR_INDEX, &ba);
    if (err != PCI_ERR_OK) {
        printf("Failed to read BAR%d: %s\n", BAR_INDEX, pci_strerror(err));
        pci_device_detach(dev->handle);
        return -1;
    }

    if (ba.type != PCI_AS_MEM) {
        printf("BAR%d is not memory-mapped\n", BAR_INDEX);
        pci_device_detach(dev->handle);
        return -1;
    }

    dev->bar_size = ba.size;
    dev->bar_vaddr = mmap(NULL, ba.size,
                          PROT_READ | PROT_WRITE | PROT_NOCACHE,
                          MAP_PHYS | MAP_SHARED,
                          NOFD, ba.addr);

    if (dev->bar_vaddr == MAP_FAILED) {
        perror("mmap BAR");
        pci_device_detach(dev->handle);
        return -1;
    }

    printf("BAR%d mapped: phys=0x%llx, virt=%p, size=%zu\n",
           BAR_INDEX, (unsigned long long)ba.addr,
           dev->bar_vaddr, dev->bar_size);

    /* Step 5: Get interrupt information */
    err = pci_device_read_irq(dev->handle, &irq);
    if (err == PCI_ERR_OK) {
        dev->irq = irq.irq;
        printf("Interrupt: IRQ %d (type: %s)\n", dev->irq,
               (irq.type == PCI_IRQ_TYPE_LEGACY) ? "Legacy" :
               (irq.type == PCI_IRQ_TYPE_MSI) ? "MSI" : "MSI-X");
    }

    printf("PCI device initialization complete!\n");
    return 0;
}

/* Cleanup PCI device */
void pci_device_cleanup(pci_device_t *dev) {
    if (dev->bar_vaddr != NULL && dev->bar_size > 0) {
        munmap(dev->bar_vaddr, dev->bar_size);
        dev->bar_vaddr = NULL;
    }

    if (dev->handle != NULL) {
        pci_device_detach(dev->handle);
        dev->handle = NULL;
    }

    printf("PCI device cleanup complete\n");
}

/* Example usage */
int main(void) {
    pci_device_t dev;

    if (pci_device_init(&dev) != 0) {
        return EXIT_FAILURE;
    }

    /* Device is ready for use */
    /* Access registers via dev.bar_vaddr */
    /* Handle interrupts on dev.irq */

    pci_device_cleanup(&dev);
    return EXIT_SUCCESS;
}
```

### 10.2 Example 2: DMA with Address Translation

```c
/* pci_dma_example.c - DMA buffer setup with PCI address translation */
#include <hw/pci.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DMA_BUF_SIZE    (1024 * 1024)  /* 1 MB DMA buffer */
#define TX_DESC_REG     0x10           /* Example: TX descriptor address reg */

typedef struct {
    pci_devhdl_t handle;
    void *vaddr;
    uint64_t paddr;
    uint64_t pci_addr;
} dma_region_t;

/* Setup DMA region for PCI device */
int setup_dma_region(pci_devhdl_t handle, dma_region_t *dma) {
    off_t offset;
    size_t contig_len;
    pci_addr_t cpu_addr, pci_addr;
    pci_asmap_t asmap;
    pci_err_t err;

    memset(dma, 0, sizeof(*dma));
    dma->handle = handle;

    /* Allocate physically contiguous DMA buffer */
    dma->vaddr = mmap(NULL, DMA_BUF_SIZE,
                      PROT_READ | PROT_WRITE | PROT_NOCACHE,
                      MAP_PHYS | MAP_ANON, NOFD, 0);

    if (dma->vaddr == MAP_FAILED) {
        perror("mmap DMA");
        return -1;
    }

    /* Get physical address */
    if (mem_offset64(dma->vaddr, NOFD, DMA_BUF_SIZE,
                     &offset, &contig_len) == -1) {
        perror("mem_offset64");
        munmap(dma->vaddr, DMA_BUF_SIZE);
        return -1;
    }
    dma->paddr = (uint64_t)offset;

    /* Translate to PCI bus address */
    memset(&asmap, 0, sizeof(asmap));
    cpu_addr = dma->paddr;

    err = pci_device_map_as(handle, PCI_AS_BUS, &cpu_addr,
                            DMA_BUF_SIZE, PCI_AS_IO_MEM,
                            &pci_addr, &asmap);
    if (err != PCI_ERR_OK) {
        printf("Address translation failed: %s\n", pci_strerror(err));
        munmap(dma->vaddr, DMA_BUF_SIZE);
        return -1;
    }

    dma->pci_addr = (uint64_t)pci_addr;

    printf("DMA Region Setup:\n");
    printf("  CPU Virtual:  %p\n", dma->vaddr);
    printf("  CPU Physical: 0x%016llX\n", (unsigned long long)dma->paddr);
    printf("  PCI Bus:      0x%016llX\n", (unsigned long long)dma->pci_addr);

    /* Program device DMA register */
    uint32_t addr_low = (uint32_t)(dma->pci_addr & 0xFFFFFFFF);
    pci_device_write_cfg(handle, TX_DESC_REG, sizeof(uint32_t), &addr_low);

    if (dma->pci_addr > 0xFFFFFFFF) {
        uint32_t addr_high = (uint32_t)(dma->pci_addr >> 32);
        pci_device_write_cfg(handle, TX_DESC_REG + 4,
                             sizeof(uint32_t), &addr_high);
    }

    printf("Device DMA register programmed\n");
    return 0;
}

void cleanup_dma_region(dma_region_t *dma) {
    if (dma->vaddr != NULL) {
        munmap(dma->vaddr, DMA_BUF_SIZE);
        dma->vaddr = NULL;
    }
}
```

### 10.3 Example 3: PCI Configuration Space Dumper

```c
/* pci_config_dump.c - Dump PCI configuration space */
#include <hw/pci.h>
#include <stdio.h>
#include <stdint.h>

void dump_config_space(pci_devhdl_t handle) {
    uint8_t config[256];
    pci_err_t err;
    int i;

    /* Read entire 256-byte configuration space */
    for (i = 0; i < 256; i += 4) {
        uint32_t val;
        err = pci_device_read_cfg(handle, i, sizeof(uint32_t), &val);
        if (err == PCI_ERR_OK) {
            config[i]     = val & 0xFF;
            config[i + 1] = (val >> 8) & 0xFF;
            config[i + 2] = (val >> 16) & 0xFF;
            config[i + 3] = (val >> 24) & 0xFF;
        }
    }

    /* Print hex dump */
    printf("PCI Configuration Space Dump:\n");
    printf("     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
    printf("   +------------------------------------------------\n");

    for (i = 0; i < 256; i += 16) {
        printf("%02X | ", i);
        for (int j = 0; j < 16; j++) {
            printf("%02X ", config[i + j]);
        }
        printf("\n");
    }

    /* Decode key fields */
    printf("\nDecoded Fields:\n");
    printf("  Vendor ID:     0x%02X%02X\n", config[1], config[0]);
    printf("  Device ID:     0x%02X%02X\n", config[3], config[2]);
    printf("  Command:       0x%02X%02X\n", config[5], config[4]);
    printf("  Status:        0x%02X%02X\n", config[7], config[6]);
    printf("  Revision:      0x%02X\n", config[8]);
    printf("  Class Code:    0x%02X%02X%02X\n", config[11], config[10], config[9]);
    printf("  Header Type:   0x%02X\n", config[14]);
    printf("  BAR0:          0x%02X%02X%02X%02X\n",
           config[0x13], config[0x12], config[0x11], config[0x10]);
    printf("  BAR1:          0x%02X%02X%02X%02X\n",
           config[0x17], config[0x16], config[0x15], config[0x14]);
    printf("  Interrupt Pin: 0x%02X\n", config[0x3D]);
    printf("  Interrupt Line: 0x%02X\n", config[0x3C]);
}
```

---

## 11. Quick Reference

### 11.1 PCI API Function Reference

| Function                 | Header       | Purpose                              |
| ------------------------ | ------------ | ------------------------------------ |
| `pci_device_find()`      | `<hw/pci.h>` | Find device by Vendor ID / Device ID |
| `pci_device_attach()`    | `<hw/pci.h>` | Attach to device and get handle      |
| `pci_device_detach()`    | `<hw/pci.h>` | Release device and free resources    |
| `pci_device_reset()`     | `<hw/pci.h>` | Reset the PCI device                 |
| `pci_device_read_cfg()`  | `<hw/pci.h>` | Read configuration space             |
| `pci_device_write_cfg()` | `<hw/pci.h>` | Write configuration space            |
| `pci_device_read_ba()`   | `<hw/pci.h>` | Read Base Address Register info      |
| `pci_device_map_as()`    | `<hw/pci.h>` | Translate CPU ↔ PCI addresses        |
| `pci_device_read_bdf()`  | `<hw/pci.h>` | Read device info by BDF              |
| `pci_device_read_irq()`  | `<hw/pci.h>` | Read interrupt information           |
| `pci_device_cfg_msi()`   | `<hw/pci.h>` | Configure MSI/MSI-X interrupts       |
| `pci_strerror()`         | `<hw/pci.h>` | Convert error code to string         |

### 11.2 PCI Error Codes

| Error Code       | Value | Description                      |
| ---------------- | ----- | -------------------------------- |
| `PCI_ERR_OK`     | 0     | Success                          |
| `PCI_ERR_ENODEV` | 1     | No such device                   |
| `PCI_ERR_EINVAL` | 2     | Invalid argument                 |
| `PCI_ERR_EAGAIN` | 3     | Resource temporarily unavailable |
| `PCI_ERR_EBUSY`  | 4     | Device or resource busy          |
| `PCI_ERR_EIO`    | 5     | I/O error                        |
| `PCI_ERR_ENOMEM` | 6     | Out of memory                    |
| `PCI_ERR_EPERM`  | 7     | Operation not permitted          |
| `PCI_ERR_ENOSYS` | 8     | Function not implemented         |

### 11.3 PCI Class Codes

| Class | Subclass | Description                        |
| ----- | -------- | ---------------------------------- |
| 0x00  | —        | Unclassified device                |
| 0x01  | 0x00     | SCSI storage controller            |
| 0x01  | 0x01     | IDE controller                     |
| 0x01  | 0x06     | SATA controller                    |
| 0x01  | 0x08     | NVM controller                     |
| 0x02  | 0x00     | Ethernet controller                |
| 0x02  | 0x80     | Network controller (other)         |
| 0x03  | 0x00     | VGA-compatible controller          |
| 0x03  | 0x02     | 3D controller                      |
| 0x04  | 0x00     | Multimedia video                   |
| 0x0C  | 0x03     | USB controller                     |
| 0x0C  | 0x05     | SMBus                              |
| 0x11  | 0x80     | Data acquisition/signal processing |

### 11.4 Common Pitfalls

| Pitfall                 | Solution                                                     |
| ----------------------- | ------------------------------------------------------------ |
| pci-server not running  | Start `pci-server` before any PCI operations                 |
| Device not found        | Check Vendor ID / Device ID; use `pci -vvv` to list devices  |
| Attach fails with EBUSY | Device already attached by another process; use shared mode or detach first |
| DMA doesn't work        | Ensure `PCI_ATTACH_BUS_MASTER` flag and enable Bus Master bit in Command Register |
| BAR mapping fails       | Verify BAR type (memory vs I/O); use correct `mmap()` flags  |
| Wrong PCI bus address   | Always use `pci_device_map_as()`; don't assume physical == PCI address |
| Interrupt not firing    | Check Interrupt Line register; verify IRQ routing in BIOS/UEFI |
| Memory not contiguous   | Use `MAP_PHYS | MAP_ANON` for DMA; verify with `mem_offset64()` |

### 11.5 PCI Utility Commands

```bash
# List all PCI devices
pci

# Verbose listing with BARs and config space
pci -vvv

# Show specific device
pci -b 0 -d 1 -f 0

# Dump configuration space
pci -vvvv

# Show device classes
pci -c
```

---

## Related Documentation

- **QNX Hardware I/O Programming Guide** — Memory mapping, DMA, register access
- **QNX IPC Method Selection Guide** — Choosing IPC mechanisms for driver design
- **QNX Official:** [PCI Library Reference](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.lib_ref/topic/p/pci.html)
- **QNX Official:** [Writing a PCI Driver](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.prog/topic/pci.html)
- **PCI-SIG:** [PCI Local Bus Specification](https://pcisig.com/specifications)
- **PCI-SIG:** [PCI Express Base Specification](https://pcisig.com/specifications)

