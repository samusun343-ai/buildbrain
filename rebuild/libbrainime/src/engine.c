/* engine.c — lattice construction + Viterbi (k-best) conversion.
 *
 * Pipeline (the standard mozc/anthy/libkkc shape, see DESIGN.md):
 *   1. parse the input into codepoints, normalise katakana to
 *      hiragana for dictionary matching;
 *   2. build a lattice: at every position, common-prefix walk of
 *      the reading trie emits dictionary nodes; unknown-word nodes
 *      (katakana span, as-typed hiragana span, ASCII runs, digit
 *      runs with kanji-numeral variants) keep the lattice connected
 *      for out-of-dictionary input;
 *   3. Viterbi shortest path with k-best kept per node
 *      (k = BI_KBEST), cost = word cost + connection cost
 *      (conn.c) + per-segment penalty;
 *   4. the best path gives the segmentation; n-best full strings
 *      come from the k-best lists at the final position;
 *      per-segment candidates are re-read from the dictionary
 *      group for the segment's reading span.
 *
 * Cost units: word cost = dictionary cost byte * 16 (mozc's cost
 * is a log-domain quantity linearly quantised to 0..255 by
 * convert_mozc.py, so additive spacing is preserved).  Connection
 * costs and penalties are defined on the same scale in conn.c.
 *
 * Debug/calibration knobs (environment, read per conversion):
 *   BI_SEG_PEN   per-segment penalty, default 260
 *   BI_CONN_PCT  connection-cost percentage, default 100
 */
#define _POSIX_C_SOURCE 200809L
#include "bi_internal.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#define BI_KBEST 32	/* M4: 16 -> 24.  With supplement nodes added
			 * (M3) the per-node k-best filled with near-tie
			 * variants and pruned gold partial paths at busy
			 * boundaries (measured on dev500, REPORT-m4 S1).
			 * M5: 24 -> 32.  48 raised pool containment
			 * (@64: 83.5% -> 84.8% on dev_big) but did NOT
			 * raise top-1 (56.1% vs 56.0%), cost 1.68x the
			 * latency and +1.0 MB RSS; 32 keeps the gain
			 * that reaches top-1 (REPORT-m5 S2). */
#define BI_NBEST 16	/* M4: 8 -> 16.  The 8-slot list was monopolised
			 * by combinatorial variants of a single cheap
			 * segment choice, evicting the gold path that
			 * WAS in the finals (REPORT-m4 S1). */
#define BI_MAX_INPUT 128
#define TOKENS_PER_SPAN 16	/* lattice nodes kept per reading span */
#define CANDS_PER_SEG 32

/* ---------------------------------------------------------------- */
/* UTF-8 helpers                                                     */
/* ---------------------------------------------------------------- */

int bi_utf8_decode(const char *s, uint32_t *cps, int max)
{
	int n = 0;
	const uint8_t *p = (const uint8_t *)s;
	while (*p) {
		uint32_t cp;
		int ln;
		if (*p < 0x80) { cp = *p; ln = 1; }
		else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; ln = 2; }
		else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; ln = 3; }
		else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; ln = 4; }
		else return -1;
		for (int i = 1; i < ln; i++) {
			if ((p[i] & 0xC0) != 0x80) return -1;
			cp = (cp << 6) | (p[i] & 0x3F);
		}
		if (n >= max) return -1;
		cps[n++] = cp;
		p += ln;
	}
	return n;
}

int bi_utf8_encode(uint32_t cp, char *out)
{
	uint8_t *o = (uint8_t *)out;
	if (cp < 0x80) { o[0] = (uint8_t)cp; return 1; }
	if (cp < 0x800) {
		o[0] = 0xC0 | (cp >> 6); o[1] = 0x80 | (cp & 0x3F); return 2;
	}
	if (cp < 0x10000) {
		o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 0x3F);
		o[2] = 0x80 | (cp & 0x3F); return 3;
	}
	o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 0x3F);
	o[2] = 0x80 | ((cp >> 6) & 0x3F); o[3] = 0x80 | (cp & 0x3F);
	return 4;
}

char *bi_utf8_str(const uint32_t *cps, int n)
{
	char *buf = malloc(n * 4 + 1);
	if (!buf) return NULL;
	int off = 0;
	for (int i = 0; i < n; i++) off += bi_utf8_encode(cps[i], buf + off);
	buf[off] = '\0';
	return buf;
}

static uint32_t normalize_cp(uint32_t cp)
{
	if (cp >= 0x30A1 && cp <= 0x30F6) return cp - 0x60;	/* katakana -> hiragana */
	if (cp == 0x30FD) return 0x309D;
	if (cp == 0x30FE) return 0x309E;
	return cp;
}

static uint32_t katakana_cp(uint32_t cp)
{
	if (cp >= 0x3041 && cp <= 0x3096) return cp + 0x60;
	if (cp == 0x309D) return 0x30FD;
	if (cp == 0x309E) return 0x30FE;
	return cp;
}

static int is_kana_cp(uint32_t cp)
{
	return (cp >= 0x3041 && cp <= 0x3096) || cp == 0x309D ||
	       cp == 0x309E || cp == 0x30FC;
}

/* ---------------------------------------------------------------- */
/* lattice                                                           */
/* ---------------------------------------------------------------- */

typedef struct {
	int start, end;		/* char offsets */
	int sid;		/* surface id, or -1 for special nodes */
	int pos;		/* POS class */
	uint16_t lid, rid;	/* mozc ids from the sidecar (0 if none) */
	int word_cost;		/* engine units */
	char *surface;		/* owned: specials always; dict nodes
				   only when the span is short enough
				   for the form check (<=3 chars) */
	const uint8_t *zsurf;	/* M3 supplement nodes: surface bytes
				   inside the dictionary mapping (not
				   owned, not NUL-terminated) */
	int zsurf_len;
	/* k-best partial paths ending with this node */
	struct { int cost; int prev_node; int prev_k; int nseg; } best[BI_KBEST];
	int nbest;
} node;

typedef struct {
	node *v;
	int n, cap;
	int *start_off;		/* per input position: first node index */
	int *start_cnt;
	int *by_end;		/* node indices sorted by end position */
	int *end_off;		/* per position: range in by_end */
} lattice;

static int lat_add(lattice *l, node *nd)
{
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 256;
		l->v = realloc(l->v, l->cap * sizeof(node));
		if (!l->v) return -1;
	}
	l->v[l->n] = *nd;
	return l->n++;
}

static void lat_free(lattice *l)
{
	for (int i = 0; i < l->n; i++) free(l->v[i].surface);
	free(l->v); free(l->start_off); free(l->start_cnt);
	free(l->by_end); free(l->end_off);
	memset(l, 0, sizeof(*l));
}

/* bucket node indices by end position (counting sort):
 * after this, nodes ending at position p are
 * by_end[end_off[p] .. end_off[p+1]). */
static void lat_index_ends(lattice *l, int nchars)
{
	l->end_off = calloc(nchars + 2, sizeof(int));
	l->by_end = malloc(sizeof(int) * (l->n > 0 ? l->n : 1));
	if (!l->end_off || !l->by_end) return;
	for (int i = 0; i < l->n; i++) l->end_off[l->v[i].end + 1]++;
	for (int p = 0; p <= nchars; p++) l->end_off[p + 1] += l->end_off[p];
	/* end_off[p] = number of nodes with end < p = bucket start */
	int *cur = malloc(sizeof(int) * (nchars + 2));
	if (!cur) return;
	memcpy(cur, l->end_off, sizeof(int) * (nchars + 2));
	for (int i = 0; i < l->n; i++)
		l->by_end[cur[l->v[i].end]++] = i;
	free(cur);
}

struct conv_ctx {
	bi_engine *e;
	const uint32_t *cps;	/* normalised input */
	const uint32_t *orig;	/* as typed */
	int n;
	const int *bounds;	/* forced boundaries (char offsets) */
	int n_bounds;
	lattice lat;
	int seg_pen;
	int conn_pct;
	int conn_scale_pm;	/* mozc matrix -> engine scale, per-mille */
	const bi_leftctx *ctx;	/* session left context, or NULL */
	node ctx_node;		/* ctx materialised as a node */
	/* no-cut zone (session resize): no node may start or end
	 * strictly inside (forbid_from, forbid_to), which pins the
	 * resized segment to a single node.  -1 = inactive. */
	int forbid_from, forbid_to;
};

static int crosses_bound(struct conv_ctx *c, int start, int end)
{
	for (int i = 0; i < c->n_bounds; i++)
		if (c->bounds[i] > start && c->bounds[i] < end) return 1;
	return 0;
}

static void add_node_lr(struct conv_ctx *c, int start, int end, int sid,
			int pos, uint16_t lid, uint16_t rid, int word_cost,
			char *surface);

static void add_node(struct conv_ctx *c, int start, int end, int sid,
		     int pos, int word_cost, char *surface)
{
	uint16_t lid = 0, rid = 0;
	if (c->e->d.have_sidecar && pos >= 0 && pos < 256) {
		lid = c->e->d.sc_cls[pos * 2];
		rid = c->e->d.sc_cls[pos * 2 + 1];
	}
	add_node_lr(c, start, end, sid, pos, lid, rid, word_cost, surface);
}

static void add_node_lr(struct conv_ctx *c, int start, int end, int sid,
			int pos, uint16_t lid, uint16_t rid, int word_cost,
			char *surface)
{
	if (crosses_bound(c, start, end)) { free(surface); return; }
	if (c->forbid_from >= 0 &&
	    ((start > c->forbid_from && start < c->forbid_to) ||
	     (end > c->forbid_from && end < c->forbid_to))) {
		free(surface);
		return;
	}
	node nd;
	memset(&nd, 0, sizeof(nd));
	nd.start = start; nd.end = end; nd.sid = sid; nd.pos = pos;
	nd.lid = lid; nd.rid = rid;
	nd.word_cost = word_cost; nd.surface = surface;
	lat_add(&c->lat, &nd);
}

/* M3: supplement node whose surface stays in the mapping
 * (zero-copy: no allocation, no byte copied at lattice build). */
