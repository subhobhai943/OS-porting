/**
 * AAAos UEFI Bootloader - Kernel Loading Functions
 *
 * This file implements kernel loading, memory map retrieval,
 * framebuffer setup, and boot services exit.
 */

#include "uefi.h"

/* ============================================================================
 * Boot Info Structures (must match kernel's boot.h)
 * ============================================================================ */

/* Boot magic number - identifies valid boot info */
#define BOOT_MAGIC          0xAAAB007

/* Memory map entry types (E820 compatible) */
#define MEMORY_TYPE_USABLE      1
#define MEMORY_TYPE_RESERVED    2
#define MEMORY_TYPE_ACPI_RECL   3
#define MEMORY_TYPE_ACPI_NVS    4
#define MEMORY_TYPE_BAD         5

/* Memory map entry (E820 format) - matches kernel definition */
typedef struct __attribute__((packed)) {
    UINT64  base;           /* Start address */
    UINT64  length;         /* Size in bytes */
    UINT32  type;           /* Memory type */
    UINT32  acpi_attrs;     /* ACPI extended attributes */
} memory_map_entry_t;

/* Boot information structure - matches kernel definition */
typedef struct __attribute__((packed)) {
    UINT32  magic;              /* Boot magic number */
    UINT32  reserved;           /* Padding */
    UINT64  mem_map_addr;       /* Physical address of memory map */
    UINT64  mem_map_count;      /* Number of memory map entries */
    UINT64  framebuffer;        /* Framebuffer address */
    UINT32  fb_width;           /* Screen width (chars or pixels) */
    UINT32  fb_height;          /* Screen height */
    UINT32  fb_bpp;             /* Bits per pixel */
    UINT32  fb_pitch;           /* Bytes per line */
} boot_info_t;

/* ELF64 Header structure */
#define ELF_MAGIC_0         0x7F
#define ELF_MAGIC_1         'E'
#define ELF_MAGIC_2         'L'
#define ELF_MAGIC_3         'F'
#define ELFCLASS64          2
#define ELFDATA2LSB         1
#define ET_EXEC             2
#define ET_DYN              3
#define EM_X86_64           0x3E
#define PT_LOAD             1

typedef struct __attribute__((packed)) {
    UINT8   e_ident[16];    /* ELF identification */
    UINT16  e_type;         /* Object file type */
    UINT16  e_machine;      /* Machine type */
    UINT32  e_version;      /* Object file version */
    UINT64  e_entry;        /* Entry point address */
    UINT64  e_phoff;        /* Program header offset */
    UINT64  e_shoff;        /* Section header offset */
    UINT32  e_flags;        /* Processor-specific flags */
    UINT16  e_ehsize;       /* ELF header size */
    UINT16  e_phentsize;    /* Size of program header entry */
    UINT16  e_phnum;        /* Number of program header entries */
    UINT16  e_shentsize;    /* Size of section header entry */
    UINT16  e_shnum;        /* Number of section header entries */
    UINT16  e_shstrndx;     /* Section name string table index */
} elf64_header_t;

typedef struct __attribute__((packed)) {
    UINT32  p_type;         /* Segment type */
    UINT32  p_flags;        /* Segment flags */
    UINT64  p_offset;       /* Offset in file */
    UINT64  p_vaddr;        /* Virtual address in memory */
    UINT64  p_paddr;        /* Physical address (unused) */
    UINT64  p_filesz;       /* Size in file */
    UINT64  p_memsz;        /* Size in memory */
    UINT64  p_align;        /* Alignment */
} elf64_phdr_t;

/* ============================================================================
 * Global Data
 * ============================================================================ */

/* Maximum memory map entries */
#define MAX_MEMORY_MAP_ENTRIES  256

/* Static storage for boot info and memory map */
static boot_info_t          g_boot_info;
static memory_map_entry_t   g_memory_map[MAX_MEMORY_MAP_ENTRIES];
static UINTN                g_memory_map_count = 0;

