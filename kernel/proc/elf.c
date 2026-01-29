/**
 * AAAos Kernel - ELF64 Executable Loader Implementation
 *
 * This module provides functionality to parse and load ELF64 executables
 * into memory. Supports both standard executables (ET_EXEC) and Position
 * Independent Executables (PIE/ET_DYN).
 */

#include "elf.h"
#include "../include/serial.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Simple memcpy implementation for ELF loading
 */
static void* elf_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/**
 * Simple memset implementation for BSS zeroing
 */
static void* elf_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

/**
 * Simple strlen for interpreter path
 */
static size_t elf_strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

/**
 * Simple strncpy for interpreter path
 */
static char* elf_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

/**
 * Simple strcmp for section name lookup
 */
static int elf_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/**
 * Convert segment flags (PF_*) to VMM flags
 */
static uint64_t elf_flags_to_vmm(uint32_t p_flags) {
    uint64_t vmm_flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;

    if (p_flags & PF_W) {
        vmm_flags |= VMM_FLAG_WRITE;
    }

    /* Note: NX bit handling - executable if PF_X is set */
    if (!(p_flags & PF_X)) {
        vmm_flags |= VMM_FLAG_NX;
    }

    return vmm_flags;
}

/**
 * Get ELF header pointer (with basic null check)
 */
static const elf64_header_t* elf_get_header(const void *data) {
    if (!data) {
        return NULL;
    }
    return (const elf64_header_t *)data;
}

/* ============================================================================
 * Error String Function
 * ============================================================================ */

const char* elf_error_string(elf_error_t error) {
    switch (error) {
        case ELF_SUCCESS:               return "Success";
        case ELF_ERR_NULL_POINTER:      return "Null pointer";
        case ELF_ERR_TOO_SMALL:         return "Data too small for ELF header";
        case ELF_ERR_BAD_MAGIC:         return "Invalid ELF magic number";
        case ELF_ERR_NOT_64BIT:         return "Not an ELF64 file";
        case ELF_ERR_BAD_ENDIAN:        return "Wrong endianness (expected little-endian)";
        case ELF_ERR_BAD_VERSION:       return "Invalid ELF version";
        case ELF_ERR_NOT_EXECUTABLE:    return "Not an executable file";
        case ELF_ERR_WRONG_ARCH:        return "Wrong architecture (expected x86_64)";
        case ELF_ERR_NO_SEGMENTS:       return "No program headers";
        case ELF_ERR_OUT_OF_MEMORY:     return "Out of memory";
        case ELF_ERR_INVALID_SEGMENT:   return "Invalid segment";
        case ELF_ERR_MAPPING_FAILED:    return "Memory mapping failed";
        case ELF_ERR_RELOCATION_FAILED: return "Relocation failed";
        default:                        return "Unknown error";
    }
}

/* ============================================================================
 * Validation Functions
 * ============================================================================ */

