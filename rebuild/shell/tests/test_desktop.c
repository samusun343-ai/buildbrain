/* test_desktop.c - unit tests for the reg store, the Desktop Entry
 * parser/catalog, the pin/recent store and the exec seam.
 * Fixtures live in tests/fixtures/applications{,2}. */
#define _GNU_SOURCE 1 /* AT_FDCWD / utimensat */
#include "reg.h"
#include "desktop.h"
#include "launcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static const char *FIX1 = "tests/fixtures/applications";
static const char *FIX2 = "tests/fixtures/applications2";

static char sysdir[256], userdir[256];

static void setup_reg(void)
{
    char tmpl[] = "/tmp/bdregXXXXXX";
    char *base = mkdtemp(tmpl);
    snprintf(sysdir, sizeof(sysdir), "%s/sys", base);
    snprintf(userdir, sizeof(userdir), "%s/user", base);
    mkdir(sysdir, 0755);
    mkdir(userdir, 0755);
    setenv("BRAINREG_SYS_DIR", sysdir, 1);
    setenv("BRAINREG_USER_DIR", userdir, 1);
    /* system-layer fixture: a defaults file */
    char path[512];
    snprintf(path, sizeof(path), "%s/shell.conf", sysdir);
    FILE *f = fopen(path, "w");
    fprintf(f, "[shell]\nvolume = 33\nwallpaper = #aabbcc\n");
    fclose(f);
    snprintf(path, sizeof(path), "%s/display.conf", sysdir);
    f = fopen(path, "w");
    fprintf(f, "[display]\nbrightness = 70\n");
    fclose(f);
}

static void test_reg(void)
{
    char *v = reg_get("shell", "wallpaper");
    CHECK(v && strcmp(v, "#aabbcc") == 0);
    free(v);
    CHECK(reg_get_int("shell", "volume", 50) == 33);
    CHECK(reg_get_int("display", "brightness", 0) == 70);
    CHECK(reg_get_int("shell", "missing", 42) == 42);
    CHECK(reg_get_bool("shell", "wifi", true) == true);

    /* user layer wins over system */
    CHECK(reg_set("shell", "volume", "80") == 0);
    CHECK(reg_get_int("shell", "volume", 50) == 80);
    /* the file on disk is brainreg format: one section per file */
    char path[512];
    snprintf(path, sizeof(path), "%s/shell.conf", userdir);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    char content[512] = "";
    size_t n = fread(content, 1, sizeof(content) - 1, f);
    content[n] = 0;
    fclose(f);
    CHECK(strstr(content, "[shell]") != NULL);
    CHECK(strstr(content, "volume = 80") != NULL);

    /* env layer wins over both */
    setenv("BRAINREG_SHELL_VOLUME", "5", 1);
    CHECK(reg_get_int("shell", "volume", 50) == 5);
    unsetenv("BRAINREG_SHELL_VOLUME");

    /* unset falls back to the system value */
    CHECK(reg_unset("shell", "volume") == 0);
    CHECK(reg_get_int("shell", "volume", 50) == 33);

    /* bool round trip */
    CHECK(reg_set("shell", "wifi", "false") == 0);
    CHECK(reg_get_bool("shell", "wifi", true) == false);

    /* list merges layers */
    reg_entry entries[32];
    int cnt = reg_list(entries, 32);
    CHECK(cnt >= 3);
    bool saw_brightness = false;
    for (int i = 0; i < cnt; i++)
        if (!strcmp(entries[i].section, "display") &&
            !strcmp(entries[i].key, "brightness") &&
            !strcmp(entries[i].value, "70"))
            saw_brightness = true;
    CHECK(saw_brightness);
}

