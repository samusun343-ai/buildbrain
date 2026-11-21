/* bi — libbrainime command-line tool.
 *
 *   bi <dict.bidf> convert <reading>     best conversion, segments
 *                                        with candidates, n-best
 *   bi <dict.bidf> lookup <reading>      raw dictionary candidates
 *                                        (exact match; used to
 *                                        cross-check bidict_lookup2)
 *   bi <dict.bidf> bench <file> [iters]  load time, mean conversion
 *                                        latency over the readings
 *                                        in <file> (one per line),
 *                                        peak RSS
 *   bi <dict.bidf> eval <pairs.tsv>      M3: batch top-1 / n-best
 *                                        accuracy + mean latency
 *                                        over reading<TAB>expected
 *                                        pairs, engine opened once
 *   bi <dict.bidf> pages <conv-file>     M3: resident pages of the
 *                                        file / connection region
 *                                        (mincore) after converting
 *                                        each reading once, plus
 *                                        surface-cache hit rate
 */
#define _POSIX_C_SOURCE 200809L
#include "brainime.h"
#include "../src/bi_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static long peak_rss_kb(void)
{
	FILE *f = fopen("/proc/self/status", "r");
	if (!f) return -1;
	char line[256];
	long v = -1;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "VmHWM: %ld kB", &v) == 1) break;
	fclose(f);
	return v;
}

static int do_convert(bi_engine *e, const char *reading)
{
	bi_result *r = bi_convert(e, reading);
	if (!r) { printf("(no result)\n"); return 1; }
	printf("best: %s\n", bi_result_nbest(r, 0));
	int ns = bi_result_segment_count(r);
	for (int i = 0; i < ns; i++) {
		printf("  [%d] %s -> %s (pos %d) cands:",
		       i, bi_result_segment_reading(r, i),
		       bi_result_segment_surface(r, i),
		       bi_result_segment_pos(r, i));
		int nc = bi_result_candidate_count(r, i);
		for (int j = 0; j < nc; j++)
			printf(" %s", bi_result_candidate(r, i, j));
		printf("\n");
	}
	int nb = bi_result_nbest_count(r);
	for (int i = 0; i < nb; i++)
		printf("  nbest%d: %s (cost %ld)\n", i,
		       bi_result_nbest(r, i), bi_result_nbest_cost(r, i));
	bi_result_free(r);
	return 0;
}

static int do_forced(bi_engine *e, const char *reading, const char *bstr)
{
	int bounds[128];
	int nb = 0;
	char *copy = strdup(bstr);
	for (char *tok = strtok(copy, ","); tok && nb < 128;
	     tok = strtok(NULL, ","))
		bounds[nb++] = atoi(tok);
	free(copy);
	bi_result *r = bi_convert_forced(e, reading, bounds, nb);
	if (!r) { printf("(no result)\n"); return 1; }
	printf("forced: %s (cost %ld)\n", bi_result_nbest(r, 0),
	       bi_result_nbest_cost(r, 0));
	int ns = bi_result_segment_count(r);
	for (int i = 0; i < ns; i++)
		printf("  [%d] %s -> %s (pos %d)\n", i,
		       bi_result_segment_reading(r, i),
		       bi_result_segment_surface(r, i),
		       bi_result_segment_pos(r, i));
	bi_result_free(r);
	return 0;
}

static int do_lookup(bi_engine *e, const char *reading)
{
	/* exact-match dictionary candidates via the internal API,
	 * printed in stored (cost) order, same as bidict_lookup2 */
	uint32_t cps[128];
	int n = bi_utf8_decode(reading, cps, 128);
	if (n <= 0) return 1;
	uint32_t codes[128];
	for (int i = 0; i < n; i++) {
		int code = bi_dict_encode_char(&e->d, cps[i]);
		if (code < 0) return 0;
		codes[i] = (uint32_t)code;
	}
	int tn = bi_dict_walk_to(&e->d, codes, n);
	if (tn < 0 || !bv_get(&e->d.rd.terminal, (uint32_t)tn)) return 0;
	uint32_t rank = bv_rank1(&e->d.rd.terminal, (uint32_t)tn - 1);
	uint32_t first, count;
	bi_dict_token_range(&e->d, rank, &first, &count);
	for (uint32_t i = 0; i < count; i++) {
		char buf[256];
		bi_dict_surface(&e->d, e->d.tok_sid[first + i], buf,
				sizeof(buf));
		printf("%s\t%d\t%d\n", buf, e->d.tok_pos[first + i],
		       e->d.tok_cost[first + i]);
	}
	return 0;
}

static int do_bench(bi_engine *e, const char *path, int iters)
{
	char lines[512][256];
	int nlines = 0;
	FILE *f = fopen(path, "r");
	if (!f) { perror("fopen"); return 1; }
	while (nlines < 512 && fgets(lines[nlines], 256, f)) {
		lines[nlines][strcspn(lines[nlines], "\n")] = '\0';
		if (lines[nlines][0]) nlines++;
	}
	fclose(f);
	/* warm-up + timed passes */
	double t0 = now_ms();
	long total_conv = 0;
	for (int it = 0; it < iters; it++) {
		for (int i = 0; i < nlines; i++) {
			bi_result *r = bi_convert(e, lines[i]);
			if (r) { total_conv++; bi_result_free(r); }
		}
	}
	double dt = now_ms() - t0;
	printf("bench: %d readings x %d iters = %ld conversions\n",
	       nlines, iters, total_conv);
	printf("  total %.1f ms, mean %.3f ms/conversion\n",
	       dt, total_conv ? dt / total_conv : 0);
	printf("  peak RSS (VmHWM): %ld kB\n", peak_rss_kb());
	return 0;
}