static void add_node_z(struct conv_ctx *c, int start, int end, int pos,
		       uint16_t lid, uint16_t rid, int word_cost,
		       const uint8_t *zsurf, int zsurf_len, char *surface)
{
	if (crosses_bound(c, start, end)) { free(surface); return; }
	if (c->forbid_from >= 0 &&
	    ((start > c->forbid_from && start < c->forbid_to) ||
	     (end > c->forbid_from && end < c->forbid_to))) {
		free(surface);
		return;
	}
	node nd;
	memset(&nd, 0, sizeof(nd));
	nd.start = start; nd.end = end; nd.sid = -2; nd.pos = pos;
	nd.lid = lid; nd.rid = rid;
	nd.word_cost = word_cost; nd.surface = surface;
	nd.zsurf = zsurf; nd.zsurf_len = zsurf_len;
	lat_add(&c->lat, &nd);
}

static char *span_str(const uint32_t *cps, int start, int len, int katakana);

/* Surface-form penalties, in dictionary cost units.  Two quirks of
 * the source data need them (measured on the shipped dictionary):
 *  - identity entries (surface == reading, e.g. わたし->わたし at
 *    cost 0) beat the kanji entry (私, cost 6) although a kana-kanji
 *    converter exists to produce the kanji form;
 *  - katakana spellings of function words are registered with
 *    equal or better costs than the standard form (デス as an
 *    auxiliary at cost 0 vs です at cost 1; ヲ vs を).
 * Content-word katakana (loanwords) is NOT penalised. */
static int surface_penalty(struct conv_ctx *c, int start, int len,
			   const char *surface, int pos)
{
	int pen = 0;
	/* identity penalty: content classes only.  Function words
	 * are SUPPOSED to surface as their reading (です, ます,
	 * ない…) — penalising them made デス beat です. */
	int content = (pos >= 1 && pos <= 16) || (pos >= 20 && pos <= 27) ||
		      (pos >= 30 && pos <= 32) || pos == 40 || pos == 41 ||
		      pos == 42;
	char *reading = span_str(c->cps, start, len, 0);
	if (reading) {
		if (content && len >= 2 && !strcmp(reading, surface))
			pen += 10;
		free(reading);
	}
	/* all-katakana surface on a function class? */
	uint32_t cps[64];
	int n = bi_utf8_decode(surface, cps, 64);
	if (n > 0) {
		int all_kata = 1;
		for (int i = 0; i < n; i++)
			if (!((cps[i] >= 0x30A1 && cps[i] <= 0x30F6) ||
			      cps[i] == 0x30FC))
				all_kata = 0;
		if (all_kata &&
		    ((pos >= 60 && pos <= 71) || (pos >= 80 && pos <= 87) ||
		     pos == 44 || pos == 45 || pos == 46))
			pen += 8;
	}
	return pen;
}

/* dictionary nodes via prefix walk */
struct walk_state { struct conv_ctx *c; int start; int kept; };
static int walk_cb(void *user, uint32_t tnode, int depth)
{
	struct walk_state *w = user;
	struct conv_ctx *c = w->c;
	uint32_t rank = bv_rank1(&c->e->d.rd.terminal, tnode - 1);
	uint32_t first, count;
	bi_dict_token_range(&c->e->d, rank, &first, &count);
	int keep = count < TOKENS_PER_SPAN ? (int)count : TOKENS_PER_SPAN;
	for (int i = 0; i < keep; i++) {
		uint32_t idx = first + i;
		char buf[256];
		char *surf = NULL;
		int pen = 0;
		int wc;
		if (bi_dict_surface(&c->e->d, c->e->d.tok_sid[idx],
				    buf, sizeof(buf)) >= 0) {
			surf = strdup(buf);
			pen = surface_penalty(c, w->start, depth, buf,
					      c->e->d.tok_pos[idx]);
		}
		wc = (c->e->d.tok_cost[idx] + pen) * 16;
		if (surf) {
			/* M2 learning: committed (reading, surface)
			 * pairs get a capped word-cost bonus */
			char *rd = span_str(c->cps, w->start, depth, 0);
			if (rd) {
				wc -= bi_learn_bonus(c->e, rd, buf);
				free(rd);
			}
			if (wc < 0) wc = 0;
		}
		if (c->e->d.have_sidecar)
			add_node_lr(c, w->start, w->start + depth,
				    (int)c->e->d.tok_sid[idx],
				    c->e->d.tok_pos[idx],
				    c->e->d.sc_lid[idx], c->e->d.sc_rid[idx],
				    wc, surf);
		else
			add_node(c, w->start, w->start + depth,
				 (int)c->e->d.tok_sid[idx],
				 c->e->d.tok_pos[idx], wc, surf);
	}
	return 1;
}

/* M2: godan onbin (音便) synthesis.
 *
 * mozc registers onbin stems as dictionary entries with their own
 * lids （書い = 五段・カ行イ音便・連用タ接続）, and the lexicalised
 * matrix knows those stems connect to て/で/た/だ almost for free —
 * but the 180k-entry selection dropped some stems (1,047 godan
 * renyou entries have no onbin stem in the BIDF), and a missing
 * stem breaks the whole て-form.
 *
 * Synthesis is needed only where the stem's last kana CHANGES
 * （促音便 っ, 撥音便 ん): イ音便 （書いて) and サ行 （話して) keep
 * the stem kana, so the dictionary stem composes directly.  The
 * changed-kana cases cannot be derived from lattice nodes — for
 * input かって the renyou node かい never matches the input — so
 * this pass works from the input itself: wherever the input has
 * っ/ん followed by て/た (resp. で/だ), it reconstructs the
 * candidate renyou readings (input prefix + each possible source
 * kana), looks them up exactly, and for every godan (class 20)
 * entry whose surface ends in that source kana it adds ONE
 * virtual node spanning stem+て/た (resp. で/だ):
 *   ち->っ+て 勝って  り->っ+て 帰って  い->っ+て 買って
 *   ひ->っ+て 言って  に->ん+で 死んで  び->ん+で 遊んで
 *   み->ん+で 読んで
 * The virtual node pays a synthesis penalty (default 400 engine
 * units, BI_ONBIN_PEN) so a real dictionary composition always
 * wins when it exists.  Its lid is the source verb's (incoming
 * connections see the renyou stem); its rid is the class-63
 * representative (outgoing connections see a completed て-form).
 * 連濁 proper (手紙 etc.) needs no synthesis: rendaku readings are
 * registered in the dictionary as readings of their own entries. */
static void add_onbin_virtuals(struct conv_ctx *c, int start,
			       const uint32_t *codes, int maxlen)
{
	static const struct { uint32_t ob, voice; uint32_t from[4]; int nf; }
	tab[] = {
		{0x3063, 0x3066, {0x3061, 0x308A, 0x3044, 0x3072}, 4}, /* っ+て */
		{0x3093, 0x3067, {0x306B, 0x3073, 0x307F, 0}, 3},      /* ん+で */
	};
	int pen = 400;
	const char *ep = getenv("BI_ONBIN_PEN");
	if (ep) pen = atoi(ep);
	for (size_t t = 0; t < sizeof(tab) / sizeof(tab[0]); t++) {
		/* L = renyou length; the virtual spans L+1 chars */
		for (int L = 2; L + 1 <= maxlen && start + L < c->n; L++) {
			if (c->cps[start + L - 1] != tab[t].ob) continue;
			uint32_t fol = c->cps[start + L];
			if (fol != tab[t].voice &&
			    fol != (tab[t].voice == 0x3066 ? 0x305F : 0x3060))
				continue;	/* not て/た (resp. で/だ) */
			for (int k = 0; k < tab[t].nf; k++) {
				uint32_t rc[BI_MAX_INPUT];
				int code = bi_dict_encode_char(&c->e->d,
							       tab[t].from[k]);
				if (code < 0) continue;
				memcpy(rc, codes, sizeof(uint32_t) * (L - 1));
				rc[L - 1] = (uint32_t)code;
				int tn = bi_dict_walk_to(&c->e->d, rc, L);
				if (tn < 0) continue;
				uint32_t rank = bv_rank1(&c->e->d.rd.terminal,
							 tn - 1);
				uint32_t first, count;
				bi_dict_token_range(&c->e->d, rank,
						    &first, &count);
				for (uint32_t i = 0; i < count; i++) {
					uint32_t idx = first + i;
					char buf[256];
					uint32_t scps[64];
					int sn;
					if (c->e->d.tok_pos[idx] != 20) continue;
					if (bi_dict_surface(&c->e->d,
							    c->e->d.tok_sid[idx],
							    buf, sizeof(buf)) < 0)
						continue;
					sn = bi_utf8_decode(buf, scps, 64);
					if (sn < 1 || scps[sn - 1] != tab[t].from[k])
						continue;
					scps[sn - 1] = tab[t].ob;
					scps[sn] = fol;
					char *surf = bi_utf8_str(scps, sn + 1);
					if (!surf) continue;
					int wc = c->e->d.tok_cost[idx] * 16 + pen;
					if (c->e->d.have_sidecar)
						add_node_lr(c, start,
							    start + L + 1, -1,
							    63, c->e->d.sc_lid[idx],
							    c->e->d.sc_cls[63 * 2 + 1],
							    wc, surf);
					else
						add_node(c, start, start + L + 1,
							 -1, 63, wc, surf);
				}
			}
		}
	}
}

static void add_dict_nodes(struct conv_ctx *c, int start)
{
	int maxlen = c->n - start;
	uint32_t *codes = malloc(sizeof(uint32_t) * maxlen);
	if (!codes) return;
	for (int i = 0; i < maxlen; i++) {
		int code = bi_dict_encode_char(&c->e->d, c->cps[start + i]);
		if (code < 0) { maxlen = i; break; }
		codes[i] = (uint32_t)code;
	}
	if (maxlen > 0) {
		struct walk_state w = { c, start, 0 };
		bi_dict_prefix_walk(&c->e->d, codes, maxlen, &w, walk_cb);
		add_onbin_virtuals(c, start, codes, maxlen);
	}
	free(codes);
}

/* M3: supplement nodes — vocabulary from non-mozc sources
 * (NEologd neologisms/proper nouns, SudachiDict gaps) that the
 * mozc-derived dictionary lacks.  Non-katakana surfaces stay in
 * the mapping (zsurf, zero-copy); katakana-form surfaces are
 * derived from the reading span itself, never stored. */
