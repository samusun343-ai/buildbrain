/* dict.c — BIDF v2 loader.
 *
 * Format (see buildbrain tools/ime/build_bidict2.py): 64-byte
 * header, codebook, reading LOUDS trie, surface LOUDS trie, token
 * stream (ULEB128 surface id, pos u8, cost u8 per entry, grouped by
 * reading in terminal BFS order), 256x256 int8 connection section
 * (all zero in the shipped dictionary — the engine's cost model in
 * conn.c is used instead; a future dictionary build can fill it).
 *
 * The file is mmap'd MAP_SHARED.  On top of the mapping the loader
 * builds only:
 *   - sampled rank/select indexes over the four bit vectors
 *     (a few tens of KB),
 *   - the decoded token index (180k * 6 B ≈ 1.1 MB),
 *   - the codebook strings (a few thousand short strings).
 * Surface strings are never bulk-decoded; they are materialised
 * per candidate by walking the surface trie leaf->root.
 */
#define _POSIX_C_SOURCE 200809L
#include "bi_internal.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- */
/* bitvec with sampled rank/select                                   */
/* ---------------------------------------------------------------- */

static uint32_t popcnt_range(const uint8_t *bits, uint32_t from, uint32_t to)
{
	/* count 1-bits in [from, to] inclusive */
	uint32_t n = 0;
	for (uint32_t p = from; p <= to; p++)
		n += (bits[p >> 3] >> (7 - (p & 7))) & 1;
	return n;
}

int bv_build(bitvec *bv, const uint8_t *bits, uint32_t nbits)
{
	memset(bv, 0, sizeof(*bv));
	bv->bits = bits;
	bv->nbits = nbits;
	uint32_t nrank = nbits / 512 + 1;
	bv->rank_idx = calloc(nrank, sizeof(uint32_t));
	if (!bv->rank_idx) return -1;
	uint32_t run = 0;
	for (uint32_t p = 0; p < nbits; p++) {
		if (p % 512 == 0) bv->rank_idx[p / 512] = run;
		run += (bits[p >> 3] >> (7 - (p & 7))) & 1;
	}
	uint32_t total1 = run, total0 = nbits - run;
	bv->nsel1 = total1 / 256 + 1;
	bv->nsel0 = total0 / 256 + 1;
	bv->sel1_idx = calloc(bv->nsel1, sizeof(uint32_t));
	bv->sel0_idx = calloc(bv->nsel0, sizeof(uint32_t));
	if (!bv->sel1_idx || !bv->sel0_idx) { bv_free(bv); return -1; }
	uint32_t c1 = 0, c0 = 0;
	for (uint32_t p = 0; p < nbits; p++) {
		if ((bits[p >> 3] >> (7 - (p & 7))) & 1) {
			if (c1 % 256 == 0) bv->sel1_idx[c1 / 256] = p;
			c1++;
		} else {
			if (c0 % 256 == 0) bv->sel0_idx[c0 / 256] = p;
			c0++;
		}
	}
	return 0;
}

void bv_free(bitvec *bv)
{
	free(bv->rank_idx); free(bv->sel1_idx); free(bv->sel0_idx);
	memset(bv, 0, sizeof(*bv));
}

int bv_get(const bitvec *bv, uint32_t p)
{
	return (bv->bits[p >> 3] >> (7 - (p & 7))) & 1;
}

uint32_t bv_rank1(const bitvec *bv, uint32_t p)
{
	if (p == UINT32_MAX) return 0;
	if (p >= bv->nbits) p = bv->nbits - 1;
	uint32_t blk = p / 512;
	uint32_t n = bv->rank_idx[blk];
	if (blk * 512 <= p) n += popcnt_range(bv->bits, blk * 512, p);
	return n;
}

static uint32_t rank0_before(const bitvec *bv, uint32_t p)
{
	/* zeros in [0, p) */
	if (p == 0) return 0;
	return p - bv_rank1(bv, p - 1);
}

uint32_t bv_select1(const bitvec *bv, uint32_t k)
{
	if (k == 0) return UINT32_MAX;
	uint32_t blk = (k - 1) / 256;
	if (blk >= bv->nsel1) return UINT32_MAX;
	uint32_t p = bv->sel1_idx[blk];
	uint32_t seen = blk * 256;	/* ones strictly before p... p itself is the (blk*256+1)-th */
	/* p points at the (blk*256+1)-th 1-bit, i.e. seen-before = blk*256 */
	while (p < bv->nbits) {
		if (bv_get(bv, p)) {
			if (seen == k - 1) return p;
			seen++;
		}
		p++;
	}
	return UINT32_MAX;
}

