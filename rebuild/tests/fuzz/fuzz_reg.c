/* Fuzz harness: brainreg .conf parser (shell/src/reg.c).
 * The parser scans the dir for .conf files per query; the harness points
 * BRAINREG_SYS_DIR at a scratch dir and rewrites ui.conf with
 * the fuzz input on every iteration. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "reg.h"

static char dirpath[] = "/tmp/fuzzregXXXXXX";
static char filepath[600];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    if (!mkdtemp(dirpath)) return -1;
    snprintf(filepath, sizeof(filepath), "%s/ui.conf", dirpath);
    setenv("BRAINREG_SYS_DIR", dirpath, 1);
    setenv("BRAINREG_USER_DIR", dirpath, 1);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FILE *f = fopen(filepath, "wb");
    if (!f) return 0;
    fwrite(data, 1, size, f);
    fclose(f);
    char *v = reg_get("ui", "mode");
    free(v);
    v = reg_get("ui", "dark");
    free(v);
    reg_entry out[16];
    reg_list(out, 16);   /* reg_entry has fixed arrays */
    return 0;
}