static void add_supp_nodes(struct conv_ctx *c, int start)
{
	struct bi_dict *d = &c->e->d;
	if (!d->n_supp || getenv("BI_NO_SUPP")) return;
	int maxlen = c->n - start;
	if (maxlen > 24) maxlen = 24;
	for (int len = 1; len <= maxlen; len++) {
		for (int idx = 0; ; idx++) {
			struct bi_supp_hit hit;
			if (!bi_dict_supp(d, c->cps + start, len, idx, &hit))
				break;
			char buf[256];
			const char *surf;
			char *owned = NULL;
			const uint8_t *zs = NULL;
			int zlen = 0;
			if (hit.is_kata) {
				owned = span_str(c->cps, start, len, 1);
				if (!owned) continue;
				surf = owned;
			} else {
				int ln = hit.surf_len;
				if (ln >= (int)sizeof(buf))
					ln = (int)sizeof(buf) - 1;
				memcpy(buf, hit.surf, (size_t)ln);
				buf[ln] = '\0';
				surf = buf;
				zs = hit.surf;
				zlen = hit.surf_len;
			}
			int pen = surface_penalty(c, start, len, surf,
						  hit.cls);
			/* M4: katakana-form supplement entries are
			 * priced on NEologd's percentile scale, whose
			 * low end (byte ~15) undercuts real dictionary
			 * words and lets one katakana node monopolise
			 * the finals (measured: せんしゅう->センシュウ
			 * path 739 units cheaper than 先週, all 8
			 * n-best slots were its variants).  A flat
			 * authority margin (BI_SUPP_KATA_PEN cost
			 * bytes, default 24) keeps such entries as
			 * candidates without letting them outrank the
			 * dictionary on a scale mismatch. */
			if (hit.is_kata) {
				const char *kp = getenv("BI_SUPP_KATA_PEN");
				pen += kp ? atoi(kp) : 24;
			}
			int wc = (hit.cost + pen) * 16;
			char *rd = span_str(c->cps, start, len, 0);
			if (rd) {
				wc -= bi_learn_bonus(c->e, rd, surf);
				free(rd);
			}
			if (wc < 0) wc = 0;
			uint16_t lid = 0, rid = 0;
			if (d->have_sidecar && hit.cls >= 0 && hit.cls < 256) {
				lid = d->sc_cls[hit.cls * 2];
				rid = d->sc_cls[hit.cls * 2 + 1];
			}
			add_node_z(c, start, start + len, hit.cls, lid, rid,
				   wc, zs, zlen, owned);
		}
	}
}

static char *span_str(const uint32_t *cps, int start, int len, int katakana)
{
	uint32_t tmp[BI_MAX_INPUT];
	for (int i = 0; i < len; i++)
		tmp[i] = katakana ? katakana_cp(cps[start + i]) : cps[start + i];
	return bi_utf8_str(tmp, len);
}

static const char *digit_kanji = "〇一二三四五六七八九"; /* utf8, 3B each */
static void kanji_numeral(const int *digits, int len, char *out, size_t outsz)
{
	static const char *units[] = { "", "十", "百", "千" };
	char tmp[256];
	tmp[0] = '\0';
	if (len == 1 && digits[0] == 0) {
		snprintf(out, outsz, "零");
		return;
	}
	if (len <= 4) {		/* positional: 1234 -> 千二百三十四 */
		for (int i = 0; i < len; i++) {
			int place = len - 1 - i;
			int dv = digits[i];
			if (dv == 0) continue;
			if (dv != 1 || place == 0)
				strncat(tmp, digit_kanji + (dv - 1) * 3 + 3, 3);
			/* digit_kanji indexing: char k at byte (k)*3 */
			if (place > 0) strcat(tmp, units[place]);
		}
		if (!tmp[0]) strcpy(tmp, "零");
		snprintf(out, outsz, "%s", tmp);
		return;
	}
	/* long runs: digit by digit */
	for (int i = 0; i < len; i++) {
		char one[4];
		memcpy(one, digit_kanji + digits[i] * 3, 3);
		one[3] = '\0';
		strcat(tmp, one);
	}
	snprintf(out, outsz, "%s", tmp);
}

static void add_special_nodes(struct conv_ctx *c, int start)
{
	uint32_t cp = c->orig[start];

	/* ASCII runs (letters/digits/symbols), emitted at run start */
	if (cp < 0x80 && cp >= 0x20) {
		if (start > 0 && c->orig[start - 1] < 0x80 &&
		    c->orig[start - 1] >= 0x20)
			return;	/* not a run start */
		int end = start;
		int all_digit = 1;
		while (end < c->n && c->orig[end] < 0x80 && c->orig[end] >= 0x20) {
			if (c->orig[end] < '0' || c->orig[end] > '9') all_digit = 0;
			end++;
		}
		/* digit runs are numbers (class 8), not symbols:
		 * as class 81 the as-typed form paid the symbol BOS
		 * connection and lost to the kanji-numeral variant */
		add_node(c, start, end, -1, all_digit ? 8 : 81, 200,
			 span_str(c->orig, start, end - start, 0));
		if (all_digit && end - start <= 12) {
			int digits[12];
			for (int i = 0; i < end - start; i++)
				digits[i] = (int)(c->orig[start + i] - '0');
			char buf[256];
			kanji_numeral(digits, end - start, buf, sizeof(buf));
			add_node(c, start, end, -1, 8, 600, strdup(buf));
		}
		return;
	}

	/* fullwidth digit runs */
	if (cp >= 0xFF10 && cp <= 0xFF19) {
		if (start > 0 && c->orig[start - 1] >= 0xFF10 &&
		    c->orig[start - 1] <= 0xFF19)
			return;
		int end = start;
		while (end < c->n && c->orig[end] >= 0xFF10 &&
		       c->orig[end] <= 0xFF19)
			end++;
		int len = end - start;
		add_node(c, start, end, -1, 8, 300,
			 span_str(c->orig, start, len, 0));
		uint32_t half[16];
		int digits[16];
		for (int i = 0; i < len && i < 16; i++) {
			digits[i] = (int)(c->orig[start + i] - 0xFF10);
			half[i] = '0' + digits[i];
		}
		add_node(c, start, end, -1, 8, 350, bi_utf8_str(half, len));
		if (len <= 12) {
			char buf[256];
			kanji_numeral(digits, len, buf, sizeof(buf));
			add_node(c, start, end, -1, 8, 600, strdup(buf));
		}
		return;
	}

	/* Kana spans: as-typed hiragana and katakana unknown words.
	 * Pricing matters: the per-character rate must stay near
	 * the dictionary's rare-word rate, or one long katakana
	 * blob undercuts a correct multi-segment path by skipping
	 * its segment penalties and connections (measured failure:
	 * ねこがにわであそ as one node beat 猫が庭で遊…).
	 * Base + 45/char in dictionary cost units, max 6 chars. */
	if (is_kana_cp(c->cps[start])) {
		for (int len = 1; len <= 6 && start + len <= c->n; len++) {
			if (!is_kana_cp(c->cps[start + len - 1])) break;
			add_node(c, start, start + len, -1, 1,
				 (185 + 45 * len) * 16,
				 span_str(c->cps, start, len, 0));
			add_node(c, start, start + len, -1, 1,
				 (170 + 45 * len) * 16,
				 span_str(c->cps, start, len, 1));
		}
	}
}

/* ---------------------------------------------------------------- */
/* Viterbi                                                           */
/* ---------------------------------------------------------------- */

static const char *node_surface(struct conv_ctx *c, node *nd, char *buf,
				size_t bufsz)
{
	if (nd->surface) return nd->surface;
	if (nd->zsurf) {
		/* supplement surface: bytes live in the mapping;
		 * materialise into the caller's buffer only */
		size_t ln = (size_t)nd->zsurf_len;
		if (ln >= bufsz) ln = bufsz ? bufsz - 1 : 0;
		memcpy(buf, nd->zsurf, ln);
		if (bufsz) buf[ln] = '\0';
		return buf;
	}
	if (nd->sid >= 0) {
		if (bi_dict_surface(&c->e->d, (uint32_t)nd->sid, buf, bufsz) >= 0)
			return buf;
	}
	return "";
}

/* M2: lexicalised connection cost from the sidecar matrix.
 * mozc's raw cost (stored quantised at resolution sc_res) is
 * rescaled by conn_scale_pm/1000; the default 343 per-mille is the
 * ratio the dictionary's own word costs went through
 * (byte = mozc_cost * 255/11895, engine word = byte * 16), so word
 * and connection costs keep mozc's internal balance. */
static int matrix_conn(struct conv_ctx *c, int left_rid, int right_lid)
{
	const struct bi_dict *d = &c->e->d;
	/* bi_dict_conn_raw reads either the .bisc u8 plane or the
	 * v3 nibble-packed CONN section, in place in the mapping */
	int raw = bi_dict_conn_raw(d, left_rid, right_lid) *
		  (int)d->sc_res;
	return raw * c->conn_scale_pm / 1000;
}

static int conn(struct conv_ctx *c, node *left, node *right)
{
	if (c->e->d.have_sidecar) {
		/* BOS has rid 0 in mozc's id space; a session left
		 * context contributes its committed segment's rid. */
		int lrid = left ? left->rid : 0;
		return matrix_conn(c, lrid, right->lid) *
			c->conn_pct / 100;
	}
	char rbuf[256], lbuf[256];
	const char *rs = NULL, *ls = NULL;
	if (right->end - right->start <= 3)
		rs = node_surface(c, right, rbuf, sizeof(rbuf));
	if (left && left->end - left->start <= 3)
		ls = node_surface(c, left, lbuf, sizeof(lbuf));
	int lc = left ? left->pos : BI_CLS_BOS;
	uint32_t last_cp = 0, right_last_cp = 0;
	if (left && left == &c->ctx_node)
		last_cp = c->ctx->last_cp;
	else if (left && left->end > left->start)
		last_cp = c->cps[left->end - 1];
	if (right->end > right->start)
		right_last_cp = c->cps[right->end - 1];
	int v = bi_conn_cost(&c->e->d, lc, right->pos, last_cp,
			     right_last_cp, ls, rs);
	return v * c->conn_pct / 100;
}

typedef struct { int cost; int pnode; int pk; int nseg; } pathend;

/* path ordering: cheaper first; on a tie, fewer segments wins
 * (a single dictionary word beats a same-cost split) */
