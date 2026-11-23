/* BrainCE Layer -- ce-loader (M1 loader, M3 module system).
 *
 * Loads a WinCE PE32/ARM image into this process together with its
 * dependency graph (loader/modules.c): file modules are relocated
 * as needed, ROM (XIP) modules are mapped at their native CE
 * bases, coredll imports bind to the shim and other DLL imports
 * bind to the hosted modules' exports (DESIGN-wince-layer.md
 * sections 5.1/5.2). DllMain functions run in dependency order,
 * then the entry point (WinMain for an EXE; for a DLL main the
 * DllMain call IS the run). The CE image runs in-process on the
 * native instruction set (the target SoC is the same ARMv5TE
 * family), so there is no emulation layer and no per-call
 * translation cost.
 *
 * Usage: ce-loader [--fs-root DIR] [--stub-log FILE]
 *                  [--rom SOURCE] [--rom-module] PROGRAM
 *   --rom SOURCE   ROM image source (block device or file) that
 *                  provides \Windows modules; may also be given
 *                  via the CE_ROM environment variable.
 *   --rom-module   PROGRAM names a ROM module (e.g. control.exe)
 *                  instead of a host file.
 * Exit status: the guest WinMain return value (mod 256), or 2 on a
 * loader error. A stub-call summary is printed to stderr at exit;
 * acceptance requires "stub calls total=0" for the test programs.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <limits.h>

#include "../shim/ce_types.h"
#include "../shim/fs_bridge.h"
#include "module.h"

/* shim entry points used by the loader */
extern void ce_stub_report(FILE *f);
extern unsigned ce_stub_total(void);
extern void ce_stub_set_log(FILE *f);
extern void ce_gwe_init(void);
extern void ce_gwe_shutdown(void);

static void die(const char *msg)
{
    fprintf(stderr, "[braince] loader error: %s\n", msg);
    exit(2);
}

/* Debug aid (BRAINCE_DEBUG_SEGV=1): report the fault site of a
 * crash inside hosted CE code -- PC, fault address, registers --
 * so a hosting incompatibility can be localised to a module+RVA
 * without a debugger. */
static void segv_handler(int sig, siginfo_t *si, void *uc_v)
{
    ucontext_t *uc = uc_v;
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "[braince] SIGSEGV in hosted code: pc=0x%08lx addr=%p "
        "r0=0x%08lx r1=0x%08lx r2=0x%08lx r3=0x%08lx "
        "r4=0x%08lx lr=0x%08lx sp=0x%08lx\n",
        (unsigned long)uc->uc_mcontext.arm_pc,
        si->si_addr,
        (unsigned long)uc->uc_mcontext.arm_r0,
        (unsigned long)uc->uc_mcontext.arm_r1,
        (unsigned long)uc->uc_mcontext.arm_r2,
        (unsigned long)uc->uc_mcontext.arm_r3,
        (unsigned long)uc->uc_mcontext.arm_r4,
        (unsigned long)uc->uc_mcontext.arm_lr,
        (unsigned long)uc->uc_mcontext.arm_sp);
    if (n > 0) {
        ssize_t w = write(2, buf, (size_t)n);
        (void)w;
    }
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(139);
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
    const char *rom = getenv("CE_ROM");
    int rom_module = 0;
    FILE *stub_log = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fs-root") == 0 && i + 1 < argc) {
            fs_root = argv[++i];
        } else if (strcmp(argv[i], "--stub-log") == 0 && i + 1 < argc) {
            stub_log = fopen(argv[++i], "a");
            if (!stub_log)
                die("cannot open stub log");
        } else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            rom = argv[++i];
        } else if (strcmp(argv[i], "--rom-module") == 0) {
            rom_module = 1;
        } else if (argv[i][0] != '-') {
            prog = argv[i];
        } else {
            fprintf(stderr, "usage: ce-loader [--fs-root DIR] "
                    "[--stub-log FILE] [--rom SOURCE] "
                    "[--rom-module] PROGRAM\n");
            return 2;
        }
    }
    if (!prog) {
        fprintf(stderr, "usage: ce-loader [--fs-root DIR] "
                "[--stub-log FILE] [--rom SOURCE] "
                "[--rom-module] PROGRAM\n");
        return 2;
    }
    if (stub_log)
        ce_stub_set_log(stub_log);
    if (getenv("BRAINCE_DEBUG_SEGV")) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = segv_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
    }
    if (ce_fs_init(fs_root) != 0)
        die("FS bridge init failed");
    fprintf(stderr, "[braince] fs root: %s, volumes: %d\n",
            ce_fs_root(), ce_fs_volume_count());

    char self[PATH_MAX];
    ssize_t sl = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (sl > 0)
        self[sl] = 0;
    else
        snprintf(self, sizeof(self), "%s", argv[0]);
    if (ce_mod_init(self, ce_fs_root(), rom) != 0)
        die("module system init failed");

    char errbuf[512];
    ce_module *m;
    if (rom_module) {
        m = ce_mod_load_main_rom(prog, errbuf, sizeof(errbuf));
    } else {
        static CE_WCHAR wpath[260];
        size_t pn = strlen(prog);
        if (pn > 259)
            pn = 259;
        for (size_t i = 0; i < pn; i++)
            wpath[i] = (CE_WCHAR)(unsigned char)prog[i];
        wpath[pn] = 0;
        m = ce_mod_load_main_file(prog, wpath, errbuf, sizeof(errbuf));
    }
    if (!m)
        die(errbuf);

    ce_gwe_init();
    int fin = ce_mod_finish_new();

    int rc;
    if (ce_mod_is_dll(m)) {
        /* DllMain (run by ce_mod_finish_new) is the whole run. */
        rc = fin == 0 ? 0 : 1;
        fprintf(stderr, "[braince] DLL main: DllMain %s\n",
                rc == 0 ? "succeeded" : "FAILED");
    } else {
        if (fin != 0)
            fprintf(stderr, "[braince] warning: a dependency "
                    "DllMain failed; continuing to entry\n");
        typedef int (*winmain_t)(void *, void *, CE_WCHAR *, int);
        static CE_WCHAR empty_cmdline[1] = {0};
        void *entry = ce_mod_entry(m);
        fprintf(stderr, "[braince] calling WinMain at %p\n", entry);
        rc = ((winmain_t)entry)(ce_mod_base(m), NULL, empty_cmdline, 0);
    }
    fprintf(stderr, "[braince] guest returned %d\n", rc);
    ce_gwe_shutdown();
    ce_stub_report(stderr);
    print_mem("at exit");
    if (stub_log)
        fclose(stub_log);
    return rc & 0xFF;
}
