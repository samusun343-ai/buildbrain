/* bidict_lookup2 — mmap a BIDF v2 dictionary and list conversion
 * candidates for a reading (exact match, or prefix search with -p).
 * The v2 candidate-generation half of the future libbrainime; ranking
 * beyond stored cost (connection costs, learning) comes with the
 * Viterbi converter.
 *
 * Usage: bidict_lookup2 dict.bidf [-p] <reading>
 *
 * LOUDS recap (see tools/ime/build_bidict2.py): the louds bit array
 * starts with a 1 (the root's virtual edge); then for each node in
 * BFS order, one 1 per child followed by one 0. Node v's children
 * are the 1-bits between select0(v-1) and select0(v), where
 * select0(-1) is position 0; a 1-bit at position p is the edge to
 * node rank1(p)-1. Parent of node v>=1: p = select1(v+1), parent =
 * rank0(p) counting zeros in [0, p).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct hdr { char magic[4]; uint32_t ver, n_entries, n_readings, n_surfaces,
	cb, rd, sf, tok, conn; };

struct trie {
	uint32_t n_nodes, label_bytes, n_term;
	const uint8_t *louds;	/* 2*n_nodes bits, MSB-first */
	const uint8_t *labels;	/* (n_nodes-1) entries, indexed by node id - 1 */
	const uint8_t *terminal;/* n_nodes bits */
	const uint8_t *counts;	/* reading trie only: n_term bytes */
	const uint32_t *samples;/* reading trie only */
};

static const uint8_t *g_base;
static struct hdr g_h;
static const char **g_book;	/* code -> UTF-8 string */
static uint32_t g_nchars;
static struct trie g_rd, g_sf;

static int bit_get(const uint8_t *bits, uint32_t p)
{
	return (bits[p >> 3] >> (7 - (p & 7))) & 1;
}

/* #1-bits in bits[0..p] inclusive; p == (uint32_t)-1 -> 0 */
static uint32_t rank1(const uint8_t *bits, uint32_t p)
{
	if (p == (uint32_t)-1) return 0;
	uint32_t n = 0;
	uint32_t full = p >> 3;
	for (uint32_t i = 0; i < full; i++)
		n += __builtin_popcount(bits[i]);
	uint32_t rem = p & 7;
	uint8_t last = bits[full] & (uint8_t)(0xFF << (7 - rem));
	n += __builtin_popcount(last);
	return n;
}

/* #0-bits in bits[0..p) */
static uint32_t rank0_excl(const uint8_t *bits, uint32_t p)
{
	if (p == 0) return 0;
	return p - rank1(bits, p - 1);
}

/* position of the k-th 1-bit (k 1-indexed), or -1 */
static int select1(const uint8_t *bits, uint32_t nbits, uint32_t k)
{
	uint32_t seen = 0;
	for (uint32_t p = 0; p < nbits; p++) {
		if (bit_get(bits, p) && ++seen == k) return (int)p;
	}
	return -1;
}

/* position of the k-th 0-bit (k 0-indexed), or -1 */
static int select0(const uint8_t *bits, uint32_t nbits, uint32_t k)
{
	uint32_t seen = 0;
	for (uint32_t p = 0; p < nbits; p++) {
		if (!bit_get(bits, p) && seen++ == k) return (int)p;
	}
	return -1;
}

static uint32_t trie_label(const struct trie *t, uint32_t node)
{
	if (t->label_bytes == 1)
		return t->labels[node - 1];
	const uint8_t *p = t->labels + (node - 1) * 2;
	return p[0] | (p[1] << 8);
}

/* Find child of v with the given label; -1 if none. Children are
 * stored in ascending label order, so stop at the first larger one. */
static int trie_child(const struct trie *t, uint32_t v, uint32_t label)
{
	uint32_t nbits = 2 * t->n_nodes;
	int lo = v > 0 ? select0(t->louds, nbits, v - 1) : 0;
	int hi = select0(t->louds, nbits, v);
	for (int p = lo + 1; p < hi; p++) {
		if (!bit_get(t->louds, p)) continue;
		uint32_t child = rank1(t->louds, p) - 1;
		uint32_t lab = trie_label(t, child);
		if (lab == label) return (int)child;
		if (lab > label) return -1;
	}
	return -1;
}