static void test_catalog(void)
{
    de_catalog *c = de_catalog_new(NULL);
    int added1 = de_catalog_load_dir(c, FIX1);
    /* dir1 valid apps: netsurf, pcmanfm, lxterminal, writer,
     * escapes, game, settings-app, noexec, nodisplay = 9
     * (hidden, link, broken are excluded) */
    CHECK(added1 == 9);
    CHECK(de_catalog_count(c) == 9);
    CHECK(de_catalog_visible_count(c) == 8); /* nodisplay excluded */

    int added2 = de_catalog_load_dir(c, FIX2);
    CHECK(added2 == 1); /* only extra.desktop; netsurf is masked */
    CHECK(de_catalog_count(c) == 10);

    const de_entry *ns = de_catalog_find_id(c, "netsurf.desktop");
    CHECK(ns != NULL);
    CHECK(ns && strcmp(ns->name, "NetSurf Web Browser") == 0);
    CHECK(ns && ns->main_category == DE_CAT_NETWORK);
    CHECK(ns && ns->launchable);

    CHECK(de_catalog_find_id(c, "hidden-app.desktop") == NULL);
    const de_entry *nd = de_catalog_find_id(c, "nodisplay.desktop");
    CHECK(nd != NULL && nd->no_display);

    const de_entry *pf = de_catalog_find_id(c, "pcmanfm.desktop");
    CHECK(pf && pf->main_category == DE_CAT_SYSTEM);
    const de_entry *lt = de_catalog_find_id(c, "lxterminal.desktop");
    CHECK(lt && lt->main_category == DE_CAT_SYSTEM);
    const de_entry *wr = de_catalog_find_id(c, "writer.desktop");
    CHECK(wr && wr->main_category == DE_CAT_OFFICE);
    const de_entry *es = de_catalog_find_id(c, "escapes.desktop");
    CHECK(es && strcmp(es->name, "My Editor") == 0); /* \s decoded */
    CHECK(es && es->main_category == DE_CAT_ACCESSORIES);
    const de_entry *gm = de_catalog_find_id(c, "game.desktop");
    CHECK(gm && gm->main_category == DE_CAT_GAME);
    const de_entry *st = de_catalog_find_id(c, "settings-app.desktop");
    CHECK(st && st->main_category == DE_CAT_SETTINGS);
    const de_entry *ne = de_catalog_find_id(c, "noexec.desktop");
    CHECK(ne && !ne->launchable);
    CHECK(ne && de_exec_argv(ne) == NULL);

    /* queries */
    const de_entry *out[32];
    size_t m = de_catalog_query(c, DE_CAT_ALL, NULL, out, 32);
    CHECK(m == 9); /* visible only */
    /* sorted by name */
    for (size_t i = 1; i < m; i++)
        CHECK(strcasecmp(out[i - 1]->name, out[i]->name) <= 0);
    m = de_catalog_query(c, DE_CAT_NETWORK, NULL, out, 32);
    CHECK(m == 1 && strcmp(out[0]->id, "netsurf.desktop") == 0);
    m = de_catalog_query(c, DE_CAT_ALL, "browser", out, 32);
    CHECK(m >= 1);
    bool found_ns = false;
    for (size_t i = 0; i < m; i++)
        if (!strcmp(out[i]->id, "netsurf.desktop")) found_ns = true;
    CHECK(found_ns);
    m = de_catalog_query(c, DE_CAT_ALL, "file", out, 32);
    CHECK(m >= 1); /* PCManFM via GenericName */
    m = de_catalog_query(c, DE_CAT_ALL, "zzz-no-match", out, 32);
    CHECK(m == 0);
    m = de_catalog_query(c, DE_CAT_SYSTEM, NULL, out, 32);
    CHECK(m == 2); /* pcmanfm + lxterminal */

    /* Exec expansion */
    char **av = de_exec_argv(ns);
    CHECK(av && av[0] && strcmp(av[0], "netsurf-gtk") == 0);
    CHECK(av && av[1] == NULL); /* %u removed: no URL passed */
    de_argv_free(av);
    av = de_exec_argv(wr);
    CHECK(av && strcmp(av[0], "writer") == 0);
    CHECK(av && av[1] && strcmp(av[1], "--start") == 0);
    CHECK(av && av[2] == NULL); /* %F removed: no files passed */
    de_argv_free(av);
    av = de_exec_argv(es);
    CHECK(av && strcmp(av[0], "my editor") == 0);       /* quoted */
    CHECK(av && av[1] && strcmp(av[1], "--title") == 0);
    CHECK(av && av[2] && strcmp(av[2], "My Editor") == 0); /* %c */
    CHECK(av && av[3] && strcmp(av[3], "--icon") == 0);    /* %i */
    CHECK(av && av[4] && strcmp(av[4], "myeditor") == 0);
    CHECK(av && av[5] && strcmp(av[5], "%") == 0);         /* %% */
    CHECK(av && av[6] == NULL);
    de_argv_free(av);

    /* Exec field codes with files/URLs (de_exec_argv_files) */
    {
        char *files2[] = { (char *)"/docs/a.txt", (char *)"/docs/b.txt" };
        char *urls2[] = { (char *)"https://example.org/",
                           (char *)"https://example.net/" };

        /* %u substitutes the (first) URL */
        av = de_exec_argv_files(ns, NULL, 0, urls2, 1);
        CHECK(av && strcmp(av[0], "netsurf-gtk") == 0);
        CHECK(av && av[1] && strcmp(av[1], "https://example.org/") == 0);
        CHECK(av && av[2] == NULL);
        de_argv_free(av);

        /* %F expands to one argument per file */
        av = de_exec_argv_files(wr, files2, 2, NULL, 0);
        CHECK(av && strcmp(av[0], "writer") == 0);
        CHECK(av && av[1] && strcmp(av[1], "--start") == 0);
        CHECK(av && av[2] && strcmp(av[2], "/docs/a.txt") == 0);
        CHECK(av && av[3] && strcmp(av[3], "/docs/b.txt") == 0);
        CHECK(av && av[4] == NULL);
        de_argv_free(av);

        /* %F with an empty list: code removed */
        av = de_exec_argv_files(wr, NULL, 0, NULL, 0);
        CHECK(av && av[1] && strcmp(av[1], "--start") == 0);
        CHECK(av && av[2] == NULL);
        de_argv_free(av);

        /* %U expands to one argument per URL */
        av = de_exec_argv_files(pf, NULL, 0, urls2, 2);
        CHECK(av && strcmp(av[0], "pcmanfm") == 0);
        CHECK(av && av[1] && strcmp(av[1], "https://example.org/") == 0);
        CHECK(av && av[2] && strcmp(av[2], "https://example.net/") == 0);
        CHECK(av && av[3] == NULL);
        de_argv_free(av);

        /* Hand-built entry: %f takes only the first file and
         * substitutes inside a token; quoted codes stay literal. */
        de_entry fe;
        memset(&fe, 0, sizeof(fe));
        fe.launchable = true;
        fe.name = (char *)"Opener";
        fe.path = (char *)"/tmp/opener.desktop";
        fe.exec_raw = (char *)"opener --file=%f";
        av = de_exec_argv_files(&fe, files2, 2, NULL, 0);
        CHECK(av && strcmp(av[0], "opener") == 0);
        CHECK(av && av[1] && strcmp(av[1], "--file=/docs/a.txt") == 0);
        CHECK(av && av[2] == NULL);
        de_argv_free(av);
        /* no files: code removed, the rest of the token kept */
        av = de_exec_argv_files(&fe, NULL, 0, NULL, 0);
        CHECK(av && av[1] && strcmp(av[1], "--file=") == 0);
        CHECK(av && av[2] == NULL);
        de_argv_free(av);

        fe.exec_raw = (char *)"opener \"%f\" %f";
        av = de_exec_argv_files(&fe, files2, 1, NULL, 0);
        CHECK(av && av[1] && strcmp(av[1], "%f") == 0); /* quoted */
        CHECK(av && av[2] && strcmp(av[2], "/docs/a.txt") == 0);
        CHECK(av && av[3] == NULL);
        de_argv_free(av);
    }

    de_catalog_free(c);

    /* localized names */
    de_catalog *cj = de_catalog_new("ja_JP");
    de_catalog_load_dir(cj, FIX1);
    const de_entry *wrj = de_catalog_find_id(cj, "writer.desktop");
    CHECK(wrj && strcmp(wrj->name, "ライター") == 0);
    const de_entry *nsj = de_catalog_find_id(cj, "netsurf.desktop");
    CHECK(nsj && strcmp(nsj->name, "NetSurf Web Browser") == 0); /* fallback */
    de_catalog_free(cj);
}

