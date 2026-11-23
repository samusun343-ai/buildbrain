/* BrainCE Layer -- loader module system (M3). See module.h.
 *
 * Hosts the main EXE plus dependent DLLs, from files and from the
 * CE ROM (XIP modules mapped at their native bases; ROM images
 * carry no relocations, so the native base is a hard requirement --
 * a collision is reported, never silently relocated).
 * Legal scope note (DESIGN section 0): original DLLs are LOADED AND
 * EXECUTED as-is; nothing here inspects or modifies their code
 * beyond the interface data (headers, import/export directories)
 * any host loader must read.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "pe.h"
#include "module.h"
#include "../shim/fs_bridge.h"

/* shim entry points */
extern void *ce_resolve_import(const char *dll, int ordinal,
                               const char *name, int *was_stub);
extern int   ce_stub_register(const char *dll, int ordinal,
                              const char *name);
extern void *ce_stub_addr(int slot);

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

#define CE_MAX_MODULES 48
#define CE_MAX_SECS    32

struct ce_module {
    int      used;
    int      is_main;
    int      from_rom;
    int      is_dll;
    int      bound;
    uint8_t *base;
    uint32_t image_size;
    intptr_t delta;
    char     name[64];          /* lowercase basename */
    char     name_orig[64];     /* basename as written */
    CE_WCHAR ce_path[260];
    char     host_path[PATH_MAX];
    uint32_t entry_rva;
    uint32_t imp_rva, exp_rva, exp_size, res_rva, res_size;
    uint32_t tls_rva;
    struct { uint32_t rva, vsz; int prot; } secs[CE_MAX_SECS];
    int      nsecs;
};

static ce_module g_mods[CE_MAX_MODULES];
static ce_module *g_order[CE_MAX_MODULES];  /* bind-completion order */
static int g_norder = 0;
static int g_finished = 0;
static ce_module *g_main_mod = NULL;
static pthread_mutex_t g_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static char g_self_path[PATH_MAX];
static char g_fs_root[PATH_MAX];
static char g_rom_arg[PATH_MAX];

/* ---------------- ROM source ---------------- */
static int      g_rom_fd = -1;
static uint32_t g_rom_physfirst = 0;
static uint32_t g_rom_toc_off = 0;   /* file offset of the TOC */
static uint32_t g_rom_nummods = 0;

static int rom_pread(void *buf, size_t len, uint32_t off)
{
    uint8_t *p = buf;
    while (len) {
        ssize_t r = pread(g_rom_fd, p, len, (off_t)off);
        if (r <= 0)
            return -1;
        p += r;
        len -= (size_t)r;
        off += (uint32_t)r;
    }
    return 0;
}

/* Validate a ROMHDR candidate at file offset `off`. */
static int romhdr_check(int fd, uint32_t off, uint32_t *physfirst,
                        uint32_t *nummods)
{
    uint8_t w[0x60 + 32];
    if (pread(fd, w, sizeof(w), (off_t)off) != (ssize_t)sizeof(w))
        return -1;
    uint32_t pf = rd32(w + 8);
    uint32_t nm = rd32(w + 16);
    uint32_t nf = rd32(w + 48);
    if (pf < 0x80000000u || pf > 0x84000000u)
        return -1;
    if (nm < 40 || nm > 400 || nf < 5 || nf > 200)
        return -1;
    const uint8_t *toc = w + 0x60;
    uint32_t namep = rd32(toc + 4);
    uint32_t loadva = rd32(toc + 16);
    if (namep < pf || namep > pf + 0x2000000u)
        return -1;
    if (loadva < pf || loadva > pf + 0x2000000u)
        return -1;
    *physfirst = pf;
    *nummods = nm;
    return 0;
}

