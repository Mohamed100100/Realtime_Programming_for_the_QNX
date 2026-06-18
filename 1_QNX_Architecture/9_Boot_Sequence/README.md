
# QNX Boot Process and Boot Sequence

## Overview

Understanding the QNX boot process is essential for two primary roles: **system integrators** who need to configure boot behavior so the system comes up properly, and **embedded developers** working with custom hardware boards who need to understand how QNX initializes and brings up the target platform. Unlike monolithic operating systems where drivers and services are bundled into a single kernel image, QNX uses a modular, component-based boot sequence that reflects its microkernel architecture.

---

## Boot Sequence Overview

The QNX boot sequence involves multiple stages, beginning with vendor-provided firmware and progressing through QNX-specific components until the system is fully operational. The process is divided across a boundary: everything to the left of the Initial Program Load (IPL) is provided by the board or System-on-Chip (SoC) vendor, while everything from the IPL onward is part of QNX.

```
┌─────────────────────────────────────────────────────────────────────┐
│                    QNX BOOT SEQUENCE OVERVIEW                         │
│                                                                      │
│  VENDOR PROVIDED                    QNX COMPONENTS                   │
│  ───────────────                    ─────────────                    │
│                                                                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐        │
│  │  ROM    │    │   IPL   │    │ Startup │    │ procnto │        │
│  │ Monitor │───►│ (Initial│───►│  Code   │───►│ + Boot  │        │
│  │ / BIOS  │    │ Program │    │         │    │ Script  │        │
│  │ / UEFI  │    │  Load)  │    │         │    │         │        │
│  └─────────┘    └─────────┘    └─────────┘    └─────────┘        │
│       │              │              │              │               │
│       │              │              │              │               │
│       ▼              ▼              ▼              ▼               │
│  ┌─────────────────────────────────────────────────────────────┐  │
│  │  STAGE 1        STAGE 2        STAGE 3        STAGE 4       │  │
│  │  Firmware       QNX Loader     Board Setup    Kernel +      │  │
│  │  Handoff        & IFS Locate   & Hardware     Services      │  │
│  │                 & Basic Init    Configuration   Launch      │  │
│  └─────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│  BOUNDARY: Vendor firmware ends, QNX control begins at IPL           │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Stage 1: Vendor Firmware (ROM Monitor, BIOS, or UEFI)

The first stage is entirely outside QNX control. It is provided by the board manufacturer or SoC vendor and is responsible for the most basic hardware initialization before any operating system can run.

### Types of Vendor Firmware

| Firmware Type | Typical Platforms | Description |
|-------------|-------------------|-------------|
| **ROM Monitor** (uBoot, RedBoot) | ARM, PowerPC, MIPS embedded boards | Minimal bootloader with command-line interface for debugging and image loading |
| **BIOS** | Legacy x86 systems | Basic Input/Output System providing hardware abstraction and boot device selection |
| **UEFI** | Modern x86 and some ARM64 systems | Unified Extensible Firmware Interface — newer, more capable replacement for BIOS |

### What Vendor Firmware Does

The vendor firmware performs the absolute minimum initialization needed to load and run code:

- **Power-on self test (POST)** — basic hardware health checks
- **Clock and voltage setup** — often minimal; just enough to run the bootloader
- **Memory controller initialization** — sometimes partial; full RAM setup may be left to QNX
- **Boot device selection** — determines where to load the operating system from (flash, disk, network, etc.)
- **Loading the next stage** — transfers control to the QNX IPL

The vendor firmware then **jumps to the QNX IPL**, passing control to QNX code. The exact mechanism varies by platform: on some systems, the IPL is loaded from a specific flash offset; on others, it is loaded from a filesystem or received over a debug interface.

---

## Stage 2: Initial Program Load (IPL)

The IPL is the first QNX code to execute. It is a small, carefully crafted assembly and C program that bridges the gap between vendor firmware and the full QNX operating system.

### IPL Responsibilities

The IPL performs the minimum hardware initialization required to locate and load the QNX Image File System (IFS):

| Task | Description | Example |
|------|-------------|---------|
| **Clock frequency configuration** | Set CPU and bus clocks to operational speeds | Configure ARM Cortex-A9 PLL for 1 GHz |
| **Chip select programming** | Configure memory controller chip selects for flash/ROM access | Setup NOR flash chip select timing |
| **RAM initialization** | Configure SDRAM controller so system has working memory | Set DDR3 timing, refresh rate, and bank mapping |
| **Stack establishment** | Create a valid stack pointer so C code can run | Point stack to top of initialized RAM |
| **IFS location** | Find where the QNX Image File System is stored | Scan flash partitions, read disk boot sector |
| **IFS loading** | Copy the IFS from storage into RAM | Decompress and copy to RAM base address |
| **Transfer to startup** | Jump to the startup code within the loaded IFS | Pass hardware information as arguments |

The IPL is intentionally minimal. It does not initialize devices that will be handled by QNX drivers later. It does not set up complex hardware features. Its sole purpose is to prepare the environment for the QNX startup code and load the IFS.

```
┌─────────────────────────────────────────────────────────────────────┐
│              IPL (INITIAL PROGRAM LOAD) EXECUTION                      │
│                                                                      │
│  Vendor Firmware hands control to QNX IPL                            │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  IPL CODE (assembly + minimal C)                              │    │
│  │                                                             │    │
│  │  1. Configure basic clocks                                  │    │
│  │     • CPU core frequency                                    │    │
│  │     • Bus / peripheral clocks                                 │    │
│  │     • Memory controller clock                                │    │
│  │                                                             │    │
│  │  2. Program chip selects                                     │    │
│  │     • Flash/ROM access timing                               │    │
│  │     • Memory region mapping                                   │    │
│  │                                                             │    │
│  │  3. Initialize RAM (minimal)                                  │    │
│  │     • SDRAM controller setup                                  │    │
│  │     • Verify memory with test pattern                         │    │
│  │     • Establish stack pointer                                 │    │
│  │                                                             │    │
│  │  4. Locate IFS in storage                                     │    │
│  │     • Check flash signature                                   │    │
│  │     • Read disk boot sector                                  │    │
│  │     • Verify IFS checksum                                     │    │
│  │                                                             │    │
│  │  5. Load IFS into RAM                                         │    │
│  │     • Decompress if compressed                                │    │
│  │     • Copy to designated RAM address                          │    │
│  │                                                             │    │
│  │  6. Jump to startup code inside IFS                            │    │
│  │     • Pass: RAM size, clock frequencies, board ID             │    │
│  │                                                             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  Startup code in IFS begins execution                                │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Stage 3: Image File System (IFS) and Startup Code

