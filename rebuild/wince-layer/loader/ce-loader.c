/* BrainCE Layer -- ce-loader (M1).
 *
 * Loads a normal (non-XIP) WinCE PE32/ARM image into this process,
 * applies base relocations, binds its imports to the coredll shim
 * (DESIGN-wince-layer.md section 5.1: the loader owns the
 * ordinal/name -> implementation table and writes shim addresses
 * directly into the IAT), runs TLS callbacks, and calls the entry
 * point (WinMain for an EXE, DllMain with DLL_PROCESS_ATTACH for a
 * DLL). Unimplemented imports bind to logging stubs in the shim.
 *
 * The CE image runs in-process on the native instruction set (the
 * target SoC is the same ARMv5TE family), so there is no emulation
 * layer and no per-call translation cost.
 *
 * Usage: ce-loader [--fs-root DIR] [--stub-log FILE] PROGRAM.exe
 * Exit status: the guest WinMain return value (mod 256), or 2 on a
 * loader error. A stub-call summary is printed to stderr at exit;
 * acceptance requires "stub calls total=0".
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "pe.h"
#include "../shim/ce_types.h"
#include "../shim/fs_bridge.h"

/* shim entry points used by the loader */
extern void *ce_resolve_import(const char *dll, int ordinal,
                               const char *name, int *was_stub);
extern void ce_stub_report(FILE *f);
extern unsigned ce_stub_total(void);
extern void ce_stub_set_log(FILE *f);