static int rom_open(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    uint32_t hdr_off = 0, pf = 0, nm = 0;
    int found = 0;
    /* Fast path: the ECEC boot block at +0x40 carries the ROMHDR
     * VA (+0x44) and its file offset (+0x48). Cross-check both. */
    uint8_t head[0x4C];
    if (pread(fd, head, sizeof(head), 0) == (ssize_t)sizeof(head)
        && memcmp(head + 0x40, "ECEC", 4) == 0) {
        uint32_t va = rd32(head + 0x44);
        uint32_t fo = rd32(head + 0x48);
        if (romhdr_check(fd, fo, &pf, &nm) == 0 && va == pf + fo) {
            hdr_off = fo;
            found = 1;
        }
    }
    if (!found) {
        /* Fallback: scan the source for the physfirst pattern and
         * validate each candidate (the header sits near the end of
         * the used ROM area on this nk.bin). */
        off_t total = lseek(fd, 0, SEEK_END);
        if (total <= 0)
            total = 0x2000000;
        static uint8_t chunk[0x100000];
        for (off_t base = 0; base + 0x1000 <= total && !found;
             base += (off_t)sizeof(chunk) - 0x80) {
            ssize_t got = pread(fd, chunk, sizeof(chunk), base);
            if (got < 0x1000)
                break;
            for (size_t i = 8; i + 0x60 + 32 <= (size_t)got; i += 4) {
                uint32_t v = rd32(chunk + i);
                if (v < 0x80000000u || v > 0x84000000u)
                    continue;
                uint32_t cand = (uint32_t)base + (uint32_t)i - 8;
                if (romhdr_check(fd, cand, &pf, &nm) == 0) {
                    hdr_off = cand;
                    found = 1;
                    break;
                }
            }
        }
    }
    if (!found) {
        close(fd);
        return -1;
    }
    g_rom_fd = fd;
    g_rom_physfirst = pf;
    g_rom_toc_off = hdr_off + 0x60;
    g_rom_nummods = nm;
    fprintf(stderr, "[braince] ROM opened: %s (physfirst 0x%x, "
            "%u modules, hdr at 0x%x)\n", path, pf, nm, hdr_off);
    return 0;
}

/* Find a ROM module by lowercase basename. Returns 0 and fills the
 * TOC fields on success. */
static int rom_find(const char *lname, uint32_t *size_out,
                    uint32_t *e32p_out, uint32_t *o32p_out,
                    uint32_t *loadva_out)
{
    if (g_rom_fd < 0)
        return -1;
    for (uint32_t i = 0; i < g_rom_nummods; i++) {
        uint8_t ent[32];
        if (rom_pread(ent, 32, g_rom_toc_off + i * 32) != 0)
            return -1;
        uint32_t namep = rd32(ent + 4);
        char nm[64];
        if (rom_pread(nm, sizeof(nm) - 1, namep - g_rom_physfirst) != 0)
            return -1;
        nm[sizeof(nm) - 1] = 0;
        /* the name buffer may run past the NUL if the name is long;
         * force termination at the first NUL within the window */
        size_t l = 0;
        while (l < sizeof(nm) - 1 && nm[l])
            l++;
        nm[l] = 0;
        char low[64];
        size_t k = 0;
        for (; nm[k] && k < sizeof(low) - 1; k++) {
            char c = nm[k];
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            low[k] = c;
        }
        low[k] = 0;
        if (strcmp(low, lname) == 0) {
            *size_out = rd32(ent + 0);
            *e32p_out = rd32(ent + 8);
            *o32p_out = rd32(ent + 12);
            *loadva_out = rd32(ent + 16);
            return 0;
        }
    }
    return -1;
}

/* ---------------- registry helpers ---------------- */