/* mock exec for launcher tests */
static int mock_calls;
static char mock_argv0[128];
static bool mock_result;
static bool mock_exec(void *ud, const de_entry *e, char *const argv[], bool terminal)
{
    (void)ud; (void)e; (void)terminal;
    mock_calls++;
    snprintf(mock_argv0, sizeof(mock_argv0), "%s", argv[0]);
    return mock_result;
}

static void test_launcher(void)
{
    de_catalog *c = de_catalog_new(NULL);
    de_catalog_load_dir(c, FIX1);

    ln_store *s = ln_store_new();
    CHECK(ln_pin_count(s) == 0);
    CHECK(ln_pin(s, "writer.desktop"));
    CHECK(!ln_pin(s, "writer.desktop")); /* duplicate */
    CHECK(ln_pin(s, "netsurf.desktop"));
    CHECK(ln_is_pinned(s, "writer.desktop"));
    CHECK(ln_pin_count(s) == 2);
    CHECK(strcmp(ln_pin_at(s, 0), "writer.desktop") == 0);

    /* launch through the mock: success records recents */
    mock_calls = 0; mock_result = true;
    const de_entry *ns = de_catalog_find_id(c, "netsurf.desktop");
    CHECK(ln_launch(s, ns, mock_exec, NULL));
    CHECK(mock_calls == 1);
    CHECK(strcmp(mock_argv0, "netsurf-gtk") == 0);
    CHECK(ln_recent_count(s) == 1);
    CHECK(strcmp(ln_recent_at(s, 0), "netsurf.desktop") == 0);

    const de_entry *wr = de_catalog_find_id(c, "writer.desktop");
    CHECK(ln_launch(s, wr, mock_exec, NULL));
    CHECK(ln_recent_count(s) == 2);
    CHECK(strcmp(ln_recent_at(s, 0), "writer.desktop") == 0);
    /* re-launch moves to front without duplicating */
    CHECK(ln_launch(s, ns, mock_exec, NULL));
    CHECK(ln_recent_count(s) == 2);
    CHECK(strcmp(ln_recent_at(s, 0), "netsurf.desktop") == 0);

    /* failing exec does not record */
    mock_result = false;
    CHECK(!ln_launch(s, wr, mock_exec, NULL));
    CHECK(strcmp(ln_recent_at(s, 0), "netsurf.desktop") == 0);

    /* non-launchable entry never reaches the exec callback */
    mock_calls = 0; mock_result = true;
    const de_entry *ne = de_catalog_find_id(c, "noexec.desktop");
    CHECK(!ln_launch(s, ne, mock_exec, NULL));
    CHECK(mock_calls == 0);

    /* unpin */
    CHECK(ln_unpin(s, "writer.desktop"));
    CHECK(!ln_unpin(s, "writer.desktop"));
    ln_pin(s, "writer.desktop");

    /* persistence across store instances (through reg files) */
    ln_store_free(s);
    s = ln_store_new();
    CHECK(ln_pin_count(s) == 2);
    CHECK(ln_is_pinned(s, "netsurf.desktop"));
    CHECK(ln_recent_count(s) == 2);
    CHECK(strcmp(ln_recent_at(s, 0), "netsurf.desktop") == 0);
    ln_store_free(s);

    /* recents cap */
    s = ln_store_new();
    char id[64];
    for (int i = 0; i < 12; i++) {
        snprintf(id, sizeof(id), "app%d.desktop", i);
        ln_record_launch(s, id);
    }
    CHECK(ln_recent_count(s) == LN_RECENT_MAX);
    CHECK(strcmp(ln_recent_at(s, 0), "app11.desktop") == 0);
    ln_store_free(s);
    de_catalog_free(c);
}