/* UEFI memory map storage */
static UINT8                g_uefi_mem_map[16384];
static UINTN                g_uefi_mem_map_size = sizeof(g_uefi_mem_map);
static UINTN                g_uefi_map_key = 0;
static UINTN                g_uefi_desc_size = 0;
static UINT32               g_uefi_desc_version = 0;

/* GOP information */
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop = NULL;

/* Kernel entry point */
static UINT64               g_kernel_entry = 0;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Simple memory copy
 */
static VOID mem_copy(VOID *dst, const VOID *src, UINTN size) {
    UINT8 *d = (UINT8 *)dst;
    const UINT8 *s = (const UINT8 *)src;
    while (size--) {
        *d++ = *s++;
    }
}

/**
 * Simple memory set
 */
static VOID mem_set(VOID *dst, UINT8 value, UINTN size) {
    UINT8 *d = (UINT8 *)dst;
    while (size--) {
        *d++ = value;
    }
}

/**
 * Print string using UEFI ConOut
 */
static VOID print(const CHAR16 *str) {
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)str);
}

/**
 * Print hex value
 */
static VOID print_hex(UINT64 value) {
    CHAR16 buffer[19]; /* "0x" + 16 digits + null */
    CHAR16 hex_chars[] = L"0123456789ABCDEF";

    buffer[0] = L'0';
    buffer[1] = L'x';

    for (INT32 i = 15; i >= 0; i--) {
        buffer[2 + (15 - i)] = hex_chars[(value >> (i * 4)) & 0xF];
    }
    buffer[18] = L'\0';

    print(buffer);
}

/**
 * Print decimal value
 */
static VOID print_dec(UINT64 value) {
    CHAR16 buffer[21]; /* Max 20 digits + null */
    INT32 i = 20;

    buffer[i] = L'\0';

    if (value == 0) {
        buffer[--i] = L'0';
    } else {
        while (value > 0) {
            buffer[--i] = L'0' + (value % 10);
            value /= 10;
        }
    }

    print(&buffer[i]);
}

/* ============================================================================
 * Convert UEFI memory type to E820 type
 * ============================================================================ */

static UINT32 convert_memory_type(EFI_MEMORY_TYPE uefi_type) {
    switch (uefi_type) {
        case EfiConventionalMemory:
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiLoaderCode:
        case EfiLoaderData:
            /* After ExitBootServices, these become usable */
            return MEMORY_TYPE_USABLE;

        case EfiACPIReclaimMemory:
            return MEMORY_TYPE_ACPI_RECL;

        case EfiACPIMemoryNVS:
            return MEMORY_TYPE_ACPI_NVS;

        case EfiUnusableMemory:
            return MEMORY_TYPE_BAD;

        case EfiReservedMemoryType:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace:
        case EfiPalCode:
        case EfiPersistentMemory:
        default:
            return MEMORY_TYPE_RESERVED;
    }
}

/* ============================================================================
 * Get Memory Map from UEFI
 * ============================================================================ */

EFI_STATUS get_memory_map(VOID) {
    EFI_STATUS status;

    print(L"[UEFI] Getting memory map...\r\n");

    /* Get UEFI memory map */
    g_uefi_mem_map_size = sizeof(g_uefi_mem_map);

    status = gBS->GetMemoryMap(
        &g_uefi_mem_map_size,
        (EFI_MEMORY_DESCRIPTOR *)g_uefi_mem_map,
        &g_uefi_map_key,
        &g_uefi_desc_size,
        &g_uefi_desc_version
    );

    if (EFI_ERROR(status)) {
        print(L"[UEFI] ERROR: Failed to get memory map\r\n");
        return status;
    }

    /* Convert UEFI memory map to E820 format */
    UINTN num_entries = g_uefi_mem_map_size / g_uefi_desc_size;
    g_memory_map_count = 0;

    for (UINTN i = 0; i < num_entries && g_memory_map_count < MAX_MEMORY_MAP_ENTRIES; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)
            (g_uefi_mem_map + i * g_uefi_desc_size);

        /* Convert to E820 format */
        g_memory_map[g_memory_map_count].base = desc->PhysicalStart;
        g_memory_map[g_memory_map_count].length = desc->NumberOfPages * EFI_PAGE_SIZE;
        g_memory_map[g_memory_map_count].type = convert_memory_type(desc->Type);
        g_memory_map[g_memory_map_count].acpi_attrs = 0;

        g_memory_map_count++;
    }

    print(L"[UEFI] Memory map entries: ");
    print_dec(g_memory_map_count);
    print(L"\r\n");

    return EFI_SUCCESS;
}

