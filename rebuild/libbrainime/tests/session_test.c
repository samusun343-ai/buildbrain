/* session_test — exercise the OSK-facing session API:
 * convert, choose a candidate, resize a segment, commit the
 * first segment and continue.  Prints PASS/FAIL lines; exit code
 * is the number of failures. */
#define _POSIX_C_SOURCE 200809L
#include "brainime.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void check(const char *what, const char *got, const char *want)
{
	if (got && want && !strcmp(got, want)) {
		printf("PASS %s -> %s\n", what, got);
	} else {
		printf("FAIL %s: got %s, want %s\n", what,
		       got ? got : "(null)", want ? want : "(null)");
		fails++;
	}
}
static void check_true(const char *what, int cond)
{
	printf("%s %s\n", cond ? "PASS" : "FAIL", what);
	if (!cond) fails++;
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s dict.bidf\n", argv[0]); return 2; }
	bi_engine *e = bi_engine_open(argv[1]);
	if (!e) { fprintf(stderr, "open failed\n"); return 2; }

	bi_session *s = bi_session_new(e);
	check_true("session_new", s != NULL);

	/* 1. plain conversion through the session */
	bi_session_set_preedit(s, "てがみをかきました");
	check("convert", bi_session_text(s), "手紙を書きました");

	/* 2. choose a different candidate for segment 0 (手紙 -> てがみ
	 * is candidate 1: the as-typed hiragana form follows the
	 * dictionary surfaces) */
	const bi_result *r = bi_session_result(s);
	check_true("segment count >= 3",
		   bi_result_segment_count(r) >= 3);
	int nc = bi_result_candidate_count(r, 0);
	int hiragana_idx = -1;
	for (int j = 0; j < nc; j++)
		if (!strcmp(bi_result_candidate(r, 0, j), "てがみ"))
			hiragana_idx = j;
	check_true("candidate てがみ exists for segment 0", hiragana_idx >= 0);
	if (hiragana_idx >= 0) {
		bi_session_choose(s, 0, hiragana_idx);
		check("choose candidate", bi_session_text(s),
		      "てがみを書きました");
	}

	/* 3. resize: fresh session, force the first segment of
	 * わたしは… to length 2 (わた) and confirm segmentation
	 * changes accordingly */
	bi_session_set_preedit(s, "わたしはがっこうへいきます");
	check("convert 2", bi_session_text(s), "私は学校へ行きます");
	check_true("resize rc", bi_session_resize(s, 0, 2) == 0);
	r = bi_session_result(s);
	check_true("resized segment 0 has length 2",
		   bi_result_segment_length(r, 0) == 2);

	/* 4. commit-first: commit 私は-like first segment, the rest
	 * of the preedit remains and reconverts */
	bi_session_set_preedit(s, "てがみをかきました");
	char out[256];
	int n = bi_session_commit_first(s, out, sizeof(out));
	check_true("commit_first wrote", n > 0);
	check("committed segment", out, "手紙");
	check("preedit after commit", bi_session_preedit(s),
	      "をかきました");
	/* M2: the remainder is now converted with the committed
	 * segment (手紙) as left context, so it no longer pays the
	 * sentence-initial particle penalty that degraded it to
	 * ヲか気ました in M1.  The candidate for segment 0 should
	 * already be を at index 0; the choose+commit flow below
	 * must keep working either way. */
	/* user picks the correct candidate for the degraded
	 * remainder's first segment, then commits it */
	r = bi_session_result(s);
	int wo_idx = -1;
	for (int j = 0; j < bi_result_candidate_count(r, 0); j++)
		if (!strcmp(bi_result_candidate(r, 0, j), "を"))
			wo_idx = j;
	check_true("candidate を exists in remainder segment 0",
		   wo_idx >= 0);
	if (wo_idx >= 0) bi_session_choose(s, 0, wo_idx);
	n = bi_session_commit_first(s, out, sizeof(out));
	check("committed segment 2", out, "を");
	check("rest reconverts", bi_session_text(s), "書きました");

	/* 6. left context (M2): commit 手紙 from a fresh preedit;
	 * the particle-initial remainder must convert cleanly
	 * WITHOUT any candidate intervention. */
	bi_session_set_preedit(s, "てがみをかきました");
	n = bi_session_commit_first(s, out, sizeof(out));
	check("ctx committed segment", out, "手紙");
	check("ctx remainder converts cleanly", bi_session_text(s),
	      "を書きました");

	/* 7. learning (M2): choosing 紙 for かみ and committing it
	 * repeatedly must eventually flip the top-1 of a fresh
	 * engine that only shares the history file.  (The target
	 * was 髪 until M3: the BIDF v3 reranker already promotes
	 * 髪を書きました on its own, so it no longer demonstrates
	 * a learning flip; it was 神 from M3 to M4, but the M5
	 * reranker promotes 神を書きました on its own.  紙 is a
	 * non-baseline choice under the M2, M3/M4 and M5
	 * configurations alike: baselines are 上 (M2) and 神
	 * (M5), both measured.) */
	const char *lpath = "/tmp/bi_learn_test.tsv";
	remove(lpath);
	bi_engine *e2 = bi_engine_open(argv[1]);
	check_true("learning engine open", e2 != NULL);
	check_true("learning enable",
		   e2 && bi_engine_enable_learning(e2, lpath) == 0);
	if (e2) {
		bi_result *base = bi_convert(e2, "かみをかきました");
		check_true("learning baseline segment 0 is not 紙",
			   base && strcmp(bi_result_segment_surface(
					   base, 0), "紙"));
		if (base) bi_result_free(base);
		for (int round = 0; round < 10; round++) {
			bi_session *ls = bi_session_new(e2);
			bi_session_set_preedit(ls, "かみをかきました");
			const bi_result *lr = bi_session_result(ls);
			int kami_idx = -1;
			for (int j = 0; j < bi_result_candidate_count(lr, 0); j++)
				if (!strcmp(bi_result_candidate(lr, 0, j),
					    "紙"))
					kami_idx = j;
			if (kami_idx >= 0)
				bi_session_choose(ls, 0, kami_idx);
			char lout[256];
			bi_session_commit_first(ls, lout, sizeof(lout));
			bi_session_free(ls);
		}
		bi_engine_close(e2);
		/* persistence: a fresh engine sees only the file */
		bi_engine *e3 = bi_engine_open(argv[1]);
		check_true("learning re-enable",
			   e3 && bi_engine_enable_learning(e3, lpath) == 0);
		if (e3) {
			bi_stats lst;
			bi_engine_stats(e3, &lst);
			check_true("learning entries persisted",
				   lst.learn_entries >= 1);
			bi_result *after = bi_convert(e3, "かみをかきました");
			check("learning flips top-1 segment",
			      after ? bi_result_segment_surface(after, 0)
				    : NULL, "紙");
			if (after) bi_result_free(after);
			bi_engine_close(e3);
		}
		remove(lpath);
	}

	/* 8. M4: supplement candidates reach the candidate strip.
	 * さわら has two NEologd supplement surfaces (佐和良/佐波良)
	 * that are NOT in the BIDF dictionary; the converter may
	 * choose one as a lattice node, and BOTH must be offered
	 * as candidates for the segment so the user can pick the
	 * other one.  (v3 dictionaries only.) */
	{
		bi_stats st8;
		bi_engine_stats(e, &st8);
		if (st8.format_v3) {
			bi_session_set_preedit(s, "さわらをたべました");
			const bi_result *r8 = bi_session_result(s);
			int saw = -1;
			for (int j = 0; j < bi_result_candidate_count(r8, 0); j++)
				if (!strcmp(bi_result_candidate(r8, 0, j),
					    "佐波良"))
					saw = j;
			check_true("supp candidate 佐波良 offered", saw >= 0);
			if (saw >= 0) {
				bi_session_choose(s, 0, saw);
				check("choose supp candidate",
				      bi_session_text(s), "佐波良を食べました");
			}
		}
	}

	/* 5. forced-boundary one-shot API */
	int bounds[] = { 3, 4, 8, 9 };
	bi_result *fr = bi_convert_forced(e, "わたしはがっこうへいきます",
					  bounds, 4);
	check_true("forced result", fr != NULL);
	if (fr) {
		check_true("forced segment 0 length 3",
			   bi_result_segment_length(fr, 0) == 3);
		bi_result_free(fr);
	}

	bi_session_free(s);
	bi_engine_close(e);
	printf("session_test: %s (%d failures)\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
