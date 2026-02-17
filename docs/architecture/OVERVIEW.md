# AAAos Architecture Overview

AAAos is a general-purpose 64-bit operating system designed for the x86_64 architecture. This document provides an overview of the system architecture, kernel design, memory model, and boot process.

## System Architecture

### Target Platform

- **Architecture**: x86_64 (AMD64/Intel 64)
- **Mode**: 64-bit Long Mode
- **Minimum RAM**: 256 MB
- **Boot Method**: BIOS (Legacy boot)

### Project Structure

```
AAAos/
├── boot/               # Bootloader code
│   └── bios/          # BIOS bootloader (Stage 1 & 2)
├── kernel/            # Kernel source code
│   ├── arch/          # Architecture-specific code
│   │   └── x86_64/   # x86_64 implementation
│   ├── include/       # Kernel headers
│   ├── mm/            # Memory management
│   └── proc/          # Process management
├── drivers/           # Device drivers
│   └── input/         # Input device drivers
├── lib/               # Libraries
│   └── libc/          # C library functions
├── scripts/           # Build and run scripts
└── build/             # Build output (generated)
```

## Kernel Design

AAAos uses a **hybrid kernel** approach, combining aspects of both monolithic and microkernel designs:

### Monolithic Components (Kernel Space)

- Memory management (PMM, VMM)
- Process scheduling
- Interrupt handling
- Core device drivers (VGA, Serial)

### Modular Components

- Device drivers (loadable in the future)
- File systems (planned)

### Kernel Features

| Feature | Status | Description |
|---------|--------|-------------|
| Long Mode | Complete | Full 64-bit operation |
| GDT | Complete | Global Descriptor Table |
| IDT | Complete | Interrupt Descriptor Table |
| PMM | Complete | Physical Memory Manager |
| VMM | Complete | Virtual Memory Manager |
| VGA Console | Complete | Text mode output |
| Serial I/O | Complete | Debug output via COM1 |
| Keyboard | Partial | Basic PS/2 input |
| Processes | In Progress | Process management |
| Scheduler | Planned | Preemptive multitasking |
| User Mode | Planned | Ring 3 execution |

## Memory Model

### Physical Memory Layout

```
0x00000000 - 0x000003FF    IVT (Real Mode)
0x00000400 - 0x000004FF    BIOS Data Area
0x00000500 - 0x00007BFF    Free (conventional memory)
0x00007C00 - 0x00007DFF    Stage 1 Bootloader (MBR)
0x00007E00 - 0x0000BFFF    Stage 2 Bootloader
0x00010000 - 0x0009FFFF    Kernel temporary load area
0x000A0000 - 0x000BFFFF    VGA Memory
0x000C0000 - 0x000FFFFF    ROM / Reserved
0x00100000 - 0x????????    Kernel (loaded at 1MB)
```

### Virtual Memory Layout (Higher Half Kernel)

```
0x0000000000000000 - 0x00007FFFFFFFFFFF    User space (canonical low)
0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF    Kernel space (canonical high)

Kernel regions:
0xFFFFFFFF80000000    Kernel code/data (planned higher-half)
0xFFFF800000000000    Direct physical memory mapping (planned)
```

### Page Table Structure

AAAos uses x86_64 4-level paging:

```
PML4 (Page Map Level 4)         512 entries, 256 TB coverage each
  └── PDPT (Page Directory Pointer Table)   512 entries, 512 GB each
        └── PD (Page Directory)             512 entries, 1 GB each
              └── PT (Page Table)           512 entries, 2 MB each
                    └── Page                4 KB each
```

Virtual address breakdown (48-bit canonical):
- Bits [63:48]: Sign extension (must match bit 47)
- Bits [47:39]: PML4 index (9 bits)
- Bits [38:30]: PDPT index (9 bits)
- Bits [29:21]: PD index (9 bits)
- Bits [20:12]: PT index (9 bits)
- Bits [11:0]: Page offset (12 bits)

## Boot Process Flow

### Stage 1: MBR (512 bytes)

Location: First sector of disk (LBA 0)