static int pathend_better(const pathend *a, const pathend *b)
{
	if (a->cost != b->cost) return a->cost < b->cost;
	return a->nseg < b->nseg;
}

static void run_viterbi(struct conv_ctx *c)
{
	lattice *l = &c->lat;
	/* scratch candidate buffer, sized for the widest boundary */
	int maxpred = 0;
	for (int p = 0; p <= c->n; p++) {
		int cnt = l->end_off[p + 1] - l->end_off[p];
		if (cnt > maxpred) maxpred = cnt;
	}
	pathend *cand = malloc(sizeof(pathend) *
			       (maxpred * BI_KBEST + 1));
	if (!cand) return;
	/* nodes are generated per start position, in order; every
	 * predecessor of a node starting at p ends at p and was
	 * generated earlier, so a single pass in index order works */
	for (int i = 0; i < l->n; i++) {
		node *nd = &l->v[i];
		int ncand = 0;
		if (nd->start == 0) {
			cand[ncand].cost = conn(c, c->ctx ? &c->ctx_node : NULL,
						nd) + nd->word_cost +
					   c->seg_pen;
			cand[ncand].pnode = -1; cand[ncand].pk = -1;
			cand[ncand].nseg = 1;
			ncand++;
		} else {
			int lo = l->end_off[nd->start];
			int hi = l->end_off[nd->start + 1];
			for (int j = lo; j < hi; j++) {
				node *pn = &l->v[l->by_end[j]];
				int cc = conn(c, pn, nd);
				for (int k = 0; k < pn->nbest; k++) {
					cand[ncand].cost = pn->best[k].cost + cc +
							   nd->word_cost + c->seg_pen;
					cand[ncand].pnode = l->by_end[j];
					cand[ncand].pk = k;
					cand[ncand].nseg = pn->best[k].nseg + 1;
					ncand++;
				}
			}
		}
		/* keep the BI_KBEST best: insert each candidate into
		 * a small sorted array — O(ncand * BI_KBEST), not
		 * O(ncand^2) (ncand reaches thousands at busy
		 * boundaries) */
		nd->nbest = 0;
		for (int a = 0; a < ncand; a++) {
			int pos = nd->nbest;
			while (pos > 0) {
				pathend cur;
				cur.cost = nd->best[pos - 1].cost;
				cur.nseg = nd->best[pos - 1].nseg;
				cur.pnode = 0; cur.pk = 0;
				if (!pathend_better(&cand[a], &cur)) break;
				pos--;
			}
			if (pos >= BI_KBEST) continue;
			int lim = nd->nbest < BI_KBEST ? nd->nbest : BI_KBEST - 1;
			for (int m = lim; m > pos; m--) {
				nd->best[m].cost = nd->best[m - 1].cost;
				nd->best[m].prev_node = nd->best[m - 1].prev_node;
				nd->best[m].prev_k = nd->best[m - 1].prev_k;
				nd->best[m].nseg = nd->best[m - 1].nseg;
			}
			nd->best[pos].cost = cand[a].cost;
			nd->best[pos].prev_node = cand[a].pnode;
			nd->best[pos].prev_k = cand[a].pk;
			nd->best[pos].nseg = cand[a].nseg;
			if (nd->nbest < BI_KBEST) nd->nbest++;
		}
	}
	free(cand);
}

/* ---------------------------------------------------------------- */
/* result                                                            */
/* ---------------------------------------------------------------- */

typedef struct {
	int start, len, pos;
	uint16_t lid, rid;
	int chosen;		/* user picked a candidate (session) */
	char *reading;
	char *surface;
	char **cands;
	int n_cands;
} segment;

struct bi_result {
	int n_chars;
	char *reading;		/* normalised reading (utf8) */
	segment *segs;
	int n_segs;
	char *nbest[BI_NBEST];
	long nbest_cost[BI_NBEST];
	int n_nbest;
};

static int collect_path(lattice *l, int node_idx, int k, int *path,
			int max)
{
	int depth = 0;
	int ni = node_idx, kk = k;
	while (ni >= 0 && depth < max) {
		path[depth++] = ni;
		int pni = l->v[ni].best[kk].prev_node;
		kk = l->v[ni].best[kk].prev_k;
		ni = pni;
	}
	/* reverse */
	for (int i = 0; i < depth / 2; i++) {
		int t = path[i]; path[i] = path[depth - 1 - i];
		path[depth - 1 - i] = t;
	}
	return depth;
}

static char *path_string(struct conv_ctx *c, lattice *l, int node_idx, int k)
{
	int path[BI_MAX_INPUT * 2];
	int depth = collect_path(l, node_idx, k, path, BI_MAX_INPUT * 2);
	char *out = malloc(4096);
	if (!out) return NULL;
	out[0] = '\0';
	size_t off = 0;
	for (int i = 0; i < depth; i++) {
		char buf[256];
		const char *s = node_surface(c, &l->v[path[i]], buf, sizeof(buf));
		size_t ln = strlen(s);
		if (off + ln < 4095) { memcpy(out + off, s, ln); off += ln; out[off] = '\0'; }
	}
	return out;
}

/* ---- M3: linear reranker over Viterbi n-best paths ---------------
 * The terminal-side "AI" is deliberately the smallest learned
 * model that can overturn mozc's near-tie homophone choices: a
 * linear model over 14 path features, inferred as one dot
 * product per candidate (no intermediate path representation is
 * built — features are computed straight from the collected node
 * array; the only strings materialised are the per-segment
 * reading/surface pairs the bigram probe needs).  A neural model
 * was rejected for the terminal: ARMv5 without NEON, 128 MB RAM,
 * and a weight budget measured in tens of bytes — a linear model
 * over good statistics beats a starved network and stays
 * inspectable.  Weights are trained host-side by structured
 * perceptron (tools/train_rerank.c) and shipped in the RNK
 * section; feature 0 with weight 1.0 and the rest 0 reproduces
 * the M2 Viterbi order exactly.
 *   0  Viterbi cost / 1000
 *   1  segment count
 *   2  corpus bigram score sum / 256 (BIGR; <BOS>/<EOS> included)
 *   3  bigram miss count
 *   4  unknown/special nodes (sid == -1)
 *   5  supplement nodes (sid == -2)
 *   6  kana-identity content segments (surface == reading).
 *      (A length-weighted variant was tried and measured worse
 *      on every set — corpus 57.3 vs 58.3, heldout58 65.5 vs
 *      67.2 — so the plain count stays.)
 *   7  proper-noun segments (pos 2..5)
 *   8  numeral segments (pos 8)
 *   9  verb segments (pos 20..27)
 *  10  first segment is a particle (pos 60..71)
 *  11  last segment is a particle
 *  12  katakana segments (>= 2 chars, >= half katakana)
 *  13  one-char segments
 * M4 (feat_ver 2): cross-boundary context -- the M3 feature set
 * summarised the whole path's bigram evidence into one sum, so
 * the model could not tell WHERE the evidence sat.  v2 splits
 * it by boundary position and by the PREVIOUS segment's class
 * and surface form (the features a bunsetsu-boundary decision
 * actually turns on):
 *  14  BOS bigram score / 256 (first boundary only)
 *  15  EOS bigram score / 256 (last boundary only)
 *  16  bigram hit count (pairs present in BIGR, BOS/EOS incl.)
 *  17  bigram sum / 256 over pairs whose PREV segment is a
 *      function class (pos 44..46, 60..87)
 *  18  bigram sum / 256 over pairs whose PREV segment is a
 *      content class
 *  19  function -> content transitions (bunsetsu starts)
 *  20  content -> function transitions (bunsetsu glue)
 *  21  segments containing at least one kanji
 *  22  total surface characters
 *  23  segments (from the 2nd) whose PREVIOUS segment's
 *      surface is all-kana
 * M5 (feat_ver 3): cost decomposition.  Feature 0 mixes word
 * costs and connection costs at the dictionary's fixed ratio
 * (conn_scale_pm), so the model could never re-calibrate the
 * word/connection balance -- the exact miscalibration the M4
 * failure analysis blamed for most ranking losses.  Splitting
 * the sums lets the perceptron learn that ratio itself:
 *  24  word cost sum / 1000 (dictionary cost bytes, engine units)
 *  25  connection cost sum / 1000 (BOS..EOS, engine units,
 *      conn_pct applied, seg_pen NOT included) */
/* Encode cps[start..start+len) as UTF-8 into a stack buffer —
 * the allocation-free form of span_str for hot paths. */
static int enc_span_buf(char *buf, size_t bufsz, const uint32_t *cps,
			int start, int len)
{
	size_t off = 0;
	for (int i = 0; i < len; i++) {
		char tmp[4];
		int k = bi_utf8_encode(cps[start + i], tmp);
		if (k <= 0 || off + (size_t)k >= bufsz) break;
		memcpy(buf + off, tmp, (size_t)k);
		off += (size_t)k;
	}
	if (bufsz) buf[off] = '\0';
	return (int)off;
}