elf_error_t elf_validate(const void *data, size_t size) {
    if (!data) {
        kprintf("[ELF] Validation failed: null pointer\n");
        return ELF_ERR_NULL_POINTER;
    }

    if (size < sizeof(elf64_header_t)) {
        kprintf("[ELF] Validation failed: data too small (%llu < %llu)\n",
                (uint64_t)size, (uint64_t)sizeof(elf64_header_t));
        return ELF_ERR_TOO_SMALL;
    }

    const elf64_header_t *hdr = elf_get_header(data);

    /* Check ELF magic number */
    if (hdr->e_ident[EI_MAG0] != ELF_MAGIC_0 ||
        hdr->e_ident[EI_MAG1] != ELF_MAGIC_1 ||
        hdr->e_ident[EI_MAG2] != ELF_MAGIC_2 ||
        hdr->e_ident[EI_MAG3] != ELF_MAGIC_3) {
        kprintf("[ELF] Validation failed: bad magic (0x%02x 0x%02x 0x%02x 0x%02x)\n",
                hdr->e_ident[EI_MAG0], hdr->e_ident[EI_MAG1],
                hdr->e_ident[EI_MAG2], hdr->e_ident[EI_MAG3]);
        return ELF_ERR_BAD_MAGIC;
    }

    /* Check 64-bit */
    if (hdr->e_ident[EI_CLASS] != ELFCLASS64) {
        kprintf("[ELF] Validation failed: not 64-bit (class=%d)\n",
                hdr->e_ident[EI_CLASS]);
        return ELF_ERR_NOT_64BIT;
    }

    /* Check little-endian */
    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        kprintf("[ELF] Validation failed: not little-endian (data=%d)\n",
                hdr->e_ident[EI_DATA]);
        return ELF_ERR_BAD_ENDIAN;
    }

    /* Check version */
    if (hdr->e_ident[EI_VERSION] != EV_CURRENT || hdr->e_version != EV_CURRENT) {
        kprintf("[ELF] Validation failed: bad version (%d/%d)\n",
                hdr->e_ident[EI_VERSION], hdr->e_version);
        return ELF_ERR_BAD_VERSION;
    }

    /* Check file type - must be executable or shared object (PIE) */
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
        kprintf("[ELF] Validation failed: not executable (type=%d)\n",
                hdr->e_type);
        return ELF_ERR_NOT_EXECUTABLE;
    }

    /* Check architecture */
    if (hdr->e_machine != EM_X86_64) {
        kprintf("[ELF] Validation failed: wrong architecture (machine=0x%x)\n",
                hdr->e_machine);
        return ELF_ERR_WRONG_ARCH;
    }

    /* Check program headers exist */
    if (hdr->e_phnum == 0 || hdr->e_phoff == 0) {
        kprintf("[ELF] Validation failed: no program headers\n");
        return ELF_ERR_NO_SEGMENTS;
    }

    /* Validate program header table fits in file */
    size_t phdr_end = hdr->e_phoff + (hdr->e_phnum * hdr->e_phentsize);
    if (phdr_end > size) {
        kprintf("[ELF] Validation failed: program headers exceed file size\n");
        return ELF_ERR_TOO_SMALL;
    }

    kprintf("[ELF] Validation successful: %s, %d segments\n",
            hdr->e_type == ET_EXEC ? "executable" : "PIE/shared",
            hdr->e_phnum);

    return ELF_SUCCESS;
}

/* ============================================================================
 * Information Query Functions
 * ============================================================================ */

virtaddr_t elf_get_entry(const void *data) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr) {
        return 0;
    }
    return (virtaddr_t)hdr->e_entry;
}

uint16_t elf_get_type(const void *data) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr) {
        return 0;
    }
    return hdr->e_type;
}

bool elf_is_pie(const void *data) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr) {
        return false;
    }
    /* PIE executables are ET_DYN with an entry point */
    return (hdr->e_type == ET_DYN && hdr->e_entry != 0);
}

const elf64_phdr_t* elf_get_phdr(const void *data, uint16_t index) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr || index >= hdr->e_phnum) {
        return NULL;
    }

    const uint8_t *base = (const uint8_t *)data;
    return (const elf64_phdr_t *)(base + hdr->e_phoff + (index * hdr->e_phentsize));
}

const elf64_shdr_t* elf_get_shdr(const void *data, uint16_t index) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr || index >= hdr->e_shnum || hdr->e_shoff == 0) {
        return NULL;
    }

    const uint8_t *base = (const uint8_t *)data;
    return (const elf64_shdr_t *)(base + hdr->e_shoff + (index * hdr->e_shentsize));
}

const char* elf_get_shstrtab(const void *data) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr || hdr->e_shstrndx == 0 || hdr->e_shstrndx >= hdr->e_shnum) {
        return NULL;
    }

    const elf64_shdr_t *strtab_shdr = elf_get_shdr(data, hdr->e_shstrndx);
    if (!strtab_shdr) {
        return NULL;
    }

    const uint8_t *base = (const uint8_t *)data;
    return (const char *)(base + strtab_shdr->sh_offset);
}

const elf64_shdr_t* elf_find_section(const void *data, const char *name) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr || !name) {
        return NULL;
    }

    const char *shstrtab = elf_get_shstrtab(data);
    if (!shstrtab) {
        return NULL;
    }

    for (uint16_t i = 0; i < hdr->e_shnum; i++) {
        const elf64_shdr_t *shdr = elf_get_shdr(data, i);
        if (shdr && elf_strcmp(shstrtab + shdr->sh_name, name) == 0) {
            return shdr;
        }
    }

    return NULL;
}

