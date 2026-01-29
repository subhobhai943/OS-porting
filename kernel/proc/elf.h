/**
 * AAAos Kernel - ELF64 Executable Loader
 *
 * Parses and loads ELF64 executables into memory.
 * Supports:
 *   - Standard ELF64 executables
 *   - Position Independent Executables (PIE)
 *   - x86_64 architecture
 */

#ifndef _AAAOS_PROC_ELF_H
#define _AAAOS_PROC_ELF_H

#include "../include/types.h"

/* ============================================================================
 * ELF Constants
 * ============================================================================ */

/* ELF Magic Number */
#define ELF_MAGIC_0         0x7F
#define ELF_MAGIC_1         'E'
#define ELF_MAGIC_2         'L'
#define ELF_MAGIC_3         'F'

/* ELF Identification Indices (e_ident) */
#define EI_MAG0             0       /* File identification byte 0 */
#define EI_MAG1             1       /* File identification byte 1 */
#define EI_MAG2             2       /* File identification byte 2 */
#define EI_MAG3             3       /* File identification byte 3 */
#define EI_CLASS            4       /* File class */
#define EI_DATA             5       /* Data encoding */
#define EI_VERSION          6       /* ELF header version */
#define EI_OSABI            7       /* OS/ABI identification */
#define EI_ABIVERSION       8       /* ABI version */
#define EI_PAD              9       /* Start of padding bytes */
#define EI_NIDENT           16      /* Size of e_ident[] */

/* ELF Class (e_ident[EI_CLASS]) */
#define ELFCLASSNONE        0       /* Invalid class */
#define ELFCLASS32          1       /* 32-bit objects */
#define ELFCLASS64          2       /* 64-bit objects */

/* ELF Data Encoding (e_ident[EI_DATA]) */
#define ELFDATANONE         0       /* Invalid data encoding */
#define ELFDATA2LSB         1       /* Little-endian */
#define ELFDATA2MSB         2       /* Big-endian */

/* ELF Version */
#define EV_NONE             0       /* Invalid version */
#define EV_CURRENT          1       /* Current version */

/* ELF OS/ABI (e_ident[EI_OSABI]) */
#define ELFOSABI_NONE       0       /* UNIX System V ABI */
#define ELFOSABI_SYSV       0       /* Alias for NONE */
#define ELFOSABI_LINUX      3       /* Linux */

/* ELF Type (e_type) */
#define ET_NONE             0       /* No file type */
#define ET_REL              1       /* Relocatable file */
#define ET_EXEC             2       /* Executable file */
#define ET_DYN              3       /* Shared object / PIE */
#define ET_CORE             4       /* Core file */

/* ELF Machine Type (e_machine) */
#define EM_NONE             0       /* No machine */
#define EM_386              3       /* Intel 80386 */
#define EM_X86_64           0x3E    /* AMD x86-64 */

/* Program Header Types (p_type) */
#define PT_NULL             0       /* Unused entry */
#define PT_LOAD             1       /* Loadable segment */
#define PT_DYNAMIC          2       /* Dynamic linking info */
#define PT_INTERP           3       /* Dynamic linker path */
#define PT_NOTE             4       /* Auxiliary information */
#define PT_SHLIB            5       /* Reserved */
#define PT_PHDR             6       /* Program header table */
#define PT_TLS              7       /* Thread-local storage segment */
#define PT_GNU_EH_FRAME     0x6474E550  /* Exception handling frame */
#define PT_GNU_STACK        0x6474E551  /* Stack executability */
#define PT_GNU_RELRO        0x6474E552  /* Read-only after relocation */

/* Segment Flags (p_flags) */
#define PF_X                BIT(0)  /* Executable */
#define PF_W                BIT(1)  /* Writable */
#define PF_R                BIT(2)  /* Readable */

/* Section Header Types (sh_type) */
#define SHT_NULL            0       /* Inactive section */
#define SHT_PROGBITS        1       /* Program data */
#define SHT_SYMTAB          2       /* Symbol table */
#define SHT_STRTAB          3       /* String table */
#define SHT_RELA            4       /* Relocation with addends */
#define SHT_HASH            5       /* Symbol hash table */
#define SHT_DYNAMIC         6       /* Dynamic linking info */
#define SHT_NOTE            7       /* Notes */
#define SHT_NOBITS          8       /* Uninitialized data (.bss) */
#define SHT_REL             9       /* Relocation without addends */
#define SHT_DYNSYM          11      /* Dynamic symbol table */