static void die(const char *msg)
{
    fprintf(stderr, "[braince] loader error: %s\n", msg);
    exit(2);
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

struct image {
    uint8_t *file;        /* raw file bytes */
    size_t file_size;
    uint8_t *base;        /* mapped image base */
    uint32_t image_base;  /* preferred base from header */
    intptr_t delta;
    const ce_optional_header *opt;
    const ce_section_header *sections;
    int nsections;
};

static const uint8_t *rva_ptr(const struct image *im, uint32_t rva)
{
    return im->base + rva;
}

static void apply_relocations(struct image *im)
{
    const ce_data_directory *dd = &im->opt->DataDirectory[CE_DIR_BASERELOC];
    if (dd->Size == 0 || im->delta == 0) {
        if (dd->Size && im->delta == 0)
            fprintf(stderr, "[braince] loaded at preferred base "
                    "0x%x, no relocations needed\n", im->image_base);
        return;
    }
    const uint8_t *p = rva_ptr(im, dd->VirtualAddress);
    const uint8_t *end = p + dd->Size;
    unsigned applied = 0, skipped = 0;
    while (p + 8 <= end) {
        uint32_t page = rd32(p);
        uint32_t block = rd32(p + 4);
        if (block < 8 || page == 0)
            break;
        const uint16_t *ents = (const uint16_t *)(p + 8);
        int n = (int)((block - 8) / 2);
        for (int i = 0; i < n; i++) {
            int type = ents[i] >> 12;
            int off = ents[i] & 0xFFF;
            if (type == CE_REL_BASED_ABSOLUTE) {
                skipped++;
                continue;
            }
            if (type == CE_REL_BASED_HIGHLOW) {
                uint32_t *site = (uint32_t *)(im->base + page + off);
                *site = (uint32_t)(*site + im->delta);
                applied++;
            } else {
                fprintf(stderr, "[braince] warning: unsupported "
                        "reloc type %d at RVA 0x%x (skipped)\n",
                        type, page + off);
                skipped++;
            }
        }
        p += block;
    }
    fprintf(stderr, "[braince] relocations: %u applied, %u skipped "
            "(delta 0x%lx)\n", applied, skipped, (long)im->delta);
}

static int bind_imports(struct image *im)
{
    const ce_data_directory *dd = &im->opt->DataDirectory[CE_DIR_IMPORT];
    if (dd->Size == 0)
        return 0;
    int total = 0, stubs = 0;
    const ce_import_descriptor *desc =
        (const ce_import_descriptor *)rva_ptr(im, dd->VirtualAddress);
    for (; desc->Name; desc++) {
        const char *dll = (const char *)rva_ptr(im, desc->Name);
        uint32_t ilt_rva = desc->OriginalFirstThunk
                           ? desc->OriginalFirstThunk
                           : desc->FirstThunk;
        const uint32_t *ilt = (const uint32_t *)rva_ptr(im, ilt_rva);
        uint32_t *iat = (uint32_t *)rva_ptr(im, desc->FirstThunk);
        for (int i = 0; ilt[i]; i++) {
            int ordinal = -1;
            const char *name = NULL;
            char namebuf[128];
            if (ilt[i] & 0x80000000u) {
                ordinal = (int)(ilt[i] & 0xFFFF);
            } else {
                const uint8_t *hn = rva_ptr(im, ilt[i] & 0x7FFFFFFFu);
                name = (const char *)(hn + 2);
                snprintf(namebuf, sizeof(namebuf), "%s", name);
                name = namebuf;
            }
            int was_stub = 0;
            void *fn = ce_resolve_import(dll, ordinal, name, &was_stub);
            iat[i] = (uint32_t)(uintptr_t)fn;
            total++;
            stubs += was_stub;
        }
    }
    fprintf(stderr, "[braince] imports bound: %d total, %d to stubs\n",
            total, stubs);
    const ce_data_directory *delay =
        &im->opt->DataDirectory[CE_DIR_DELAYIMP];
    if (delay->Size)
        fprintf(stderr, "[braince] warning: delay-load imports "
                "present but not supported in M1\n");
    return total;
}

static void run_tls_callbacks(struct image *im)
{
    const ce_data_directory *dd = &im->opt->DataDirectory[CE_DIR_TLS];
    if (dd->Size == 0)
        return;
    const ce_tls_directory *tls =
        (const ce_tls_directory *)rva_ptr(im, dd->VirtualAddress);
    if (!tls->AddressOfCallBacks)
        return;
    typedef void (*tls_cb)(void *, uint32_t, void *);
    uint32_t *cbs = (uint32_t *)(uintptr_t)tls->AddressOfCallBacks;
    for (int i = 0; cbs[i]; i++) {
        tls_cb cb = (tls_cb)(uintptr_t)cbs[i];
        cb(im->base, 1 /* DLL_PROCESS_ATTACH */, NULL);
    }
}

static void protect_sections(struct image *im)
{
    long page = sysconf(_SC_PAGESIZE);
    /* headers: read-only */
    mprotect(im->base, page, PROT_READ);
    for (int i = 0; i < im->nsections; i++) {
        const ce_section_header *s = &im->sections[i];
        if (s->VirtualSize == 0)
            continue;
        uintptr_t start = (uintptr_t)(im->base + s->VirtualAddress);
        uintptr_t end = start + s->VirtualSize;
        start &= ~(uintptr_t)(page - 1);
        end = (end + page - 1) & ~(uintptr_t)(page - 1);
        int prot = 0;
        if (s->Characteristics & CE_SCN_READ)
            prot |= PROT_READ;
        if (s->Characteristics & CE_SCN_WRITE)
            prot |= PROT_WRITE;
        if (s->Characteristics & CE_SCN_EXECUTE)
            prot |= PROT_EXEC;
        if (prot == 0)
            prot = PROT_READ;
        if (mprotect((void *)start, end - start, prot) != 0)
            fprintf(stderr, "[braince] warning: mprotect section %d "
                    "failed: %s\n", i, strerror(errno));
    }
}

static void print_mem(const char *tag)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f)
        return;
    char line[256];
    long rss = -1, hwm = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld kB", &rss) == 1)
            continue;
        if (sscanf(line, "VmHWM: %ld kB", &hwm) == 1)
            continue;
    }
    fclose(f);
    fprintf(stderr, "[braince] mem %s: VmRSS=%ld kB VmHWM=%ld kB\n",
            tag, rss, hwm);
}