/* ============================================================================
 * Segment Loading Functions
 * ============================================================================ */

elf_error_t elf_load_segment(const elf64_phdr_t *phdr, const void *file_data,
                             virtaddr_t base_addr) {
    if (!phdr || !file_data) {
        return ELF_ERR_NULL_POINTER;
    }

    /* Only load PT_LOAD segments */
    if (phdr->p_type != PT_LOAD) {
        return ELF_SUCCESS;  /* Not an error, just skip */
    }

    /* Calculate virtual address with base offset */
    virtaddr_t vaddr = phdr->p_vaddr + base_addr;
    virtaddr_t vaddr_aligned = ALIGN_DOWN(vaddr, PAGE_SIZE);
    size_t page_offset = vaddr - vaddr_aligned;

    /* Calculate memory size (must be at least file size) */
    size_t memsz = phdr->p_memsz;
    size_t filesz = phdr->p_filesz;

    if (memsz == 0) {
        kprintf("[ELF] Skipping empty segment at 0x%llx\n", vaddr);
        return ELF_SUCCESS;
    }

    /* Calculate number of pages needed */
    size_t total_size = page_offset + memsz;
    size_t num_pages = ALIGN_UP(total_size, PAGE_SIZE) / PAGE_SIZE;

    kprintf("[ELF] Loading segment: vaddr=0x%llx, filesz=%llu, memsz=%llu, pages=%llu\n",
            vaddr, (uint64_t)filesz, (uint64_t)memsz, (uint64_t)num_pages);

    /* Get VMM flags for this segment */
    uint64_t vmm_flags = elf_flags_to_vmm(phdr->p_flags);

    kprintf("[ELF]   Flags: %c%c%c -> VMM flags 0x%llx\n",
            (phdr->p_flags & PF_R) ? 'R' : '-',
            (phdr->p_flags & PF_W) ? 'W' : '-',
            (phdr->p_flags & PF_X) ? 'X' : '-',
            vmm_flags);

    /* Allocate and map pages */
    for (size_t i = 0; i < num_pages; i++) {
        virtaddr_t page_vaddr = vaddr_aligned + (i * PAGE_SIZE);

        /* Check if page is already mapped */
        if (vmm_is_mapped(page_vaddr)) {
            kprintf("[ELF]   Page 0x%llx already mapped, skipping allocation\n",
                    page_vaddr);
            continue;
        }

        /* Allocate a physical page */
        physaddr_t page_phys = pmm_alloc_page();
        if (page_phys == 0) {
            kprintf("[ELF] ERROR: Failed to allocate physical page\n");
            return ELF_ERR_OUT_OF_MEMORY;
        }

        /* Map the page */
        if (!vmm_map_page(page_vaddr, page_phys, vmm_flags)) {
            kprintf("[ELF] ERROR: Failed to map page 0x%llx -> 0x%llx\n",
                    page_vaddr, page_phys);
            pmm_free_page(page_phys);
            return ELF_ERR_MAPPING_FAILED;
        }

        /* Zero the page initially */
        elf_memset((void *)page_vaddr, 0, PAGE_SIZE);
    }

    /* Copy segment data from file */
    if (filesz > 0) {
        const uint8_t *src = (const uint8_t *)file_data + phdr->p_offset;
        elf_memcpy((void *)vaddr, src, filesz);
        kprintf("[ELF]   Copied %llu bytes from file offset 0x%llx\n",
                (uint64_t)filesz, phdr->p_offset);
    }

    /* BSS portion (memsz > filesz) is already zeroed from memset above */
    if (memsz > filesz) {
        kprintf("[ELF]   BSS: %llu bytes zeroed\n", (uint64_t)(memsz - filesz));
    }

    return ELF_SUCCESS;
}

/* ============================================================================
 * Main ELF Loading Functions
 * ============================================================================ */

/**
 * Calculate the memory range needed for all PT_LOAD segments
 */
static void elf_calc_load_range(const void *data, virtaddr_t *min_addr,
                                virtaddr_t *max_addr) {
    const elf64_header_t *hdr = elf_get_header(data);
    *min_addr = UINT64_MAX;
    *max_addr = 0;

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = elf_get_phdr(data, i);
        if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0) {
            continue;
        }

        virtaddr_t seg_start = phdr->p_vaddr;
        virtaddr_t seg_end = phdr->p_vaddr + phdr->p_memsz;

        if (seg_start < *min_addr) {
            *min_addr = seg_start;
        }
        if (seg_end > *max_addr) {
            *max_addr = seg_end;
        }
    }
}