/* Section Flags (sh_flags) */
#define SHF_WRITE           BIT(0)  /* Writable */
#define SHF_ALLOC           BIT(1)  /* Occupies memory during execution */
#define SHF_EXECINSTR       BIT(2)  /* Executable */

/* Dynamic Section Tags (d_tag) */
#define DT_NULL             0       /* End of dynamic section */
#define DT_NEEDED           1       /* Name of needed library */
#define DT_PLTRELSZ         2       /* Size of PLT relocations */
#define DT_PLTGOT           3       /* PLT and/or GOT */
#define DT_HASH             4       /* Symbol hash table */
#define DT_STRTAB           5       /* String table */
#define DT_SYMTAB           6       /* Symbol table */
#define DT_RELA             7       /* Relocation table */
#define DT_RELASZ           8       /* Size of RELA relocations */
#define DT_RELAENT          9       /* Size of RELA entry */
#define DT_STRSZ            10      /* Size of string table */
#define DT_SYMENT           11      /* Size of symbol table entry */
#define DT_INIT             12      /* Address of init function */
#define DT_FINI             13      /* Address of fini function */
#define DT_RPATH            15      /* Library search path */
#define DT_REL              17      /* Relocation table */
#define DT_RELSZ            18      /* Size of REL relocations */
#define DT_RELENT           19      /* Size of REL entry */
#define DT_PLTREL           20      /* Type of PLT relocation */
#define DT_DEBUG            21      /* For debugging */
#define DT_JMPREL           23      /* PLT relocations */

/* Relocation Types for x86_64 */
#define R_X86_64_NONE       0       /* No relocation */
#define R_X86_64_64         1       /* Direct 64-bit */
#define R_X86_64_PC32       2       /* PC relative 32-bit signed */
#define R_X86_64_GOT32      3       /* 32-bit GOT entry */
#define R_X86_64_PLT32      4       /* 32-bit PLT address */
#define R_X86_64_COPY       5       /* Copy symbol at runtime */
#define R_X86_64_GLOB_DAT   6       /* Create GOT entry */
#define R_X86_64_JUMP_SLOT  7       /* Create PLT entry */
#define R_X86_64_RELATIVE   8       /* Adjust by program base */

/* ============================================================================
 * ELF Data Structures
 * ============================================================================ */

/**
 * ELF64 File Header
 * Located at the beginning of every ELF file
 */
typedef struct PACKED {
    uint8_t     e_ident[EI_NIDENT]; /* ELF identification */
    uint16_t    e_type;             /* Object file type */
    uint16_t    e_machine;          /* Machine type */
    uint32_t    e_version;          /* Object file version */
    uint64_t    e_entry;            /* Entry point address */
    uint64_t    e_phoff;            /* Program header offset */
    uint64_t    e_shoff;            /* Section header offset */
    uint32_t    e_flags;            /* Processor-specific flags */
    uint16_t    e_ehsize;           /* ELF header size */
    uint16_t    e_phentsize;        /* Size of program header entry */
    uint16_t    e_phnum;            /* Number of program header entries */
    uint16_t    e_shentsize;        /* Size of section header entry */
    uint16_t    e_shnum;            /* Number of section header entries */
    uint16_t    e_shstrndx;         /* Section name string table index */
} elf64_header_t;

/**
 * ELF64 Program Header
 * Describes a segment to be loaded into memory
 */
typedef struct PACKED {
    uint32_t    p_type;             /* Segment type */
    uint32_t    p_flags;            /* Segment flags */
    uint64_t    p_offset;           /* Offset in file */
    uint64_t    p_vaddr;            /* Virtual address in memory */
    uint64_t    p_paddr;            /* Physical address (unused) */
    uint64_t    p_filesz;           /* Size in file */
    uint64_t    p_memsz;            /* Size in memory */
    uint64_t    p_align;            /* Alignment */
} elf64_phdr_t;

/**
 * ELF64 Section Header
 * Describes a section in the file (for linking/debugging)
 */