int main(int argc, char **argv)
{
    const char *fs_root = NULL;
    const char *prog = NULL;
    FILE *stub_log = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fs-root") == 0 && i + 1 < argc) {
            fs_root = argv[++i];
        } else if (strcmp(argv[i], "--stub-log") == 0 && i + 1 < argc) {
            stub_log = fopen(argv[++i], "a");
            if (!stub_log)
                die("cannot open stub log");
        } else if (argv[i][0] != '-') {
            prog = argv[i];
        } else {
            fprintf(stderr, "usage: ce-loader [--fs-root DIR] "
                    "[--stub-log FILE] PROGRAM.exe\n");
            return 2;
        }
    }
    if (!prog) {
        fprintf(stderr, "usage: ce-loader [--fs-root DIR] "
                "[--stub-log FILE] PROGRAM.exe\n");
        return 2;
    }
    if (stub_log)
        ce_stub_set_log(stub_log);
    if (ce_fs_init(fs_root) != 0)
        die("FS bridge init failed");
    fprintf(stderr, "[braince] fs root: %s\n", ce_fs_root());

    int fd = open(prog, O_RDONLY);
    if (fd < 0)
        die("cannot open program");
    struct stat st;
    if (fstat(fd, &st) != 0)
        die("cannot stat program");
    struct image im;
    memset(&im, 0, sizeof(im));
    im.file_size = (size_t)st.st_size;
    im.file = malloc(im.file_size);
    if (!im.file)
        die("out of memory");
    size_t got = 0;
    while (got < im.file_size) {
        ssize_t r = read(fd, im.file + got, im.file_size - got);
        if (r <= 0)
            die("cannot read program");
        got += (size_t)r;
    }
    close(fd);

    if (im.file_size < sizeof(ce_dos_header))
        die("file too small");
    const ce_dos_header *dos = (const ce_dos_header *)im.file;
    if (dos->e_magic != 0x5A4D)
        die("not an MZ executable");
    const uint8_t *pe = im.file + dos->e_lfanew;
    if (rd32(pe) != 0x00004550)
        die("PE signature missing");
    const ce_coff_header *coff = (const ce_coff_header *)(pe + 4);
    if (coff->Machine != CE_MACHINE_ARM
        && coff->Machine != CE_MACHINE_ARMNT
        && coff->Machine != CE_MACHINE_THUMB)
        die("not an ARM PE");
    im.opt = (const ce_optional_header *)(pe + 4 + sizeof(*coff));
    if (im.opt->Magic != 0x10B)
        die("not a PE32 optional header");
    /* copy the headers into loader-owned memory: the pointers into
     * the file buffer must not be used after it is freed */
    static ce_optional_header opt_copy;
    static ce_section_header sec_copy[96];
    const ce_section_header *file_sections = (const ce_section_header *)
        ((const uint8_t *)im.opt + coff->SizeOfOptionalHeader);
    memcpy(&opt_copy, im.opt, sizeof(opt_copy));
    im.opt = &opt_copy;
    if (coff->NumberOfSections > 96)
        die("too many sections");
    memcpy(sec_copy, file_sections,
           sizeof(ce_section_header) * coff->NumberOfSections);
    im.sections = sec_copy;
    im.nsections = coff->NumberOfSections;
    im.image_base = im.opt->ImageBase;
    /* header fields used after the file buffer is freed */
    uint32_t entry_rva = im.opt->AddressOfEntryPoint;
    int is_dll = (coff->Characteristics & CE_IMAGE_FILE_DLL) != 0;

    uint32_t size_image = im.opt->SizeOfImage;
    void *base = mmap((void *)(uintptr_t)im.image_base, size_image,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                      -1, 0);
    if (base == MAP_FAILED) {
        base = mmap(NULL, size_image,
                    PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
            die("cannot reserve image memory");
    }
    im.base = base;
    im.delta = (intptr_t)((uint8_t *)base - im.image_base);
    fprintf(stderr, "[braince] image mapped at %p (preferred 0x%x)\n",
            base, im.image_base);

    /* headers + sections */
    memcpy(im.base, im.file,
           im.opt->SizeOfHeaders < im.file_size
               ? im.opt->SizeOfHeaders : im.file_size);
    for (int i = 0; i < im.nsections; i++) {
        const ce_section_header *s = &im.sections[i];
        if (s->SizeOfRawData == 0)
            continue;
        if (s->PointerToRawData + s->SizeOfRawData > im.file_size)
            die("section raw data out of file bounds");
        memcpy(im.base + s->VirtualAddress,
               im.file + s->PointerToRawData, s->SizeOfRawData);
    }
    free(im.file);
    im.file = NULL;

    apply_relocations(&im);
    bind_imports(&im);
    protect_sections(&im);
    __builtin___clear_cache((char *)im.base,
                            (char *)im.base + size_image);

    run_tls_callbacks(&im);

    void *entry = im.base + entry_rva;
    int rc;
    if (is_dll) {
        typedef int (*dllmain_t)(void *, uint32_t, void *);
        fprintf(stderr, "[braince] calling DllMain (DLL_PROCESS_ATTACH)\n");
        rc = ((dllmain_t)entry)(im.base, 1, NULL) ? 0 : 1;
    } else {
        typedef int (*winmain_t)(void *, void *, CE_WCHAR *, int);
        static CE_WCHAR empty_cmdline[1] = {0};
        fprintf(stderr, "[braince] calling WinMain at %p\n", entry);
        rc = ((winmain_t)entry)(im.base, NULL, empty_cmdline, 0);
    }
    fprintf(stderr, "[braince] guest returned %d\n", rc);
    ce_stub_report(stderr);
    print_mem("at exit");
    if (stub_log)
        fclose(stub_log);
    return rc & 0xFF;
}