/**
 * Process PT_INTERP segment to extract dynamic linker path
 */
static void elf_process_interp(const void *data, elf_load_info_t *info) {
    const elf64_header_t *hdr = elf_get_header(data);

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = elf_get_phdr(data, i);
        if (phdr->p_type == PT_INTERP && phdr->p_filesz > 0) {
            info->has_interp = true;

            const char *interp = (const char *)((const uint8_t *)data + phdr->p_offset);
            size_t len = elf_strlen(interp);
            if (len >= sizeof(info->interp_path)) {
                len = sizeof(info->interp_path) - 1;
            }
            elf_strncpy(info->interp_path, interp, len);
            info->interp_path[len] = '\0';

            kprintf("[ELF] Found interpreter: %s\n", info->interp_path);
            return;
        }
    }

    info->has_interp = false;
    info->interp_path[0] = '\0';
}

/**
 * Apply relocations for PIE executable
 */
static elf_error_t elf_apply_relocations(const void *data, virtaddr_t base_addr) {
    const elf64_header_t *hdr = elf_get_header(data);

    /* Find PT_DYNAMIC segment */
    const elf64_phdr_t *dynamic_phdr = NULL;
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = elf_get_phdr(data, i);
        if (phdr->p_type == PT_DYNAMIC) {
            dynamic_phdr = phdr;
            break;
        }
    }

    if (!dynamic_phdr) {
        /* No dynamic section - static executable or no relocations */
        kprintf("[ELF] No PT_DYNAMIC segment, skipping relocations\n");
        return ELF_SUCCESS;
    }

    kprintf("[ELF] Processing dynamic section at offset 0x%llx\n",
            dynamic_phdr->p_offset);

    /* Parse dynamic section to find relocation tables */
    const elf64_dyn_t *dyn = (const elf64_dyn_t *)((const uint8_t *)data +
                                                    dynamic_phdr->p_offset);

    virtaddr_t rela_addr = 0;
    size_t rela_size = 0;
    size_t rela_ent = 0;

    while (dyn->d_tag != DT_NULL) {
        switch (dyn->d_tag) {
            case DT_RELA:
                rela_addr = dyn->d_un.d_ptr;
                break;
            case DT_RELASZ:
                rela_size = dyn->d_un.d_val;
                break;
            case DT_RELAENT:
                rela_ent = dyn->d_un.d_val;
                break;
        }
        dyn++;
    }

    if (rela_addr == 0 || rela_size == 0 || rela_ent == 0) {
        kprintf("[ELF] No RELA relocations found\n");
        return ELF_SUCCESS;
    }

    kprintf("[ELF] Found RELA: addr=0x%llx, size=%llu, entsize=%llu\n",
            rela_addr, (uint64_t)rela_size, (uint64_t)rela_ent);

    /* Process relocations */
    size_t rela_count = rela_size / rela_ent;
    virtaddr_t rela_vaddr = rela_addr + base_addr;

    for (size_t i = 0; i < rela_count; i++) {
        const elf64_rela_t *rela = (const elf64_rela_t *)(rela_vaddr + (i * rela_ent));
        uint32_t type = ELF64_R_TYPE(rela->r_info);

        virtaddr_t target = rela->r_offset + base_addr;

        switch (type) {
            case R_X86_64_RELATIVE: {
                /* Adjust by base address: *target = base + addend */
                uint64_t *ptr = (uint64_t *)target;
                *ptr = base_addr + rela->r_addend;
                break;
            }

            case R_X86_64_NONE:
                /* No action needed */
                break;

            default:
                kprintf("[ELF] Warning: Unsupported relocation type %d at 0x%llx\n",
                        type, target);
                break;
        }
    }

    kprintf("[ELF] Applied %llu relocations\n", (uint64_t)rela_count);
    return ELF_SUCCESS;
}