uint32_t bv_select0(const bitvec *bv, uint32_t k)
{
	/* k 0-indexed: find the (k+1)-th 0-bit */
	uint32_t blk = k / 256;
	if (blk >= bv->nsel0) return UINT32_MAX;
	uint32_t p = bv->sel0_idx[blk];
	uint32_t seen = blk * 256;	/* zeros before p */
	while (p < bv->nbits) {
		if (!bv_get(bv, p)) {
			if (seen == k) return p;
			seen++;
		}
		p++;
	}
	return UINT32_MAX;
}

/* ---------------------------------------------------------------- */
/* LOUDS trie                                                        */
/* ---------------------------------------------------------------- */

uint32_t trie_label(const trie *t, uint32_t node)
{
	if (t->label_bytes == 1) return t->labels[node - 1];
	const uint8_t *p = t->labels + (node - 1) * 2;
	return p[0] | (p[1] << 8);
}

uint32_t trie_parent(const trie *t, uint32_t v)
{
	uint32_t p = bv_select1(&t->louds, v + 1);
	return rank0_before(&t->louds, p);
}

void trie_children(const trie *t, uint32_t v,
		   void (*cb)(void *ctx, uint32_t child, uint32_t label),
		   void *ctx)
{
	uint32_t lo = v > 0 ? bv_select0(&t->louds, v - 1) : 0;
	uint32_t hi = bv_select0(&t->louds, v);
	if (lo == UINT32_MAX || hi == UINT32_MAX) return;
	for (uint32_t p = lo + 1; p < hi; p++) {
		if (!bv_get(&t->louds, p)) continue;
		uint32_t child = bv_rank1(&t->louds, p) - 1;
		cb(ctx, child, trie_label(t, child));
	}
}

static int trie_child(const trie *t, uint32_t v, uint32_t label)
{
	uint32_t lo = v > 0 ? bv_select0(&t->louds, v - 1) : 0;
	uint32_t hi = bv_select0(&t->louds, v);
	if (lo == UINT32_MAX || hi == UINT32_MAX) return -1;
	for (uint32_t p = lo + 1; p < hi; p++) {
		if (!bv_get(&t->louds, p)) continue;
		uint32_t child = bv_rank1(&t->louds, p) - 1;
		uint32_t lab = trie_label(t, child);
		if (lab == label) return (int)child;
		if (lab > label) return -1;	/* children sorted ascending */
	}
	return -1;
}

/* ---------------------------------------------------------------- */
/* loader                                                            */
/* ---------------------------------------------------------------- */

struct hdr { char magic[4]; uint32_t ver, n_entries, n_readings,
	n_surfaces, cb, rd, sf, tok, conn; };

static const uint8_t *parse_trie(trie *t, const uint8_t *p, int has_counts)
{
	const uint8_t *q = p;
	t->n_nodes = q[0] | (q[1] << 8) | (q[2] << 16) | ((uint32_t)q[3] << 24); q += 4;
	t->label_bytes = q[0] | (q[1] << 8) | (q[2] << 16) | ((uint32_t)q[3] << 24); q += 4;
	t->n_term = q[0] | (q[1] << 8) | (q[2] << 16) | ((uint32_t)q[3] << 24); q += 4;
	if (bv_build(&t->louds, q, 2 * t->n_nodes) != 0) return NULL;
	q += (2 * t->n_nodes + 7) / 8;
	t->labels = q; q += (t->n_nodes - 1) * t->label_bytes;
	if (bv_build(&t->terminal, q, t->n_nodes) != 0) return NULL;
	q += (t->n_nodes + 7) / 8;
	if (has_counts) {
		t->counts = q; q += t->n_term;
		q += 4 * ((t->n_term + 1023) / 1024);	/* samples: recomputed below */
	} else {
		t->counts = NULL;
	}
	return q;
}

static uint32_t uleb(const uint8_t **pp)
{
	const uint8_t *p = *pp;
	uint32_t v = 0;
	int shift = 0;
	for (;;) {
		uint8_t b = *p++;
		v |= (uint32_t)(b & 0x7F) << shift;
		if (!(b & 0x80)) break;
		shift += 7;
	}
	*pp = p;
	return v;
}

/* CRC32 (IEEE, reflected) — verifies BIDF v3 sections at open:
 * the dictionary sits on a shared SD card and is never trusted
 * blindly.  One linear pass over the mapping at open; the pages
 * are then shared with every other process that maps the file. */
