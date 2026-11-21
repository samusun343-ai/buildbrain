/* test_textcore.c - unit tests for the textcore editing core. */
#include "textcore.h"
#include <stdio.h>
#include <string.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void expect_text(tc_buffer *b, const char *want)
{
    char buf[4096];
    tc_text(b, buf, sizeof(buf));
    if (strcmp(buf, want) != 0) {
        fails++;
        printf("FAIL text: got \"%s\" want \"%s\"\n", buf, want);
    } else passes++;
}

/* simple in-memory clipboard */
static char clip_store[4096];
static void clip_set(void *ud, const char *text, size_t len)
{
    (void)ud;
    if (len >= sizeof(clip_store)) len = sizeof(clip_store) - 1;
    memcpy(clip_store, text, len);
    clip_store[len] = '\0';
}
static size_t clip_get(void *ud, char *out, size_t cap)
{
    (void)ud;
    size_t len = strlen(clip_store);
    if (out && cap) {
        size_t w = len < cap - 1 ? len : cap - 1;
        memcpy(out, clip_store, w);
        out[w] = '\0';
    }
    return len;
}

static int default_width(void *ud, uint32_t cp)
{
    (void)ud;
    if (cp < 0x80) return 1;
    if (tc_cp_is_combining(cp)) return 0;
    return 2;
}

struct wrap_acc { size_t starts[16]; size_t lens[16]; int n; };
static void wrap_cb(void *ud, size_t start, size_t len)
{
    struct wrap_acc *a = ud;
    if (a->n < 16) { a->starts[a->n] = start; a->lens[a->n] = len; a->n++; }
}