elf_error_t elf_load_at(const void *data, size_t size, virtaddr_t base_addr,
                        elf_load_info_t *info) {
    /* Validate the ELF file */
    elf_error_t err = elf_validate(data, size);
    if (err != ELF_SUCCESS) {
        return err;
    }

    if (!info) {
        return ELF_ERR_NULL_POINTER;
    }

    const elf64_header_t *hdr = elf_get_header(data);

    /* Initialize load info */
    info->is_pie = elf_is_pie(data);
    info->base_address = base_addr;
    info->bss_start = 0;
    info->bss_end = 0;

    /* For non-PIE executables, base_addr should be 0 */
    if (hdr->e_type == ET_EXEC && base_addr != 0) {
        kprintf("[ELF] Warning: base_addr ignored for static executable\n");
        base_addr = 0;
        info->base_address = 0;
    }

    kprintf("[ELF] Loading %s executable, base=0x%llx\n",
            info->is_pie ? "PIE" : "static", base_addr);

    /* Process interpreter segment */
    elf_process_interp(data, info);

    /* Load all PT_LOAD segments */
    virtaddr_t load_end = 0;

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = elf_get_phdr(data, i);

        if (phdr->p_type == PT_LOAD) {
            err = elf_load_segment(phdr, data, base_addr);
            if (err != ELF_SUCCESS) {
                kprintf("[ELF] ERROR: Failed to load segment %d: %s\n",
                        i, elf_error_string(err));
                return err;
            }

            /* Track BSS region */
            if (phdr->p_memsz > phdr->p_filesz) {
                virtaddr_t bss_start = phdr->p_vaddr + phdr->p_filesz + base_addr;
                virtaddr_t bss_end = phdr->p_vaddr + phdr->p_memsz + base_addr;
                if (info->bss_start == 0 || bss_start < info->bss_start) {
                    info->bss_start = bss_start;
                }
                if (bss_end > info->bss_end) {
                    info->bss_end = bss_end;
                }
            }

            /* Track end of loaded segments */
            virtaddr_t seg_end = phdr->p_vaddr + phdr->p_memsz + base_addr;
            if (seg_end > load_end) {
                load_end = seg_end;
            }
        }
    }

    info->load_end = load_end;

    /* Apply relocations for PIE */
    if (info->is_pie) {
        err = elf_apply_relocations(data, base_addr);
        if (err != ELF_SUCCESS) {
            kprintf("[ELF] ERROR: Failed to apply relocations: %s\n",
                    elf_error_string(err));
            return err;
        }
    }

    /* Set entry point (adjusted for base address) */
    info->entry_point = hdr->e_entry + base_addr;

    kprintf("[ELF] Load complete: entry=0x%llx, end=0x%llx\n",
            info->entry_point, info->load_end);

    return ELF_SUCCESS;
}

elf_error_t elf_load(const void *data, size_t size, elf_load_info_t *info) {
    /* Validate first */
    elf_error_t err = elf_validate(data, size);
    if (err != ELF_SUCCESS) {
        return err;
    }

    const elf64_header_t *hdr = elf_get_header(data);

    /* Determine base address */
    virtaddr_t base_addr = 0;

    if (hdr->e_type == ET_DYN) {
        /*
         * PIE executable - choose a suitable base address
         * We'll use a typical user-space base address.
         * In a real system, this would use ASLR.
         */
        virtaddr_t min_addr, max_addr;
        elf_calc_load_range(data, &min_addr, &max_addr);

        /* Default PIE base address in user space */
        #define PIE_DEFAULT_BASE    0x400000ULL

        /*
         * If the ELF specifies addresses starting at 0, we need
         * to relocate to a valid user-space address
         */
        if (min_addr == 0 || min_addr < PIE_DEFAULT_BASE) {
            base_addr = PIE_DEFAULT_BASE;
            kprintf("[ELF] PIE: Using base address 0x%llx\n", base_addr);
        }
    }

    return elf_load_at(data, size, base_addr, info);
}

/* ============================================================================
 * Debug/Dump Functions
 * ============================================================================ */

