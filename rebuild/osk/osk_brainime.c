/* osk_brainime.c - see osk_brainime.h.  Thin, mechanical mapping
 * from the osk_ime_backend vtable onto bi_session; all conversion
 * intelligence lives in libbrainime. */
#include "osk_brainime.h"
#include "brainime.h"
#include <string.h>

static void *be_new(void *engine)
{
    return bi_session_new((bi_engine *)engine);
}
static void be_free(void *s) { bi_session_free((bi_session *)s); }
static int be_set(void *s, const char *kana)
{
    return bi_session_set_preedit((bi_session *)s, kana);
}
static const char *be_text(void *s)
{
    return bi_session_text((bi_session *)s);
}
static const char *be_preedit(void *s)
{
    return bi_session_preedit((bi_session *)s);
}
static int be_segcount(void *s)
{
    const bi_result *r = bi_session_result((bi_session *)s);
    return r ? bi_result_segment_count(r) : 0;
}
static const char *be_segreading(void *s, int seg)
{
    const bi_result *r = bi_session_result((bi_session *)s);
    return r ? bi_result_segment_reading(r, seg) : NULL;
}
static const char *be_segtext(void *s, int seg)
{
    const bi_result *r = bi_session_result((bi_session *)s);
    return r ? bi_result_segment_surface(r, seg) : NULL;
}
static int be_candcount(void *s, int seg)
{
    const bi_result *r = bi_session_result((bi_session *)s);
    return r ? bi_result_candidate_count(r, seg) : 0;
}
static const char *be_cand(void *s, int seg, int j)
{
    const bi_result *r = bi_session_result((bi_session *)s);
    return r ? bi_result_candidate(r, seg, j) : NULL;
}
static int be_choose(void *s, int seg, int cand)
{
    return bi_session_choose((bi_session *)s, seg, cand);
}
static int be_resize(void *s, int seg, int new_len)
{
    return bi_session_resize((bi_session *)s, seg, new_len);
}
static int be_commit(void *s, char *out, size_t outsz)
{
    return bi_session_commit_first((bi_session *)s, out, outsz);
}

static const osk_ime_backend BRAINIME_BACKEND = {
    .session_new = be_new, .session_free = be_free,
    .session_set_reading = be_set, .session_text = be_text,
    .session_preedit = be_preedit, .segment_count = be_segcount,
    .segment_reading = be_segreading, .segment_text = be_segtext,
    .candidate_count = be_candcount,
    .candidate = be_cand, .choose = be_choose, .resize = be_resize,
    .commit_first = be_commit,
};

const osk_ime_backend *osk_brainime_backend(void)
{
    return &BRAINIME_BACKEND;
}