The Image File System (IFS) is a self-contained package containing all the components needed to boot QNX. It is created during the build process and includes the startup code, the kernel (procnto), and the boot script.

### IFS Contents

| Component | Description | Role |
|-----------|-------------|------|
| **Startup code** | Board-specific hardware initialization | Final hardware setup before kernel runs |
| **procnto** | Process Manager + Neutrino microkernel | The QNX kernel itself |
| **Boot script** | List of drivers and services to launch | Post-kernel system configuration |

### Startup Code: Board-Specific Hardware Initialization

The startup code is where the majority of hardware-specific configuration occurs. Unlike the IPL, which is minimal and generic, the startup code is tailored to the specific board and SoC.

```
┌─────────────────────────────────────────────────────────────────────┐
│              STARTUP CODE RESPONSIBILITIES                             │
│                                                                      │
│  The startup code is BOARD-SPECIFIC and CUSTOM for each platform.    │
│  It handles low-level hardware configuration that drivers cannot do.   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  MEMORY CONFIGURATION                                         │   │
│  │                                                             │   │
│  │  • Total RAM size and detection                               │   │
│  │  • Memory layout and region definitions                       │   │
│  │  • Reserved memory regions (graphics, DMA, firmware)        │   │
│  │  • Memory hole / holey memory handling                         │   │
│  │                                                             │   │
│  │  Example: 2GB DDR3 at 0x8000_0000, 256MB reserved for GPU    │   │
│  │  at 0x9000_0000, 16MB DMA bounce buffer at 0x9800_0000      │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  INTERRUPT CONTROLLER SETUP                                 │   │
│  │                                                             │   │
│  │  • Interrupt controller base address and type (GIC, VIC, etc.)│   │
│  │  • Number of interrupt sources                                │   │
│  │  • Priority levels and grouping                               │   │
│  │  • Interrupt routing to CPU cores                             │   │
│  │                                                             │   │
│  │  Example: ARM GICv2 at 0xF8A0_1000, 160 SPIs, 16 priority   │   │
│  │  levels, interrupts routed to all cores in cluster 0          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  TIMER CONFIGURATION                                         │   │
│  │                                                             │   │
│  │  • Timer hardware type and base address                     │   │
│  │  • Timer frequency and clock source                         │   │
│  │  • System tick rate (typically 1ms or 10ms)                 │   │
│  │  • Watchdog timer setup if applicable                         │   │
│  │                                                             │   │
│  │  Example: ARM Generic Timer at 25 MHz, 1ms tick rate         │   │
│  │  (1000 ticks per second for scheduling)                     │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  CPU CLUSTER CONFIGURATION                                   │   │
│  │                                                             │   │
│  │  • Number of CPU cores and their IDs                         │   │
│  │  • Cluster definitions for scheduling                         │   │
│  │  • Core capabilities (big.LITTLE, performance vs. efficiency) │   │
│  │  • Cache hierarchy and shared cache groups                    │   │
│  │                                                             │   │
│  │  Example: 8-core system with two clusters:                   │   │
│  │  • Cluster 0 (big): cores 0-3, high performance, shared L3  │   │
│  │  • Cluster 1 (LITTLE): cores 4-7, low power, shared L3       │   │
│  │  • Custom cluster: cores 0,1,2,3 (cache affinity)             │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  SYSTEM PAGE CONSTRUCTION                                    │   │
│  │                                                             │   │
│  │  All configuration is compiled into the SYSTEM PAGE — a      │   │
│  │  read-only data structure mapped into every process.           │   │
│  │                                                             │   │
│  │  • Hardware capabilities (CPU features, cache sizes)         │   │
│  │  • Memory map (RAM regions, device memory, reserved areas)   │   │
│  │  • Interrupt information (controller base, number of IRQs)     │   │
│  │  • Timer specifications (frequency, tick rate)               │   │
│  │  • Cluster definitions (core groupings for scheduling)         │   │
│  │                                                             │   │
│  │  Processes can read the system page (with appropriate rights)  │   │
│  │  to discover hardware configuration at runtime.               │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### System Page: The Hardware Configuration Database

The system page is a critical data structure created by the startup code. It is a read-only mapping that is visible to all processes in the system, allowing them to query hardware configuration without direct hardware access.

**What the System Page Contains:**

- **CPU information** — Number of cores, architecture version, features (NEON, VFP, etc.), cache sizes
- **Memory map** — Physical RAM regions, device memory areas, reserved regions, holey memory
- **Interrupt controller** — Type, base address, number of interrupts, priority scheme
- **Timer specifications** — Frequency, tick rate, watchdog configuration
- **Cluster definitions** — Core groupings for the QNX scheduler

**Why the System Page Matters:**

Processes that need hardware information — such as device drivers determining interrupt numbers, or applications choosing CPU affinity — can read the system page rather than hardcoding values. This makes code more portable across different boards using the same SoC family.

---

## Stage 4: procnto and Boot Script Execution

After the startup code completes hardware initialization and builds the system page, it transfers control to **procnto** — the combined Process Manager and Neutrino microkernel. This is the core of the QNX operating system.

### procnto Initialization

procnto performs the following steps:

1. **Kernel initialization** — Sets up internal data structures for thread scheduling, IPC, and memory management
2. **Process Manager initialization** — Prepares process tracking, path namespace management, and resource manager framework
3. **System page attachment** — Maps the system page into the kernel address space
4. **Boot script execution** — Reads and executes the boot script

### The Boot Script: QNX's Modular Service Launch

The boot script is where QNX fundamentally differs from monolithic operating systems like Linux. In Linux, drivers and essential services are compiled into the kernel or loaded as kernel modules, all started from a single monolithic image. In QNX, the boot script is an explicit list of processes to launch — drivers, services, and applications that the system integrator or embedded developer wants running at startup.

```
┌─────────────────────────────────────────────────────────────────────┐
│              QNX BOOT SCRIPT vs. MONOLITHIC KERNEL                     │
│                                                                      │
│  MONOLITHIC OS (Linux-style):                                        │
│  ──────────────────────────                                          │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    KERNEL IMAGE                                │    │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌──────┐ │    │
│  │  │ Scheduler│ │ Memory  │ │ Serial  │ │ Network │ │ Disk │ │    │
│  │  │         │ │ Manager │ │ Driver  │ │ Driver  │ │Driver│ │    │
│  │  │         │ │         │ │ (built-in│ │ (built-in│ │(built│ │    │
│  │  │         │ │         │ │ )       │ │ )       │ │-in)  │ │    │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └──────┘ │    │
│  │                                                             │    │
│  │  All drivers compiled into kernel or loaded as modules.      │    │
│  │  Kernel is large. Driver bugs can crash system.             │    │
│  │  Adding driver requires kernel rebuild or module compilation. │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  init process starts (limited control over what was already loaded) │
│                                                                      │
│  ─────────────────────────────────────────────────────────────────   │
│                                                                      │
│  QNX MICROKERNEL (Boot Script):                                      │
│  ─────────────────────────────                                       │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    SMALL KERNEL (procnto)                    │    │
│  │  ┌─────────────────┐    ┌─────────────────┐                  │    │
│  │  │  Neutrino       │    │  Process Manager │                  │    │
│  │  │  Microkernel    │    │  (memory, paths) │                  │    │
│  │  │  • Scheduling   │    │  • Process create│                  │    │
│  │  │  • IPC          │    │  • Namespace     │                  │    │
│  │  │  • Interrupts   │    │  • Resource mgr  │                  │    │
│  │  │  • Timers       │    │  framework       │                  │    │
│  │  └─────────────────┘    └─────────────────┘                  │    │
│  │                                                             │    │
│  │  Kernel is minimal (~100KB). Nothing unnecessary included.   │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    BOOT SCRIPT EXECUTION                       │    │
│  │                                                             │    │
│  │  [1]  devc-ser8250  &        ← Serial console driver         │    │
│  │  [2]  devb-eide  &           ← Disk driver                   │    │
│  │  [3]  io-sock  &             ← TCP/IP network stack         │    │
│  │  [4]  pci-server  &          ← PCI bus manager               │    │
│  │  [5]  pipe  &                ← Pipe service (optional)         │    │
│  │  [6]  random  &              ← Random number service           │    │
│  │  [7]  slogger2  &            ← System logger                 │    │
│  │  [8]  /my_application  &     ← Main application              │    │
│  │                                                             │    │
│  │  Each line starts a process. Each is independent.            │    │
│  │  If one fails, others continue. Kernel is unaffected.         │    │
│  │  Services can be added/removed without kernel rebuild.        │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ═══════════════════════════════════════════════════════════════════   │
│  KEY DIFFERENCE: In QNX, YOU control what runs. The boot script is     │
│  your explicit configuration. No hidden drivers. No mandatory       │
│  services. Only what you put in the script starts.                   │
│  ═══════════════════════════════════════════════════════════════════   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### What Goes in the Boot Script