void elf_dump_header(const void *data) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr) {
        kprintf("[ELF] Cannot dump header: null pointer\n");
        return;
    }

    kprintf("[ELF] === ELF Header ===\n");
    kprintf("[ELF] Magic:       %02x %02x %02x %02x\n",
            hdr->e_ident[0], hdr->e_ident[1], hdr->e_ident[2], hdr->e_ident[3]);
    kprintf("[ELF] Class:       %s\n",
            hdr->e_ident[EI_CLASS] == ELFCLASS64 ? "ELF64" :
            hdr->e_ident[EI_CLASS] == ELFCLASS32 ? "ELF32" : "Unknown");
    kprintf("[ELF] Data:        %s\n",
            hdr->e_ident[EI_DATA] == ELFDATA2LSB ? "Little Endian" :
            hdr->e_ident[EI_DATA] == ELFDATA2MSB ? "Big Endian" : "Unknown");
    kprintf("[ELF] Version:     %d\n", hdr->e_ident[EI_VERSION]);
    kprintf("[ELF] OS/ABI:      %d\n", hdr->e_ident[EI_OSABI]);

    const char *type_str;
    switch (hdr->e_type) {
        case ET_NONE:   type_str = "NONE"; break;
        case ET_REL:    type_str = "REL (Relocatable)"; break;
        case ET_EXEC:   type_str = "EXEC (Executable)"; break;
        case ET_DYN:    type_str = "DYN (Shared/PIE)"; break;
        case ET_CORE:   type_str = "CORE"; break;
        default:        type_str = "Unknown"; break;
    }
    kprintf("[ELF] Type:        %s\n", type_str);

    kprintf("[ELF] Machine:     0x%x (%s)\n", hdr->e_machine,
            hdr->e_machine == EM_X86_64 ? "x86_64" : "other");
    kprintf("[ELF] Entry:       0x%llx\n", hdr->e_entry);
    kprintf("[ELF] PHOFF:       0x%llx\n", hdr->e_phoff);
    kprintf("[ELF] SHOFF:       0x%llx\n", hdr->e_shoff);
    kprintf("[ELF] Flags:       0x%x\n", hdr->e_flags);
    kprintf("[ELF] Header Size: %d\n", hdr->e_ehsize);
    kprintf("[ELF] PH Entry:    %d bytes\n", hdr->e_phentsize);
    kprintf("[ELF] PH Count:    %d\n", hdr->e_phnum);
    kprintf("[ELF] SH Entry:    %d bytes\n", hdr->e_shentsize);
    kprintf("[ELF] SH Count:    %d\n", hdr->e_shnum);
    kprintf("[ELF] SH StrIdx:   %d\n", hdr->e_shstrndx);
}

void elf_dump_phdrs(const void *data) {
    const elf64_header_t *hdr = elf_get_header(data);
    if (!hdr) {
        kprintf("[ELF] Cannot dump phdrs: null pointer\n");
        return;
    }

    kprintf("[ELF] === Program Headers (%d entries) ===\n", hdr->e_phnum);

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = elf_get_phdr(data, i);
        if (!phdr) continue;

        const char *type_str;
        switch (phdr->p_type) {
            case PT_NULL:           type_str = "NULL"; break;
            case PT_LOAD:           type_str = "LOAD"; break;
            case PT_DYNAMIC:        type_str = "DYNAMIC"; break;
            case PT_INTERP:         type_str = "INTERP"; break;
            case PT_NOTE:           type_str = "NOTE"; break;
            case PT_PHDR:           type_str = "PHDR"; break;
            case PT_TLS:            type_str = "TLS"; break;
            case PT_GNU_EH_FRAME:   type_str = "GNU_EH_FRAME"; break;
            case PT_GNU_STACK:      type_str = "GNU_STACK"; break;
            case PT_GNU_RELRO:      type_str = "GNU_RELRO"; break;
            default:                type_str = "UNKNOWN"; break;
        }

        kprintf("[ELF] [%2d] %-14s ", i, type_str);
        kprintf("off=0x%08llx vaddr=0x%08llx ", phdr->p_offset, phdr->p_vaddr);
        kprintf("filesz=0x%06llx memsz=0x%06llx ", phdr->p_filesz, phdr->p_memsz);
        kprintf("%c%c%c align=0x%llx\n",
                (phdr->p_flags & PF_R) ? 'R' : '-',
                (phdr->p_flags & PF_W) ? 'W' : '-',
                (phdr->p_flags & PF_X) ? 'X' : '-',
                phdr->p_align);
    }
}