/* The catalog parses each .desktop file exactly once at load;
 * queries afterwards must work from memory even if the files
 * disappear (overhead principle: no re-parse per launch/refresh). */
static void test_no_reparse(void)
{
    char tmpl[] = "/tmp/bdnorepXXXXXX";
    char *base = mkdtemp(tmpl);
    char dir[256], path[512];
    snprintf(dir, sizeof(dir), "%s/apps", base);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/one.desktop", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "[Desktop Entry]\nType=Application\nName=One App\n"
               "Exec=one\nCategories=Utility;\n");
    fclose(f);
    de_catalog *c = de_catalog_new(NULL);
    CHECK(de_catalog_load_dir(c, dir) == 1);
    remove(path);
    rmdir(dir);
    const de_entry *out[4];
    CHECK(de_catalog_query(c, DE_CAT_ALL, NULL, out, 4) == 1);
    for (int i = 0; i < 50; i++)
        de_catalog_query(c, DE_CAT_ALL, "one", out, 4);
    const de_entry *e = de_catalog_find_id(c, "one.desktop");
    CHECK(e && strcmp(e->name, "One App") == 0);
    char **av = de_exec_argv(e);
    CHECK(av && strcmp(av[0], "one") == 0);
    de_argv_free(av);
    de_catalog_free(c);
}

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    fputs(content, f);
    fclose(f);
}

