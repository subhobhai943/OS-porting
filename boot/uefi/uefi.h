/**
 * AAAos UEFI Bootloader - UEFI Type Definitions and Protocols
 *
 * This header provides UEFI types, structures, and protocol definitions
 * required for the UEFI bootloader implementation.
 */

#ifndef _AAAOS_UEFI_H
#define _AAAOS_UEFI_H

/* ============================================================================
 * Basic UEFI Types
 * ============================================================================ */

/* Fixed-width types for UEFI */
typedef unsigned char       UINT8;
typedef signed char         INT8;
typedef unsigned short      UINT16;
typedef signed short        INT16;
typedef unsigned int        UINT32;
typedef signed int          INT32;
typedef unsigned long long  UINT64;
typedef signed long long    INT64;

/* Native-width types */
typedef UINT64              UINTN;
typedef INT64               INTN;

/* Pointer and boolean types */
typedef void                VOID;
typedef UINT8               BOOLEAN;
typedef UINT16              CHAR16;
typedef char                CHAR8;

#define TRUE                1
#define FALSE               0
#define NULL                ((VOID *)0)

/* UEFI calling convention */
#define EFIAPI              __attribute__((ms_abi))

/* UEFI Status codes */
typedef UINTN               EFI_STATUS;

#define EFI_SUCCESS                     0
#define EFI_ERROR_BIT                   (1ULL << 63)
#define EFI_ERROR(status)               ((INTN)(status) < 0)

#define EFI_LOAD_ERROR                  (EFI_ERROR_BIT | 1)
#define EFI_INVALID_PARAMETER           (EFI_ERROR_BIT | 2)
#define EFI_UNSUPPORTED                 (EFI_ERROR_BIT | 3)
#define EFI_BAD_BUFFER_SIZE             (EFI_ERROR_BIT | 4)
#define EFI_BUFFER_TOO_SMALL            (EFI_ERROR_BIT | 5)
#define EFI_NOT_READY                   (EFI_ERROR_BIT | 6)
#define EFI_DEVICE_ERROR                (EFI_ERROR_BIT | 7)
#define EFI_WRITE_PROTECTED             (EFI_ERROR_BIT | 8)
#define EFI_OUT_OF_RESOURCES            (EFI_ERROR_BIT | 9)
#define EFI_NOT_FOUND                   (EFI_ERROR_BIT | 14)
#define EFI_ACCESS_DENIED               (EFI_ERROR_BIT | 15)
#define EFI_ABORTED                     (EFI_ERROR_BIT | 21)

/* Handle and event types */
typedef VOID                *EFI_HANDLE;
typedef VOID                *EFI_EVENT;
typedef UINT64              EFI_PHYSICAL_ADDRESS;
typedef UINT64              EFI_VIRTUAL_ADDRESS;
typedef UINT64              EFI_LBA;

/* GUID type */
typedef struct {
    UINT32  Data1;
    UINT16  Data2;
    UINT16  Data3;
    UINT8   Data4[8];
} EFI_GUID;

/* Memory types */
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

/* Memory descriptor */
typedef struct {
    UINT32                  Type;
    EFI_PHYSICAL_ADDRESS    PhysicalStart;
    EFI_VIRTUAL_ADDRESS     VirtualStart;
    UINT64                  NumberOfPages;
    UINT64                  Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Memory attributes */
#define EFI_MEMORY_UC               0x0000000000000001ULL
#define EFI_MEMORY_WC               0x0000000000000002ULL
#define EFI_MEMORY_WT               0x0000000000000004ULL
#define EFI_MEMORY_WB               0x0000000000000008ULL
#define EFI_MEMORY_UCE              0x0000000000000010ULL
#define EFI_MEMORY_WP               0x0000000000001000ULL
#define EFI_MEMORY_RP               0x0000000000002000ULL
#define EFI_MEMORY_XP               0x0000000000004000ULL
#define EFI_MEMORY_RUNTIME          0x8000000000000000ULL

/* Allocate type */
typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

/* ============================================================================
 * Table Header
 * ============================================================================ */

typedef struct {
    UINT64  Signature;
    UINT32  Revision;
    UINT32  HeaderSize;
    UINT32  CRC32;
    UINT32  Reserved;
} EFI_TABLE_HEADER;

/* ============================================================================
 * Simple Text Output Protocol
 * ============================================================================ */

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    CHAR16                                  *String
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    UINTN                                   Column,
    UINTN                                   Row
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    UINTN                                   Attribute
);