typedef struct PACKED {
    uint32_t    sh_name;            /* Section name (string table index) */
    uint32_t    sh_type;            /* Section type */
    uint64_t    sh_flags;           /* Section flags */
    uint64_t    sh_addr;            /* Address in memory */
    uint64_t    sh_offset;          /* Offset in file */
    uint64_t    sh_size;            /* Size of section */
    uint32_t    sh_link;            /* Link to another section */
    uint32_t    sh_info;            /* Additional info */
    uint64_t    sh_addralign;       /* Alignment */
    uint64_t    sh_entsize;         /* Entry size if section holds table */
} elf64_shdr_t;

/**
 * ELF64 Symbol Table Entry
 */
typedef struct PACKED {
    uint32_t    st_name;            /* Symbol name (string table index) */
    uint8_t     st_info;            /* Symbol type and binding */
    uint8_t     st_other;           /* Symbol visibility */
    uint16_t    st_shndx;           /* Section index */
    uint64_t    st_value;           /* Symbol value */
    uint64_t    st_size;            /* Symbol size */
} elf64_sym_t;

/**
 * ELF64 Relocation Entry (without addend)
 */
typedef struct PACKED {
    uint64_t    r_offset;           /* Address to relocate */
    uint64_t    r_info;             /* Relocation type and symbol index */
} elf64_rel_t;

/**
 * ELF64 Relocation Entry (with addend)
 */
typedef struct PACKED {
    uint64_t    r_offset;           /* Address to relocate */
    uint64_t    r_info;             /* Relocation type and symbol index */
    int64_t     r_addend;           /* Addend */
} elf64_rela_t;

/**
 * ELF64 Dynamic Section Entry
 */
typedef struct PACKED {
    int64_t     d_tag;              /* Dynamic entry type */
    union {
        uint64_t d_val;             /* Integer value */
        uint64_t d_ptr;             /* Address value */
    } d_un;
} elf64_dyn_t;

/* Macros for extracting relocation info */
#define ELF64_R_SYM(info)       ((info) >> 32)
#define ELF64_R_TYPE(info)      ((info) & 0xFFFFFFFFL)
#define ELF64_R_INFO(sym, type) (((uint64_t)(sym) << 32) | ((type) & 0xFFFFFFFFL))

/* Macros for extracting symbol info */
#define ELF64_ST_BIND(info)     ((info) >> 4)
#define ELF64_ST_TYPE(info)     ((info) & 0xF)
#define ELF64_ST_INFO(bind, type) (((bind) << 4) | ((type) & 0xF))

/* Symbol binding values */
#define STB_LOCAL               0   /* Local symbol */
#define STB_GLOBAL              1   /* Global symbol */
#define STB_WEAK                2   /* Weak symbol */

/* Symbol type values */
#define STT_NOTYPE              0   /* Symbol type is unspecified */
#define STT_OBJECT              1   /* Symbol is a data object */
#define STT_FUNC                2   /* Symbol is a code object */
#define STT_SECTION             3   /* Symbol associated with a section */
#define STT_FILE                4   /* Symbol's name is file name */

/* ============================================================================
 * ELF Loader Error Codes
 * ============================================================================ */

typedef enum {
    ELF_SUCCESS = 0,            /* Success */
    ELF_ERR_NULL_POINTER,       /* Null pointer passed */
    ELF_ERR_TOO_SMALL,          /* Data too small for ELF header */
    ELF_ERR_BAD_MAGIC,          /* Invalid ELF magic number */
    ELF_ERR_NOT_64BIT,          /* Not an ELF64 file */
    ELF_ERR_BAD_ENDIAN,         /* Wrong endianness */
    ELF_ERR_BAD_VERSION,        /* Invalid ELF version */
    ELF_ERR_NOT_EXECUTABLE,     /* Not an executable or PIE */
    ELF_ERR_WRONG_ARCH,         /* Wrong architecture */
    ELF_ERR_NO_SEGMENTS,        /* No program headers */
    ELF_ERR_OUT_OF_MEMORY,      /* Memory allocation failed */
    ELF_ERR_INVALID_SEGMENT,    /* Invalid segment data */
    ELF_ERR_MAPPING_FAILED,     /* VMM mapping failed */
    ELF_ERR_RELOCATION_FAILED,  /* Relocation processing failed */
} elf_error_t;

/**
 * ELF Load Information
 * Returned after successful load
 */
