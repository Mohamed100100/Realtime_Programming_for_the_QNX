# QNX Hardware Programming — Introduction

This module introduces the fundamentals of hardware programming in QNX Neutrino, covering register access, DMA memory, PCI device configuration, and interrupt handling.

---

## Topics Covered

| Topic                        | Description                                                  |
| ---------------------------- | ------------------------------------------------------------ |
| **Hardware Register Access** | Reading and writing device control registers                 |
| **Memory-Mapped I/O**        | Accessing video memory and device memory regions             |
| **DMA Memory Allocation**    | Allocating contiguous memory for DMA transfers               |
| **PCI Device Configuration** | Using the PCI server and PCI API to discover and configure devices |
| **Interrupt Handling**       | Attaching to hardware interrupts and writing ISR code        |
| **Hands-On Exercise**        | Writing code to handle real hardware interrupts              |

---

## What You'll Learn

- How to map hardware registers into process address space
- How to allocate DMA-safe memory
- How to query and configure PCI devices via the PCI server
- How to attach interrupt handlers using QNX APIs
- How to safely handle interrupts in user-space threads

---

## Prerequisites

- QNX Message Passing fundamentals
- Basic C programming
- Familiarity with device driver concepts

---

## Related Modules

- QNX Interrupt Handling (detailed)
- QNX Resource Managers
- QNX Threading and Synchronization
