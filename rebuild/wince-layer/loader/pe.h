/* BrainCE Layer -- minimal PE32 definitions (public PE format). */
#ifndef BRAINCE_PE_H
#define BRAINCE_PE_H

#include <stdint.h>

typedef struct {
    uint16_t e_magic;      /* 'MZ' */
    uint8_t  _pad[58];
    int32_t  e_lfanew;
} ce_dos_header;

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} ce_coff_header;

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} ce_data_directory;

typedef struct {
    uint16_t Magic;        /* 0x10B = PE32 */
    uint8_t  MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOSVersion, MinorOSVersion;
    uint16_t MajorImageVersion, MinorImageVersion;
    uint16_t MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    ce_data_directory DataDirectory[16];
} ce_optional_header;

typedef struct {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} ce_section_header;

typedef struct {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
} ce_import_descriptor;

typedef struct {
    uint32_t StartAddressOfRawData;
    uint32_t EndAddressOfRawData;
    uint32_t AddressOfIndex;
    uint32_t AddressOfCallBacks;
    uint32_t SizeOfZeroFill;
    uint32_t Characteristics;
} ce_tls_directory;

#define CE_IMAGE_FILE_DLL 0x2000
#define CE_MACHINE_ARM    0x01C0
#define CE_MACHINE_ARMNT  0x01C4
#define CE_MACHINE_THUMB  0x01C2

#define CE_DIR_IMPORT    1
#define CE_DIR_BASERELOC 5
#define CE_DIR_TLS       9
#define CE_DIR_DELAYIMP  13

#define CE_REL_BASED_ABSOLUTE 0
#define CE_REL_BASED_HIGHLOW  3

#define CE_SCN_EXECUTE 0x20000000
#define CE_SCN_READ    0x40000000
#define CE_SCN_WRITE   0x80000000

#endif