static void path_features(struct conv_ctx *c, lattice *l, const int *path,
			  int depth, long cost, double *f)
{
	struct bi_dict *d = &c->e->d;
	for (int i = 0; i < BI_RERANK_NFEAT; i++) f[i] = 0;
	f[0] = cost / 1000.0;
	f[1] = depth;
	/* M5: word/conn cost decomposition (features 24/25).
	 * Recomputed from the path's nodes with the same conn()
	 * the Viterbi pass used, so the two sums plus seg_pen *
	 * depth reconstruct the path's pre-EOS cost. */
	{
		long wsum = 0, csum = 0;
		node *prev = c->ctx ? &c->ctx_node : NULL;
		for (int i = 0; i < depth; i++) {
			node *nd = &l->v[path[i]];
			wsum += nd->word_cost;
			csum += conn(c, prev, nd);
			prev = nd;
		}
		if (depth > 0) {
			node *last = &l->v[path[depth - 1]];
			int eos = c->e->d.have_sidecar
				? matrix_conn(c, last->rid, 0)
				: bi_conn_eos(last->pos);
			csum += eos * c->conn_pct / 100;
		}
		f[24] = wsum / 1000.0;
		f[25] = csum / 1000.0;
	}
	char pr[256] = "<BOS>", ps[256] = "<BOS>";
	double bsum = 0;
	int miss = 0;
	int prev_fn = 0, prev_content = 0, prev_allkana = 0;
	for (int i = 0; i <= depth; i++) {
		char crb[256], csb[256];
		const char *cr, *cs;
		node *nd = NULL;
		if (i < depth) {
			nd = &l->v[path[i]];
			enc_span_buf(crb, sizeof(crb), c->cps, nd->start,
				     nd->end - nd->start);
			const char *s = node_surface(c, nd, csb, sizeof(csb));
			if (s != csb) snprintf(csb, sizeof(csb), "%s", s);
			cr = crb;
			cs = csb;
		} else {
			cr = "<EOS>";
			cs = "<EOS>";
		}
		int sc = bi_dict_bigram(d, pr, ps, cr, cs);
		if (sc >= 0) {
			bsum += sc;
			f[16] += 1;
			if (i == 0) f[14] = sc / 256.0;
			if (i == depth) f[15] = sc / 256.0;
			if (i > 0 && i < depth) {
				if (prev_fn) f[17] += sc / 256.0;
				if (prev_content) f[18] += sc / 256.0;
			}
		} else miss++;
		if (i == depth) break;
		int pos = nd->pos;
		int seglen = nd->end - nd->start;
		if (nd->sid == -1) f[4] += 1;
		if (nd->sid == -2) f[5] += 1;
		int content = (pos >= 1 && pos <= 16) ||
			      (pos >= 20 && pos <= 27) ||
			      (pos >= 30 && pos <= 32) ||
			      pos == 40 || pos == 41 || pos == 42;
		if (content && !strcmp(cr, cs)) f[6] += 1;
		if (pos >= 2 && pos <= 5) f[7] += 1;
		if (pos == 8) f[8] += 1;
		if (pos >= 20 && pos <= 27) f[9] += 1;
		if (i == 0 && pos >= 60 && pos <= 71) f[10] = 1;
		if (i == depth - 1 && pos >= 60 && pos <= 71) f[11] = 1;
		if (seglen == 1) f[13] += 1;
		/* M4 v2 features: transitions and prev-segment form */
		{
			int fn = (pos >= 60 && pos <= 87) ||
				 (pos >= 44 && pos <= 46);
			if (i > 0) {
				if (prev_fn && content) f[19] += 1;
				if (prev_content && fn) f[20] += 1;
				if (prev_allkana) f[23] += 1;
			}
			prev_fn = fn;
			prev_content = content;
			/* current segment's surface form, for the
			 * next iteration and features 21/22 */
			uint32_t fcps[64];
			int fn2 = bi_utf8_decode(cs, fcps, 64);
			int allkana = fn2 > 0, haskanji = 0;
			for (int j = 0; j < fn2; j++) {
				uint32_t u = fcps[j];
				if ((u >= 0x4E00 && u <= 0x9FFF) ||
				    (u >= 0x3400 && u <= 0x4DBF) ||
				    u == 0x3005 || u == 0x3006)
					haskanji = 1;
				if (!((u >= 0x3041 && u <= 0x3096) ||
				      (u >= 0x30A1 && u <= 0x30F6) ||
				      u == 0x30FC))
					allkana = 0;
			}
			if (haskanji) f[21] += 1;
			if (fn2 > 0) f[22] += fn2;
			prev_allkana = allkana;
		}
		if (seglen >= 2) {
			uint32_t scps[64];
			int sn = bi_utf8_decode(cs, scps, 64);
			int kata = 0;
			for (int j = 0; j < sn; j++)
				if ((scps[j] >= 0x30A1 && scps[j] <= 0x30F6) ||
				    scps[j] == 0x30FC)
					kata++;
			if (sn > 0 && kata * 2 >= sn) f[12] += 1;
		}
		snprintf(pr, sizeof(pr), "%s", cr);
		snprintf(ps, sizeof(ps), "%s", cs);
	}
	f[2] = bsum / 256.0;
	f[3] = miss;
}

static double rerank_score(const float *w, const double *f)
{
	double s = 0;
	for (int i = 0; i < BI_RERANK_NFEAT; i++) s += w[i] * f[i];
	return s;
}

static bi_result *build_result(struct conv_ctx *c)
{
	lattice *l = &c->lat;
	/* Final candidates: every (node, k) path of every node
	 * ending at n.  Count first and allocate: a fixed cap here
	 * silently dropped late-generated nodes' paths (the た of
	 * かき+まし+た lost to か+来ました that way — the cheaper
	 * path existed but never reached the finals list). */
	typedef struct { int cost; int node; int k; int nseg; } final_t;
	int npairs = 0;
	for (int i = 0; i < l->n; i++)
		if (l->v[i].end == c->n) npairs += l->v[i].nbest;
	if (npairs == 0) return NULL;
	final_t *finals = malloc(sizeof(final_t) * npairs);
	if (!finals) return NULL;
	int nfin = 0;
	for (int i = 0; i < l->n; i++) {
		node *nd = &l->v[i];
		if (nd->end != c->n) continue;
		for (int k = 0; k < nd->nbest; k++) {
			int eos = c->e->d.have_sidecar
				? matrix_conn(c, nd->rid, 0)
				: bi_conn_eos(nd->pos);
			finals[nfin].cost = nd->best[k].cost +
				eos * c->conn_pct / 100;
			finals[nfin].node = i;
			finals[nfin].k = k;
			finals[nfin].nseg = nd->best[k].nseg;
			nfin++;
		}
	}
	for (int a = 0; a < nfin; a++)
		for (int b = a + 1; b < nfin; b++)
			if (finals[b].cost < finals[a].cost ||
			    (finals[b].cost == finals[a].cost &&
			     finals[b].nseg < finals[a].nseg)) {
				final_t t = finals[a];
				finals[a] = finals[b]; finals[b] = t;
			}

	/* M3: linear rerank — promote the best-scoring final to
	 * finals[0]; segments and n-best[0] follow it.  With the
	 * shipped initial weights (w0=1, rest 0) this is a no-op.
	 * Authority window: the reranker exists to overturn the
	 * dictionary's NEAR-TIE homophone choices, so only finals
	 * within BI_RERANK_WINDOW engine units of the Viterbi best
	 * (default 400) may be promoted; a runaway promotion of a
	 * much costlier path (measured: わたしは学校… over
	 * 私は学校…, gap 461) is exactly what the window blocks. */
	if (c->e->d.rnk_w && c->e->d.rnk_nfeat == BI_RERANK_NFEAT &&
	    !getenv("BI_NO_RERANK") && nfin > 1) {
		const float *w = c->e->d.rnk_w;
		const char *we = getenv("BI_RERANK_WINDOW");
		int window = we ? atoi(we) : 400;
		int m = nfin < BI_RERANK_MAXPATH ? nfin : BI_RERANK_MAXPATH;
		int besti = 0;
		double bestscore = 0;
		for (int i = 0; i < m; i++) {
			if (window >= 0 &&
			    finals[i].cost - finals[0].cost > window)
				break;	/* finals are cost-sorted */
			int path[BI_MAX_INPUT * 2];
			int depth = collect_path(l, finals[i].node,
						 finals[i].k, path,
						 BI_MAX_INPUT * 2);
			double f[BI_RERANK_NFEAT];
			path_features(c, l, path, depth, finals[i].cost, f);
			double sc = rerank_score(w, f);
			if (i == 0 || sc < bestscore) {
				bestscore = sc;
				besti = i;
			}
		}
		if (besti != 0) {
			/* M4: insertion, not swap.  A swap moved the
			 * Viterbi best string to position besti --
			 * possibly outside the visible n-best window,
			 * evicting it from the list entirely (measured:
			 * dev500 n-best 74.6% with swap vs 74.4% is the
			 * wrong direction; the swap-out was one of the
			 * M3 n-best loss mechanisms).  Insertion evicts
			 * only the marginal last string. */
			final_t t = finals[besti];
			memmove(&finals[1], &finals[0],
				sizeof(final_t) * (size_t)besti);
			finals[0] = t;
		}
	}

	bi_result *r = calloc(1, sizeof(*r));
	if (!r) { free(finals); return NULL; }
	r->n_chars = c->n;
	r->reading = bi_utf8_str(c->cps, c->n);

	/* n-best strings, deduped, in cost order.  (M4 tried a
	 * diversity pass -- accept only finals differing in >= 2
	 * segments first: it fixed individual crowding cases but
	 * cost 6.6 pt of dev500 n-best inclusion overall, because
	 * diverse-but-expensive finals evicted mid-ranked gold
	 * paths.  Measured and reverted; see REPORT-m4 S1.) */
	for (int i = 0; i < nfin && r->n_nbest < BI_NBEST; i++) {
		char *s = path_string(c, l, finals[i].node, finals[i].k);
		if (!s) continue;
		int dup = 0;
		for (int j = 0; j < r->n_nbest; j++)
			if (!strcmp(r->nbest[j], s)) dup = 1;
		if (dup) { free(s); continue; }
		r->nbest[r->n_nbest] = s;
		r->nbest_cost[r->n_nbest] = finals[i].cost;
		r->n_nbest++;
	}
	/* segments from the single best path */
	int path[BI_MAX_INPUT * 2];
	int depth = collect_path(l, finals[0].node, finals[0].k, path,
				 BI_MAX_INPUT * 2);
	if (getenv("BI_TRACE")) {
		int prev = -1;
		for (int i = 0; i < depth; i++) {
			node *nd = &l->v[path[i]];
			char buf[256];
			int cc = prev < 0 ? conn(c, NULL, nd)
					  : conn(c, &l->v[prev], nd);
			fprintf(stderr,
				"  trace [%d-%d] %s pos=%d word=%d conn=%d seg=%d\n",
				nd->start, nd->end,
				node_surface(c, nd, buf, sizeof(buf)),
				nd->pos, nd->word_cost, cc, c->seg_pen);
			prev = path[i];
		}
		fprintf(stderr, "  trace eos=%d total=%d\n",
			bi_conn_eos(l->v[path[depth - 1]].pos) *
				c->conn_pct / 100,
			finals[0].cost);
	}
	r->segs = calloc(depth, sizeof(segment));
	r->n_segs = depth;
	for (int i = 0; i < depth; i++) {
		node *nd = &l->v[path[i]];
		segment *sg = &r->segs[i];
		sg->start = nd->start;
		sg->len = nd->end - nd->start;
		sg->pos = nd->pos;
		sg->lid = nd->lid;
		sg->rid = nd->rid;
		sg->reading = span_str(c->cps, nd->start, sg->len, 0);
		char buf[256];
		sg->surface = strdup(node_surface(c, nd, buf, sizeof(buf)));

		/* candidates: dictionary group for this span, chosen
		 * first, then by cost; then supplement entries for
		 * the span (M4: these were missing -- a supplement
		 * node could be CHOSEN by the converter yet its
		 * sibling supplement surfaces never reached the
		 * candidate strip, e.g. さわら -> 佐和良 was offered
		 * but 佐波良 was not); then kana forms */
		char *list[CANDS_PER_SEG + 10];
		int nl = 0;
		list[nl++] = strdup(sg->surface);	/* own copy: cands[]
							   are freed
							   independently */
		/* find dict group: walk to the span's terminal node */
		int maxlen = sg->len;
		uint32_t codes[BI_MAX_INPUT];
		int enc_ok = 1;
		for (int j = 0; j < maxlen; j++) {
			int code = bi_dict_encode_char(&c->e->d,
						       c->cps[nd->start + j]);
			if (code < 0) { enc_ok = 0; break; }
			codes[j] = (uint32_t)code;
		}
		if (enc_ok) {
			int tn = bi_dict_walk_to(&c->e->d, codes, maxlen);
			if (tn >= 0 &&
			    bv_get(&c->e->d.rd.terminal, (uint32_t)tn)) {
				uint32_t tnode = (uint32_t)tn;
				uint32_t rank = bv_rank1(&c->e->d.rd.terminal,
							 tnode - 1);
				uint32_t first, count;
				bi_dict_token_range(&c->e->d, rank, &first, &count);
				for (uint32_t t = 0; t < count &&
				     nl < CANDS_PER_SEG; t++) {
					char sbuf[256];
					if (bi_dict_surface(&c->e->d,
						c->e->d.tok_sid[first + t],
						sbuf, sizeof(sbuf)) < 0)
						continue;
					int dup = 0;
					for (int j = 0; j < nl; j++)
						if (!strcmp(list[j], sbuf)) dup = 1;
					if (!dup) list[nl++] = strdup(sbuf);
				}
			}
		}
		/* M4: supplement candidates for this span */
		if (c->e->d.n_supp) {
			for (int idx = 0; nl < CANDS_PER_SEG + 8; idx++) {
				struct bi_supp_hit hit;
				if (!bi_dict_supp(&c->e->d,
						  c->cps + nd->start,
						  sg->len, idx, &hit))
					break;
				char sbuf[256];
				const char *sv;
				char *kata = NULL;
				if (hit.is_kata) {
					kata = span_str(c->cps, nd->start,
							sg->len, 1);
					if (!kata) continue;
					sv = kata;
				} else {
					int ln = hit.surf_len;
					if (ln >= (int)sizeof(sbuf))
						ln = (int)sizeof(sbuf) - 1;
					memcpy(sbuf, hit.surf, (size_t)ln);
					sbuf[ln] = '\0';
					sv = sbuf;
				}
				int dup = 0;
				for (int j = 0; j < nl; j++)
					if (!strcmp(list[j], sv)) dup = 1;
				if (!dup) list[nl++] = strdup(sv);
				free(kata);
			}
		}
		if (is_kana_cp(c->cps[nd->start])) {
			char *h = span_str(c->cps, nd->start, sg->len, 0);
			char *k = span_str(c->cps, nd->start, sg->len, 1);
			int dh = 0, dk = 0;
			for (int j = 0; j < nl; j++) {
				if (!strcmp(list[j], h)) dh = 1;
				if (!strcmp(list[j], k)) dk = 1;
			}
			if (!dh && nl < CANDS_PER_SEG + 10) list[nl++] = h; else free(h);
			if (!dk && nl < CANDS_PER_SEG + 10) list[nl++] = k; else free(k);
		}
		sg->cands = malloc(sizeof(char *) * nl);
		for (int j = 0; j < nl; j++) sg->cands[j] = list[j];
		sg->n_cands = nl;
	}
	free(finals);
	return r;
}