The boot script contains the processes that must be running for your system to function. This is entirely application-specific and reflects QNX's philosophy of modularity.

**Typical boot script contents:**

| Process | Purpose | When to Include |
|---------|---------|---------------|
| `devc-ser8250` | Serial console driver | Always (for debug console) |
| `devc-con` | Virtual console driver | If multiple terminal sessions needed |
| `devb-eide` / `devb-sd` | Disk/SD storage driver | If persistent storage required |
| `io-sock` | TCP/IP network stack | If networking needed |
| `io-usb` | USB host stack | If USB devices supported |
| `pci-server` | PCI bus enumeration | If PCI/PCIe devices present |
| `pipe` | Pipe service | If command-line shell usage expected |
| `random` | Random number generator | If cryptographic operations needed |
| `slogger2` | System logger | If logging required |
| `dumper` | Core dump handler | If debugging enabled |
| `qconn` | IDE target agent | If remote debugging from Momentics |
| Custom applications | Your software | Always — this is your code |

**Example: Minimal Embedded System Boot Script**

A sensor node with no storage, no network, no console interaction:

```
procnto                          ← Kernel starts automatically
[1] devc-ser8250 -e -b115200 &   ← Serial port for debug output
[2] /apps/sensor_reader &        ← Main sensor acquisition application
```