static void name_key(const char *in, char *out, size_t outsz)
{
    const char *base = in;
    for (const char *p = in; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    size_t n = 0;
    while (base[n] && n + 1 < outsz) {
        char c = base[n];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[n] = c;
        n++;
    }
    out[n] = 0;
}

static void base_orig(const char *in, char *out, size_t outsz)
{
    const char *base = in;
    for (const char *p = in; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    snprintf(out, outsz, "%s", base);
}

static ce_module *mod_alloc(void)
{
    for (int i = 0; i < CE_MAX_MODULES; i++)
        if (!g_mods[i].used) {
            memset(&g_mods[i], 0, sizeof(g_mods[i]));
            g_mods[i].used = 1;
            return &g_mods[i];
        }
    return NULL;
}

static ce_module *mod_by_key(const char *key)
{
    for (int i = 0; i < CE_MAX_MODULES; i++)
        if (g_mods[i].used && strcmp(g_mods[i].name, key) == 0)
            return &g_mods[i];
    return NULL;
}

ce_module *ce_mod_by_base(void *base)
{
    if (!base)
        return NULL;
    for (int i = 0; i < CE_MAX_MODULES; i++)
        if (g_mods[i].used && g_mods[i].base == (uint8_t *)base)
            return &g_mods[i];
    return NULL;
}

ce_module *ce_mod_main(void) { return g_main_mod; }
void *ce_mod_base(ce_module *m) { return m ? m->base : NULL; }
void *ce_mod_entry(ce_module *m)
{
    return m ? (void *)(m->base + m->entry_rva) : NULL;
}
int ce_mod_is_dll(ce_module *m) { return m ? m->is_dll : 0; }
void *ce_loader_main_base(void) { return g_main_mod ? g_main_mod->base : NULL; }
const char *ce_loader_self_path(void) { return g_self_path; }
const char *ce_loader_fs_root_arg(void) { return g_fs_root; }
const char *ce_loader_rom_arg(void) { return g_rom_arg; }

/* ---------------- export resolution ---------------- */

static ce_module *ensure_module(const char *dllname, ce_module *importer);

static void *mod_export_depth(ce_module *m, int ordinal,
                              const char *name, int depth)
{
    if (!m || !m->exp_rva || depth > 8)
        return NULL;
    const uint8_t *exp = m->base + m->exp_rva;
    uint32_t obase = rd32(exp + 16);
    uint32_t nfunc = rd32(exp + 20);
    uint32_t nname = rd32(exp + 24);
    uint32_t aof = rd32(exp + 28);
    uint32_t aon = rd32(exp + 32);
    uint32_t aono = rd32(exp + 36);
    uint32_t frva = 0;
    if (ordinal >= 0) {
        uint32_t idx = (uint32_t)ordinal - obase;
        if (idx >= nfunc)
            return NULL;
        frva = rd32(m->base + aof + idx * 4);
    } else if (name) {
        int found = 0;
        for (uint32_t i = 0; i < nname; i++) {
            uint32_t nrva = rd32(m->base + aon + i * 4);
            const char *en = (const char *)(m->base + nrva);
            if (strcmp(en, name) == 0) {
                uint16_t oidx;
                memcpy(&oidx, m->base + aono + i * 2, 2);
                frva = rd32(m->base + aof + (uint32_t)oidx * 4);
                found = 1;
                break;
            }
        }
        if (!found)
            return NULL;
    } else {
        return NULL;
    }
    if (frva == 0)
        return NULL;
    if (frva >= m->exp_rva && frva < m->exp_rva + m->exp_size) {
        /* forwarder string "DLL.name" or "DLL.#ordinal" */
        const char *fwd = (const char *)(m->base + frva);
        char dllbuf[64];
        const char *dot = strrchr(fwd, '.');
        if (!dot || dot == fwd)
            return NULL;
        size_t dl = (size_t)(dot - fwd);
        if (dl >= sizeof(dllbuf) - 5)
            return NULL;
        memcpy(dllbuf, fwd, dl);
        memcpy(dllbuf + dl, ".dll", 5);
        ce_module *target = ensure_module(dllbuf, m);
        if (!target)
            return NULL;
        if (dot[1] == '#')
            return mod_export_depth(target, atoi(dot + 2), NULL,
                                    depth + 1);
        return mod_export_depth(target, -1, dot + 1, depth + 1);
    }
    return m->base + frva;
}

void *ce_mod_export(ce_module *m, int ordinal, const char *name)
{
    return mod_export_depth(m, ordinal, name, 0);
}

/* ---------------- relocations (file modules) ---------------- */

static void apply_relocations(ce_module *m, const uint8_t *reloc,
                              uint32_t reloc_size)
{
    if (!reloc || reloc_size == 0 || m->delta == 0)
        return;
    const uint8_t *p = reloc;
    const uint8_t *end = reloc + reloc_size;
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
                uint32_t *site = (uint32_t *)(m->base + page + off);
                *site = (uint32_t)(*site + m->delta);
                applied++;
            } else {
                skipped++;
            }
        }
        p += block;
    }
    fprintf(stderr, "[braince] %s relocations: %u applied, %u skipped "
            "(delta 0x%lx)\n", m->name, applied, skipped, (long)m->delta);
}

/* ---------------- import binding ---------------- */

static void protect_module(ce_module *m)
{
    long page = sysconf(_SC_PAGESIZE);
    mprotect(m->base, page, PROT_READ);
    for (int i = 0; i < m->nsecs; i++) {
        if (m->secs[i].vsz == 0)
            continue;
        uintptr_t start = (uintptr_t)(m->base + m->secs[i].rva);
        uintptr_t end = start + m->secs[i].vsz;
        start &= ~(uintptr_t)(page - 1);
        end = (end + page - 1) & ~(uintptr_t)(page - 1);
        int prot = m->secs[i].prot ? m->secs[i].prot : PROT_READ;
        if (mprotect((void *)start, end - start, prot) != 0)
            fprintf(stderr, "[braince] warning: mprotect %s sec %d "
                    "failed: %s\n", m->name, i, strerror(errno));
    }
    __builtin___clear_cache((char *)m->base,
                            (char *)m->base + m->image_size);
}