static int trie_walk(const struct trie *t, const uint32_t *codes, int n)
{
	uint32_t v = 0;
	for (int i = 0; i < n; i++) {
		int c = trie_child(t, v, codes[i]);
		if (c < 0) return -1;
		v = (uint32_t)c;
	}
	return (int)v;
}

static int trie_parent(const struct trie *t, uint32_t v)
{
	int p = select1(t->louds, 2 * t->n_nodes, v + 1);
	return (int)rank0_excl(t->louds, (uint32_t)p);
}

/* UTF-8 char length from its lead byte */
static int utf8_len(unsigned char c)
{
	if (c < 0x80) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	return 4;
}

/* Encode a UTF-8 string to codes via the codebook; returns count or -1. */
static int encode(const char *s, uint32_t *codes, int max)
{
	int n = 0;
	while (*s) {
		int ln = utf8_len((unsigned char)*s);
		uint32_t found = (uint32_t)-1;
		for (uint32_t c = 0; c < g_nchars; c++) {
			if ((int)strlen(g_book[c]) == ln && !memcmp(g_book[c], s, ln)) {
				found = c;
				break;
			}
		}
		if (found == (uint32_t)-1) return -1;
		if (n >= max) return -1;
		codes[n++] = found;
		s += ln;
	}
	return n;
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

/* Decode surface id to a UTF-8 string in buf (buf must hold it). */
static void surface_str(uint32_t sid, char *buf, size_t bufsz)
{
	uint32_t node = (uint32_t)select1(g_sf.terminal, g_sf.n_nodes, sid + 1);
	uint32_t path[64];
	int depth = 0;
	while (node > 0) {
		path[depth++] = trie_label(&g_sf, node);
		node = (uint32_t)trie_parent(&g_sf, node);
	}
	size_t off = 0;
	for (int i = depth - 1; i >= 0; i--) {
		const char *s = g_book[path[i]];
		size_t ln = strlen(s);
		if (off + ln < bufsz) memcpy(buf + off, s, ln);
		off += ln;
	}
	buf[off < bufsz ? off : bufsz - 1] = '\0';
}

static uint32_t first_token(uint32_t rank)
{
	uint32_t block = rank / 1024;
	uint32_t first = g_rd.samples[block];
	for (uint32_t r = block * 1024; r < rank; r++)
		first += g_rd.counts[r];
	return first;
}

static void print_candidates(uint32_t node)
{
	uint32_t rank = rank1(g_rd.terminal, node - 1);
	uint32_t first = first_token(rank);
	uint32_t count = g_rd.counts[rank];
	const uint8_t *p = g_base + g_h.tok;
	/* tokens are sequential; walk from the section start. For a
	 * lookup tool this linear scan is fine (180k entries). */
	uint32_t idx = 0;
	char sbuf[256];
	while (idx < first + count) {
		uint32_t sid = uleb(&p);
		uint8_t pos = *p++;
		uint8_t cost = *p++;
		if (idx >= first) {
			surface_str(sid, sbuf, sizeof(sbuf));
			printf("%s\t%d\t%d\n", sbuf, pos, cost);
		}
		idx++;
	}
}

static void print_candidates_prefixed(const char *reading, uint32_t node)
{
	/* same as print_candidates but with the "reading => " prefix */
	uint32_t rank = rank1(g_rd.terminal, node - 1);
	uint32_t first = first_token(rank);
	uint32_t count = g_rd.counts[rank];
	const uint8_t *p = g_base + g_h.tok;
	uint32_t idx = 0;
	char sbuf[256];
	while (idx < first + count) {
		uint32_t sid = uleb(&p);
		uint8_t pos = *p++;
		uint8_t cost = *p++;
		if (idx >= first) {
			surface_str(sid, sbuf, sizeof(sbuf));
			printf("%s => %s\t%d\t%d\n", reading, sbuf, pos, cost);
		}
		idx++;
	}
}

static void dfs_prefix(uint32_t node, uint32_t *path, int depth)
{
	if (bit_get(g_rd.terminal, node)) {
		char rbuf[256];
		size_t off = 0;
		for (int i = 0; i < depth; i++) {
			const char *s = g_book[path[i]];
			size_t ln = strlen(s);
			if (off + ln < sizeof(rbuf)) memcpy(rbuf + off, s, ln);
			off += ln;
		}
		rbuf[off < sizeof(rbuf) ? off : sizeof(rbuf) - 1] = '\0';
		print_candidates_prefixed(rbuf, node);
	}
	uint32_t nbits = 2 * g_rd.n_nodes;
	int lo = node > 0 ? select0(g_rd.louds, nbits, node - 1) : 0;
	int hi = select0(g_rd.louds, nbits, node);
	for (int p = lo + 1; p < hi; p++) {
		if (!bit_get(g_rd.louds, p)) continue;
		uint32_t child = rank1(g_rd.louds, p) - 1;
		path[depth] = trie_label(&g_rd, child);
		dfs_prefix(child, path, depth + 1);
	}
}

static const uint8_t *parse_trie(struct trie *t, const uint8_t *p, int has_counts)
{
	const uint8_t *q = p;
	t->n_nodes = q[0] | (q[1] << 8) | (q[2] << 16) | (q[3] << 24); q += 4;
	t->label_bytes = q[0] | (q[1] << 8) | (q[2] << 16) | (q[3] << 24); q += 4;
	t->n_term = q[0] | (q[1] << 8) | (q[2] << 16) | (q[3] << 24); q += 4;
	t->louds = q; q += (2 * t->n_nodes + 7) / 8;
	t->labels = q; q += (t->n_nodes - 1) * t->label_bytes;
	t->terminal = q; q += (t->n_nodes + 7) / 8;
	if (has_counts) {
		t->counts = q; q += t->n_term;
		t->samples = (const uint32_t *)q;
		q += 4 * ((t->n_term + 1023) / 1024);
	} else {
		t->counts = NULL;
		t->samples = NULL;
	}
	return q;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s dict.bidf [-p] reading\n", argv[0]); return 2; }
	int prefix = argc > 3 && !strcmp(argv[2], "-p");
	const char *reading = argv[argc - 1];

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }
	struct stat st; fstat(fd, &st);
	g_base = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (g_base == MAP_FAILED) { perror("mmap"); return 1; }
	memcpy(&g_h, g_base, sizeof(g_h));
	if (memcmp(g_h.magic, "BIDF", 4) || g_h.ver != 2) {
		fprintf(stderr, "not a BIDF v2 file\n");
		return 1;
	}
	/* codebook */
	const uint8_t *p = g_base + g_h.cb;
	g_nchars = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); p += 4;
	g_book = malloc(g_nchars * sizeof(char *));
	for (uint32_t i = 0; i < g_nchars; i++) {
		int ln = *p++;
		char *s = malloc(ln + 1);
		memcpy(s, p, ln);
		s[ln] = '\0';
		g_book[i] = s;
		p += ln;
	}
	const uint8_t *end = parse_trie(&g_rd, g_base + g_h.rd, 1);
	if (end != g_base + g_h.sf) { fprintf(stderr, "reading trie size mismatch\n"); return 1; }
	end = parse_trie(&g_sf, g_base + g_h.sf, 0);
	if (end != g_base + g_h.tok) { fprintf(stderr, "surface trie size mismatch\n"); return 1; }

	uint32_t codes[64];
	int n = encode(reading, codes, 64);
	if (n < 0) return 0; /* unreadable reading: no candidates */
	int node = trie_walk(&g_rd, codes, n);
	if (!prefix) {
		if (node < 0 || !bit_get(g_rd.terminal, (uint32_t)node))
			return 0;
		print_candidates((uint32_t)node);
		return 0;
	}
	if (node < 0) return 0;
	uint32_t path[64];
	memcpy(path, codes, sizeof(uint32_t) * n);
	dfs_prefix((uint32_t)node, path, n);
	return 0;
}