typedef struct {
    INT32   MaxMode;
    INT32   Mode;
    INT32   Attribute;
    INT32   CursorColumn;
    INT32   CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID                            *Reset;
    EFI_TEXT_STRING                 OutputString;
    VOID                            *TestString;
    VOID                            *QueryMode;
    VOID                            *SetMode;
    EFI_TEXT_SET_ATTRIBUTE          SetAttribute;
    EFI_TEXT_CLEAR_SCREEN           ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION    SetCursorPosition;
    VOID                            *EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE         *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Text attributes */
#define EFI_BLACK           0x00
#define EFI_BLUE            0x01
#define EFI_GREEN           0x02
#define EFI_CYAN            0x03
#define EFI_RED             0x04
#define EFI_MAGENTA         0x05
#define EFI_BROWN           0x06
#define EFI_LIGHTGRAY       0x07
#define EFI_DARKGRAY        0x08
#define EFI_LIGHTBLUE       0x09
#define EFI_LIGHTGREEN      0x0A
#define EFI_LIGHTCYAN       0x0B
#define EFI_LIGHTRED        0x0C
#define EFI_LIGHTMAGENTA    0x0D
#define EFI_YELLOW          0x0E
#define EFI_WHITE           0x0F
#define EFI_BACKGROUND_BLACK    0x00
#define EFI_BACKGROUND_BLUE     0x10
#define EFI_BACKGROUND_GREEN    0x20
#define EFI_BACKGROUND_CYAN     0x30
#define EFI_BACKGROUND_RED      0x40
#define EFI_BACKGROUND_MAGENTA  0x50
#define EFI_BACKGROUND_BROWN    0x60
#define EFI_BACKGROUND_LIGHTGRAY 0x70

/* ============================================================================
 * Boot Services
 * ============================================================================ */

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    EFI_ALLOCATE_TYPE       Type,
    EFI_MEMORY_TYPE         MemoryType,
    UINTN                   Pages,
    EFI_PHYSICAL_ADDRESS    *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS    Memory,
    UINTN                   Pages
);

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN                   *MemoryMapSize,
    EFI_MEMORY_DESCRIPTOR   *MemoryMap,
    UINTN                   *MapKey,
    UINTN                   *DescriptorSize,
    UINT32                  *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE         PoolType,
    UINTN                   Size,
    VOID                    **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(
    VOID                    *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(
    UINTN                   Timeout,
    UINT64                  WatchdogCode,
    UINTN                   DataSize,
    CHAR16                  *WatchdogData
);

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE              Handle,
    EFI_GUID                *Protocol,
    VOID                    **Interface
);

typedef enum {
    AllHandles,
    ByRegisterNotify,
    ByProtocol
} EFI_LOCATE_SEARCH_TYPE;

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE)(
    EFI_LOCATE_SEARCH_TYPE  SearchType,
    EFI_GUID                *Protocol,
    VOID                    *SearchKey,
    UINTN                   *BufferSize,
    EFI_HANDLE              *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID                *Protocol,
    VOID                    *Registration,
    VOID                    **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE              ImageHandle,
    UINTN                   MapKey
);

typedef EFI_STATUS (EFIAPI *EFI_STALL)(
    UINTN                   Microseconds
);