static bi_result *convert_internal(bi_engine *e, const char *reading_utf8,
				   const int *bounds, int n_bounds,
				   const bi_leftctx *ctx,
				   int forbid_from, int forbid_to)
{
	uint32_t orig[BI_MAX_INPUT], cps[BI_MAX_INPUT];
	int n = bi_utf8_decode(reading_utf8, orig, BI_MAX_INPUT);
	if (n <= 0) return NULL;
	/* M4: deferred CRC32 verification completes here, before
	 * the lattice reads any not-yet-verified section.  A
	 * corrupt dictionary fails the conversion, never converts
	 * with unverified data. */
	if (bi_dict_verify_pending(&e->d) != 0) return NULL;
	for (int i = 0; i < n; i++) cps[i] = normalize_cp(orig[i]);

	struct conv_ctx c;
	memset(&c, 0, sizeof(c));
	c.e = e; c.cps = cps; c.orig = orig; c.n = n;
	c.bounds = bounds; c.n_bounds = n_bounds;
	c.seg_pen = 260;
	c.conn_pct = 100;
	c.conn_scale_pm = 200;
	const char *sp = getenv("BI_SEG_PEN");
	if (sp) c.seg_pen = atoi(sp);
	const char *cpct = getenv("BI_CONN_PCT");
	if (cpct) c.conn_pct = atoi(cpct);
	const char *scpm = getenv("BI_CONN_SCALE_PM");
	if (scpm) c.conn_scale_pm = atoi(scpm);
	c.ctx = ctx;
	c.forbid_from = forbid_from;
	c.forbid_to = forbid_to;
	if (ctx) {
		memset(&c.ctx_node, 0, sizeof(c.ctx_node));
		c.ctx_node.pos = ctx->pos;
		c.ctx_node.lid = ctx->lid;
		c.ctx_node.rid = ctx->rid;
		c.ctx_node.start = 0;
		c.ctx_node.end = ctx->reading_len;
		c.ctx_node.surface = (char *)ctx->surface;
	}

	c.lat.start_off = calloc(n + 1, sizeof(int));
	c.lat.start_cnt = calloc(n + 1, sizeof(int));
	for (int i = 0; i < n; i++) {
		c.lat.start_off[i] = c.lat.n;
		add_dict_nodes(&c, i);
		add_special_nodes(&c, i);
		add_supp_nodes(&c, i);
		c.lat.start_cnt[i] = c.lat.n - c.lat.start_off[i];
	}
	lat_index_ends(&c.lat, n);
	run_viterbi(&c);
	bi_result *r = build_result(&c);
	lat_free(&c.lat);	/* also frees start_off/start_cnt */
	return r;
}

/* Host-side trainer support: the same lattice + finals pipeline
 * as convert_internal, but instead of a bi_result it returns the
 * top paths with their reranker feature vectors (Viterbi order,
 * duplicates by surface string removed). */
int bi_internal_collect(bi_engine *e, const char *reading_utf8,
			bi_pathinfo *out, int max)
{
	uint32_t orig[BI_MAX_INPUT], cps[BI_MAX_INPUT];
	int n = bi_utf8_decode(reading_utf8, orig, BI_MAX_INPUT);
	if (n <= 0 || max <= 0) return 0;
	if (bi_dict_verify_pending(&e->d) != 0) return 0;
	for (int i = 0; i < n; i++) cps[i] = normalize_cp(orig[i]);

	struct conv_ctx c;
	memset(&c, 0, sizeof(c));
	c.e = e; c.cps = cps; c.orig = orig; c.n = n;
	c.seg_pen = 260;
	c.conn_pct = 100;
	c.conn_scale_pm = 200;
	c.forbid_from = -1;
	c.forbid_to = -1;
	c.lat.start_off = calloc(n + 1, sizeof(int));
	c.lat.start_cnt = calloc(n + 1, sizeof(int));
	for (int i = 0; i < n; i++) {
		c.lat.start_off[i] = c.lat.n;
		add_dict_nodes(&c, i);
		add_special_nodes(&c, i);
		add_supp_nodes(&c, i);
		c.lat.start_cnt[i] = c.lat.n - c.lat.start_off[i];
	}
	lat_index_ends(&c.lat, n);
	run_viterbi(&c);

	lattice *l = &c.lat;
	typedef struct { int cost; int node; int k; int nseg; } fin_t;
	int npairs = 0;
	for (int i = 0; i < l->n; i++)
		if (l->v[i].end == c.n) npairs += l->v[i].nbest;
	int got = 0;
	if (npairs > 0) {
		fin_t *finals = malloc(sizeof(fin_t) * npairs);
		if (finals) {
			int nfin = 0;
			for (int i = 0; i < l->n; i++) {
				node *nd = &l->v[i];
				if (nd->end != c.n) continue;
				for (int k = 0; k < nd->nbest; k++) {
					int eos = c.e->d.have_sidecar
						? matrix_conn(&c, nd->rid, 0)
						: bi_conn_eos(nd->pos);
					finals[nfin].cost = nd->best[k].cost +
						eos * c.conn_pct / 100;
					finals[nfin].node = i;
					finals[nfin].k = k;
					finals[nfin].nseg = nd->best[k].nseg;
					nfin++;
				}
			}
			for (int a = 0; a < nfin; a++)
				for (int b = a + 1; b < nfin; b++)
					if (finals[b].cost < finals[a].cost ||
					    (finals[b].cost == finals[a].cost &&
					     finals[b].nseg < finals[a].nseg)) {
						fin_t t = finals[a];
						finals[a] = finals[b];
						finals[b] = t;
					}
			int m = nfin < BI_RERANK_MAXPATH ? nfin
							: BI_RERANK_MAXPATH;
			for (int i = 0; i < m && got < max; i++) {
				char *s = path_string(&c, l, finals[i].node,
						      finals[i].k);
				if (!s) continue;
				int dup = 0;
				for (int j = 0; j < got; j++)
					if (!strcmp(out[j].text, s)) dup = 1;
				if (dup) { free(s); continue; }
				int path[BI_MAX_INPUT * 2];
				int depth = collect_path(l, finals[i].node,
							 finals[i].k, path,
							 BI_MAX_INPUT * 2);
				out[got].text = s;
				out[got].cost = finals[i].cost;
				path_features(&c, l, path, depth,
					      finals[i].cost, out[got].feat);
				got++;
			}
			free(finals);
		}
	}
	lat_free(&c.lat);
	return got;
}