int main(void)
{
    /* --- basic insert / cursor --- */
    tc_buffer *b = tc_new();
    CHECK(b != NULL);
    tc_insert(b, "hello");
    expect_text(b, "hello");
    CHECK(tc_length(b) == 5);
    CHECK(tc_count(b) == 5);
    CHECK(tc_cursor(b) == 5);
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 4);
    tc_insert(b, "X");
    expect_text(b, "hellXo");
    tc_free(b);

    /* --- coalesced typing undo: one undo removes the whole run --- */
    b = tc_new();
    tc_insert(b, "a"); tc_insert(b, "b"); tc_insert(b, "c");
    expect_text(b, "abc");
    CHECK(tc_can_undo(b));
    CHECK(tc_undo(b));
    expect_text(b, "");
    CHECK(!tc_can_undo(b));
    CHECK(tc_redo(b));
    expect_text(b, "abc");
    tc_free(b);

    /* --- selection replace + undo restores selection content --- */
    b = tc_new();
    tc_set_text(b, "hello world");
    tc_move_to(b, 0, false);
    tc_move(b, TC_MOVE_CLUSTER, 1, true);
    tc_move(b, TC_MOVE_CLUSTER, 1, true);
    tc_move(b, TC_MOVE_CLUSTER, 1, true);
    tc_move(b, TC_MOVE_CLUSTER, 1, true);
    tc_move(b, TC_MOVE_CLUSTER, 1, true);
    CHECK(tc_has_selection(b));
    tc_insert(b, "HELLO");
    expect_text(b, "HELLO world");
    CHECK(tc_undo(b));           /* undo insert */
    CHECK(tc_undo(b));           /* undo the selection delete */
    expect_text(b, "hello world");
    tc_free(b);

    /* --- backspace coalescing --- */
    b = tc_new();
    tc_set_text(b, "abcdef");
    tc_move_to(b, 6, false);
    CHECK(tc_delete(b, -1));
    CHECK(tc_delete(b, -1));
    CHECK(tc_delete(b, -1));
    expect_text(b, "abc");
    CHECK(tc_undo(b));
    expect_text(b, "abcdef");
    tc_free(b);

    /* --- forward delete, delete_range --- */
    b = tc_new();
    tc_set_text(b, "abcdef");
    tc_move_to(b, 0, false);
    CHECK(tc_delete(b, 1));
    expect_text(b, "bcdef");
    tc_delete_range(b, 1, 3);
    expect_text(b, "bef");
    CHECK(tc_undo(b));
    expect_text(b, "bcdef");
    tc_free(b);

    /* --- Japanese text: codepoints, cluster movement, delete --- */
    b = tc_new();
    tc_set_text(b, "あいうえお");   /* 5 codepoints, 15 bytes */
    CHECK(tc_length(b) == 15);
    CHECK(tc_count(b) == 5);
    tc_move_to(b, 15, false);
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 12);
    CHECK(tc_delete(b, -1));
    expect_text(b, "あいうお"); /* backspace at start of お deletes え */
    tc_move_to(b, 0, false);
    tc_move(b, TC_MOVE_WORD, 1, false);
    CHECK(tc_cursor(b) == 12);  /* whole kana run is one word */
    tc_free(b);

    /* --- combining characters move/delete as one cluster --- */
    b = tc_new();
    /* "e" + U+0301 (combining acute) + "x" */
    tc_set_text(b, "e\xcc\x81x");
    CHECK(tc_count(b) == 3);
    tc_move_to(b, 4, false); /* end */
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 3); /* start of x */
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 0); /* e+combining is one cluster */
    tc_move_to(b, 4, false);
    CHECK(tc_delete(b, -1));  /* delete x */
    CHECK(tc_delete(b, -1));  /* delete e+combining together */
    expect_text(b, "");
    tc_free(b);

    /* --- emoji: ZWJ sequence is one cluster; flag is one cluster --- */
    b = tc_new();
    /* family-ish: U+1F468 ZWJ U+1F469 ZWJ U+1F467 */
    tc_set_text(b, "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7");
    size_t total = tc_length(b);
    tc_move_to(b, total, false);
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 0); /* whole ZWJ chain is one cluster */
    CHECK(tc_delete(b, 1));
    expect_text(b, "");
    tc_free(b);

    b = tc_new();
    /* flag JP: two regional indicators */
    tc_set_text(b, "\xf0\x9f\x87\xaf\xf0\x9f\x87\xb5" "a");
    total = tc_length(b);
    tc_move_to(b, total, false);
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 8);  /* after the flag */
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 0);  /* flag deleted/moved as one */
    tc_free(b);

    /* --- skin tone modifier attaches --- */
    b = tc_new();
    /* thumbs up + medium skin tone */
    tc_set_text(b, "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd");
    total = tc_length(b);
    tc_move_to(b, total, false);
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_cursor(b) == 0);
    tc_free(b);

    /* --- word movement (latin) --- */
    b = tc_new();
    tc_set_text(b, "foo bar  baz");
    tc_move_to(b, 0, false);
    tc_move(b, TC_MOVE_WORD, 1, false);
    CHECK(tc_cursor(b) == 3);
    tc_move(b, TC_MOVE_WORD, 1, false);
    CHECK(tc_cursor(b) == 7);
    tc_move(b, TC_MOVE_WORD, -1, false);
    CHECK(tc_cursor(b) == 4);
    tc_free(b);

    /* --- line operations --- */
    b = tc_new();
    tc_set_text(b, "one\ntwo\nthree");
    CHECK(tc_line_count(b) == 3);
    size_t ls, ll;
    CHECK(tc_line_range(b, 1, &ls, &ll));
    CHECK(ls == 4 && ll == 3);
    size_t line, col;
    tc_offset_to_linecol(b, 9, &line, &col);
    CHECK(line == 2 && col == 1);
    CHECK(tc_linecol_to_offset(b, 2, 3) == 11);
    tc_move_to(b, 5, false);
    tc_move(b, TC_MOVE_LINE_START, -1, false);
    CHECK(tc_cursor(b) == 4);
    tc_move(b, TC_MOVE_LINE_END, 1, false);
    CHECK(tc_cursor(b) == 7);
    tc_free(b);

    /* --- wrapping via the layout bridge --- */
    b = tc_new();
    tc_set_text(b, "aa bb cc dd");
    struct wrap_acc acc = {0};
    tc_wrap_line(b, 0, 5, default_width, NULL, wrap_cb, &acc);
    CHECK(acc.n == 2);
    CHECK(acc.lens[0] == 6);  /* "aa bb " wraps at the second space */
    CHECK(acc.lens[1] == 5);  /* "cc dd" */
    tc_free(b);

    /* --- clipboard: copy/cut/paste through callbacks --- */
    b = tc_new();
    tc_clipboard cb = { NULL, clip_set, clip_get };
    tc_set_clipboard(b, &cb);
    clip_store[0] = '\0';
    tc_set_text(b, "copy me please");
    tc_select_all(b);
    CHECK(tc_copy(b));
    CHECK(strcmp(clip_store, "copy me please") == 0);
    CHECK(tc_cut(b));
    expect_text(b, "");
    CHECK(tc_paste(b));
    expect_text(b, "copy me please");
    /* paste into the middle */
    tc_move_to(b, 5, false);
    clip_set(NULL, "XX", 2);
    CHECK(tc_paste(b));
    expect_text(b, "copy XXme please");
    tc_free(b);

    /* copy with no selection / no provider returns false */
    b = tc_new();
    tc_set_text(b, "abc");
    CHECK(!tc_copy(b));
    tc_free(b);

    /* --- composition (IME preedit) is separate from content --- */
    b = tc_new();
    tc_set_text(b, "abc");
    tc_set_composition(b, "かん");
    char comp[64];
    CHECK(tc_composition(b, comp, sizeof(comp)) == 6);
    CHECK(strcmp(comp, "かん") == 0);
    expect_text(b, "abc");  /* content unchanged */
    tc_set_composition(b, NULL);
    CHECK(tc_composition(b, comp, sizeof(comp)) == 0);
    tc_free(b);

    /* --- select_all + collapse --- */
    b = tc_new();
    tc_set_text(b, "xyz");
    tc_select_all(b);
    CHECK(tc_has_selection(b));
    tc_collapse_selection(b, true);
    CHECK(tc_cursor(b) == 3 && !tc_has_selection(b));
    tc_free(b);

    /* --- redo cleared by a new edit --- */
    b = tc_new();
    tc_insert(b, "one");
    tc_break_undo_group(b);
    CHECK(tc_undo(b));
    CHECK(tc_can_redo(b));
    tc_insert(b, "two");
    CHECK(!tc_can_redo(b));
    expect_text(b, "two");
    tc_free(b);

    /* --- large-ish editing sanity (gap moves) --- */
    b = tc_new();
    for (int i = 0; i < 200; i++) tc_append(b, "abcdefghij");
    CHECK(tc_length(b) == 2000);
    tc_move_to(b, 1000, false);
    tc_insert(b, "ZZZ");
    CHECK(tc_length(b) == 2003);
    char probe[8];
    tc_range_text(b, 998, 1005, probe, sizeof(probe));
    CHECK(strcmp(probe, "ijZZZab") == 0);
    tc_free(b);

    /* --- composition segments (M2) --- */
    b = tc_new();
    tc_set_text(b, "x");
    size_t slens[4];
    CHECK(tc_composition_segments(b, slens, 4) == 0);
    tc_set_composition(b, "あい");
    CHECK(tc_composition_segments(b, slens, 4) == 1);
    CHECK(slens[0] == 6);
    size_t sgood[2] = { 3, 3 };
    tc_set_composition_segments(b, "あい", sgood, 2, 1, true);
    CHECK(tc_composition_segments(b, slens, 4) == 2);
    CHECK(slens[0] == 3 && slens[1] == 3);
    CHECK(tc_composition_active(b) == 1);
    CHECK(tc_composition_highlight(b));
    /* lengths that do not tile the string: segmentation dropped */
    size_t sbad[2] = { 3, 2 };
    tc_set_composition_segments(b, "あい", sbad, 2, 0, false);
    CHECK(tc_composition_segments(b, slens, 4) == 1);
    CHECK(slens[0] == 6);
    CHECK(!tc_composition_highlight(b));
    /* a zero-length segment is invalid too */
    size_t szero[2] = { 0, 6 };
    tc_set_composition_segments(b, "あい", szero, 2, 0, false);
    CHECK(tc_composition_segments(b, slens, 4) == 1);
    /* out-of-range active segment clamps to 0 */
    tc_set_composition_segments(b, "あい", sgood, 2, 9, false);
    CHECK(tc_composition_active(b) == 0);
    /* clearing the composition drops the segmentation */
    tc_set_composition(b, NULL);
    CHECK(tc_composition_segments(b, slens, 4) == 0);
    tc_free(b);

    /* --- invalid UTF-8 does not crash and stays editable --- */
    b = tc_new();
    tc_set_text(b, "a\xff\xfez");
    tc_move_to(b, tc_length(b), false);
    tc_move(b, TC_MOVE_CLUSTER, -1, false);
    CHECK(tc_delete(b, 1) || tc_cursor(b) > 0);
    tc_free(b);

    /* --- replace / replace-all (audit M6) --- */
    {
        char out[256];
        b = tc_new();
        tc_set_text(b, "one two one two one");
        /* replace-all: every occurrence, count returned */
        CHECK(tc_replace_all(b, "one", "1", false) == 3);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "1 two 1 two 1") == 0);
        /* no match: 0 replacements, text untouched */
        CHECK(tc_replace_all(b, "zzz", "q", false) == 0);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "1 two 1 two 1") == 0);
        /* replacement containing the needle must terminate */
        tc_set_text(b, "aa");
        CHECK(tc_replace_all(b, "a", "aa", false) == 2);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "aaaa") == 0);
        /* deletion via empty replacement */
        tc_set_text(b, "a-b-c");
        CHECK(tc_replace_all(b, "-", "", false) == 2);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "abc") == 0);
        /* empty needle is rejected */
        CHECK(tc_replace_all(b, "", "x", false) == 0);
        tc_free(b);

        b = tc_new();
        tc_set_text(b, "cat dog cat");
        tc_move_to(b, 0, false);
        /* single replace: first match from the cursor */
        CHECK(tc_replace(b, "cat", "fox", false) == 0);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "fox dog cat") == 0);
        /* the replacement is selected afterwards */
        CHECK(tc_has_selection(b));
        size_t lo, hi;
        tc_selection_range(b, &lo, &hi);
        CHECK(lo == 0 && hi == 3);
        /* next call replaces the NEXT match (selection differs) */
        CHECK(tc_replace(b, "cat", "fox", false) == 8);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "fox dog fox") == 0);
        /* nothing left: -1 */
        CHECK(tc_replace(b, "cat", "fox", false) == -1);
        /* case-insensitive replace */
        tc_set_text(b, "Cat CAT cat");
        CHECK(tc_replace_all(b, "cat", "dog", true) == 3);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "dog dog dog") == 0);
        /* undo still works after replace-all */
        CHECK(tc_can_undo(b));
        /* Japanese needle (multibyte, byte-exact) */
        tc_set_text(b, "ねこ いぬ ねこ");
        CHECK(tc_replace_all(b, "ねこ", "とら", false) == 2);
        tc_text(b, out, sizeof out);
        CHECK(strcmp(out, "とら いぬ とら") == 0);
        tc_free(b);
    }

    printf("textcore: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