typedef struct {
    EFI_TABLE_HEADER            Hdr;

    /* Task Priority Services */
    VOID                        *RaiseTPL;
    VOID                        *RestoreTPL;

    /* Memory Services */
    EFI_ALLOCATE_PAGES          AllocatePages;
    EFI_FREE_PAGES              FreePages;
    EFI_GET_MEMORY_MAP          GetMemoryMap;
    EFI_ALLOCATE_POOL           AllocatePool;
    EFI_FREE_POOL               FreePool;

    /* Event & Timer Services */
    VOID                        *CreateEvent;
    VOID                        *SetTimer;
    VOID                        *WaitForEvent;
    VOID                        *SignalEvent;
    VOID                        *CloseEvent;
    VOID                        *CheckEvent;

    /* Protocol Handler Services */
    VOID                        *InstallProtocolInterface;
    VOID                        *ReinstallProtocolInterface;
    VOID                        *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL         HandleProtocol;
    VOID                        *Reserved;
    VOID                        *RegisterProtocolNotify;
    EFI_LOCATE_HANDLE           LocateHandle;
    VOID                        *LocateDevicePath;
    VOID                        *InstallConfigurationTable;

    /* Image Services */
    VOID                        *LoadImage;
    VOID                        *StartImage;
    VOID                        *Exit;
    VOID                        *UnloadImage;
    EFI_EXIT_BOOT_SERVICES      ExitBootServices;

    /* Miscellaneous Services */
    VOID                        *GetNextMonotonicCount;
    EFI_STALL                   Stall;
    EFI_SET_WATCHDOG_TIMER      SetWatchdogTimer;

    /* DriverSupport Services */
    VOID                        *ConnectController;
    VOID                        *DisconnectController;

    /* Open and Close Protocol Services */
    VOID                        *OpenProtocol;
    VOID                        *CloseProtocol;
    VOID                        *OpenProtocolInformation;

    /* Library Services */
    VOID                        *ProtocolsPerHandle;
    VOID                        *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL         LocateProtocol;
    VOID                        *InstallMultipleProtocolInterfaces;
    VOID                        *UninstallMultipleProtocolInterfaces;

    /* 32-bit CRC Services */
    VOID                        *CalculateCrc32;

    /* Miscellaneous Services */
    VOID                        *CopyMem;
    VOID                        *SetMem;
    VOID                        *CreateEventEx;
} EFI_BOOT_SERVICES;

/* ============================================================================
 * Runtime Services
 * ============================================================================ */