static int bind_module(ce_module *m)
{
    if (m->bound)
        return 0;
    m->bound = 1;   /* set early: circular imports terminate */
    int total = 0, stubs = 0;
    if (m->imp_rva) {
        const uint8_t *desc = m->base + m->imp_rva;
        for (;;) {
            uint32_t name_rva = rd32(desc + 12);
            if (!name_rva)
                break;
            const char *dll = (const char *)(m->base + name_rva);
            uint32_t ilt_rva = rd32(desc + 0);
            uint32_t iat_rva = rd32(desc + 16);
            if (!ilt_rva)
                ilt_rva = iat_rva;
            const uint32_t *ilt = (const uint32_t *)(m->base + ilt_rva);
            uint32_t *iat = (uint32_t *)(m->base + iat_rva);
            int is_core = (strcasecmp(dll, "coredll.dll") == 0);
            ce_module *dep = NULL;
            if (!is_core)
                dep = ensure_module(dll, m);
            for (int i = 0; ilt[i]; i++) {
                int ordinal = -1;
                const char *name = NULL;
                char namebuf[128];
                if (ilt[i] & 0x80000000u) {
                    ordinal = (int)(ilt[i] & 0xFFFF);
                } else {
                    const uint8_t *hn = m->base + (ilt[i] & 0x7FFFFFFFu);
                    name = (const char *)(hn + 2);
                    snprintf(namebuf, sizeof(namebuf), "%s", name);
                    name = namebuf;
                }
                void *fn = NULL;
                if (is_core) {
                    int was_stub = 0;
                    fn = ce_resolve_import(dll, ordinal, name, &was_stub);
                    stubs += was_stub;
                } else if (dep) {
                    fn = ce_mod_export(dep, ordinal, name);
                    if (!fn) {
                        int slot = ce_stub_register(dll, ordinal, name);
                        fn = ce_stub_addr(slot < 0 ? 0 : slot);
                        stubs++;
                        fprintf(stderr, "[braince] module import "
                                "unresolved: %s!%s -> stub\n", dll,
                                name ? name : "(ordinal)");
                    }
                } else {
                    int slot = ce_stub_register(dll, ordinal, name);
                    fn = ce_stub_addr(slot < 0 ? 0 : slot);
                    stubs++;
                }
                iat[i] = (uint32_t)(uintptr_t)fn;
                total++;
            }
            desc += 20;
        }
    }
    fprintf(stderr, "[braince] %s imports bound: %d total, %d to stubs\n",
            m->name, total, stubs);
    protect_module(m);
    if (g_norder < CE_MAX_MODULES)
        g_order[g_norder++] = m;
    return 0;
}

/* ---------------- file module loading ---------------- */