void bi_internal_free_paths(bi_pathinfo *p, int n)
{
	for (int i = 0; i < n; i++) free(p[i].text);
}

/* ---------------------------------------------------------------- */
/* public one-shot API                                               */
/* ---------------------------------------------------------------- */

bi_engine *bi_engine_open(const char *bidf_path)
{
	bi_engine *e = calloc(1, sizeof(*e));
	if (!e) return NULL;
	if (bi_dict_open(&e->d, bidf_path) != 0) { free(e); return NULL; }
	return e;
}

void bi_engine_close(bi_engine *e)
{
	if (!e) return;
	bi_dict_close(&e->d);
	if (e->learn_keys) {
		for (uint32_t i = 0; i <= e->learn_mask; i++)
			free(e->learn_keys[i]);
		free(e->learn_keys);
	}
	free(e->learn_counts);
	free(e->learn_path);
	free(e);
}

void bi_engine_stats(const bi_engine *e, bi_stats *out)
{
	*out = e->d.stats;
	out->learn_entries = e->learn_n;
}

/* ---------------------------------------------------------------- */
/* learning (M2 origin; M3 journal persistence)                                            */
/*                                                                   */
/* History = set of (reading, surface) pairs the user committed,     */
/* with counts, persisted as TSV at the path given to                */
/* bi_engine_enable_learning().  A dictionary node whose pair is     */
/* in the history gets a word-cost bonus of 100 engine units per     */
/* commit, capped at 10 commits (max 1000 — comparable to one        */
/* connection-cost step, so history can flip a close homophone       */
/* choice but cannot drag in a rare word from nowhere).              */
/*                                                                   */
/* Crash safety (the target device resets/power-cuts routinely):     */
/*  - M3: the file is an APPEND-ONLY JOURNAL.  Each commit appends   */
/*    one "reading<TAB>surface" line and fsyncs.  Appending one      */
/*    short line is the smallest possible write to the shared SD     */
/*    card (the M2 design rewrote the whole file per commit —        */
/*    write amplification and a rename per keystroke-commit).        */
/*  - when the journal outgrows its budget (line count based,        */
/*    never time based — the device clock is untrusted) it is        */
/*    compacted to "reading<TAB>surface<TAB>count" lines by the      */
/*    atomic save below: write "<path>.tmp", fflush + fsync,         */
/*    rename over the real file, fsync the directory.  A power       */
/*    cut leaves either the whole old file or the whole new one,     */
/*    never a torn one; a leftover .tmp is deleted at enable time.   */
/*  - loads are paranoid: only complete '\n'-terminated lines in      */
/*    the exact 2-field (journal event, +1) or 3-field (compacted    */
/*    count) shape are read; a torn tail or any malformed line is    */
/*    skipped, never fatal.  A corrupt history file can cost         */
/*    learned bonuses, it can never stop the engine from starting.   */
/*  - the table is capped at LEARN_MAX_ENTRIES distinct pairs.       */
/*    At the cap, new pairs are simply not learned (existing         */
/*    pairs keep counting).  Eviction is deliberately NOT            */
/*    time-based: the device clock is untrusted and there is no      */
/*    expiry concept anywhere in this file.                          */
/* ---------------------------------------------------------------- */

#define LEARN_BONUS_PER 100
#define LEARN_BONUS_CAP 10
#define LEARN_MAX_ENTRIES 4096

static uint32_t learn_hash(const char *s)
{
	uint32_t h = 2166136261u;
	while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
	return h;
}

static int learn_find(bi_engine *e, const char *key, int create)
{
	if (!e->learn_mask) {
		if (!create) return -1;
		e->learn_mask = 1023;
		e->learn_keys = calloc(1024, sizeof(char *));
		e->learn_counts = calloc(1024, sizeof(int));
		if (!e->learn_keys || !e->learn_counts) return -1;
	}
	uint32_t m = learn_hash(key) & e->learn_mask;
	while (e->learn_keys[m]) {
		if (!strcmp(e->learn_keys[m], key)) return (int)m;
		m = (m + 1) & e->learn_mask;
	}
	if (!create) return -1;
	if (e->learn_n >= LEARN_MAX_ENTRIES) return -1; /* hard cap */
	if (e->learn_n > (int)(e->learn_mask * 3 / 4)) {
		/* grow: rehash into a table twice the size */
		char **ok = e->learn_keys;
		int *oc = e->learn_counts;
		uint32_t omask = e->learn_mask;
		e->learn_mask = omask * 2 + 1;
		e->learn_keys = calloc(e->learn_mask + 1, sizeof(char *));
		e->learn_counts = calloc(e->learn_mask + 1, sizeof(int));
		if (!e->learn_keys || !e->learn_counts) return -1;
		e->learn_n = 0;
		for (uint32_t i = 0; i <= omask; i++) {
			if (!ok[i]) continue;
			uint32_t s = learn_hash(ok[i]) & e->learn_mask;
			while (e->learn_keys[s]) s = (s + 1) & e->learn_mask;
			e->learn_keys[s] = ok[i];
			e->learn_counts[s] = oc[i];
			e->learn_n++;
		}
		free(ok);
		free(oc);
		m = learn_hash(key) & e->learn_mask;
		while (e->learn_keys[m]) m = (m + 1) & e->learn_mask;
	}
	e->learn_keys[m] = strdup(key);
	e->learn_counts[m] = 0;
	e->learn_n++;
	return (int)m;
}

/* Atomic compaction: tmp file in the same directory + fsync +
 * rename + directory fsync.  Any failure keeps the previous file
 * intact.  Returns 0 on success. */
static int learn_save(bi_engine *e)
{
	if (!e->learn_path) return -1;
	char tmp[1200];
	snprintf(tmp, sizeof(tmp), "%s.tmp", e->learn_path);
	FILE *f = fopen(tmp, "w");
	if (!f) return -1;
	for (uint32_t i = 0; i <= e->learn_mask; i++) {
		if (!e->learn_keys[i]) continue;
		/* key is "reading\tsurface" */
		fprintf(f, "%s\t%d\n", e->learn_keys[i], e->learn_counts[i]);
	}
	if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
		fclose(f);
		unlink(tmp);
		return -1;
	}
	if (fclose(f) != 0 || rename(tmp, e->learn_path) != 0) {
		unlink(tmp);
		return -1;
	}
	/* make the rename itself durable */
	char dir[1200];
	snprintf(dir, sizeof(dir), "%s", e->learn_path);
	char *slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	else snprintf(dir, sizeof(dir), ".");
	int dfd = open(dir, O_RDONLY);
	if (dfd >= 0) {
		fsync(dfd);
		close(dfd);
	}
	e->learn_lines = e->learn_n;
	return 0;
}

/* Append one journal event and fsync.  Failure is silent: the
 * in-memory table still has the bonus for this process, and a
 * lost journal line can never break conversion. */
static void learn_append(bi_engine *e, const char *reading,
			 const char *surface)
{
	FILE *f = fopen(e->learn_path, "a");
	if (!f) return;
	fprintf(f, "%s\t%s\n", reading, surface);
	if (fflush(f) == 0) {
		fsync(fileno(f));
		e->learn_lines++;
	}
	fclose(f);
	/* Compaction budget (counts, never time): compact when the
	 * journal is large in absolute terms, or when events dwarf
	 * the distinct-pair count (heavy repetition). */
	if (e->learn_lines >= 8192 ||
	    (e->learn_lines > 1024 && e->learn_lines > 4L * e->learn_n))
		learn_save(e);
}

int bi_engine_enable_learning(bi_engine *e, const char *path)
{
	free(e->learn_path);
	e->learn_path = strdup(path);
	if (!e->learn_path) return -1;
	/* probe writability without truncating existing history */
	FILE *probe = fopen(path, "a");
	if (!probe) { free(e->learn_path); e->learn_path = NULL; return -1; }
	fclose(probe);
	/* a leftover tmp file is the residue of an interrupted save;
	 * the real file is intact (rename is atomic) — discard it */
	char tmp[1200];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	unlink(tmp);
	FILE *f = fopen(path, "r");
	if (f) {
		char line[1024];
		while (fgets(line, sizeof(line), f)) {
			size_t len = strlen(line);
			/* Only complete, '\n'-terminated lines are data.
			 * A final line without '\n' is a torn write from a
			 * power cut: discard it (and stop — nothing valid
			 * can follow it).  An overlong line that fills the
			 * buffer without a newline is likewise treated as
			 * garbage and skipped to its end. */
			if (len == 0 || line[len - 1] != '\n') {
				if (feof(f)) break;
				int ch;
				while ((ch = fgetc(f)) != EOF && ch != '\n')
					;
				continue;
			}
			line[len - 1] = '\0';
			/* two accepted shapes:
			 *   journal event:  reading TAB surface      (+1)
			 *   compacted line: reading TAB surface TAB digits
			 * both fields non-empty; anything else is skipped */
			char *t1 = strchr(line, '\t');
			if (!t1 || t1 == line || !t1[1])
				continue;
			char *t2 = strrchr(line, '\t');
			int count = 1;
			if (t2 != t1) {
				if (strchr(t1 + 1, '\t') != t2 || t1 + 1 == t2)
					continue; /* not exactly two tabs */
				const char *cnt = t2 + 1;
				if (!*cnt ||
				    strspn(cnt, "0123456789") != strlen(cnt))
					continue;
				count = atoi(cnt);
				*t2 = '\0'; /* line is now the key */
			}
			int slot = learn_find(e, line, 1);
			if (slot >= 0) {
				e->learn_counts[slot] += count;
				e->learn_lines++;
			}
		}
		fclose(f);
	}
	e->d.stats.learn_entries = e->learn_n;
	return 0;
}

int bi_learn_bonus(bi_engine *e, const char *reading, const char *surface)
{
	if (!e->learn_path || !e->learn_mask) return 0;
	char key[1024];
	snprintf(key, sizeof(key), "%s\t%s", reading, surface);
	int slot = learn_find(e, key, 0);
	if (slot < 0) return 0;
	int n = e->learn_counts[slot];
	if (n > LEARN_BONUS_CAP) n = LEARN_BONUS_CAP;
	return n * LEARN_BONUS_PER;
}