1. BIOS loads MBR to 0x7C00
2. Set up real mode segments (DS, ES, SS)
3. Save boot drive number
4. Load Stage 2 using INT 13h extensions (LBA mode)
5. Jump to Stage 2 at 0x7E00

```nasm
; Stage 1 loads Stage 2 from sectors 1-32
STAGE2_LOAD_OFF     equ 0x7E00
STAGE2_SECTORS      equ 32
STAGE2_START_SECTOR equ 1
```

### Stage 2: Extended Bootloader

Location: 0x7E00 (sectors 1-32)

1. **Enable A20 Line**
   - Try BIOS method (INT 15h, AX=2401h)
   - Fall back to keyboard controller method

2. **Get Memory Map**
   - Use INT 15h, E820h
   - Store entries for kernel use

3. **Load Kernel**
   - Load from sector 9 to 0x10000 (temporary)
   - Copy to 1MB in protected mode

4. **Enter Protected Mode**
   - Load 32-bit GDT
   - Set CR0 PE bit
   - Far jump to 32-bit code

5. **Set Up Paging**
   - Create identity mapping for first 2MB
   - Enable PAE (CR4)
   - Enable Long Mode (EFER MSR)
   - Enable paging (CR0)

6. **Enter Long Mode**
   - Load 64-bit GDT
   - Far jump to 64-bit code
   - Set up stack
   - Jump to kernel at 0x100000

### Stage 3: Kernel Entry

Location: 0x100000 (1MB)

1. **Assembly Entry** (`kernel/arch/x86_64/boot.asm`)
   - Clear BSS section
   - Set up 16KB kernel stack
   - Call `kernel_main(boot_info)`

2. **C Kernel Main** (`kernel/main.c`)
   - Initialize serial port (COM1)
   - Initialize VGA console
   - Initialize GDT
   - Initialize IDT
   - Register keyboard handler
   - Enable interrupts
   - Enter idle loop

## Boot Information Structure

The bootloader passes system information to the kernel via a `boot_info_t` structure:

```c
typedef struct PACKED {
    uint32_t magic;             // 0xAAAB007
    uint32_t reserved;
    uint64_t mem_map_addr;      // Physical address of E820 map
    uint64_t mem_map_count;     // Number of memory map entries
    uint64_t framebuffer;       // VGA buffer address (0xB8000)
    uint32_t fb_width;          // Screen width (80)
    uint32_t fb_height;         // Screen height (25)
    uint32_t fb_bpp;            // Bits per pixel
    uint32_t fb_pitch;          // Bytes per line
} boot_info_t;
```

## CPU Initialization

### Global Descriptor Table (GDT)

Segments configured:
- 0x00: Null descriptor
- 0x08: Kernel code (64-bit, Ring 0)
- 0x10: Kernel data (Ring 0)
- 0x18: User code (64-bit, Ring 3)
- 0x20: User data (Ring 3)
- 0x28: TSS (Task State Segment)

### Interrupt Descriptor Table (IDT)

- Vectors 0-31: CPU exceptions
- Vectors 32-47: Hardware IRQs (remapped from 0-15)
- Vector 33: Keyboard (IRQ1)

### PIC Configuration

The 8259 PIC is remapped:
- Master PIC: IRQs 0-7 -> Vectors 32-39
- Slave PIC: IRQs 8-15 -> Vectors 40-47

## Version Information

- **Kernel Version**: 0.1.0
- **Codename**: Genesis
- **Output Format**: ELF64-x86-64

## ARMv7 (32-bit) Port Status

AAAos now includes an **experimental ARMv7-A bring-up path** focused on producing a minimal kernel ELF for early platform work:

- `kernel/arch/armv7/boot.S` provides `_start`, stack setup, and a low-power idle loop.
- `kernel/main_armv7.c` provides `kernel_main_armv7()` as a minimal C entrypoint.
- `kernel/linker_armv7.ld` provides a 32-bit ARM linker layout and stack symbol definitions.
- `make kernel-armv7` builds `build/armv7/kernel-armv7.elf` when `arm-none-eabi-*` tools are installed.

This is an initial port scaffold; device drivers, MMU setup, interrupt controller support, and board-specific boot integration are still pending.