static ce_module *load_file_module(const char *host_path,
                                   const CE_WCHAR *ce_path,
                                   int is_main, char *errbuf,
                                   size_t errlen)
{
    if (errlen)
        errbuf[0] = 0;
    int fd = open(host_path, O_RDONLY);
    if (fd < 0) {
        snprintf(errbuf, errlen, "cannot open %s", host_path);
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        snprintf(errbuf, errlen, "cannot stat %s", host_path);
        return NULL;
    }
    size_t fsize = (size_t)st.st_size;
    uint8_t *file = malloc(fsize ? fsize : 1);
    if (!file) {
        close(fd);
        snprintf(errbuf, errlen, "out of memory");
        return NULL;
    }
    size_t got = 0;
    while (got < fsize) {
        ssize_t r = read(fd, file + got, fsize - got);
        if (r <= 0) {
            free(file);
            close(fd);
            snprintf(errbuf, errlen, "cannot read %s", host_path);
            return NULL;
        }
        got += (size_t)r;
    }
    close(fd);

    ce_module *m = NULL;
    if (fsize < sizeof(ce_dos_header))
        goto bad;
    {
        const ce_dos_header *dos = (const ce_dos_header *)file;
        if (dos->e_magic != 0x5A4D)
            goto bad;
        const uint8_t *pe = file + dos->e_lfanew;
        if (rd32(pe) != 0x00004550u)
            goto bad;
        const ce_coff_header *coff = (const ce_coff_header *)(pe + 4);
        if (coff->Machine != CE_MACHINE_ARM
            && coff->Machine != CE_MACHINE_ARMNT
            && coff->Machine != CE_MACHINE_THUMB)
            goto bad;
        const ce_optional_header *opt =
            (const ce_optional_header *)(pe + 4 + sizeof(*coff));
        if (opt->Magic != 0x10B)
            goto bad;
        if (coff->NumberOfSections > CE_MAX_SECS)
            goto bad;
        const ce_section_header *sections = (const ce_section_header *)
            ((const uint8_t *)opt + coff->SizeOfOptionalHeader);

        m = mod_alloc();
        if (!m) {
            snprintf(errbuf, errlen, "module registry full");
            goto bad_nomod;
        }
        m->is_main = is_main;
        m->is_dll = (coff->Characteristics & CE_IMAGE_FILE_DLL) != 0;
        m->image_size = opt->SizeOfImage;
        m->entry_rva = opt->AddressOfEntryPoint;
        m->imp_rva = opt->DataDirectory[CE_DIR_IMPORT].VirtualAddress;
        m->exp_rva = opt->DataDirectory[0].VirtualAddress;
        m->exp_size = opt->DataDirectory[0].Size;
        m->res_rva = opt->DataDirectory[2].VirtualAddress;
        m->res_size = opt->DataDirectory[2].Size;
        m->tls_rva = opt->DataDirectory[CE_DIR_TLS].VirtualAddress;
        uint32_t pref = opt->ImageBase;
        void *base = mmap((void *)(uintptr_t)pref, m->image_size,
                          PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANONYMOUS
                              | MAP_FIXED_NOREPLACE, -1, 0);
        if (base == MAP_FAILED) {
            base = mmap(NULL, m->image_size,
                        PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (base == MAP_FAILED) {
                snprintf(errbuf, errlen, "cannot reserve image memory");
                goto bad_nomod;
            }
        }
        m->base = base;
        m->delta = (intptr_t)((uint8_t *)base - pref);
        memcpy(m->base, file,
               opt->SizeOfHeaders < fsize ? opt->SizeOfHeaders : fsize);
        for (int i = 0; i < coff->NumberOfSections; i++) {
            const ce_section_header *s = &sections[i];
            if (s->SizeOfRawData) {
                if (s->PointerToRawData + s->SizeOfRawData > fsize)
                    goto bad;
                memcpy(m->base + s->VirtualAddress,
                       file + s->PointerToRawData, s->SizeOfRawData);
            }
            int prot = 0;
            if (s->Characteristics & CE_SCN_READ)
                prot |= PROT_READ;
            if (s->Characteristics & CE_SCN_WRITE)
                prot |= PROT_WRITE;
            if (s->Characteristics & CE_SCN_EXECUTE)
                prot |= PROT_EXEC;
            m->secs[m->nsecs].rva = s->VirtualAddress;
            m->secs[m->nsecs].vsz = s->VirtualSize;
            m->secs[m->nsecs].prot = prot;
            m->nsecs++;
        }
        /* relocations before binding (binding reads the ILT, whose
         * entries are RVA-based and unaffected, but code/data VAs
         * must be final before any CE code runs) */
        const ce_data_directory *rd =
            &opt->DataDirectory[CE_DIR_BASERELOC];
        if (rd->Size && rd->VirtualAddress)
            apply_relocations(m, m->base + rd->VirtualAddress, rd->Size);
        else if (m->delta != 0)
            fprintf(stderr, "[braince] warning: %s has no relocations "
                    "but could not load at its preferred base\n",
                    host_path);
    }
    base_orig(host_path, m->name_orig, sizeof(m->name_orig));
    name_key(host_path, m->name, sizeof(m->name));
    snprintf(m->host_path, sizeof(m->host_path), "%s", host_path);
    if (ce_path) {
        size_t n = 0;
        while (ce_path[n] && n < 259) {
            m->ce_path[n] = ce_path[n];
            n++;
        }
        m->ce_path[n] = 0;
    }
    free(file);
    fprintf(stderr, "[braince] module %s mapped at %p (%s)\n",
            m->name, m->base, m->from_rom ? "rom" : "file");
    bind_module(m);
    return m;

bad:
    if (!errbuf[0])
        snprintf(errbuf, errlen, "not a valid ARM PE32: %s", host_path);
    if (m) {
        if (m->base)
            munmap(m->base, m->image_size);
        m->used = 0;
        m = NULL;
    }
bad_nomod:
    if (m) {
        m->used = 0;
        m = NULL;
    }
    free(file);
    return NULL;
}

/* ---------------- ROM module loading ---------------- */

static ce_module *load_rom_module(const char *lname)
{
    uint32_t size, e32p, o32p, loadva;
    if (rom_find(lname, &size, &e32p, &o32p, &loadva) != 0)
        return NULL;
    uint8_t e32[41 * 4];
    if (rom_pread(e32, sizeof(e32), e32p - g_rom_physfirst) != 0)
        return NULL;
    uint32_t objcnt = rd32(e32 + 0) & 0xFF;
    uint32_t entry = rd32(e32 + 4);
    uint32_t vbase = rd32(e32 + 8);
    uint32_t size_image = rd32(e32 + 20);
    if (objcnt == 0 || objcnt > CE_MAX_SECS || size_image == 0)
        return NULL;
    ce_module *m = mod_alloc();
    if (!m)
        return NULL;
    m->from_rom = 1;
    m->is_dll = 1;      /* ROM modules with an entry export DllMain;
                         * EXE mains from ROM override below */
    m->image_size = size_image;
    /* The E32 entry field is a VA in Microsoft's slot DLLs
     * (commctrl: 0x40121870 with base 0x40120000) but a plain RVA
     * in some third-party ROM DLLs (edwgsp: 0x1104 with base
     * 0x41AA0000) -- measured on the real image. Accept both. */
    if (entry >= vbase && entry < vbase + size_image)
        m->entry_rva = entry - vbase;
    else if (entry < size_image)
        m->entry_rva = entry;
    else
        m->entry_rva = entry - vbase;
    m->exp_rva = rd32(e32 + 36);
    m->exp_size = rd32(e32 + 40);
    m->imp_rva = rd32(e32 + 44);
    m->res_rva = rd32(e32 + 52);
    m->res_size = rd32(e32 + 56);
    m->tls_rva = rd32(e32 + 9 * 4 + 2 * 9 * 4);  /* datadir[9] = TLS */
    void *base = mmap((void *)(uintptr_t)vbase, size_image,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                      -1, 0);
    if (base == MAP_FAILED) {
        fprintf(stderr, "[braince] ROM module %s: native base 0x%x "
                "unavailable (%s); XIP modules cannot relocate\n",
                lname, vbase, strerror(errno));
        m->used = 0;
        return NULL;
    }
    m->base = base;
    m->delta = 0;
    for (uint32_t i = 0; i < objcnt; i++) {
        uint8_t o32[24];
        if (rom_pread(o32, 24, o32p - g_rom_physfirst + i * 24) != 0)
            goto fail;
        uint32_t vsz = rd32(o32 + 0);
        uint32_t rva = rd32(o32 + 4);
        uint32_t psz = rd32(o32 + 8);
        uint32_t dptr = rd32(o32 + 12);
        uint32_t acc = rd32(o32 + 20);
        if (psz) {
            if (rom_pread(m->base + rva, psz, dptr - g_rom_physfirst)
                != 0)
                goto fail;
        }
        int prot = PROT_READ;
        if (acc & 0x80000000u)
            prot |= PROT_WRITE;
        if (acc & 0x00000020u)
            prot |= PROT_EXEC;
        m->secs[m->nsecs].rva = rva;
        m->secs[m->nsecs].vsz = vsz;
        m->secs[m->nsecs].prot = prot;
        m->nsecs++;
    }
    snprintf(m->name, sizeof(m->name), "%s", lname);
    snprintf(m->name_orig, sizeof(m->name_orig), "%s", lname);
    {
        /* ce_path = \Windows\<name> */
        static const char pre[] = "\\Windows\\";
        size_t n = 0;
        for (size_t i = 0; pre[i] && n < 250; i++)
            m->ce_path[n++] = (CE_WCHAR)pre[i];
        for (size_t i = 0; lname[i] && n < 258; i++)
            m->ce_path[n++] = (CE_WCHAR)(uint8_t)lname[i];
        m->ce_path[n] = 0;
    }
    fprintf(stderr, "[braince] module %s mapped at %p (rom)\n",
            m->name, m->base);
    bind_module(m);
    return m;

fail:
    munmap(m->base, m->image_size);
    m->used = 0;
    return NULL;
}

/* ---------------- dependency resolution ---------------- */

static ce_module *ensure_module(const char *dllname, ce_module *importer)
{
    char key[64];
    name_key(dllname, key, sizeof(key));
    ce_module *m = mod_by_key(key);
    if (m)
        return m;
    /* ROM first: \Windows DLLs live there (DESIGN 5.2). */
    m = load_rom_module(key);
    if (m)
        return m;
    /* Files: next to the importer, then next to the main module. */
    char orig[64];
    base_orig(dllname, orig, sizeof(orig));
    char cand[PATH_MAX];
    char errbuf[256];
    const char *dirs[2];
    char dirbuf[2][PATH_MAX];
    int ndirs = 0;
    ce_module *anchors[2] = { importer, g_main_mod };
    for (int a = 0; a < 2; a++) {
        if (!anchors[a] || !anchors[a]->host_path[0])
            continue;
        const char *slash = strrchr(anchors[a]->host_path, '/');
        if (!slash)
            continue;
        size_t dl = (size_t)(slash - anchors[a]->host_path);
        if (dl >= sizeof(dirbuf[ndirs]))
            continue;
        memcpy(dirbuf[ndirs], anchors[a]->host_path, dl);
        dirbuf[ndirs][dl] = 0;
        dirs[ndirs] = dirbuf[ndirs];
        ndirs++;
    }
    for (int d = 0; d < ndirs; d++) {
        snprintf(cand, sizeof(cand), "%s/%s", dirs[d], orig);
        struct stat st;
        if (stat(cand, &st) != 0)
            continue;
        CE_WCHAR wname[260];
        size_t n = 0;
        while (orig[n] && n < 259) {
            wname[n] = (CE_WCHAR)(uint8_t)orig[n];
            n++;
        }
        wname[n] = 0;
        m = load_file_module(cand, wname, 0, errbuf, sizeof(errbuf));
        if (m)
            return m;
    }
    fprintf(stderr, "[braince] dependency %s not found (ROM miss, "
            "no sibling file)\n", dllname);
    return NULL;
}

/* ---------------- finish: TLS + DllMain in dependency order ------ */

static void run_tls(ce_module *m)
{
    if (!m->tls_rva)
        return;
    const uint8_t *tls = m->base + m->tls_rva;
    uint32_t cbs_va = rd32(tls + 12);
    if (!cbs_va)
        return;
    uint32_t *cbs = (uint32_t *)(uintptr_t)cbs_va;
    typedef void (*tls_cb)(void *, uint32_t, void *);
    for (int i = 0; cbs[i]; i++)
        ((tls_cb)(uintptr_t)cbs[i])(m->base, 1, NULL);
}

int ce_mod_finish_new(void)
{
    int rc = 0;
    while (g_finished < g_norder) {
        ce_module *m = g_order[g_finished++];
        run_tls(m);
        if (m->is_dll && m->entry_rva) {
            typedef int (*dllmain_t)(void *, uint32_t, void *);
            fprintf(stderr, "[braince] DllMain %s "
                    "(DLL_PROCESS_ATTACH)\n", m->name);
            int ok = ((dllmain_t)(m->base + m->entry_rva))(m->base, 1,
                                                           NULL);
            if (!ok) {
                fprintf(stderr, "[braince] DllMain %s returned "
                        "FALSE\n", m->name);
                rc = -1;
            }
        }
    }
    return rc;
}

/* ---------------- init + main loading ---------------- */

int ce_mod_init(const char *self_path, const char *fs_root,
                const char *rom_path)
{
    if (self_path)
        snprintf(g_self_path, sizeof(g_self_path), "%s", self_path);
    if (fs_root)
        snprintf(g_fs_root, sizeof(g_fs_root), "%s", fs_root);
    if (rom_path) {
        snprintf(g_rom_arg, sizeof(g_rom_arg), "%s", rom_path);
        if (rom_open(rom_path) != 0)
            fprintf(stderr, "[braince] warning: ROM source %s not "
                    "usable; ROM modules unavailable\n", rom_path);
    }
    return 0;
}

ce_module *ce_mod_load_main_file(const char *host_path,
                                 const CE_WCHAR *ce_path,
                                 char *errbuf, size_t errlen)
{
    ce_module *m = load_file_module(host_path, ce_path, 1, errbuf,
                                    errlen);
    if (m)
        g_main_mod = m;
    return m;
}

ce_module *ce_mod_load_main_rom(const char *modname, char *errbuf,
                                size_t errlen)
{
    char key[64];
    name_key(modname, key, sizeof(key));
    ce_module *m = load_rom_module(key);
    if (!m) {
        snprintf(errbuf, errlen, "ROM module %s not loadable", modname);
        return NULL;
    }
    m->is_main = 1;
    /* A ROM module whose name ends in .exe is an EXE: its entry is
     * the CRT startup, not DllMain. */
    size_t l = strlen(key);
    if (l > 4 && strcmp(key + l - 4, ".exe") == 0)
        m->is_dll = 0;
    g_main_mod = m;
    return m;
}

/* ---------------- shim-facing API ---------------- */

void *ce_loader_load_library(const CE_WCHAR *name)
{
    if (!name)
        return NULL;
    pthread_mutex_lock(&g_lock);
    /* reduce to an ASCII basename for registry/ROM lookup */
    char u8[260];
    if (ce_w2u8(name, u8, sizeof(u8)) < 0) {
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }
    char key[64];
    name_key(u8, key, sizeof(key));
    ce_module *m = mod_by_key(key);
    if (!m)
        m = load_rom_module(key);
    if (!m) {
        /* try the path as a file under the CE root / volumes */
        char host[PATH_MAX];
        if (ce_fs_map(name, host, sizeof(host)) == 0) {
            struct stat st;
            char errbuf[256];
            if (stat(host, &st) == 0)
                m = load_file_module(host, name, 0, errbuf,
                                     sizeof(errbuf));
        }
    }
    if (!m) {
        /* volume file: extract to a scratch file, then load */
        CE_DWORD err = 0;
        ce_vfile *vf = ce_fs_open(name, O_RDONLY, &err);
        if (vf) {
            char tmp[PATH_MAX];
            snprintf(tmp, sizeof(tmp), "%s/.libload-%s",
                     ce_fs_root(), key);
            int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd >= 0) {
                uint8_t buf[16384];
                uint32_t r;
                while ((r = ce_vfile_read(vf, buf, sizeof(buf))) > 0) {
                    uint32_t off = 0;
                    while (off < r) {
                        ssize_t w = write(fd, buf + off, r - off);
                        if (w <= 0)
                            break;
                        off += (uint32_t)w;
                    }
                }
                close(fd);
                char errbuf[256];
                m = load_file_module(tmp, name, 0, errbuf,
                                     sizeof(errbuf));
            }
            ce_vfile_close(vf);
        }
    }
    void *base = m ? m->base : NULL;
    if (m)
        ce_mod_finish_new();
    pthread_mutex_unlock(&g_lock);
    return base;
}