void bi_learn_record(bi_engine *e, const char *reading, const char *surface)
{
	if (!e->learn_path) return;
	char key[1024];
	snprintf(key, sizeof(key), "%s\t%s", reading, surface);
	int slot = learn_find(e, key, 1);
	if (slot < 0) return;
	e->learn_counts[slot]++;
	e->d.stats.learn_entries = e->learn_n;
	learn_append(e, reading, surface);
}

bi_result *bi_convert(bi_engine *e, const char *reading_utf8)
{
	return convert_internal(e, reading_utf8, NULL, 0, NULL, -1, -1);
}

bi_result *bi_convert_forced(bi_engine *e, const char *reading_utf8,
			     const int *bounds, int n_bounds)
{
	return convert_internal(e, reading_utf8, bounds, n_bounds, NULL,
				-1, -1);
}

void bi_result_free(bi_result *r)
{
	if (!r) return;
	for (int i = 0; i < r->n_segs; i++) {
		free(r->segs[i].reading);
		free(r->segs[i].surface);
		for (int j = 0; j < r->segs[i].n_cands; j++)
			free(r->segs[i].cands[j]);
		free(r->segs[i].cands);
	}
	free(r->segs);
	for (int i = 0; i < r->n_nbest; i++) free(r->nbest[i]);
	free(r->reading);
	free(r);
}

int bi_result_nbest_count(const bi_result *r) { return r->n_nbest; }
const char *bi_result_nbest(const bi_result *r, int rank)
{
	return (rank >= 0 && rank < r->n_nbest) ? r->nbest[rank] : NULL;
}
long bi_result_nbest_cost(const bi_result *r, int rank)
{
	return (rank >= 0 && rank < r->n_nbest) ? r->nbest_cost[rank] : -1;
}
int bi_result_segment_count(const bi_result *r) { return r->n_segs; }
int bi_result_segment_start(const bi_result *r, int i)
{
	return (i >= 0 && i < r->n_segs) ? r->segs[i].start : -1;
}
int bi_result_segment_length(const bi_result *r, int i)
{
	return (i >= 0 && i < r->n_segs) ? r->segs[i].len : -1;
}
const char *bi_result_segment_reading(const bi_result *r, int i)
{
	return (i >= 0 && i < r->n_segs) ? r->segs[i].reading : NULL;
}
const char *bi_result_segment_surface(const bi_result *r, int i)
{
	return (i >= 0 && i < r->n_segs) ? r->segs[i].surface : NULL;
}
int bi_result_segment_pos(const bi_result *r, int i)
{
	return (i >= 0 && i < r->n_segs) ? r->segs[i].pos : -1;
}
int bi_result_candidate_count(const bi_result *r, int i)
{
	return (i >= 0 && i < r->n_segs) ? r->segs[i].n_cands : 0;
}
const char *bi_result_candidate(const bi_result *r, int i, int j)
{
	if (i < 0 || i >= r->n_segs || j < 0 || j >= r->segs[i].n_cands)
		return NULL;
	return r->segs[i].cands[j];
}

/* ---------------------------------------------------------------- */
/* session                                                           */
/* ---------------------------------------------------------------- */

struct bi_session {
	bi_engine *e;
	char *preedit;		/* as typed (utf8) */
	bi_result *result;
	int *bounds;
	int n_bounds, cap_bounds;
	char *text;		/* rebuilt lazily */
	/* M2: left context — the last committed segment, carried
	 * into the reconversion of the remainder so a remainder
	 * starting with a particle is no longer scored as a
	 * sentence-initial particle (the M1 degradation). */
	bi_leftctx ctx;
	char *ctx_surface;	/* owned; ctx.surface points here */
	int has_ctx;
	int forbid_from, forbid_to;	/* resize no-cut zone, -1 off */
};

static int session_reconvert(bi_session *s)
{
	bi_result_free(s->result);
	s->result = NULL;
	free(s->text); s->text = NULL;
	if (!s->preedit || !s->preedit[0]) return 0;
	if (s->has_ctx) s->ctx.surface = s->ctx_surface;
	s->result = convert_internal(s->e, s->preedit, s->bounds,
				     s->n_bounds,
				     s->has_ctx ? &s->ctx : NULL,
				     s->forbid_from, s->forbid_to);
	return s->result ? 0 : -1;
}

static void session_clear_ctx(bi_session *s)
{
	s->has_ctx = 0;
	free(s->ctx_surface);
	s->ctx_surface = NULL;
}

bi_session *bi_session_new(bi_engine *e)
{
	bi_session *s = calloc(1, sizeof(*s));
	if (s) {
		s->e = e;
		s->forbid_from = s->forbid_to = -1;
	}
	return s;
}

void bi_session_free(bi_session *s)
{
	if (!s) return;
	free(s->preedit);
	bi_result_free(s->result);
	free(s->bounds);
	free(s->text);
	free(s->ctx_surface);
	free(s);
}

int bi_session_set_preedit(bi_session *s, const char *reading_utf8)
{
	free(s->preedit);
	s->preedit = strdup(reading_utf8);
	s->n_bounds = 0;
	s->forbid_from = s->forbid_to = -1;
	session_clear_ctx(s);
	return session_reconvert(s);
}

const char *bi_session_preedit(const bi_session *s) { return s->preedit; }

const bi_result *bi_session_result(bi_session *s) { return s->result; }

int bi_session_choose(bi_session *s, int seg, int cand)
{
	if (!s->result || seg < 0 || seg >= s->result->n_segs) return -1;
	segment *sg = &s->result->segs[seg];
	if (cand < 0 || cand >= sg->n_cands) return -1;
	free(sg->surface);
	sg->surface = strdup(sg->cands[cand]);
	sg->chosen = 1;
	free(s->text); s->text = NULL;
	return 0;
}

static void session_set_bounds(bi_session *s, const int *b, int nb)
{
	if (nb > s->cap_bounds) {
		int *nb2 = realloc(s->bounds, (nb + 8) * sizeof(int));
		if (!nb2) return;	/* OOM: keep the old bounds;
					   never touch n_bounds/cap
					   (the old code pressed on with
					   bounds == NULL and cap raised,
					   a real crash path for nb > 0) */
		s->bounds = nb2;
		s->cap_bounds = nb + 8;
	}
	/* M4 (UBSan finding, via the OSK line): committing the
	 * first segment of a session that has no bounds calls
	 * this with nb == 0 while s->bounds is still NULL, i.e.
	 * memcpy(NULL, b, 0) -- a null argument to a nonnull-
	 * attributed function.  Harmless on glibc (zero bytes are
	 * copied) but undefined behaviour; skip the copy. */
	if (nb > 0)
		memcpy(s->bounds, b, nb * sizeof(int));
	s->n_bounds = nb;
}

int bi_session_resize(bi_session *s, int seg, int new_len)
{
	if (!s->result || seg < 0 || seg >= s->result->n_segs) return -1;
	segment *sg = &s->result->segs[seg];
	if (new_len < 1) return -1;
	int total = s->result->n_chars;
	int cut = sg->start + new_len;
	if (cut >= total) return -1;
	int nb[BI_MAX_INPUT];
	int n = 0;
	for (int i = 0; i < s->n_bounds; i++)
		if (s->bounds[i] <= sg->start) nb[n++] = s->bounds[i];
	nb[n++] = cut;
	session_set_bounds(s, nb, n);
	/* pin the resized segment: no cut may appear inside it,
	 * so it reconverts as exactly one segment of the new
	 * length (a forced bound alone only guarantees A cut at
	 * the end position, not that the segment spans it whole) */
	s->forbid_from = sg->start;
	s->forbid_to = cut;
	return session_reconvert(s);
}

int bi_session_commit_first(bi_session *s, char *out, size_t outsz)
{
	if (!s->result || s->result->n_segs == 0) return -1;
	segment *sg = &s->result->segs[0];
	snprintf(out, outsz, "%s", sg->surface);
	int written = (int)strlen(out);
	/* M2: record the commit for learning, and carry this
	 * segment as the left context of the remainder. */
	bi_learn_record(s->e, sg->reading, sg->surface);
	{
		uint32_t pcps[BI_MAX_INPUT];
		int pn = bi_utf8_decode(s->preedit, pcps, BI_MAX_INPUT);
		s->ctx.pos = sg->pos;
		s->ctx.lid = sg->lid;
		s->ctx.rid = sg->rid;
		s->ctx.reading_len = sg->len;
		s->ctx.last_cp = (pn > sg->start + sg->len - 1 &&
				  sg->len >= 1)
			? normalize_cp(pcps[sg->start + sg->len - 1]) : 0;
		free(s->ctx_surface);
		s->ctx_surface = strdup(sg->surface);
		s->has_ctx = 1;
	}
	/* drop the first segment's characters from the preedit */
	uint32_t cps[BI_MAX_INPUT];
	int n = bi_utf8_decode(s->preedit, cps, BI_MAX_INPUT);
	if (n < 0) return -1;
	int drop = sg->len;
	if (drop >= n) {
		free(s->preedit);
		s->preedit = strdup("");
		s->n_bounds = 0;
		session_clear_ctx(s);
		session_reconvert(s);
		return written;
	}
	char *rest = bi_utf8_str(cps + drop, n - drop);
	free(s->preedit);
	s->preedit = rest;
	/* the no-cut zone referred to pre-commit coordinates */
	s->forbid_from = s->forbid_to = -1;
	int nb[BI_MAX_INPUT];
	int m = 0;
	for (int i = 0; i < s->n_bounds; i++)
		if (s->bounds[i] > drop) nb[m++] = s->bounds[i] - drop;
	session_set_bounds(s, nb, m);
	session_reconvert(s);
	return written;
}

const char *bi_session_text(bi_session *s)
{
	if (!s->result) return "";
	if (s->text) return s->text;
	size_t cap = 4096, off = 0;
	s->text = malloc(cap);
	s->text[0] = '\0';
	for (int i = 0; i < s->result->n_segs; i++) {
		const char *surf = s->result->segs[i].surface;
		size_t ln = strlen(surf);
		if (off + ln < cap) {
			memcpy(s->text + off, surf, ln);
			off += ln;
			s->text[off] = '\0';
		}
	}
	return s->text;
}
