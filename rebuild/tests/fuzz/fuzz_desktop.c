/* Fuzz harness: .desktop catalog parser (shell/src/desktop.c). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "desktop.h"

static char dirpath[] = "/tmp/fuzzdeskXXXXXX";
static char filepath[600];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    if (!mkdtemp(dirpath)) return -1;
    snprintf(filepath, sizeof(filepath), "%s/fuzz.desktop",
             dirpath);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FILE *f = fopen(filepath, "wb");
    if (!f) return 0;
    fwrite(data, 1, size, f);
    fclose(f);
    de_catalog *c = de_catalog_new(NULL);
    if (!c) return 0;
    de_catalog_load_dir(c, dirpath);
    const de_entry *e = de_catalog_find_id(c, "fuzz");
    (void)e;
    de_catalog_free(c);
    return 0;
}