typedef struct {
    virtaddr_t  entry_point;        /* Entry point address */
    virtaddr_t  base_address;       /* Load base address (for PIE) */
    virtaddr_t  load_end;           /* End of loaded segments */
    virtaddr_t  bss_start;          /* Start of BSS section */
    virtaddr_t  bss_end;            /* End of BSS section */
    bool        is_pie;             /* True if PIE executable */
    bool        has_interp;         /* True if has interpreter */
    char        interp_path[256];   /* Dynamic linker path (if any) */
} elf_load_info_t;

/* ============================================================================
 * ELF Loader Functions
 * ============================================================================ */

/**
 * Validate that data contains a valid ELF64 executable for x86_64
 * @param data Pointer to ELF file data
 * @param size Size of the data in bytes
 * @return ELF_SUCCESS if valid, error code otherwise
 */
elf_error_t elf_validate(const void *data, size_t size);

/**
 * Get a human-readable string for an ELF error code
 * @param error Error code
 * @return Static string describing the error
 */
const char* elf_error_string(elf_error_t error);

/**
 * Get the entry point address from an ELF file
 * Does NOT perform full validation
 * @param data Pointer to ELF file data
 * @return Entry point address, or 0 if invalid
 */
virtaddr_t elf_get_entry(const void *data);

/**
 * Get the ELF file type
 * @param data Pointer to ELF file data
 * @return ELF type (ET_EXEC, ET_DYN, etc.) or 0 if invalid
 */
uint16_t elf_get_type(const void *data);

/**
 * Check if ELF is a Position Independent Executable (PIE)
 * @param data Pointer to ELF file data
 * @return true if PIE, false otherwise
 */
bool elf_is_pie(const void *data);

/**
 * Load a single program segment into memory
 * @param phdr Pointer to program header
 * @param file_data Pointer to beginning of ELF file
 * @param base_addr Base address offset (for PIE)
 * @return ELF_SUCCESS on success, error code otherwise
 */
elf_error_t elf_load_segment(const elf64_phdr_t *phdr, const void *file_data,
                             virtaddr_t base_addr);

/**
 * Load an ELF64 executable into the current address space
 * @param data Pointer to ELF file data
 * @param size Size of the data in bytes
 * @param info Output: load information (entry point, base address, etc.)
 * @return ELF_SUCCESS on success, error code otherwise
 *
 * For PIE executables, a suitable base address is chosen automatically.
 * For static executables, segments are loaded at their specified addresses.
 */
elf_error_t elf_load(const void *data, size_t size, elf_load_info_t *info);

/**
 * Load an ELF64 executable at a specific base address
 * @param data Pointer to ELF file data
 * @param size Size of the data in bytes
 * @param base_addr Base address to load at (for PIE; ignored for static)
 * @param info Output: load information
 * @return ELF_SUCCESS on success, error code otherwise
 */
elf_error_t elf_load_at(const void *data, size_t size, virtaddr_t base_addr,
                        elf_load_info_t *info);

/**
 * Get program header at specified index
 * @param data Pointer to ELF file data
 * @param index Program header index
 * @return Pointer to program header, or NULL if invalid index
 */
const elf64_phdr_t* elf_get_phdr(const void *data, uint16_t index);

/**
 * Get section header at specified index
 * @param data Pointer to ELF file data
 * @param index Section header index
 * @return Pointer to section header, or NULL if invalid index
 */
const elf64_shdr_t* elf_get_shdr(const void *data, uint16_t index);

/**
 * Find a section by name
 * @param data Pointer to ELF file data
 * @param name Section name to find
 * @return Pointer to section header, or NULL if not found
 */
const elf64_shdr_t* elf_find_section(const void *data, const char *name);

/**
 * Get the string table pointer for section names
 * @param data Pointer to ELF file data
 * @return Pointer to string table, or NULL if not present
 */
const char* elf_get_shstrtab(const void *data);

/**
 * Dump ELF header info to serial console (for debugging)
 * @param data Pointer to ELF file data
 */
void elf_dump_header(const void *data);

/**
 * Dump program headers to serial console (for debugging)
 * @param data Pointer to ELF file data
 */
void elf_dump_phdrs(const void *data);

#endif /* _AAAOS_PROC_ELF_H */