/* ============================================================================
 * Setup Graphics Output Protocol (GOP) Framebuffer
 * ============================================================================ */

EFI_STATUS setup_framebuffer(VOID) {
    EFI_STATUS status;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    print(L"[UEFI] Setting up framebuffer...\r\n");

    /* Locate GOP */
    status = gBS->LocateProtocol(&gop_guid, NULL, (VOID **)&g_gop);
    if (EFI_ERROR(status)) {
        print(L"[UEFI] WARNING: GOP not available, using fallback\r\n");

        /* Fallback: Use VGA text mode address */
        g_boot_info.framebuffer = 0xB8000;
        g_boot_info.fb_width = 80;
        g_boot_info.fb_height = 25;
        g_boot_info.fb_bpp = 16;
        g_boot_info.fb_pitch = 160;

        return EFI_SUCCESS;
    }

    /* Get current mode information */
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info = g_gop->Mode->Info;

    print(L"[UEFI] GOP Mode: ");
    print_dec(g_gop->Mode->Mode);
    print(L" Resolution: ");
    print_dec(mode_info->HorizontalResolution);
    print(L"x");
    print_dec(mode_info->VerticalResolution);
    print(L"\r\n");

    /* Try to find a better mode (prefer 1024x768 or higher) */
    UINT32 best_mode = g_gop->Mode->Mode;
    UINT32 best_width = mode_info->HorizontalResolution;
    UINT32 best_height = mode_info->VerticalResolution;

    for (UINT32 i = 0; i < g_gop->Mode->MaxMode; i++) {
        UINTN info_size;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;

        status = g_gop->QueryMode(g_gop, i, &info_size, &info);
        if (EFI_ERROR(status)) continue;

        /* Check if this is a 32-bit pixel format */
        if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
            info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {

            /* Prefer resolutions >= 800x600 but not too large */
            if (info->HorizontalResolution >= 800 &&
                info->HorizontalResolution <= 1920 &&
                info->VerticalResolution >= 600 &&
                info->HorizontalResolution * info->VerticalResolution >
                    best_width * best_height) {

                best_mode = i;
                best_width = info->HorizontalResolution;
                best_height = info->VerticalResolution;
            }
        }
    }

    /* Set the best mode if different from current */
    if (best_mode != g_gop->Mode->Mode) {
        print(L"[UEFI] Switching to mode ");
        print_dec(best_mode);
        print(L" (");
        print_dec(best_width);
        print(L"x");
        print_dec(best_height);
        print(L")\r\n");

        status = g_gop->SetMode(g_gop, best_mode);
        if (EFI_ERROR(status)) {
            print(L"[UEFI] WARNING: Failed to set mode, using current\r\n");
        }
    }

    /* Fill boot info with framebuffer data */
    g_boot_info.framebuffer = g_gop->Mode->FrameBufferBase;
    g_boot_info.fb_width = g_gop->Mode->Info->HorizontalResolution;
    g_boot_info.fb_height = g_gop->Mode->Info->VerticalResolution;
    g_boot_info.fb_bpp = 32; /* GOP always uses 32-bit pixels */
    g_boot_info.fb_pitch = g_gop->Mode->Info->PixelsPerScanLine * 4;

    print(L"[UEFI] Framebuffer at ");
    print_hex(g_boot_info.framebuffer);
    print(L"\r\n");

    return EFI_SUCCESS;
}