static void set_mtime(const char *path, long sec)
{
    struct stat st;
    stat(path, &st);
    struct timespec ts[2];
    ts[0] = st.st_atim;
    ts[1].tv_sec = sec;
    ts[1].tv_nsec = 0;
    utimensat(AT_FDCWD, path, ts, 0);
}

/* restore an exact (sec, nsec) mtime captured by get_mtime_full */
static void restore_mtime(const char *path, struct timespec mt)
{
    struct stat st;
    stat(path, &st);
    struct timespec ts[2];
    ts[0] = st.st_atim;
    ts[1] = mt;
    utimensat(AT_FDCWD, path, ts, 0);
}

static struct timespec get_mtime_full(const char *path)
{
    struct stat st;
    stat(path, &st);
    return st.st_mtim;
}

static long get_mtime(const char *path)
{
    struct stat st;
    stat(path, &st);
    return (long)st.st_mtime;
}

/* mtime-invalidated refresh: unchanged files are not re-parsed,
 * changed files are, deletions and Hidden flips are applied. */
static void test_refresh(void)
{
    char tmpl[] = "/tmp/bdrefXXXXXX";
    char *base = mkdtemp(tmpl);
    char dir[256], p1[512], p2[512], p3[512];
    snprintf(dir, sizeof(dir), "%s/apps", base);
    mkdir(dir, 0755);
    snprintf(p1, sizeof(p1), "%s/one.desktop", dir);
    snprintf(p2, sizeof(p2), "%s/two.desktop", dir);
    snprintf(p3, sizeof(p3), "%s/three.desktop", dir);
    write_file(p1, "[Desktop Entry]\nType=Application\nName=One App\n"
                   "Exec=one\nCategories=Utility;\n");
    write_file(p2, "[Desktop Entry]\nType=Application\nName=Two App\n"
                   "Exec=two\nCategories=Utility;\n");
    de_catalog *c = de_catalog_new(NULL);
    CHECK(de_catalog_load_dir(c, dir) == 2);
    CHECK(de_catalog_refresh(c) == 0); /* nothing changed */

    /* content changed but mtime restored exactly: NOT re-parsed */
    struct timespec m1f = get_mtime_full(p1);
    long m1 = get_mtime(p1);
    write_file(p1, "[Desktop Entry]\nType=Application\nName=One Renamed\n"
                   "Exec=one\nCategories=Utility;\n");
    restore_mtime(p1, m1f);
    CHECK(de_catalog_refresh(c) == 0);
    const de_entry *e = de_catalog_find_id(c, "one.desktop");
    CHECK(e && strcmp(e->name, "One App") == 0);

    /* mtime bumped: re-parsed */
    set_mtime(p1, m1 + 10);
    CHECK(de_catalog_refresh(c) >= 1);
    e = de_catalog_find_id(c, "one.desktop");
    CHECK(e && strcmp(e->name, "One Renamed") == 0);

    /* new file appears */
    write_file(p3, "[Desktop Entry]\nType=Application\nName=Three App\n"
                   "Exec=three\nCategories=Game;\n");
    CHECK(de_catalog_refresh(c) >= 1);
    CHECK(de_catalog_count(c) == 3);

    /* deletion is applied */
    remove(p2);
    CHECK(de_catalog_refresh(c) >= 1);
    CHECK(de_catalog_count(c) == 2);
    CHECK(de_catalog_find_id(c, "two.desktop") == NULL);

    /* Hidden flip removes the entry (and masks the id) */
    long m3 = get_mtime(p3);
    write_file(p3, "[Desktop Entry]\nType=Application\nName=Three App\n"
                   "Exec=three\nHidden=true\nCategories=Game;\n");
    set_mtime(p3, m3 + 10);
    CHECK(de_catalog_refresh(c) >= 1);
    CHECK(de_catalog_count(c) == 1);
    CHECK(de_catalog_find_id(c, "three.desktop") == NULL);
    de_catalog_free(c);
}

/* Memory budget: trimming drops Comment/Keywords but never the
 * fields needed to list and launch an app. */
