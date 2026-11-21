/* test_files - bd-files model tests (fmodel + bdstore + bdmime).
 *
 * Groups:
 *   L listing + sorting + grouping
 *   N navigation (history, breadcrumbs)
 *   O file operations (mkdir/create/rename/copy/move/delete)
 *   S scoped search
 *   P properties + image header sniffing
 *   C dimension cache (budgeted LRU)
 *   T trash (capacity eviction, restore, orphan adoption)
 *   M mime associations + open argv (via the shell catalog)
 */
#define _POSIX_C_SOURCE 200809L
#include "fmodel.h"
#include "bdstore.h"
#include "bdmime.h"
#include "desktop.h"
#include "reg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static int passes, fails;
static void check_true(const char *what, int cond)
{
    if (cond) { passes++; printf("PASS %s\n", what); }
    else { fails++; printf("FAIL %s\n", what); }
}
static void check_int(const char *what, long got, long want)
{
    if (got == want) { passes++; printf("PASS %s -> %ld\n", what, got); }
    else { fails++; printf("FAIL %s: got %ld, want %ld\n", what, got, want); }
}
static void check_str(const char *what, const char *got, const char *want)
{
    if (got && want && !strcmp(got, want)) {
        passes++; printf("PASS %s -> %s\n", what, got);
    } else {
        fails++;
        printf("FAIL %s: got %s, want %s\n", what,
               got ? got : "(null)", want ? want : "(null)");
    }
}

static char ROOT[512];

static void path_of(char *out, size_t cap, const char *rel)
{
    snprintf(out, cap, "%s/%s", ROOT, rel);
}

static void write_file(const char *rel, const char *data, size_t len)
{
    char p[1024];
    path_of(p, sizeof(p), rel);
    FILE *f = fopen(p, "wb");
    if (!f) { perror("write_file"); exit(2); }
    if (len && fwrite(data, 1, len, f) != len) { perror("fwrite"); exit(2); }
    fclose(f);
}

static void make_dir(const char *rel)
{
    char p[1024];
    path_of(p, sizeof(p), rel);
    if (mkdir(p, 0755) != 0 && errno != EEXIST) { perror("mkdir"); exit(2); }
}

static bool exists_rel(const char *rel)
{
    char p[1024];
    path_of(p, sizeof(p), rel);
    struct stat st;
    return stat(p, &st) == 0;
}

static char *read_rel(const char *rel)
{
    char p[1024];
    path_of(p, sizeof(p), rel);
    return bd_read_all(p, NULL);
}

/* A minimal PNG: signature + IHDR with w=640,h=480. */
static void write_png(const char *rel, unsigned w, unsigned h)
{
    unsigned char buf[33];
    memset(buf, 0, sizeof(buf));
    static const unsigned char sig[8] =
        { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    memcpy(buf, sig, 8);
    buf[11] = 13; /* IHDR length */
    memcpy(buf + 12, "IHDR", 4);
    buf[16] = (w >> 24) & 0xFF; buf[17] = (w >> 16) & 0xFF;
    buf[18] = (w >> 8) & 0xFF;  buf[19] = w & 0xFF;
    buf[20] = (h >> 24) & 0xFF; buf[21] = (h >> 16) & 0xFF;
    buf[22] = (h >> 8) & 0xFF;  buf[23] = h & 0xFF;
    write_file(rel, (const char *)buf, sizeof(buf));
}

/* A minimal JPEG: SOI, APP0, SOF0 with w=320,h=240. */
static void write_jpeg(const char *rel, unsigned w, unsigned h)
{
    unsigned char buf[64];
    size_t i = 0;
    buf[i++] = 0xFF; buf[i++] = 0xD8;             /* SOI */
    buf[i++] = 0xFF; buf[i++] = 0xE0;             /* APP0 */
    buf[i++] = 0x00; buf[i++] = 0x10;             /* len 16 */
    for (int k = 0; k < 14; k++) buf[i++] = 0;    /* payload */
    buf[i++] = 0xFF; buf[i++] = 0xC0;             /* SOF0 */
    buf[i++] = 0x00; buf[i++] = 0x11;             /* len 17 */
    buf[i++] = 8;                                 /* precision */
    buf[i++] = (h >> 8) & 0xFF; buf[i++] = h & 0xFF;
    buf[i++] = (w >> 8) & 0xFF; buf[i++] = w & 0xFF;
    buf[i++] = 3;                                 /* components */
    for (int k = 0; k < 9; k++) buf[i++] = 0;
    write_file(rel, (const char *)buf, i);
}

static const fm_entry *find_entry(const fm_listing *l, const char *name)
{
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i].name, name) == 0) return &l->v[i];
    return NULL;
}