static uint32_t bi_crc32(const uint8_t *p, size_t n)
{
	/* slicing-by-8: the verification pass runs over the whole
	 * dictionary at open, so it must not become the load-time
	 * bottleneck (byte-wise CRC measured ~90 ms for 7.8 MB;
	 * this is several times faster and still trivially small) */
	static uint32_t tab[8][256];
	static int ready;
	if (!ready) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			tab[0][i] = c;
		}
		for (int i = 0; i < 256; i++)
			for (int k = 1; k < 8; k++)
				tab[k][i] = (tab[k - 1][i] >> 8) ^
					    tab[0][tab[k - 1][i] & 0xFF];
		ready = 1;
	}
	uint32_t c = 0xFFFFFFFFu;
	while (n >= 8) {
		uint32_t lo = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
			      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
		uint32_t hi = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
			      ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
		lo ^= c;
		c = tab[7][lo & 0xFF] ^ tab[6][(lo >> 8) & 0xFF] ^
		    tab[5][(lo >> 16) & 0xFF] ^ tab[4][lo >> 24] ^
		    tab[3][hi & 0xFF] ^ tab[2][(hi >> 8) & 0xFF] ^
		    tab[1][(hi >> 16) & 0xFF] ^ tab[0][hi >> 24];
		p += 8;
		n -= 8;
	}
	while (n--)
		c = tab[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

/* ---- surface materialisation cache (bounded, direct-mapped) ----
 * Surfaces are decoded from the varint token stream on every
 * candidate enumeration; the same surface id recurs constantly
 * (segments x n-best x conversions).  This cache is the ONLY
 * process-side copy of dictionary-derived strings: bounded by
 * BI_SURF_CACHE entries (default 1024, 0 = off), one hash probe
 * per lookup, collision replaces (no chains, no scans — a
 * linear-scan LRU version measured slower than no cache at all).
 * Everything else — token planes, connection matrix, supplement,
 * bigrams, weights — is read directly from the shared mapping
 * with no copy at all. */
#define SURF_CACHE_DEFAULT 1024

static uint32_t surf_slot(const struct bi_dict *d, uint32_t sid)
{
	/* multiplicative hash, high bits (cap is a power of two) */
	uint32_t shift = 0;
	while ((1u << shift) < d->surf_cache_cap) shift++;
	return (sid * 2654435761u) >> (32 - shift);
}

static const char *surf_cache_get(struct bi_dict *d, uint32_t sid)
{
	if (!d->surf_cache_cap || !d->surf_cache_str) {
		if (d->surf_cache_cap) d->stats.surf_misses++;
		return NULL;
	}
	uint32_t slot = surf_slot(d, sid);
	if (d->surf_cache_str[slot] && d->surf_cache_sid[slot] == sid) {
		d->stats.surf_hits++;
		return d->surf_cache_str[slot];
	}
	d->stats.surf_misses++;
	return NULL;
}

static void surf_cache_put(struct bi_dict *d, uint32_t sid, const char *s)
{
	if (!d->surf_cache_cap) return;
	if (!d->surf_cache_str) {
		d->surf_cache_str = calloc(d->surf_cache_cap, sizeof(char *));
		d->surf_cache_sid = calloc(d->surf_cache_cap, sizeof(uint32_t));
		if (!d->surf_cache_str || !d->surf_cache_sid) {
			d->surf_cache_cap = 0;
			return;
		}
	}
	uint32_t slot = surf_slot(d, sid);
	char *copy = strdup(s);
	if (!copy) return;
	free(d->surf_cache_str[slot]);
	d->surf_cache_str[slot] = copy;
	d->surf_cache_sid[slot] = sid;
}

/* ---- M2 sidecar (.bisc) ----
 * Layout (tools/gen_sidecar.py is the writer of record):
 *   64-byte header: "BISC", ver u32, n_tokens u32, n_ids u32,
 *   matrix_resolution u32, off_tok_lid, off_tok_rid, off_cls,
 *   off_matrix (all u32); then u16 lid plane, u16 rid plane,
 *   256*(lid,rid) class representatives, and the connection
 *   matrix as u8[n_ids*n_ids] (raw mozc cost / resolution).
 * Location: $BI_SIDECAR if set, else "<bidf path>.bisc".
 * $BI_NO_SIDECAR=1 forces the M1 rule model (A/B measurement). */
struct bisc_hdr {
	char magic[4];
	uint32_t ver, n_tokens, n_ids, res, off_lid, off_rid, off_cls,
		 off_matrix;
};

static void load_sidecar(struct bi_dict *d, const char *bidf_path,
			 uint32_t n_entries)
{
	if (getenv("BI_NO_SIDECAR")) return;
	const char *env = getenv("BI_SIDECAR");
	char *path = NULL;
	if (env && env[0]) {
		path = strdup(env);
	} else {
		size_t ln = strlen(bidf_path);
		path = malloc(ln + 6);
		if (path) sprintf(path, "%s.bisc", bidf_path);
	}
	if (!path) return;
	int fd = open(path, O_RDONLY);
	free(path);
	if (fd < 0) return;
	struct stat st;
	if (fstat(fd, &st) != 0) { close(fd); return; }
	uint8_t *map = mmap(NULL, (size_t)st.st_size, PROT_READ,
			    MAP_SHARED, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return;
	struct bisc_hdr h;
	if ((size_t)st.st_size < sizeof(h)) goto bad;
	memcpy(&h, map, sizeof(h));
	if (memcmp(h.magic, "BISC", 4) || h.ver != 1 ||
	    h.n_tokens != n_entries || h.n_ids == 0 ||
	    (size_t)h.off_matrix + (size_t)h.n_ids * h.n_ids >
		(size_t)st.st_size)
		goto bad;
	d->sc_map = map;
	d->sc_len = (size_t)st.st_size;
	d->sc_lid = (const uint16_t *)(map + h.off_lid);
	d->sc_rid = (const uint16_t *)(map + h.off_rid);
	d->sc_cls = (const uint16_t *)(map + h.off_cls);
	d->sc_matrix = map + h.off_matrix;
	d->sc_n_ids = h.n_ids;
	d->sc_res = h.res;
	d->have_sidecar = 1;
	d->stats.lexicalized = 1;
	d->conn_ptr = d->sc_matrix;
	d->conn_len = (size_t)h.n_ids * h.n_ids;
	return;
bad:
	munmap(map, (size_t)st.st_size);
}

/* ---- M3: BIDF v3 ("BID3") ---------------------------------------
 * One file: 144-byte header + directory of (id, off, len, crc32)
 * entries, then sections.  DICT is a complete BIDF v2 image;
 * TOKID/CLSREP/CONN replace the .bisc sidecar (CONN is the mozc
 * matrix with ids permuted by corpus hotness, 4-bit linear
 * quantised, nibbles read directly from the mapping); SUPP holds
 * supplement vocabulary from non-mozc sources; BIGR is a
 * corpus bigram hash table probed in place; RNK holds the linear
 * reranker's float32 weights, used through a bare pointer.
 * The full spec is in DESIGN.md / REPORT-m3.md.  Nothing here
 * copies section bytes: every pointer below aims into d->map. */

/* section ids, mirroring tools/build_bidict3.py */
enum {
	V3_DICT = 1, V3_TOKID, V3_CLSREP, V3_CONN,
	V3_SUPP, V3_BIGR, V3_RNK, V3_META,
	V3_PRIO		/* M7: per-token surface-prior cost deltas */
};

static uint32_t v3_rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
#define rd32 v3_rd32

struct v3_sec { const uint8_t *p; uint32_t len; };

/* Parse + CRC-verify the v3 container.  On success fills the
 * section pointers and returns the DICT section location; the
 * caller then runs the ordinary v2 parse at that base. */
static int v3_parse(struct bi_dict *d, struct v3_sec secs[10],
		    const uint8_t **dict_base, uint32_t *dict_len)
{
	const uint8_t *m = d->map;
	size_t total = d->map_len;
	if (total < 144 || memcmp(m, "BID3", 4)) return -1;
	if (rd32(m + 4) != 3) return -1;		/* format version */
	uint32_t nsec = rd32(m + 8);
	if (nsec == 0 || nsec > 16 || 16 + 16 * nsec > total) return -1;
	memset(secs, 0, sizeof(struct v3_sec) * 10);
	for (uint32_t i = 0; i < nsec; i++) {
		const uint8_t *e = m + 16 + 16 * i;
		uint32_t id = rd32(e), off = rd32(e + 4);
		uint32_t len = rd32(e + 8), crc = rd32(e + 12);
		if (id == 0 || id > 9 || secs[id].p) return -1;
		if ((size_t)off + len > total) return -1;
		secs[id].p = m + off;
		secs[id].len = len;
		/* M4 deferred verification: record every section;
		 * verify now only the sections the loader itself
		 * consumes at open (DICT is parsed by the v2 code
		 * right after this) plus the small control sections
		 * (CLSREP/RNK/META).  TOKID/CONN/SUPP/BIGR are
		 * verified by bi_dict_verify_pending() before the
		 * engine first reads them.  BI_CRC_FULL=1 verifies
		 * everything here (the M3 behaviour). */
		d->v3_sec_ptr[id] = m + off;
		d->v3_sec_len[id] = len;
		d->v3_sec_crc[id] = crc;
		if (getenv("BI_CRC_FULL") || id == V3_DICT ||
		    id == V3_CLSREP || id == V3_RNK || id == V3_META ||
		    id == V3_PRIO) {
			/* integrity: a corrupt dictionary is rejected
			 * here, never half-trusted */
			if (bi_crc32(m + off, len) != crc) return -1;
			d->v3_verified[id] = 1;
		}
	}
	if (!secs[V3_DICT].p || !secs[V3_TOKID].p || !secs[V3_CONN].p ||
	    !secs[V3_CLSREP].p)
		return -1;
	*dict_base = secs[V3_DICT].p;
	*dict_len = secs[V3_DICT].len;
	return 0;
}

/* Wire the v3 sections into the sidecar-shaped fields the engine
 * already consumes, plus the M3-only accessors' state. */
static void v3_wire(struct bi_dict *d, const struct v3_sec secs[10],
		    uint32_t n_entries)
{
	const uint8_t *tk = secs[V3_TOKID].p;
	uint32_t ntok = rd32(tk);
	d->sc_lid = (const uint16_t *)(tk + 4);
	d->sc_rid = (const uint16_t *)(tk + 4 + 2 * (size_t)ntok);
	(void)n_entries;	/* count equality checked by caller */
	d->sc_cls = (const uint16_t *)secs[V3_CLSREP].p;
	const uint8_t *cn = secs[V3_CONN].p;
	d->sc_n_ids = rd32(cn);
	d->v3_conn_lut = (const uint16_t *)(cn + 4);
	d->v3_conn = cn + 4 + 32;
	d->sc_res = 64;		/* LUT values live in the /64 domain */
	d->have_sidecar = 1;
	d->stats.lexicalized = 1;
	d->conn_ptr = cn;
	d->conn_len = secs[V3_CONN].len;
	if (secs[V3_SUPP].p) {
		d->n_supp = rd32(secs[V3_SUPP].p);
		d->supp_offs = (const uint32_t *)(secs[V3_SUPP].p + 4);
		d->supp_recs = secs[V3_SUPP].p + 4 +
			       4 * (size_t)(d->n_supp + 1);
		d->stats.n_supp = d->n_supp;
	}
	if (secs[V3_BIGR].p) {
		d->bigr_nslots = rd32(secs[V3_BIGR].p);
		d->stats.n_bigram = rd32(secs[V3_BIGR].p + 4);
		d->bigr_slots = secs[V3_BIGR].p + 8;
	}
	if (secs[V3_RNK].p && secs[V3_RNK].len >= 8) {
		uint32_t fv = rd32(secs[V3_RNK].p);
		uint32_t nf = rd32(secs[V3_RNK].p + 4);
		if (fv < 1 || fv > 3) nf = 0;	/* unknown feature
						   version: no rerank */
		if (nf > 0 && nf <= 64 &&
		    secs[V3_RNK].len >= 8 + 4 * nf) {
			d->rnk_w = (const float *)(secs[V3_RNK].p + 8);
			d->rnk_nfeat = nf;
		}
	}
	d->is_v3 = 1;
	d->stats.format_v3 = 1;
}

int bi_dict_verify_pending(struct bi_dict *d)
{
	if (!d->is_v3) return 0;
	if (d->v3_broken) return -1;
	for (int id = 1; id <= 8; id++) {
		if (!d->v3_sec_ptr[id] || d->v3_verified[id]) continue;
		if (bi_crc32(d->v3_sec_ptr[id], d->v3_sec_len[id]) !=
		    d->v3_sec_crc[id]) {
			d->v3_broken = 1;
			return -1;
		}
		d->v3_verified[id] = 1;
	}
	return 0;
}

size_t bi_dict_unverified_bytes(const struct bi_dict *d)
{
	size_t n = 0;
	if (!d->is_v3) return 0;
	for (int id = 1; id <= 8; id++)
		if (d->v3_sec_ptr[id] && !d->v3_verified[id])
			n += d->v3_sec_len[id];
	return n;
}

/* accessor-side guard (the dictionary is const through the
 * engine; the verification bookkeeping mutates, like the
 * surface cache's does) */
static int v3_ensure(const struct bi_dict *d)
{
	if (!d->is_v3) return 0;
	for (int id = 1; id <= 8; id++)
		if (d->v3_sec_ptr[id] && !d->v3_verified[id])
			return bi_dict_verify_pending((struct bi_dict *)d);
	return d->v3_broken ? -1 : 0;
}

int bi_dict_conn_raw(const struct bi_dict *d, int rid, int lid)
{
	if (d->is_v3 && v3_ensure(d) != 0) return 0;
	if (d->is_v3) {
		size_t idx = (size_t)rid * d->sc_n_ids + (size_t)lid;
		uint8_t byte = d->v3_conn[idx >> 1];
		int nib = (idx & 1) ? (byte & 0x0F) : (byte >> 4);
		return d->v3_conn_lut[nib];
	}
	return d->sc_matrix[(size_t)rid * d->sc_n_ids + (size_t)lid];
}

int bi_dict_region(const struct bi_dict *d, const char *name,
		   const uint8_t **ptr, size_t *len)
{
	if (!strcmp(name, "file")) {
		*ptr = d->map; *len = d->map_len;
		return 0;
	}
	if (!strcmp(name, "conn") && d->conn_ptr) {
		*ptr = d->conn_ptr; *len = d->conn_len;
		return 0;
	}
	return -1;
}

/* ---- supplement (SUPP) -------------------------------------------
 * Records sorted by 1-byte-kana-code reading; binary search runs
 * over the offsets array in the mapping (no index is built). */
static int kana_code_of(uint32_t cp)
{
	if (cp >= 0x3041 && cp <= 0x3096) return (int)(cp - 0x3041);
	if (cp == 0x30FC) return 86;
	if (cp == 0x309D) return 87;
	if (cp == 0x309E) return 88;
	return -1;
}

static const uint8_t *supp_rec(const struct bi_dict *d, uint32_t i)
{
	return d->supp_recs + d->supp_offs[i];
}

/* compare a record's encoded reading with the query codes:
 * <0 record sorts before query, 0 equal, >0 after */
static int supp_cmp(const uint8_t *rec, const uint8_t *q, int qlen)
{
	int rlen = rec[0];
	int n = rlen < qlen ? rlen : qlen;
	int c = memcmp(rec + 4, q, (size_t)n);
	if (c) return c;
	if (rlen == qlen) return 0;
	return rlen < qlen ? -1 : 1;
}

int bi_dict_supp(const struct bi_dict *d, const uint32_t *cps, int len,
		 int idx, struct bi_supp_hit *out)
{
	if (v3_ensure(d) != 0) return 0;
	if (!d->n_supp || len <= 0 || len > 24) return 0;
	uint8_t q[24];
	for (int i = 0; i < len; i++) {
		int code = kana_code_of(cps[i]);
		if (code < 0) return 0;
		q[i] = (uint8_t)code;
	}
	/* lower bound: first record with reading >= query */
	uint32_t lo = 0, hi = d->n_supp;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (supp_cmp(supp_rec(d, mid), q, len) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	uint32_t i = lo + (uint32_t)idx;
	if (i >= d->n_supp) return 0;
	const uint8_t *rec = supp_rec(d, i);
	if (supp_cmp(rec, q, len) != 0) return 0;
	int rlen = rec[0];
	out->is_kata = (rec[1] & 1) != 0;
	out->cls = rec[2];
	out->cost = rec[3];
	if (out->is_kata) {
		out->surf = NULL;
		out->surf_len = 0;
	} else {
		out->surf_len = rec[4 + rlen];
		out->surf = rec + 4 + rlen + 1;
	}
	return 1;
}

/* ---- bigram (BIGR) ------------------------------------------------
 * FNV-1a 64 over "pr \x01 ps \x01 cr \x01 cs" (UTF-8 bytes),
 * folded to a u32 key (0 remapped to 1); linear probing over
 * power-of-two slots of {u32 key, i16 score, u16 pad}.  Must match
 * tools/build_bidict3.py exactly. */
static uint64_t fnv_feed(uint64_t h, const char *s)
{
	while (*s) {
		h ^= (uint8_t)*s++;
		h *= 0x100000001b3ULL;
	}
	return h;
}

int bi_dict_bigram(const struct bi_dict *d, const char *pr,
		   const char *ps, const char *cr, const char *cs)
{
	if (v3_ensure(d) != 0) return -1;
	if (!d->bigr_slots) return -1;
	uint64_t h = 0xcbf29ce484222325ULL;
	h = fnv_feed(h, pr); h ^= 1; h *= 0x100000001b3ULL;
	h = fnv_feed(h, ps); h ^= 1; h *= 0x100000001b3ULL;
	h = fnv_feed(h, cr); h ^= 1; h *= 0x100000001b3ULL;
	h = fnv_feed(h, cs);
	uint32_t key = (uint32_t)(h >> 32) ^ (uint32_t)h;
	if (!key) key = 1;
	uint32_t mask = d->bigr_nslots - 1;
	uint32_t i = key & mask;
	for (uint32_t t = 0; t < d->bigr_nslots; t++) {
		const uint8_t *slot = d->bigr_slots + (size_t)i * 8;
		uint32_t k = rd32(slot);
		if (k == 0) return -1;
		if (k == key) {
			int16_t sc;
			memcpy(&sc, slot + 4, 2);
			return sc;
		}
		i = (i + 1) & mask;
	}
	return -1;
}

int bi_dict_open(struct bi_dict *d, const char *path)
{
	memset(d, 0, sizeof(*d));
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -1;
	struct stat st;
	if (fstat(fd, &st) != 0) { close(fd); return -1; }
	d->map_len = (size_t)st.st_size;
	d->map = mmap(NULL, d->map_len, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (d->map == MAP_FAILED) { d->map = NULL; return -1; }

	/* BIDF v3 container?  Sections are CRC-verified here; the
	 * DICT section is then parsed by the ordinary v2 code below
	 * at a nonzero base. */
	struct v3_sec secs[10];
	const uint8_t *base = d->map;
	size_t base_len = d->map_len;
	int v3 = 0;
	if (d->map_len >= 4 && !memcmp(d->map, "BID3", 4)) {
		uint32_t dlen = 0;
		if (v3_parse(d, secs, &base, &dlen) != 0) goto fail;
		base_len = dlen;
		v3 = 1;
	}

	struct hdr h;
	memcpy(&h, base, sizeof(h));
	if (memcmp(h.magic, "BIDF", 4) || h.ver != 2) goto fail;
	d->stats.n_entries = h.n_entries;
	d->stats.n_readings = h.n_readings;
	d->stats.n_surfaces = h.n_surfaces;
	d->stats.file_bytes = d->map_len;

	/* surface cache budget (0 disables; default 1024;
	 * rounded up to a power of two for the direct-mapped table) */
	{
		const char *ce = getenv("BI_SURF_CACHE");
		long cap = ce ? atol(ce) : SURF_CACHE_DEFAULT;
		if (cap < 0) cap = 0;
		if (cap > 65536) cap = 65536;
		uint32_t p2 = 1;
		while ((long)p2 < cap) p2 <<= 1;
		d->surf_cache_cap = cap ? p2 : 0;
	}

	/* codebook */
	const uint8_t *p = base + h.cb;
	d->nchars = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); p += 4;
	d->book = calloc(d->nchars, sizeof(char *));
	d->book_cp = calloc(d->nchars, sizeof(uint32_t));
	if (!d->book || !d->book_cp) goto fail;
	for (uint32_t i = 0; i < d->nchars; i++) {
		int ln = *p++;
		char *s = malloc(ln + 1);
		if (!s) goto fail;
		memcpy(s, p, ln);
		s[ln] = '\0';
		d->book[i] = s;
		uint32_t cps[2];
		d->book_cp[i] = bi_utf8_decode(s, cps, 2) == 1 ? cps[0] : 0;
		p += ln;
	}
	/* codepoint -> code hash */
	d->cp_hash_size = 16;
	while (d->cp_hash_size < d->nchars * 4) d->cp_hash_size <<= 1;
	d->cp_hash = malloc(d->cp_hash_size * sizeof(int32_t));
	if (!d->cp_hash) goto fail;
	for (uint32_t i = 0; i < d->cp_hash_size; i++) d->cp_hash[i] = -1;
	for (uint32_t i = 0; i < d->nchars; i++) {
		uint32_t m = (d->book_cp[i] * 2654435761u) & (d->cp_hash_size - 1);
		while (d->cp_hash[m] >= 0) m = (m + 1) & (d->cp_hash_size - 1);
		d->cp_hash[m] = (int32_t)i;
	}

	/* tries */
	const uint8_t *end = parse_trie(&d->rd, base + h.rd, 1);
	if (!end || end != base + h.sf) goto fail;
	end = parse_trie(&d->sf, base + h.sf, 0);
	if (!end || end != base + h.tok) goto fail;

	/* token index: decode the whole stream once */
	d->tok_sid = malloc(h.n_entries * sizeof(uint32_t));
	d->tok_pos = malloc(h.n_entries);
	d->tok_cost = malloc(h.n_entries);
	d->tok_first = malloc((d->rd.n_term + 1) * sizeof(uint32_t));
	if (!d->tok_sid || !d->tok_pos || !d->tok_cost || !d->tok_first) goto fail;
	const uint8_t *tp = base + h.tok;
	for (uint32_t i = 0; i < h.n_entries; i++) {
		d->tok_sid[i] = uleb(&tp);
		d->tok_pos[i] = *tp++;
		d->tok_cost[i] = *tp++;
	}
	uint32_t acc = 0;
	for (uint32_t r = 0; r < d->rd.n_term; r++) {
		d->tok_first[r] = acc;
		acc += d->rd.counts[r];
	}
	d->tok_first[d->rd.n_term] = acc;

	/* connection section: present but all-zero in the shipped file */
	d->conn = (const int8_t *)(base + h.conn);
	d->conn_from_dict = 0;
	if ((size_t)h.conn + 256 * 256 <= base_len) {
		for (int i = 0; i < 256 * 256; i++)
			if (d->conn[i] != 0) { d->conn_from_dict = 1; break; }
	} else {
		d->conn = NULL;
	}
	d->stats.conn_from_dict = d->conn_from_dict;
	if (v3) {
		if (rd32(secs[V3_TOKID].p) != h.n_entries) goto fail;
		/* M7 PRIO: per-token surface-prior deltas (cost-byte
		 * units) woven into the decoded token index.  The
		 * corpus preference among surfaces of one reading is
		 * expressed in the word cost itself, not as a reranker
		 * feature (M6 showed two linear features cannot carry
		 * it).  Section absent (pre-M7 files) = no change. */
		if (secs[V3_PRIO].p) {
			const uint8_t *pr = secs[V3_PRIO].p;
			if (secs[V3_PRIO].len != 4 + h.n_entries ||
			    rd32(pr) != h.n_entries)
				goto fail;
			const int8_t *dl = (const int8_t *)(pr + 4);
			for (uint32_t i = 0; i < h.n_entries; i++) {
				int v;
				if (!dl[i]) continue;
				v = (int)d->tok_cost[i] + dl[i];
				d->tok_cost[i] = (uint8_t)(v < 0 ? 0 :
							   v > 255 ? 255 : v);
				d->stats.n_prior++;
			}
		}
		v3_wire(d, secs, h.n_entries);
	} else {
		load_sidecar(d, path, h.n_entries);
	}
	return 0;
fail:
	bi_dict_close(d);
	return -1;
}

void bi_dict_close(struct bi_dict *d)
{
	if (d->book) {
		for (uint32_t i = 0; i < d->nchars; i++) free(d->book[i]);
		free(d->book);
	}
	free(d->book_cp);
	free(d->cp_hash);
	bv_free(&d->rd.louds); bv_free(&d->rd.terminal);
	bv_free(&d->sf.louds); bv_free(&d->sf.terminal);
	free(d->tok_sid); free(d->tok_pos); free(d->tok_cost); free(d->tok_first);
	if (d->surf_cache_str) {
		for (uint32_t i = 0; i < d->surf_cache_cap; i++)
			free(d->surf_cache_str[i]);
		free(d->surf_cache_str);
	}
	free(d->surf_cache_sid);
	if (d->sc_map) munmap(d->sc_map, d->sc_len);
	if (d->map) munmap(d->map, d->map_len);
	memset(d, 0, sizeof(*d));
}

int bi_dict_encode_char(const struct bi_dict *d, uint32_t cp)
{
	if (!d->cp_hash) return -1;
	uint32_t m = (cp * 2654435761u) & (d->cp_hash_size - 1);
	while (d->cp_hash[m] >= 0) {
		int32_t code = d->cp_hash[m];
		if (d->book_cp[code] == cp) return (int)code;
		m = (m + 1) & (d->cp_hash_size - 1);
	}
	return -1;
}

int bi_dict_surface(const struct bi_dict *d, uint32_t sid, char *buf, size_t bufsz)
{
	/* the cache mutates bookkeeping only; the mapping and the
	 * dictionary data stay read-only (hence the cast) */
	struct bi_dict *dm = (struct bi_dict *)d;
	const char *hit = surf_cache_get(dm, sid);
	if (hit) {
		size_t ln = strlen(hit);
		if (bufsz) {
			size_t cp = ln < bufsz ? ln : bufsz - 1;
			memcpy(buf, hit, cp);
			buf[cp] = '\0';
		}
		return (int)ln;
	}
	uint32_t node = bv_select1(&d->sf.terminal, sid + 1);
	if (node == UINT32_MAX) return -1;
	uint32_t path[64];
	int depth = 0;
	while (node > 0 && depth < 64) {
		path[depth++] = trie_label(&d->sf, node);
		node = trie_parent(&d->sf, node);
	}
	size_t off = 0;
	for (int i = depth - 1; i >= 0; i--) {
		const char *s = d->book[path[i]];
		size_t ln = strlen(s);
		if (off + ln < bufsz) memcpy(buf + off, s, ln);
		off += ln;
	}
	if (bufsz) buf[off < bufsz ? off : bufsz - 1] = '\0';
	if (off < bufsz) surf_cache_put(dm, sid, buf);
	return (int)off;
}

void bi_dict_token_range(const struct bi_dict *d, uint32_t terminal_rank,
			 uint32_t *first, uint32_t *count)
{
	*first = d->tok_first[terminal_rank];
	*count = d->tok_first[terminal_rank + 1] - *first;
}

/* Prefix-walk helper for the engine: follow codes from the root,
 * invoking cb at every terminal node reached.  Stops early when cb
 * returns 0 or the trie path dies. */
struct walk_ctx {
	const struct bi_dict *d;
	const uint32_t *codes;
	int max_depth;
	void *user;
	int (*cb)(void *user, uint32_t node, int depth);
};

static void walk_rec(const trie *t, uint32_t node, const uint32_t *codes,
		     int depth, int max_depth, void *user,
		     int (*cb)(void *, uint32_t, int))
{
	if (depth >= max_depth) return;
	int child = trie_child(t, node, codes[depth]);
	if (child < 0) return;
	if (bv_get(&t->terminal, (uint32_t)child)) {
		if (!cb(user, (uint32_t)child, depth + 1)) return;
	}
	walk_rec(t, (uint32_t)child, codes, depth + 1, max_depth, user, cb);
}

void bi_dict_prefix_walk(const struct bi_dict *d, const uint32_t *codes,
			 int max_depth, void *user,
			 int (*cb)(void *, uint32_t, int))
{
	walk_rec(&d->rd, 0, codes, 0, max_depth, user, cb);
}

int bi_dict_walk_to(const struct bi_dict *d, const uint32_t *codes, int len)
{
	uint32_t v = 0;
	for (int i = 0; i < len; i++) {
		int ch = trie_child(&d->rd, v, codes[i]);
		if (ch < 0) return -1;
		v = (uint32_t)ch;
	}
	return (int)v;
}