static void test_budget(void)
{
    de_catalog *big = de_catalog_new(NULL);
    de_catalog_set_budget(big, 0); /* unlimited */
    de_catalog_load_dir(big, FIX1);
    size_t mem_big = de_catalog_memory(big);
    CHECK(!de_catalog_trimmed(big));

    de_catalog *small = de_catalog_new(NULL);
    de_catalog_set_budget(small, 1024);
    de_catalog_load_dir(small, FIX1);
    CHECK(de_catalog_trimmed(small));
    CHECK(de_catalog_memory(small) < mem_big);
    const de_entry *ns = de_catalog_find_id(small, "netsurf.desktop");
    CHECK(ns && ns->launchable);
    CHECK(ns && ns->comment == NULL); /* trimmed */
    const de_entry *out[4];
    CHECK(de_catalog_query(small, DE_CAT_ALL, "netsurf", out, 4) >= 1);
    de_catalog_free(big);
    de_catalog_free(small);
}

/* Durability against sudden reset (device reality, DESIGN
 * section 12): torn files degrade to defaults, leftover .tmp is
 * ignored, the next write recovers a clean file, and persisted
 * launcher ids are sanitized at load. */
static void test_durability(void)
{
    char path[512], tmp[512];
    snprintf(path, sizeof(path), "%s/shell.conf", userdir);
    snprintf(tmp, sizeof(tmp), "%s/shell.conf.tmp", userdir);

    /* leftover tmp from a crashed write is ignored */
    write_file(tmp, "garbage\x01\x02 not a config");
    CHECK(reg_get_int("shell", "volume", 50) == 33); /* sys value */

    /* torn user file: truncated mid-key, no newline */
    write_file(path, "[shell]\nvolu");
    CHECK(reg_get_int("shell", "volume", 50) == 33); /* falls back */
    CHECK(reg_get_int("shell", "missing", 42) == 42);

    /* the next write recovers: valid file, tmp consumed by rename */
    CHECK(reg_set("shell", "volume", "91") == 0);
    CHECK(reg_get_int("shell", "volume", 50) == 91);
    CHECK(access(tmp, F_OK) != 0);
    FILE *f = fopen(path, "r");
    char content[512] = "";
    size_t n = fread(content, 1, sizeof(content) - 1, f);
    content[n] = 0;
    fclose(f);
    CHECK(strstr(content, "volume = 91") != NULL);
    CHECK(strstr(content, "volu\n") == NULL); /* garbage key gone */

    /* corrupt persisted ids are sanitized at store load */
    CHECK(reg_set("shell", "pinned",
                  "../evil.desktop;writer.desktop;writer.desktop;"
                  "/abs.desktop;") == 0);
    ln_store *s = ln_store_new();
    CHECK(ln_pin_count(s) == 1);
    CHECK(ln_is_pinned(s, "writer.desktop"));
    ln_store_free(s);
    reg_unset("shell", "pinned");
    reg_unset("shell", "recent");
}

/* Real exec: a vanished binary must be a failed launch (not
 * recorded), a real one must succeed. */
static void test_real_exec(void)
{
    char tmpl[] = "/tmp/bdexecXXXXXX";
    char *base = mkdtemp(tmpl);
    char dir[256], p[512];
    snprintf(dir, sizeof(dir), "%s/apps", base);
    mkdir(dir, 0755);
    snprintf(p, sizeof(p), "%s/true.desktop", dir);
    write_file(p, "[Desktop Entry]\nType=Application\nName=True\n"
                  "Exec=/bin/true\nCategories=Utility;\n");
    snprintf(p, sizeof(p), "%s/gone.desktop", dir);
    write_file(p, "[Desktop Entry]\nType=Application\nName=Gone\n"
                  "Exec=/nonexistent/bd-missing-bin\nCategories=Utility;\n");
    de_catalog *c = de_catalog_new(NULL);
    CHECK(de_catalog_load_dir(c, dir) == 2);
    ln_store *s = ln_store_new();
    const de_entry *et = de_catalog_find_id(c, "true.desktop");
    const de_entry *eg = de_catalog_find_id(c, "gone.desktop");
    CHECK(ln_launch(s, et, ln_exec_real, NULL));
    CHECK(ln_recent_count(s) == 1);
    CHECK(!ln_launch(s, eg, ln_exec_real, NULL));
    CHECK(ln_recent_count(s) == 1); /* failure not recorded */
    CHECK(strcmp(ln_recent_at(s, 0), "true.desktop") == 0);
    ln_store_free(s);
    de_catalog_free(c);
    reg_unset("shell", "recent");
}

/* SD removed / never present: catalog operations degrade to an
 * empty catalog, never to a crash. */
