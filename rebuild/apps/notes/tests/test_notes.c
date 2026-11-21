/* test_notes - bd-notes model tests + IME-integrated editor test.
 *
 * Groups:
 *   B basics: create/list/title derivation/sort/pin
 *   F folders: create/rename/delete/default protection
 *   D delete/restore + capacity-budgeted Recently Deleted
 *   S search (title, body snippet, attachment names)
 *   A attachments
 *   K checklist helpers
 *   R recovery: missing meta, orphan meta, torn counters,
 *     leftover tmp files, truncated body
 *   I IME integration: OSK + libbrainime drive the notes editor
 *     (composition lands in the editor buffer, commit persists
 *     through the atomic autosave path)
 *
 * Usage: test_notes <dict.bidf> [sidecar.bisc]
 * The I group is skipped (with a note) when the dictionary is
 * unavailable.
 */
#define _POSIX_C_SOURCE 200809L
#include "nmodel.h"
#include "bdstore.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static const nm_note *find_note(const nm_note *v, size_t n, uint64_t id)
{
    for (size_t i = 0; i < n; i++)
        if (v[i].id == id) return &v[i];
    return NULL;
}

int main(int argc, char **argv)
{
    char tmpl[] = "/tmp/bdnotes-test-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    snprintf(ROOT, sizeof(ROOT), "%s", tmpl);

    nm_store *s = nm_open(ROOT, 1 << 20);
    check_true("B open", s != NULL);

    /* ---------------- B: basics ---------------- */
    uint64_t n1 = nm_create(s, NULL, "\n\n買い物リスト\n牛乳\n卵\n");
    uint64_t n2 = nm_create(s, NULL, "会議メモ\n来週の予定を決める\n");
    check_true("B ids", n1 != 0 && n2 != 0 && n1 != n2);
    {
        nm_note *v = NULL;
        size_t n = 0;
        check_int("B list rc", nm_list(s, NULL, false, NM_SORT_MODIFIED,
                                       &v, &n), 0);
        check_int("B list count", (long)n, 2);
        const nm_note *a = find_note(v, n, n1);
        check_true("B found n1", a != NULL);
        if (a) {
            check_str("B title skips blank lines", a->title, "買い物リスト");
            check_true("B preview", strstr(a->preview, "牛乳") != NULL);
        }
        /* modified order: n2 (created later) first */
        check_int("B modified order", (long)v[0].id, (long)n2);
        nm_list_free(v, n);
    }
    /* autosave path: save_body bumps modified order */
    check_int("B save rc", nm_save_body(s, n1, "買い物リスト\n牛乳\n卵\nパン\n"), 0);
    {
        nm_note *v = NULL;
        size_t n = 0;
        nm_list(s, NULL, false, NM_SORT_MODIFIED, &v, &n);
        check_int("B modified after save", (long)v[0].id, (long)n1);
        nm_list_free(v, n);
        char *body = nm_load_body(s, n1);
        check_str("B body saved", body, "買い物リスト\n牛乳\n卵\nパン\n");
        free(body);
    }
    /* title sort + pin */
    check_int("B pin rc", nm_set_pinned(s, n2, true), 0);
    {
        nm_note *v = NULL;
        size_t n = 0;
        nm_list(s, NULL, false, NM_SORT_TITLE, &v, &n);
        check_int("B pinned first under title sort", (long)v[0].id, (long)n2);
        check_true("B pinned flag", v[0].pinned);
        nm_list_free(v, n);
        nm_list(s, NULL, false, NM_SORT_TITLE, &v, &n);
        /* 会議メモ (kaigi) vs 買い物リスト (kaimono): byte order of
         * UTF-8 puts 会 (E4BC9A) before 買 (E8B2B7) */
        check_int("B title order unpinned", (long)v[1].id, (long)n1);
        nm_list_free(v, n);
    }

    /* ---------------- F: folders ---------------- */
    check_int("F create folder", nm_folder_create(s, "仕事"), 0);
    uint64_t n3 = nm_create(s, "仕事", "仕様書ドラフト\n本文\n");
    check_true("F create in folder", n3 != 0);
    {
        char **folders = NULL;
        size_t fn = nm_folders(s, &folders);
        check_int("F folder count", (long)fn, 2);
        check_str("F default first", folders[0], NM_DEFAULT_FOLDER);
        nm_strv_free(folders, fn);
        nm_note *v = NULL;
        size_t n = 0;
        nm_list(s, "仕事", false, NM_SORT_MODIFIED, &v, &n);
        check_int("F folder list", (long)n, 1);
        nm_list_free(v, n);
    }
    check_int("F move folder", nm_move_folder(s, n3, NM_DEFAULT_FOLDER), 0);
    check_int("F move back", nm_move_folder(s, n3, "仕事"), 0);
    check_int("F rename folder", nm_folder_rename(s, "仕事", "業務"), 0);
    {
        nm_note m;
        check_true("F get after rename", nm_get(s, n3, &m));
        check_str("F folder renamed", m.folder, "業務");
        free(m.folder); free(m.title); free(m.preview);
    }
    check_true("F delete default refused",
               nm_folder_delete(s, NM_DEFAULT_FOLDER) != 0);
    check_int("F delete folder", nm_folder_delete(s, "業務"), 0);
    {
        nm_note m;
        check_true("F get after folder delete", nm_get(s, n3, &m));
        check_str("F notes moved to default", m.folder, NM_DEFAULT_FOLDER);
        free(m.folder); free(m.title); free(m.preview);
    }
    check_true("F bad name", !nm_folder_name_valid("a/b") &&
                             !nm_folder_name_valid("") &&
                             nm_folder_name_valid("ok"));

    /* ---------------- D: delete / restore / budget ------------- */
    int evicted = 0;
    check_int("D delete", nm_delete(s, n2, &evicted), 0);
    {
        nm_note *v = NULL;
        size_t n = 0;
        nm_list(s, NULL, true, NM_SORT_MODIFIED, &v, &n);
        check_int("D deleted count", (long)n, 1);
        check_int("D deleted id", (long)v[0].id, (long)n2);
        nm_list_free(v, n);
        nm_list(s, NULL, false, NM_SORT_MODIFIED, &v, &n);
        check_true("D live excludes deleted", find_note(v, n, n2) == NULL);
        nm_list_free(v, n);
    }
    check_int("D restore", nm_restore(s, n2), 0);
    {
        nm_note m;
        check_true("D restored to orig folder",
                   nm_get(s, n2, &m) && !m.deleted);
        free(m.folder); free(m.title); free(m.preview);
    }
    /* budget: separate store, 100-byte trash budget */
    {
        char sub[600];
        path_of(sub, sizeof(sub), "store2");
        nm_store *s2 = nm_open(sub, 100);
        char big[80];
        memset(big, 'z', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        uint64_t b1 = nm_create(s2, NULL, big);
        uint64_t b2 = nm_create(s2, NULL, big);
        int ev = 0;
        check_int("D2 delete 1", nm_delete(s2, b1, &ev), 0);
        check_int("D2 delete 2", nm_delete(s2, b2, &ev), 0);
        check_true("D2 eviction", ev >= 1);
        check_true("D2 within budget", nm_deleted_bytes(s2) <= 100);
        nm_note *v = NULL;
        size_t n = 0;
        nm_list(s2, NULL, true, NM_SORT_MODIFIED, &v, &n);
        check_int("D2 one remains", (long)n, 1);
        check_int("D2 survivor", (long)v[0].id, (long)b2);
        nm_list_free(v, n);
        check_int("D2 empty", nm_empty_deleted(s2), 0);
        check_int("D2 bytes 0", (long)nm_deleted_bytes(s2), 0);
        nm_close(s2);
    }

    /* ---------------- A: attachments --------------------------- */
    {
        char src[600];
        path_of(src, sizeof(src), "attach-src.txt");
        FILE *f = fopen(src, "wb");
        fputs("attachment body\n", f);
        fclose(f);
        char name[256];
        check_int("A add", nm_attach_add(s, n1, src, name, sizeof(name)), 0);
        check_str("A stored name", name, "attach-src.txt");
        check_int("A add again", nm_attach_add(s, n1, src, name,
                                               sizeof(name)), 0);
        check_str("A unique name", name, "attach-src 2.txt");
        char **names = NULL;
        size_t an = nm_attach_list(s, n1, &names);
        check_int("A list", (long)an, 2);
        nm_strv_free(names, an);
        char ap[NM_PATH_MAX];
        check_int("A path", nm_attach_path(s, n1, "attach-src.txt", ap,
                                           sizeof(ap)), 0);
        char *content = bd_read_all(ap, NULL);
        check_str("A content", content, "attachment body\n");
        free(content);
        check_int("A remove", nm_attach_remove(s, n1, "attach-src 2.txt"), 0);
        check_true("A traversal rejected",
                   nm_attach_path(s, n1, "../x", ap, sizeof(ap)) != 0);
    }

    /* ---------------- S: search -------------------------------- */
    {
        nm_hit *hits = NULL;
        size_t hn = 0;
        check_int("S title rc", nm_search(s, "会議", &hits, &hn), 0);
        check_int("S title hits", (long)hn, 1);
        if (hn) {
            check_int("S title id", (long)hits[0].id, (long)n2);
            check_true("S in_title", hits[0].in_title);
        }
        nm_search_free(hits, hn);
        hits = NULL; hn = 0;
        check_int("S body rc", nm_search(s, "パン", &hits, &hn), 0);
        check_int("S body hits", (long)hn, 1);
        if (hn) check_true("S snippet has hit",
                           strstr(hits[0].snippet, "パン") != NULL);
        nm_search_free(hits, hn);
        hits = NULL; hn = 0;
        check_int("S attach rc", nm_search(s, "attach-src", &hits, &hn), 0);
        check_true("S attach hit", hn >= 1);
        nm_search_free(hits, hn);
        hits = NULL; hn = 0;
        check_int("S none rc", nm_search(s, "存在しない語", &hits, &hn), 0);
        check_int("S none", (long)hn, 0);
        nm_search_free(hits, hn);
    }

    /* ---------------- K: checklist ------------------------------ */
    {
        const char *body = "買い物\n[ ] 牛乳\n[x] 卵\n";
        bool now = false;
        char *toggled = nm_checklist_toggle(body, 1, &now);
        check_true("K toggle on", toggled && now &&
                                  strstr(toggled, "[x] 牛乳") != NULL);
        free(toggled);
        toggled = nm_checklist_toggle(body, 2, &now);
        check_true("K toggle off", toggled && !now &&
                                   strstr(toggled, "[ ] 卵") != NULL);
        free(toggled);
        check_true("K no checkbox", nm_checklist_toggle(body, 0, &now) == NULL);
        size_t done = 0, total = 0;
        nm_checklist_count(body, &done, &total);
        check_int("K count total", (long)total, 2);
        check_int("K count done", (long)done, 1);
    }

    nm_close(s);

    /* ---------------- R: recovery ------------------------------- */
    {
        /* (a) body without meta -> synthesized at open */
        char bp[700], mp[700];
        path_of(bp, sizeof(bp), "notes/Notes/900.txt");
        FILE *f = fopen(bp, "wb");
        fputs("復旧ノート\n本文です\n", f);
        fclose(f);
        /* (b) meta without body -> dropped */
        path_of(mp, sizeof(mp), "notes/Notes/901.meta");
        f = fopen(mp, "wb");
        fputs("folder=Notes\npinned=0\ncseq=1\nmseq=1\ndseq=0\n", f);
        fclose(f);
        /* (c) torn counters */
        char cp[700];
        path_of(cp, sizeof(cp), "nextid");
        f = fopen(cp, "wb");
        fputs("garbage!!", f);
        fclose(f);
        path_of(cp, sizeof(cp), "seq");
        f = fopen(cp, "wb");
        fputs("", f);
        fclose(f);
        /* (d) leftover tmp from an interrupted save */
        path_of(cp, sizeof(cp), "notes/Notes/1.txt.tmp.4242");
        f = fopen(cp, "wb");
        fputs("partial", f);
        fclose(f);

        nm_store *s3 = nm_open(ROOT, 1 << 20);
        check_true("R reopen", s3 != NULL);
        nm_note *v = NULL;
        size_t n = 0;
        nm_list(s3, NULL, false, NM_SORT_MODIFIED, &v, &n);
        const nm_note *rec = find_note(v, n, 900);
        check_true("R orphan body adopted", rec != NULL);
        if (rec) check_str("R orphan title", rec->title, "復旧ノート");
        check_true("R orphan meta dropped", find_note(v, n, 901) == NULL);
        nm_list_free(v, n);
        /* new ids must not collide with the adopted 900 */
        uint64_t nid = nm_create(s3, NULL, "新規\n");
        check_true("R id no collision", nid > 900);
        /* tmp leftover swept */
        struct stat st;
        check_true("R tmp swept", stat(cp, &st) != 0);
        /* (e) truncated body (cut mid-UTF-8) loads without crash */
        char tp[700];
        path_of(tp, sizeof(tp), "notes/Notes/950.txt");
        f = fopen(tp, "wb");
        /* "あ" is E3 81 82; write only the first two bytes */
        fputc(0xE3, f); fputc(0x81, f);
        fclose(f);
        nm_close(s3);
        nm_store *s4 = nm_open(ROOT, 1 << 20);
        char *body = nm_load_body(s4, 950);
        check_true("R truncated loads", body != NULL);
        free(body);
        nm_close(s4);
    }

    /* ---------------- I: IME integration ------------------------ */
    if (argc >= 2) {
        extern int notes_ime_test(const char *dict, const char *bisc,
                                  const char *root);
        char sub[600];
        path_of(sub, sizeof(sub), "ime-store");
        int rc = notes_ime_test(argv[1], argc >= 3 ? argv[2] : NULL, sub);
        check_int("I ime integration rc", rc, 0);
    } else {
        printf("SKIP I ime integration (no dictionary argument)\n");
    }

    printf("test_notes: %d PASS, %d FAIL\n", passes, fails);
    return fails ? 1 : 0;
}