int main(void)
{
    char tmpl[] = "/tmp/bdfiles-test-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    snprintf(ROOT, sizeof(ROOT), "%s", tmpl);

    /* fixture tree */
    make_dir("docs");
    make_dir("pics");
    write_file("docs/b.md", "# Beta\n", 7);
    write_file("docs/a.txt", "alpha\n", 6);
    write_file("docs/c.txt", "c\n", 2);
    write_file(".hidden.txt", "h\n", 2);
    write_png("pics/p.png", 640, 480);
    write_jpeg("pics/j.jpg", 320, 240);
    {
        char big[300];
        memset(big, 'x', sizeof(big));
        write_file("big.bin", big, sizeof(big));
    }

    /* ---------------- L: listing + sort + group ---------------- */
    {
        char p[1024];
        path_of(p, sizeof(p), ".");
        fm_listing l;
        check_int("L load no-hidden rc", fm_listing_load(&l, p, false), 0);
        check_int("L count no-hidden", (long)l.n, 3); /* docs, pics, big.bin */
        check_true("L hidden excluded", find_entry(&l, ".hidden.txt") == NULL);
        fm_listing_free(&l);
        check_int("L load hidden rc", fm_listing_load(&l, p, true), 0);
        check_int("L count hidden", (long)l.n, 4);
        check_true("L hidden included", find_entry(&l, ".hidden.txt") != NULL);
        fm_listing_free(&l);

        path_of(p, sizeof(p), "docs");
        check_int("L docs rc", fm_listing_load(&l, p, false), 0);
        check_int("L docs count", (long)l.n, 3);
        fm_listing_sort(&l, FM_SORT_NAME, false, true);
        check_str("L sort name 0", l.v[0].name, "a.txt");
        check_str("L sort name 2", l.v[2].name, "c.txt");
        fm_listing_sort(&l, FM_SORT_SIZE, true, true);
        check_str("L sort size desc 0", l.v[0].name, "b.md");
        fm_listing_sort(&l, FM_SORT_TYPE, false, true);
        check_str("L kind txt", find_entry(&l, "a.txt")->kind, "Text document");
        /* grouping labels */
        char label[64];
        int64_t now = l.v[0].mtime;
        int g = fm_entry_group(&l.v[0], FM_GROUP_DATE, now - 3600,
                               now - 90000, label, sizeof(label));
        check_true("L group date today", g == 2 && !strcmp(label, "Today"));
        g = fm_entry_group(&l.v[0], FM_GROUP_TYPE, 0, 0, label, sizeof(label));
        check_true("L group type", g == 1 && label[0]);
        fm_listing_free(&l);
    }

    /* ---------------- N: history + breadcrumbs ----------------- */
    {
        fm_history h;
        fm_history_init(&h);
        check_true("N empty current", fm_history_current(&h) == NULL);
        fm_history_push(&h, "/a");
        fm_history_push(&h, "/a/b");
        fm_history_push(&h, "/a/b"); /* no-op */
        check_str("N current", fm_history_current(&h), "/a/b");
        check_true("N can back", fm_history_can_back(&h));
        check_str("N back", fm_history_back(&h), "/a");
        check_true("N can fwd", fm_history_can_forward(&h));
        fm_history_push(&h, "/x"); /* truncates forward */
        check_true("N fwd truncated", !fm_history_can_forward(&h));
        check_str("N current after push", fm_history_current(&h), "/x");
        check_str("N back 2", fm_history_back(&h), "/a");
        check_true("N back at start", fm_history_back(&h) == NULL);
        fm_history_free(&h);

        check_int("N crumbs root", fm_crumb_count("/"), 1);
        check_int("N crumbs /a/b", fm_crumb_count("/a/b"), 3);
        char label[64], cp[1024];
        check_true("N crumb 0", fm_crumb_at("/a/b", 0, label, sizeof(label),
                                            cp, sizeof(cp)) &&
                                !strcmp(cp, "/"));
        check_true("N crumb 2", fm_crumb_at("/a/b", 2, label, sizeof(label),
                                            cp, sizeof(cp)) &&
                                !strcmp(label, "b") && !strcmp(cp, "/a/b"));
        check_true("N crumb oob", !fm_crumb_at("/a/b", 3, label,
                                               sizeof(label), cp, sizeof(cp)));
    }

    /* ---------------- O: file operations ----------------------- */
    {
        char dir[1024], out[1024];
        path_of(dir, sizeof(dir), "docs");
        check_int("O mkdir", fm_mkdir(dir, "sub", out, sizeof(out)), 0);
        check_true("O mkdir exists", exists_rel("docs/sub"));
        errno = 0;
        check_true("O mkdir EEXIST",
                   fm_mkdir(dir, "sub", out, sizeof(out)) != 0 &&
                   errno == EEXIST);
        check_int("O create", fm_create_file(dir, "new.txt", out, sizeof(out)), 0);
        check_int("O rename", fm_rename(out, "renamed.txt", out, sizeof(out)), 0);
        check_true("O renamed exists", exists_rel("docs/renamed.txt"));
        check_true("O old name gone", !exists_rel("docs/new.txt"));
        errno = 0;
        char src[1024];
        path_of(src, sizeof(src), "docs/a.txt");
        check_true("O rename EEXIST",
                   fm_rename(src, "renamed.txt", out, sizeof(out)) != 0 &&
                   errno == EEXIST);

        /* copy file: content + atomicity (no tmp leftovers) */
        char dst_dir[1024];
        path_of(dst_dir, sizeof(dst_dir), "pics");
        int copied = 0;
        check_int("O copy file", fm_copy(src, dst_dir, false, out,
                                         sizeof(out), &copied), 0);
        check_int("O copy count", copied, 1);
        char *content = read_rel("pics/a.txt");
        check_str("O copy content", content, "alpha\n");
        free(content);
        errno = 0;
        check_true("O copy EEXIST",
                   fm_copy(src, dst_dir, false, out, sizeof(out), NULL) != 0 &&
                   errno == EEXIST);
        check_int("O copy overwrite", fm_copy(src, dst_dir, true, out,
                                              sizeof(out), NULL), 0);
        /* no .tmp files anywhere after copies */
        fm_listing l;
        check_int("O list pics", fm_listing_load(&l, dst_dir, true), 0);
        bool tmp_seen = false;
        for (size_t i = 0; i < l.n; i++)
            if (strstr(l.v[i].name, ".tmp.")) tmp_seen = true;
        check_true("O no tmp leftovers", !tmp_seen);
        fm_listing_free(&l);

        /* recursive copy of a directory */
        char docs[1024];
        path_of(docs, sizeof(docs), "docs");
        char rootp[1024];
        path_of(rootp, sizeof(rootp), ".");
        copied = 0;
        check_int("O copy dir", fm_copy(docs, rootp, false, out,
                                        sizeof(out), &copied), -1); /* docs exists in root */
        /* copy docs into pics/ instead */
        check_int("O copy dir 2", fm_copy(docs, dst_dir, false, out,
                                          sizeof(out), &copied), 0);
        check_true("O copy dir nested", exists_rel("pics/docs/a.txt"));
        check_true("O copy dir subdir", exists_rel("pics/docs/sub"));

        /* copying a directory into itself is refused */
        errno = 0;
        check_true("O copy into itself",
                   fm_copy(docs, docs, false, out, sizeof(out), NULL) != 0);

        /* move */
        char mv_src[1024];
        path_of(mv_src, sizeof(mv_src), "docs/c.txt");
        check_int("O move", fm_move(mv_src, dst_dir, false, out,
                                    sizeof(out)), 0);
        check_true("O moved", exists_rel("pics/c.txt") &&
                              !exists_rel("docs/c.txt"));

        /* permanent delete */
        char del[1024];
        path_of(del, sizeof(del), "pics/docs");
        check_int("O delete tree", fm_delete_permanent(del), 0);
        check_true("O tree gone", !exists_rel("pics/docs"));
    }

    /* ---------------- S: search -------------------------------- */
    {
        char rootp[1024];
        path_of(rootp, sizeof(rootp), ".");
        fm_listing res;
        bool trunc = false;
        check_int("S rc", fm_search(rootp, "a", false, &res, 64, &trunc), 0);
        check_true("S finds a.txt", find_entry(&res, "a.txt") != NULL);
        check_true("S finds pics/a.txt too", res.n >= 2);
        check_true("S hidden excluded", find_entry(&res, ".hidden.txt") == NULL);
        fm_listing_free(&res);
        check_int("S rc hidden", fm_search(rootp, "hidden", true, &res, 64,
                                           &trunc), 0);
        check_int("S hidden found", (long)res.n, 1);
        fm_listing_free(&res);
        check_int("S rc cap", fm_search(rootp, "", false, &res, 2, &trunc), 0);
        check_true("S truncated flag", trunc);
        fm_listing_free(&res);
    }

    /* ---------------- P: properties + image dims --------------- */
    {
        char p[1024];
        path_of(p, sizeof(p), "pics/p.png");
        int w = 0, h = 0;
        check_true("P png dims", fm_image_dims(p, &w, &h) && w == 640 && h == 480);
        path_of(p, sizeof(p), "pics/j.jpg");
        check_true("P jpg dims", fm_image_dims(p, &w, &h) && w == 320 && h == 240);
        path_of(p, sizeof(p), "docs/a.txt");
        check_true("P txt no dims", !fm_image_dims(p, &w, &h));

        fm_props pr;
        path_of(p, sizeof(p), "docs/a.txt");
        check_int("P props file rc", fm_props_load(p, &pr), 0);
        check_int("P props size", (long)pr.size, 6);
        check_str("P props kind", pr.kind, "Text document");
        check_true("P props perms", pr.perms[0] == '-' && strlen(pr.perms) == 10);
        path_of(p, sizeof(p), "pics");
        check_int("P props dir rc", fm_props_load(p, &pr), 0);
        check_true("P props dir is_dir", pr.is_dir);
        check_true("P props dir counts", pr.n_files >= 4 && pr.size > 0);
        path_of(p, sizeof(p), "pics/p.png");
        check_int("P props png rc", fm_props_load(p, &pr), 0);
        check_true("P props png dims", pr.img_w == 640 && pr.img_h == 480);
    }

    /* ---------------- C: dimension cache ------------------------ */
    {
        fm_thumbcache *c = fm_thumbcache_new(1024 * 1024);
        char p[1024];
        path_of(p, sizeof(p), "pics/p.png");
        int w = 0, h = 0;
        check_true("C miss loads", fm_thumbcache_dims(c, p, &w, &h) &&
                                   w == 640);
        check_int("C misses 1", (long)fm_thumbcache_misses(c), 1);
        check_true("C hit", fm_thumbcache_dims(c, p, &w, &h));
        check_int("C hits 1", (long)fm_thumbcache_hits(c), 1);
        /* change the file and force a distinct mtime -> invalidated
         * (the two PNGs have the same byte size, and mtime has
         * one-second granularity, so bump the timestamp) */
        write_png("pics/p.png", 100, 50);
        {
            struct stat st;
            char pp[1024];
            path_of(pp, sizeof(pp), "pics/p.png");
            if (stat(pp, &st) == 0) {
                struct utimbuf ub;
                ub.actime = st.st_atime;
                ub.modtime = st.st_mtime + 10;
                (void)utime(pp, &ub);
            }
        }
        check_true("C invalidated", fm_thumbcache_dims(c, p, &w, &h) &&
                                    w == 100 && h == 50);
        check_int("C misses 2", (long)fm_thumbcache_misses(c), 2);
        fm_thumbcache_free(c);

        /* tiny budget: eviction keeps bytes <= budget */
        fm_thumbcache *small = fm_thumbcache_new(200);
        const char *names[] = { "pics/p.png", "pics/j.jpg", "docs/a.txt",
                                "docs/b.md", "big.bin" };
        for (int i = 0; i < 5; i++) {
            char q[1024];
            path_of(q, sizeof(q), names[i]);
            fm_thumbcache_dims(small, q, &w, &h);
        }
        check_true("C budget respected",
                   fm_thumbcache_bytes(small) <= 200 ||
                   fm_thumbcache_count(small) <= 1);
        fm_thumbcache_free(small);
    }

    /* ---------------- T: trash ---------------------------------- */
    {
        char troot[1024];
        path_of(troot, sizeof(troot), "trash");
        fm_trash *t = fm_trash_open(troot, 1024 * 1024);
        check_true("T open", t != NULL);
        char victim[1024];
        path_of(victim, sizeof(victim), "docs/b.md");
        int evicted = -1;
        check_int("T add", fm_trash_add(t, victim, &evicted), 0);
        check_true("T file moved", !exists_rel("docs/b.md"));
        fm_trash_list tl;
        check_int("T list rc", fm_trash_list_all(t, &tl), 0);
        check_int("T list count", (long)tl.n, 1);
        check_str("T list name", tl.v[0].name, "b.md");
        check_true("T list orig", strstr(tl.v[0].orig_path, "docs/b.md") != NULL);
        uint64_t id = tl.v[0].id;
        fm_trash_list_free(&tl);
        char out[1024];
        check_int("T restore", fm_trash_restore(t, id, troot, out,
                                                sizeof(out)), 0);
        check_true("T restored", exists_rel("docs/b.md"));

        /* restore onto an occupied name -> "name 2" */
        write_file("docs/dup.txt", "one\n", 4);
        char dup[1024];
        path_of(dup, sizeof(dup), "docs/dup.txt");
        check_int("T add dup", fm_trash_add(t, dup, NULL), 0);
        write_file("docs/dup.txt", "two\n", 4);
        check_int("T list rc2", fm_trash_list_all(t, &tl), 0);
        id = tl.v[tl.n - 1].id;
        fm_trash_list_free(&tl);
        check_int("T restore dup", fm_trash_restore(t, id, troot, out,
                                                    sizeof(out)), 0);
        check_true("T restore unique", strstr(out, "dup 2.txt") != NULL &&
                                       exists_rel("docs/dup 2.txt"));
        fm_trash_close(t);

        /* capacity eviction: budget 100 bytes, two 60-byte files */
        char troot2[1024];
        path_of(troot2, sizeof(troot2), "trash2");
        fm_trash *t2 = fm_trash_open(troot2, 100);
        char sixty[61];
        memset(sixty, 'y', 60);
        sixty[60] = '\0';
        write_file("e1.bin", sixty, 60);
        write_file("e2.bin", sixty, 60);
        char e1[1024], e2[1024];
        path_of(e1, sizeof(e1), "e1.bin");
        path_of(e2, sizeof(e2), "e2.bin");
        evicted = 0;
        check_int("T add e1", fm_trash_add(t2, e1, &evicted), 0);
        check_int("T add e2", fm_trash_add(t2, e2, &evicted), 0);
        check_true("T eviction happened", evicted >= 1);
        check_true("T total within budget", fm_trash_total_bytes(t2) <= 100);
        check_int("T list rc3", fm_trash_list_all(t2, &tl), 0);
        check_int("T one remains", (long)tl.n, 1);
        check_str("T survivor is e2", tl.v[0].name, "e2.bin");
        fm_trash_list_free(&tl);
        check_int("T empty", fm_trash_empty(t2), 0);
        check_int("T total 0", (long)fm_trash_total_bytes(t2), 0);
        fm_trash_close(t2);

        /* orphan adoption: payload without info */
        char troot3[1024];
        path_of(troot3, sizeof(troot3), "trash3");
        fm_trash *t3 = fm_trash_open(troot3, 1 << 20);
        fm_trash_close(t3);
        char orphan[1024];
        snprintf(orphan, sizeof(orphan), "%s/files", troot3);
        if (mkdir(orphan, 0755) != 0) { /* exists */ }
        snprintf(orphan, sizeof(orphan), "%s/files/77_lost.txt", troot3);
        FILE *of = fopen(orphan, "wb");
        fputs("lost\n", of);
        fclose(of);
        /* interrupted-write leftover in info/ must be swept */
        char leftover[1024];
        snprintf(leftover, sizeof(leftover), "%s/info/88.info.tmp.999", troot3);
        FILE *lf = fopen(leftover, "wb");
        fputs("orig=/x\n", lf);
        fclose(lf);
        t3 = fm_trash_open(troot3, 1 << 20);
        check_true("T reopen", t3 != NULL);
        check_int("T list rc4", fm_trash_list_all(t3, &tl), 0);
        check_int("T orphan adopted", (long)tl.n, 1);
        check_str("T orphan name", tl.v[0].name, "item");
        uint64_t oid = tl.v[0].id;
        fm_trash_list_free(&tl);
        struct stat st;
        check_true("T tmp swept", stat(leftover, &st) != 0);
        char fb[1024];
        path_of(fb, sizeof(fb), "restored");
        check_int("T orphan restore", fm_trash_restore(t3, oid, fb, out,
                                                        sizeof(out)), 0);
        check_true("T orphan restored file", exists_rel("restored/item"));
        fm_trash_close(t3);
    }

    /* ---------------- M: mime + catalog argv -------------------- */
    {
        char ext[32];
        bd_mime_extension("/a/b/Photo.JPG", ext, sizeof(ext));
        check_str("M ext lower", ext, "jpg");
        bd_mime_extension("/a/b/noext", ext, sizeof(ext));
        check_str("M ext none", ext, "");
        const char *kind = bd_mime_kind("/x/y.png", false);
        check_str("M kind png", kind, "PNG image");
        check_str("M kind dir", bd_mime_kind("/x", true), "Folder");

        /* reg-backed user association + catalog argv */
        char regdir[1024];
        path_of(regdir, sizeof(regdir), "reguser");
        if (bd_mkdir_p(regdir) != 0) { perror("regdir"); }
        setenv("BRAINREG_USER_DIR", regdir, 1);
        setenv("BRAINREG_SYS_DIR", regdir, 1);
        check_int("M set handler", bd_mime_set_handler("txt", "bd-notes.desktop"), 0);
        char hid[128];
        check_true("M handler user", bd_mime_handler_copy("/a/note.txt",
                                                          hid, sizeof(hid)) &&
                                     !strcmp(hid, "bd-notes.desktop"));

        /* a real catalog with our .desktop entries */
        char appdir[1024];
        path_of(appdir, sizeof(appdir), "applications");
        if (bd_mkdir_p(appdir) != 0) { perror("appdir"); }
        {
            const char *de =
                "[Desktop Entry]\nType=Application\nName=Notes\n"
                "Exec=bd-notes --open %f\nIcon=bd-notes\n"
                "Categories=Office;\n";
            write_file("applications/bd-notes.desktop", de, strlen(de));
            const char *de2 =
                "[Desktop Entry]\nType=Application\nName=Multi\n"
                "Exec=bd-multi %F\nIcon=bd-multi\n"
                "Categories=Office;\n";
            write_file("applications/bd-multi.desktop", de2, strlen(de2));
            const char *de3 =
                "[Desktop Entry]\nType=Application\nName=Plain\n"
                "Exec=bd-plain\nIcon=bd-plain\n"
                "Categories=Office;\n";
            write_file("applications/bd-plain.desktop", de3, strlen(de3));
        }
        de_catalog *cat = de_catalog_new("en");
        check_true("M catalog", cat != NULL);
        check_int("M catalog load", de_catalog_load_dir(cat, appdir), 3);
        char target[1024];
        path_of(target, sizeof(target), "docs/a.txt");
        char **argv = bd_mime_argv(cat, target);
        check_true("M argv built", argv != NULL);
        if (argv) {
            /* Regular field-code path (de_exec_argv_files): %f is
             * substituted IN PLACE - the path lands exactly where
             * the Exec line puts it, not appended at the end. */
            check_str("M argv0", argv[0], "bd-notes");
            check_str("M argv1 flag", argv[1], "--open");
            check_str("M argv2 path at %f", argv[2], target);
            check_true("M argv ends", argv[3] == NULL);
            bd_mime_argv_free(argv);
        }
        /* %F expands to one argument per file (multi-open) */
        check_int("M set md handler",
                  bd_mime_set_handler("md", "bd-multi.desktop"), 0);
        char t1[1024], t2[1024];
        path_of(t1, sizeof(t1), "docs/a.md");
        path_of(t2, sizeof(t2), "docs/b.md");
        char *two[2] = { t1, t2 };
        argv = bd_mime_argv_files(cat, two, 2);
        check_true("M %F argv built", argv != NULL);
        if (argv) {
            check_str("M %F argv0", argv[0], "bd-multi");
            check_str("M %F argv1", argv[1], t1);
            check_str("M %F argv2", argv[2], t2);
            check_true("M %F argv ends", argv[3] == NULL);
            bd_mime_argv_free(argv);
        }
        /* single-file call against a %F handler gets the one file */
        argv = bd_mime_argv(cat, t1);
        check_true("M %F single built", argv != NULL);
        if (argv) {
            check_str("M %F single argv1", argv[1], t1);
            check_true("M %F single ends", argv[2] == NULL);
            bd_mime_argv_free(argv);
        }
        /* An Exec with NO file code receives NO path (freedesktop
         * semantics after the migration; the wave-1 workaround
         * appended it). The handler still launches, empty. */
        check_int("M set md plain",
                  bd_mime_set_handler("md", "bd-plain.desktop"), 0);
        argv = bd_mime_argv(cat, t1);
        check_true("M nocode argv built", argv != NULL);
        if (argv) {
            check_str("M nocode argv0", argv[0], "bd-plain");
            check_true("M nocode no path", argv[1] == NULL);
            bd_mime_argv_free(argv);
        }
        /* unknown type: no handler -> NULL argv */
        check_true("M argv unknown", bd_mime_argv(cat, "/x/y.unknownext") == NULL);
        de_catalog_free(cat);
    }

    printf("test_files: %d PASS, %d FAIL\n", passes, fails);
    return fails ? 1 : 0;
}