**Example: Networked Industrial Controller Boot Script**

A system with storage, networking, and logging:

```
procnto
[1] devc-ser8250 -e -b115200 &
[2] devb-eide &
[3] io-sock -d mydriver &
[4] pci-server &
[5] slogger2 &
[6] /apps/plc_controller &
[7] /apps/web_interface &
```

**Example: Full Development System Boot Script**

Everything enabled for debugging and development:

```
procnto
[1] devc-ser8250 -e -b115200 &
[2] devc-con &
[3] devb-eide &
[4] devb-sd &
[5] io-sock &
[6] io-usb &
[7] pci-server &
[8] pipe &
[9] random &
[10] mqueue &
[11] slogger2 &
[12] dumper &
[13] qconn &
[14] /bin/sh &
```

---

## Boot Sequence Summary

```
┌─────────────────────────────────────────────────────────────────────┐
│                    COMPLETE QNX BOOT FLOW                              │
│                                                                      │
│  POWER ON                                                            │
│      │                                                               │
│      ▼                                                               │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  STAGE 1: VENDOR FIRMWARE                                    │     │
│  │  • ROM Monitor (uBoot, RedBoot) or BIOS/UEFI                 │     │
│  │  • Minimal hardware init (clocks, RAM basic setup)          │     │
│  │  • Locates and loads QNX IPL                                 │     │
│  └─────────────────────────────────────────────────────────────┘     │
│      │                                                               │
│      ▼                                                               │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  STAGE 2: QNX IPL (Initial Program Load)                     │     │
│  │  • Configure core clocks and chip selects                      │     │
│  │  • Initialize RAM (full configuration)                         │     │
│  │  • Establish stack for C code execution                        │     │
│  │  • Locate Image File System (IFS) in storage                   │     │
│  │  • Load IFS into RAM (decompress if needed)                    │     │
│  │  • Jump to startup code inside IFS                             │     │
│  └─────────────────────────────────────────────────────────────┘     │
│      │                                                               │
│      ▼                                                               │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  STAGE 3: STARTUP CODE (inside IFS)                          │     │
│  │  • Board-specific hardware initialization                     │     │
│  │  • RAM detection and full configuration                       │     │
│  │  • Interrupt controller setup                                  │     │
│  │  • Timer configuration (frequency, tick rate)                 │     │
│  │  • CPU cluster definitions (for scheduler)                     │     │
│  │  • Memory region reservations (graphics, DMA)                  │     │
│  │  • Build SYSTEM PAGE with all hardware info                    │     │
│  │  • Transfer control to procnto                                 │     │
│  └─────────────────────────────────────────────────────────────┘     │
│      │                                                               │
│      ▼                                                               │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  STAGE 4: procnto (Kernel + Process Manager)                  │     │
│  │  • Initialize Neutrino microkernel (scheduling, IPC)         │     │
│  │  • Initialize Process Manager (processes, namespace)           │     │
│  │  • Map system page into kernel space                           │     │
│  │  • Execute boot script                                         │     │
│  └─────────────────────────────────────────────────────────────┘     │
│      │                                                               │
│      ▼                                                               │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  STAGE 5: BOOT SCRIPT EXECUTION                                │     │
│  │  • Launch drivers (serial, disk, network, USB)             │     │
│  │  • Launch services (logging, pipes, random)                    │     │
│  │  • Launch custom applications                                  │     │
│  │  • System is fully operational                                 │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                                                                      │
│  ═══════════════════════════════════════════════════════════════════  │
│  TOTAL BOOT TIME: Typically milliseconds to seconds depending on     │
│  • IPL complexity (flash type, decompression)                        │
│  • Number of drivers/services in boot script                        │
│  • Application initialization time                                   │
│  ═══════════════════════════════════════════════════════════════════  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Customization Points for Embedded Developers

### Creating Custom IPL Code

For custom boards, you often need to write or modify the IPL:

- **New SoC support** — Adapt existing IPL for a new chip in the same family
- **Custom board bring-up** — Different RAM chips, clock crystals, or flash layouts
- **Secure boot requirements** — Add signature verification before loading IFS
- **Fast boot optimization** — Minimize IPL code to reduce boot time

The IPL source is typically in the Board Support Package (BSP) and is written in a combination of assembly (for the earliest initialization) and C (once a stack is available).

### Modifying Startup Code

The startup code is where most board-specific customization occurs:

- **RAM configuration** — Different RAM chips require different timing parameters
- **Memory reservations** — Reserve regions for GPU, video buffers, DMA, or firmware
- **Interrupt routing** — Direct interrupts to specific CPU cores in asymmetric systems
- **Cluster definitions** — Group cores for big.LITTLE or safety-segregated configurations
- **Custom system page entries** — Add board-specific information for drivers to query

### Writing Boot Scripts

The boot script is the primary tool for system integrators. You control:

- **Which drivers run** — Only include hardware you have
- **Driver order** — Some devices must be initialized before others (e.g., PCI before PCIe devices)
- **Driver parameters** — Pass command-line options to configure behavior
- **Application launch** — Start your software with appropriate priority and scheduling
- **Redundancy** — Launch backup services or watchdog processes

---

## Relationship to Other Topics

The boot process connects directly to other QNX concepts:

| Topic | Connection to Boot Process |
|-------|---------------------------|
| **QNX Architecture** | procnto is the kernel loaded from IFS; microkernel design enables minimal boot image |
| **Process Manager** | Startup code builds system page that Process Manager uses for memory management |
| **Resource Managers** | Boot script launches drivers as resource manager processes |
| **Scheduling** | Startup code defines CPU clusters used by the scheduler |
| **IPC** | procnto initializes message passing that boot script processes use |
| **OS Services** | Boot script determines which services (pipe, random, etc.) are available |

For detailed instructions on creating boot images, configuring IPL and startup code, and writing boot scripts, see the dedicated section **Building a QNX Neutrino Boot/OS Image**.

---

## Summary

| Stage | Component | Provided By | Key Function |
|-------|-----------|-------------|--------------|
| 1 | ROM Monitor / BIOS / UEFI | Board/SOC Vendor | Basic hardware init, load IPL |
| 2 | IPL | QNX | Clocks, RAM, stack, locate and load IFS |
| 3 | Startup code | QNX (BSP) | Full hardware config, system page, clusters |
| 4 | procnto | QNX | Kernel init, process manager, execute boot script |
| 5 | Boot script | System Integrator | Launch drivers, services, applications |

The QNX boot process reflects the microkernel philosophy at every stage: minimal initial code, modular component loading, explicit service configuration, and maximum flexibility for the embedded developer and system integrator.

---

*Happy learning!* 🚀
