/* bench_catalog_mem - resident-memory diagnostic for the
 * .desktop catalog (M3, DESIGN-shell-v2.md section 2.3): generates
 * N synthetic .desktop files, loads them, and reports the
 * catalog's own byte estimate plus this process's VmRSS delta.
 * Not a pass/fail test; run before/after catalog storage changes
 * and compare. Usage: bench_catalog_mem [n_entries] */
#include "desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static long rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld kB", &v) == 1) break;
    }
    fclose(f);
    return v;
}

static void write_desktop(const char *dir, int i)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/app%03d.desktop", dir, i);
    FILE *f = fopen(path, "w");
    fprintf(f,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=アプリ %03d サンプル\n"
        "GenericName=汎用ツール %03d\n"
        "Comment=これはベンチマーク用の説明文です。項目 %03d のコメント。\n"
        "Icon=app-icon-%03d\n"
        "Exec=/usr/bin/sample-app-%03d --mode fast %%U\n"
        "TryExec=/usr/bin/sample-app-%03d\n"
        "Categories=Utility;FileTools;Settings;\n"
        "Keywords=sample;bench;tool;%03d;\n",
        i, i, i, i, i, i, i);
    fclose(f);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 240;
    char dir[] = "/tmp/bdbenchXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    for (int i = 0; i < n; i++) write_desktop(dir, i);

    long rss0 = rss_kb();
    de_catalog *c = de_catalog_new("ja");
    int added = de_catalog_load_dir(c, dir);
    long rss1 = rss_kb();
    printf("entries=%d added=%d catalog_memory=%zu rss_before=%ldkB "
           "rss_after=%ldkB rss_delta=%ldkB\n",
           n, added, de_catalog_memory(c), rss0, rss1, rss1 - rss0);
    printf("arena_bytes=%zu generations=%zu\n",
           de_catalog_arena_bytes(c), de_catalog_generations(c));
    /* one changed file -> refresh re-parses just it */
    char path[512];
    snprintf(path, sizeof(path), "%s/app000.desktop", dir);
    FILE *f = fopen(path, "a");
    fprintf(f, "Comment=変更されたコメント\n");
    fclose(f);
    int changes = de_catalog_refresh(c);
    long rss2 = rss_kb();
    printf("after_refresh changes=%d catalog_memory=%zu rss=%ldkB\n",
           changes, de_catalog_memory(c), rss2);
    printf("arena_bytes=%zu generations=%zu\n",
           de_catalog_arena_bytes(c), de_catalog_generations(c));
    de_catalog_free(c);
    return 0;
}