static int do_eval(bi_engine *e, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) { perror("fopen"); return 1; }
	char line[1024];
	int n = 0, top1 = 0, nbest = 0;
	double total_ms = 0;
	while (fgets(line, sizeof(line), f)) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = '\0';
		char *exp = tab + 1;
		exp[strcspn(exp, "\r\n")] = '\0';
		if (!line[0] || !exp[0]) continue;
		double t0 = now_ms();
		bi_result *r = bi_convert(e, line);
		total_ms += now_ms() - t0;
		if (!r) { n++; continue; }
		n++;
		if (bi_result_nbest_count(r) > 0 &&
		    !strcmp(bi_result_nbest(r, 0), exp))
			top1++;
		for (int i = 0; i < bi_result_nbest_count(r); i++)
			if (!strcmp(bi_result_nbest(r, i), exp)) {
				nbest++;
				break;
			}
		bi_result_free(r);
	}
	fclose(f);
	printf("eval: %d pairs, top-1 %d (%.1f%%), n-best %d (%.1f%%), "
	       "mean %.3f ms/conversion\n",
	       n, top1, n ? 100.0 * top1 / n : 0,
	       nbest, n ? 100.0 * nbest / n : 0,
	       n ? total_ms / n : 0);
	return 0;
}

static void report_pages(bi_engine *e, const char *label)
{
	long ps = sysconf(_SC_PAGESIZE);
	const char *names[2] = { "file", "conn" };
	for (int i = 0; i < 2; i++) {
		const uint8_t *ptr;
		size_t len;
		if (bi_dict_region(&e->d, names[i], &ptr, &len) != 0)
			continue;
		size_t off = (size_t)((uintptr_t)ptr & (ps - 1));
		size_t total_pages = (len + off + ps - 1) / ps;
		unsigned char *vec = malloc(total_pages);
		if (!vec) return;
		if (mincore((void *)((uintptr_t)ptr & ~(uintptr_t)(ps - 1)),
			    len + off, vec) == 0) {
			size_t res = 0;
			for (size_t j = 0; j < total_pages; j++)
				if (vec[j] & 1) res++;
			printf("  %s %s: %zu/%zu pages resident "
			       "(%zu/%zu bytes)\n",
			       label, names[i], res, total_pages,
			       res * (size_t)ps, total_pages * (size_t)ps);
		}
		free(vec);
	}
}

static int do_pages(bi_engine *e, const char *path)
{
	report_pages(e, "after-open");
	FILE *f = fopen(path, "r");
	if (!f) { perror("fopen"); return 1; }
	char line[512];
	int n = 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		char *tab = strchr(line, '\t');
		if (tab) *tab = '\0';
		if (!line[0]) continue;
		bi_result *r = bi_convert(e, line);
		if (r) bi_result_free(r);
		n++;
	}
	fclose(f);
	printf("pages: converted %d readings\n", n);
	report_pages(e, "after-conv");
	bi_stats st;
	bi_engine_stats(e, &st);
	unsigned long tot = st.surf_hits + st.surf_misses;
	printf("  surface cache: %lu hits, %lu misses (%.1f%% hit)\n",
	       st.surf_hits, st.surf_misses,
	       tot ? 100.0 * st.surf_hits / tot : 0.0);
	printf("  peak RSS (VmHWM): %ld kB\n", peak_rss_kb());
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr,
			"usage: %s dict.bidf convert|lookup <reading>\n"
			"       %s dict.bidf bench <readings-file> [iters]\n"
			"       %s dict.bidf eval|pages <pairs-or-readings-file>\n",
			argv[0], argv[0], argv[0]);
		return 2;
	}
	double t0 = now_ms();
	bi_engine *e = bi_engine_open(argv[1]);
	if (!e) { fprintf(stderr, "cannot open dictionary\n"); return 1; }
	const char *lf = getenv("BI_LEARN_FILE");
	if (lf && lf[0] && bi_engine_enable_learning(e, lf) != 0)
		fprintf(stderr, "learning file %s not writable\n", lf);
	double load_ms = now_ms() - t0;
	bi_stats st;
	bi_engine_stats(e, &st);
	fprintf(stderr,
		"dict: %u entries, %u readings, %u surfaces, %zu bytes, "
		"conn_from_dict=%d, lexicalized=%d, learn_entries=%d, "
		"load %.1f ms\n",
		st.n_entries, st.n_readings, st.n_surfaces, st.file_bytes,
		st.conn_from_dict, st.lexicalized, st.learn_entries,
		load_ms);
	if (st.format_v3)
		fprintf(stderr,
			"  v3: supp=%u entries, bigram=%u pairs, "
			"rerank features=%d\n",
			st.n_supp, st.n_bigram, BI_RERANK_NFEAT);

	int rc = 0;
	if (!strcmp(argv[2], "convert")) rc = do_convert(e, argv[3]);
	else if (!strcmp(argv[2], "forced") && argc > 4)
		rc = do_forced(e, argv[3], argv[4]);
	else if (!strcmp(argv[2], "lookup")) rc = do_lookup(e, argv[3]);
	else if (!strcmp(argv[2], "bench"))
		rc = do_bench(e, argv[3], argc > 4 ? atoi(argv[4]) : 20);
	else if (!strcmp(argv[2], "eval")) rc = do_eval(e, argv[3]);
	else if (!strcmp(argv[2], "pages")) rc = do_pages(e, argv[3]);
	else { fprintf(stderr, "unknown mode\n"); rc = 2; }
	bi_engine_close(e);
	return rc;
}