/* ============================================================================
 * Load Kernel ELF
 * ============================================================================ */

EFI_STATUS load_kernel(EFI_FILE_PROTOCOL *root, const CHAR16 *path) {
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *kernel_file = NULL;

    print(L"[UEFI] Loading kernel: ");
    print(path);
    print(L"\r\n");

    /* Open kernel file */
    status = root->Open(root, &kernel_file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        print(L"[UEFI] ERROR: Cannot open kernel file\r\n");
        return status;
    }

    /* Get file size using GetInfo */
    EFI_GUID file_info_guid = EFI_FILE_INFO_GUID;
    UINT8 file_info_buffer[256];
    UINTN file_info_size = sizeof(file_info_buffer);

    status = kernel_file->GetInfo(kernel_file, &file_info_guid,
                                   &file_info_size, file_info_buffer);
    if (EFI_ERROR(status)) {
        print(L"[UEFI] ERROR: Cannot get kernel file info\r\n");
        kernel_file->Close(kernel_file);
        return status;
    }

    EFI_FILE_INFO *file_info = (EFI_FILE_INFO *)file_info_buffer;
    UINT64 kernel_size = file_info->FileSize;

    print(L"[UEFI] Kernel size: ");
    print_dec(kernel_size);
    print(L" bytes\r\n");

    /* Allocate memory for kernel file */
    VOID *kernel_buffer = NULL;
    status = gBS->AllocatePool(EfiLoaderData, kernel_size, &kernel_buffer);
    if (EFI_ERROR(status)) {
        print(L"[UEFI] ERROR: Cannot allocate memory for kernel\r\n");
        kernel_file->Close(kernel_file);
        return status;
    }

    /* Read kernel file */
    UINTN read_size = kernel_size;
    status = kernel_file->Read(kernel_file, &read_size, kernel_buffer);
    kernel_file->Close(kernel_file);

    if (EFI_ERROR(status) || read_size != kernel_size) {
        print(L"[UEFI] ERROR: Failed to read kernel file\r\n");
        gBS->FreePool(kernel_buffer);
        return EFI_LOAD_ERROR;
    }

    /* Validate ELF header */
    elf64_header_t *elf = (elf64_header_t *)kernel_buffer;

    if (elf->e_ident[0] != ELF_MAGIC_0 ||
        elf->e_ident[1] != ELF_MAGIC_1 ||
        elf->e_ident[2] != ELF_MAGIC_2 ||
        elf->e_ident[3] != ELF_MAGIC_3) {
        print(L"[UEFI] ERROR: Invalid ELF magic\r\n");
        gBS->FreePool(kernel_buffer);
        return EFI_LOAD_ERROR;
    }

    if (elf->e_ident[4] != ELFCLASS64) {
        print(L"[UEFI] ERROR: Not a 64-bit ELF\r\n");
        gBS->FreePool(kernel_buffer);
        return EFI_LOAD_ERROR;
    }

    if (elf->e_ident[5] != ELFDATA2LSB) {
        print(L"[UEFI] ERROR: Not little-endian ELF\r\n");
        gBS->FreePool(kernel_buffer);
        return EFI_LOAD_ERROR;
    }

    if (elf->e_machine != EM_X86_64) {
        print(L"[UEFI] ERROR: Not x86_64 ELF\r\n");
        gBS->FreePool(kernel_buffer);
        return EFI_LOAD_ERROR;
    }

    if (elf->e_type != ET_EXEC && elf->e_type != ET_DYN) {
        print(L"[UEFI] ERROR: Not an executable ELF\r\n");
        gBS->FreePool(kernel_buffer);
        return EFI_LOAD_ERROR;
    }

    print(L"[UEFI] Valid ELF64 x86_64 executable\r\n");
    print(L"[UEFI] Entry point: ");
    print_hex(elf->e_entry);
    print(L"\r\n");

    /* Process program headers and load segments */
    elf64_phdr_t *phdrs = (elf64_phdr_t *)((UINT8 *)kernel_buffer + elf->e_phoff);

    /* First pass: calculate memory requirements */
    UINT64 min_vaddr = 0xFFFFFFFFFFFFFFFF;
    UINT64 max_vaddr = 0;

    for (UINT16 i = 0; i < elf->e_phnum; i++) {
        elf64_phdr_t *phdr = &phdrs[i];

        if (phdr->p_type == PT_LOAD) {
            if (phdr->p_vaddr < min_vaddr) {
                min_vaddr = phdr->p_vaddr;
            }
            if (phdr->p_vaddr + phdr->p_memsz > max_vaddr) {
                max_vaddr = phdr->p_vaddr + phdr->p_memsz;
            }
        }
    }

    /* For higher-half kernel, use physical address mapping */
    /* The kernel may be linked at 0xFFFFFFFF80100000 but loaded at 0x100000 */
    UINT64 load_base = 0x100000; /* Load at 1MB */
    UINT64 virt_base = min_vaddr;
    UINT64 offset = 0;

    if (virt_base >= 0xFFFFFFFF80000000ULL) {
        /* Higher-half kernel */
        offset = virt_base - load_base;
        print(L"[UEFI] Higher-half kernel detected\r\n");
    } else {
        load_base = min_vaddr;
    }

    UINT64 total_size = max_vaddr - min_vaddr;
    UINTN pages_needed = EFI_SIZE_TO_PAGES(total_size) + 1;

    print(L"[UEFI] Allocating ");
    print_dec(pages_needed);
    print(L" pages for kernel\r\n");

    /* Allocate memory for kernel at specific address */
    EFI_PHYSICAL_ADDRESS kernel_phys = load_base;
    status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
                                 pages_needed, &kernel_phys);

    if (EFI_ERROR(status)) {
        /* Try allocating anywhere */
        kernel_phys = 0;
        status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                     pages_needed, &kernel_phys);
        if (EFI_ERROR(status)) {
            print(L"[UEFI] ERROR: Cannot allocate pages for kernel\r\n");
            gBS->FreePool(kernel_buffer);
            return status;
        }
        load_base = kernel_phys;
        offset = virt_base - load_base;
    }

    print(L"[UEFI] Kernel physical base: ");
    print_hex(kernel_phys);
    print(L"\r\n");

    /* Clear allocated memory */
    mem_set((VOID *)kernel_phys, 0, EFI_PAGES_TO_SIZE(pages_needed));

    /* Second pass: load segments */
    for (UINT16 i = 0; i < elf->e_phnum; i++) {
        elf64_phdr_t *phdr = &phdrs[i];

        if (phdr->p_type == PT_LOAD) {
            /* Calculate physical load address */
            UINT64 segment_phys = phdr->p_vaddr - offset;

            print(L"[UEFI] Loading segment ");
            print_dec(i);
            print(L": vaddr=");
            print_hex(phdr->p_vaddr);
            print(L" paddr=");
            print_hex(segment_phys);
            print(L" size=");
            print_hex(phdr->p_filesz);
            print(L"\r\n");

            /* Copy segment data */
            if (phdr->p_filesz > 0) {
                mem_copy((VOID *)segment_phys,
                         (UINT8 *)kernel_buffer + phdr->p_offset,
                         phdr->p_filesz);
            }

            /* BSS is already zeroed by mem_set above */
        }
    }

    /* Set kernel entry point */
    g_kernel_entry = elf->e_entry - offset;

    print(L"[UEFI] Kernel loaded at physical ");
    print_hex(kernel_phys);
    print(L"\r\n");
    print(L"[UEFI] Physical entry point: ");
    print_hex(g_kernel_entry);
    print(L"\r\n");

    /* Free kernel file buffer */
    gBS->FreePool(kernel_buffer);

    return EFI_SUCCESS;
}