static void test_vanishing_dir(void)
{
    de_catalog *c = de_catalog_new(NULL);
    CHECK(de_catalog_load_dir(c, "/nonexistent/bd-apps") == 0);
    CHECK(de_catalog_refresh(c) == 0);
    char tmpl[] = "/tmp/bdvanishXXXXXX";
    char *base = mkdtemp(tmpl);
    char dir[256], p[512];
    snprintf(dir, sizeof(dir), "%s/apps", base);
    mkdir(dir, 0755);
    snprintf(p, sizeof(p), "%s/one.desktop", dir);
    write_file(p, "[Desktop Entry]\nType=Application\nName=One\n"
                  "Exec=one\nCategories=Utility;\n");
    CHECK(de_catalog_load_dir(c, dir) == 1);
    remove(p);
    rmdir(dir); /* the whole medium is gone */
    CHECK(de_catalog_refresh(c) >= 1);
    CHECK(de_catalog_count(c) == 0);
    const de_entry *out[4];
    CHECK(de_catalog_query(c, DE_CAT_ALL, NULL, out, 4) == 0);
    de_catalog_free(c);
}

/* Generation arena lifecycle (M3): one load = one generation;
 * a no-change refresh creates none; replacing an entry moves it
 * to a new generation; when the last entry of a generation is
 * replaced/removed the generation's block is freed whole. */
static void test_arena_generations(void)
{
    char tmpl[] = "/tmp/bdarenaXXXXXX";
    char *base = mkdtemp(tmpl);
    char dir[256], p1[512], p2[512];
    snprintf(dir, sizeof(dir), "%s/apps", base);
    mkdir(dir, 0755);
    snprintf(p1, sizeof(p1), "%s/one.desktop", dir);
    snprintf(p2, sizeof(p2), "%s/two.desktop", dir);
    write_file(p1, "[Desktop Entry]\nType=Application\nName=One\n"
                   "Exec=one\nCategories=Utility;\n");
    write_file(p2, "[Desktop Entry]\nType=Application\nName=Two\n"
                   "Exec=two\nCategories=Game;\n");
    de_catalog *c = de_catalog_new(NULL);
    CHECK(de_catalog_load_dir(c, dir) == 2);
    CHECK(de_catalog_generations(c) == 1);
    size_t bytes1 = de_catalog_arena_bytes(c);
    CHECK(bytes1 > 0);
    /* strings are really in the arena era: entry data readable */
    const de_entry *e = de_catalog_find_id(c, "one.desktop");
    CHECK(e && strcmp(e->name, "One") == 0 && strcmp(e->exec_raw, "one") == 0);
    CHECK(e->id && strcmp(e->id, "one.desktop") == 0);
    /* no-change refresh: no new generation, no byte growth */
    CHECK(de_catalog_refresh(c) == 0);
    CHECK(de_catalog_generations(c) == 1);
    CHECK(de_catalog_arena_bytes(c) == bytes1);
    /* change one file: it migrates to generation 2, gen 1 stays
     * alive for the untouched entry */
    write_file(p1, "[Desktop Entry]\nType=Application\nName=One Renamed\n"
                   "Exec=one2\nCategories=Utility;\n");
    set_mtime(p1, get_mtime(p1) + 10);
    CHECK(de_catalog_refresh(c) == 1);
    CHECK(de_catalog_generations(c) == 2);
    e = de_catalog_find_id(c, "one.desktop");
    CHECK(e && strcmp(e->name, "One Renamed") == 0);
    /* change the other file too: generation 1 now has no live
     * entries and must be freed whole */
    write_file(p2, "[Desktop Entry]\nType=Application\nName=Two Renamed\n"
                   "Exec=two2\nCategories=Game;\n");
    set_mtime(p2, get_mtime(p2) + 10);
    CHECK(de_catalog_refresh(c) == 1);
    CHECK(de_catalog_generations(c) == 2); /* gens 2 and 3 */
    /* remove everything: all generations die */
    remove(p1);
    remove(p2);
    CHECK(de_catalog_refresh(c) >= 2);
    CHECK(de_catalog_count(c) == 0);
    CHECK(de_catalog_generations(c) == 0);
    CHECK(de_catalog_arena_bytes(c) == 0);
    de_catalog_free(c);
}

int main(void)
{
    setup_reg();
    test_reg();
    test_catalog();
    test_launcher();
    test_no_reparse();
    test_refresh();
    test_budget();
    test_durability();
    test_real_exec();
    test_vanishing_dir();
    test_arena_generations();
    printf("test_desktop: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