void *ce_loader_get_proc(void *hmod, const char *name, long ordinal)
{
    ce_module *m = ce_mod_by_base(hmod);
    if (!m)
        return NULL;
    pthread_mutex_lock(&g_lock);
    void *r = ce_mod_export(m, ordinal >= 0 ? (int)ordinal : -1, name);
    pthread_mutex_unlock(&g_lock);
    return r;
}

void *ce_loader_module_handle(const CE_WCHAR *name)
{
    if (!name)
        return g_main_mod ? g_main_mod->base : NULL;
    char u8[260];
    if (ce_w2u8(name, u8, sizeof(u8)) < 0)
        return NULL;
    char key[64];
    name_key(u8, key, sizeof(key));
    ce_module *m = mod_by_key(key);
    if (!m && !strchr(key, '.')) {
        char key2[64];
        snprintf(key2, sizeof(key2), "%s.dll", key);
        m = mod_by_key(key2);
    }
    return m ? m->base : NULL;
}

int ce_loader_module_filename(void *hmod, CE_WCHAR *buf, int max)
{
    ce_module *m = hmod ? ce_mod_by_base(hmod) : g_main_mod;
    if (!m || !buf || max <= 0)
        return 0;
    int n = 0;
    while (m->ce_path[n] && n < max - 1) {
        buf[n] = m->ce_path[n];
        n++;
    }
    buf[n] = 0;
    return n;
}

int ce_loader_module_resdir(void *hmod, uint32_t *rva, uint32_t *size)
{
    ce_module *m = hmod ? ce_mod_by_base(hmod) : g_main_mod;
    if (!m || !m->res_rva)
        return -1;
    if (rva)
        *rva = m->res_rva;
    if (size)
        *size = m->res_size;
    return 0;
}