typedef struct {
    EFI_TABLE_HEADER    Hdr;

    /* Time Services */
    VOID                *GetTime;
    VOID                *SetTime;
    VOID                *GetWakeupTime;
    VOID                *SetWakeupTime;

    /* Virtual Memory Services */
    VOID                *SetVirtualAddressMap;
    VOID                *ConvertPointer;

    /* Variable Services */
    VOID                *GetVariable;
    VOID                *GetNextVariableName;
    VOID                *SetVariable;

    /* Miscellaneous Services */
    VOID                *GetNextHighMonotonicCount;
    VOID                *ResetSystem;

    /* UEFI 2.0 Capsule Services */
    VOID                *UpdateCapsule;
    VOID                *QueryCapsuleCapabilities;

    /* Miscellaneous UEFI 2.0 Services */
    VOID                *QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

/* ============================================================================
 * System Table
 * ============================================================================ */

typedef struct {
    EFI_TABLE_HEADER                Hdr;
    CHAR16                          *FirmwareVendor;
    UINT32                          FirmwareRevision;
    EFI_HANDLE                      ConsoleInHandle;
    VOID                            *ConIn;
    EFI_HANDLE                      ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                      StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES            *RuntimeServices;
    EFI_BOOT_SERVICES               *BootServices;
    UINTN                           NumberOfTableEntries;
    VOID                            *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ============================================================================
 * Loaded Image Protocol
 * ============================================================================ */

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef struct {
    UINT32              Revision;
    EFI_HANDLE          ParentHandle;
    EFI_SYSTEM_TABLE    *SystemTable;

    /* Source location of the image */
    EFI_HANDLE          DeviceHandle;
    VOID                *FilePath;
    VOID                *Reserved;

    /* Image's load options */
    UINT32              LoadOptionsSize;
    VOID                *LoadOptions;

    /* Location where image was loaded */
    VOID                *ImageBase;
    UINT64              ImageSize;
    EFI_MEMORY_TYPE     ImageCodeType;
    EFI_MEMORY_TYPE     ImageDataType;
    VOID                *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ============================================================================
 * Simple File System Protocol
 * ============================================================================ */

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x0964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

struct _EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    struct _EFI_FILE_PROTOCOL   *This,
    struct _EFI_FILE_PROTOCOL   **NewHandle,
    CHAR16                      *FileName,
    UINT64                      OpenMode,
    UINT64                      Attributes
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
    struct _EFI_FILE_PROTOCOL   *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    struct _EFI_FILE_PROTOCOL   *This,
    UINTN                       *BufferSize,
    VOID                        *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(
    struct _EFI_FILE_PROTOCOL   *This,
    UINTN                       *BufferSize,
    VOID                        *Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POSITION)(
    struct _EFI_FILE_PROTOCOL   *This,
    UINT64                      *Position
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(
    struct _EFI_FILE_PROTOCOL   *This,
    UINT64                      Position
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
    struct _EFI_FILE_PROTOCOL   *This,
    EFI_GUID                    *InformationType,
    UINTN                       *BufferSize,
    VOID                        *Buffer
);

typedef struct _EFI_FILE_PROTOCOL {
    UINT64                  Revision;
    EFI_FILE_OPEN           Open;
    EFI_FILE_CLOSE          Close;
    VOID                    *Delete;
    EFI_FILE_READ           Read;
    EFI_FILE_WRITE          Write;
    EFI_FILE_GET_POSITION   GetPosition;
    EFI_FILE_SET_POSITION   SetPosition;
    EFI_FILE_GET_INFO       GetInfo;
    VOID                    *SetInfo;
    VOID                    *Flush;
    /* UEFI 2.0+ */
    VOID                    *OpenEx;
    VOID                    *ReadEx;
    VOID                    *WriteEx;
    VOID                    *FlushEx;
} EFI_FILE_PROTOCOL;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_OPEN_VOLUME)(
    struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
    EFI_FILE_PROTOCOL                       **Root
);

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64                              Revision;
    EFI_SIMPLE_FILE_SYSTEM_OPEN_VOLUME  OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* File open modes */
#define EFI_FILE_MODE_READ      0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE     0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE    0x8000000000000000ULL

/* File attributes */
#define EFI_FILE_READ_ONLY      0x0000000000000001ULL
#define EFI_FILE_HIDDEN         0x0000000000000002ULL
#define EFI_FILE_SYSTEM         0x0000000000000004ULL
#define EFI_FILE_RESERVED       0x0000000000000008ULL
#define EFI_FILE_DIRECTORY      0x0000000000000010ULL
#define EFI_FILE_ARCHIVE        0x0000000000000020ULL
#define EFI_FILE_VALID_ATTR     0x0000000000000037ULL

/* File info GUID */
#define EFI_FILE_INFO_GUID \
    { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef struct {
    UINT64  Size;           /* Size of this structure */
    UINT64  FileSize;       /* Size of the file in bytes */
    UINT64  PhysicalSize;   /* Physical size of the file */
    VOID    *CreateTime;    /* EFI_TIME */
    VOID    *LastAccessTime;
    VOID    *ModificationTime;
    UINT64  Attribute;
    CHAR16  FileName[1];    /* Variable length filename */
} EFI_FILE_INFO;

/* ============================================================================
 * Graphics Output Protocol (GOP)
 * ============================================================================ */

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32  RedMask;
    UINT32  GreenMask;
    UINT32  BlueMask;
    UINT32  ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32                      Version;
    UINT32                      HorizontalResolution;
    UINT32                      VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT   PixelFormat;
    EFI_PIXEL_BITMASK           PixelInformation;
    UINT32                      PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                              MaxMode;
    UINT32                              Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                               SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                FrameBufferBase;
    UINTN                               FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL    *This,
    UINT32                                  ModeNumber,
    UINTN                                   *SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    **Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL    *This,
    UINT32                                  ModeNumber
);

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    VOID                                    *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE       *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* ============================================================================
 * Global Variables (defined in main.c)
 * ============================================================================ */

extern EFI_HANDLE           gImageHandle;
extern EFI_SYSTEM_TABLE     *gST;
extern EFI_BOOT_SERVICES    *gBS;
extern EFI_RUNTIME_SERVICES *gRS;

/* ============================================================================
 * Utility Macros
 * ============================================================================ */

/* Page size constants */
#define EFI_PAGE_SIZE       4096
#define EFI_PAGE_SHIFT      12

/* Convert bytes to pages (rounding up) */
#define EFI_SIZE_TO_PAGES(size) \
    (((size) + EFI_PAGE_SIZE - 1) >> EFI_PAGE_SHIFT)

/* Convert pages to bytes */
#define EFI_PAGES_TO_SIZE(pages) \
    ((pages) << EFI_PAGE_SHIFT)

#endif /* _AAAOS_UEFI_H */