/* ============================================================================
 * Exit Boot Services and Jump to Kernel
 * ============================================================================ */

EFI_STATUS EFIAPI exit_boot_services(VOID) {
    EFI_STATUS status;

    print(L"[UEFI] Preparing to exit boot services...\r\n");

    /* Finalize boot info structure */
    g_boot_info.magic = BOOT_MAGIC;
    g_boot_info.reserved = 0;
    g_boot_info.mem_map_addr = (UINT64)g_memory_map;
    g_boot_info.mem_map_count = g_memory_map_count;

    /* Get final memory map (required for ExitBootServices) */
    g_uefi_mem_map_size = sizeof(g_uefi_mem_map);

    status = gBS->GetMemoryMap(
        &g_uefi_mem_map_size,
        (EFI_MEMORY_DESCRIPTOR *)g_uefi_mem_map,
        &g_uefi_map_key,
        &g_uefi_desc_size,
        &g_uefi_desc_version
    );

    if (EFI_ERROR(status)) {
        print(L"[UEFI] ERROR: Failed to get final memory map\r\n");
        return status;
    }

    /* Update E820-format memory map */
    UINTN num_entries = g_uefi_mem_map_size / g_uefi_desc_size;
    g_memory_map_count = 0;

    for (UINTN i = 0; i < num_entries && g_memory_map_count < MAX_MEMORY_MAP_ENTRIES; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)
            (g_uefi_mem_map + i * g_uefi_desc_size);

        g_memory_map[g_memory_map_count].base = desc->PhysicalStart;
        g_memory_map[g_memory_map_count].length = desc->NumberOfPages * EFI_PAGE_SIZE;
        g_memory_map[g_memory_map_count].type = convert_memory_type(desc->Type);
        g_memory_map[g_memory_map_count].acpi_attrs = 0;

        g_memory_map_count++;
    }

    g_boot_info.mem_map_count = g_memory_map_count;

    print(L"[UEFI] Exiting boot services...\r\n");

    /* Disable watchdog timer */
    gBS->SetWatchdogTimer(0, 0, 0, NULL);

    /* Exit boot services */
    status = gBS->ExitBootServices(gImageHandle, g_uefi_map_key);

    if (EFI_ERROR(status)) {
        /* Memory map may have changed, try again */
        g_uefi_mem_map_size = sizeof(g_uefi_mem_map);

        gBS->GetMemoryMap(
            &g_uefi_mem_map_size,
            (EFI_MEMORY_DESCRIPTOR *)g_uefi_mem_map,
            &g_uefi_map_key,
            &g_uefi_desc_size,
            &g_uefi_desc_version
        );

        status = gBS->ExitBootServices(gImageHandle, g_uefi_map_key);

        if (EFI_ERROR(status)) {
            /* Cannot print after potential partial exit */
            return status;
        }
    }

    /* === POINT OF NO RETURN === */
    /* Boot services are now gone - cannot use any UEFI services */
    /* Cannot print, allocate memory, or use UEFI protocols */

    /* Disable interrupts */
    __asm__ volatile("cli");

    /* Set up pointer to boot info for kernel */
    boot_info_t *boot_info_ptr = &g_boot_info;

    /* Jump to kernel entry point */
    /* Pass boot_info pointer in RDI (System V AMD64 ABI first argument) */
    typedef void (*kernel_entry_t)(boot_info_t *);
    kernel_entry_t kernel_main = (kernel_entry_t)g_kernel_entry;

    /* Call kernel - this should never return */
    kernel_main(boot_info_ptr);

    /* Should never reach here */
    while (1) {
        __asm__ volatile("hlt");
    }

    return EFI_SUCCESS;
}

/* ============================================================================
 * Get kernel entry point
 * ============================================================================ */

UINT64 get_kernel_entry(VOID) {
    return g_kernel_entry;
}

/* ============================================================================
 * Get boot info pointer
 * ============================================================================ */

boot_info_t *get_boot_info(VOID) {
    return &g_boot_info;
}
