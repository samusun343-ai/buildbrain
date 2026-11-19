/* bidict_lookup — mmap a BIDF v1 dictionary and list conversion
 * candidates for a reading (exact match, or prefix search with -p).
 * This is the candidate-generation half of the future libbrainime;
 * ranking beyond stored cost (connection costs, learning) comes
 * with the Viterbi converter.
 *
 * Usage: bidict_lookup dict.bidf [-p] <reading>
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

struct hdr { char magic[4]; uint32_t ver, count, idx, tok, pool, conn, nread; };
struct idxent { uint32_t roff, first; uint16_t count, pad; };
struct tok { uint32_t soff; uint8_t pos, cost; uint16_t pad; };

static const char *pool_str(const char *pool, uint32_t off) { return pool + off; }

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s dict.bidf [-p] reading\n", argv[0]); return 2; }
	int prefix = argc > 3 && !strcmp(argv[2], "-p");
	const char *reading = argv[argc - 1];

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }
	struct stat st; fstat(fd, &st);
	const uint8_t *base = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (base == MAP_FAILED) { perror("mmap"); return 1; }
	const struct hdr *h = (const struct hdr *)base;
	if (memcmp(h->magic, "BIDF", 4) || h->ver != 1) { fprintf(stderr, "not a BIDF v1 file\n"); return 1; }
	const struct idxent *idx = (const struct idxent *)(base + h->idx);
	const struct tok *toks = (const struct tok *)(base + h->tok);
	const char *pool = (const char *)(base + h->pool);
	int nread = (int)h->nread;
	int lo = 0, hi = nread - 1, found = -1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int cmp = strcmp(reading, pool_str(pool, idx[mid].roff));
		if (!prefix && cmp == 0) { found = mid; break; }
		if (cmp < 0) hi = mid - 1; else { found = mid; lo = mid + 1; }
	}
	if (!prefix) {
		if (found < 0 || strcmp(reading, pool_str(pool, idx[found].roff)))
			return 0; /* no candidates */
		for (int k = 0; k < idx[found].count; k++) {
			const struct tok *t = &toks[idx[found].first + k];
			printf("%s\t%d\t%d\n", pool_str(pool, t->soff), t->pos, t->cost);
		}
		return 0;
	}
	/* prefix: back up to the first reading with this prefix, then walk */
	while (found > 0 && strncmp(pool_str(pool, idx[found - 1].roff), reading, strlen(reading)) >= 0
	       && strncmp(pool_str(pool, idx[found - 1].roff), reading, strlen(reading)) == 0)
		found--;
	if (found < 0) found = 0;
	for (int i = found; i < nread; i++) {
		const char *r = pool_str(pool, idx[i].roff);
		if (strncmp(r, reading, strlen(reading))) {
			if (strcmp(r, reading) > 0 && i > found) break;
			continue;
		}
		for (int k = 0; k < idx[i].count; k++) {
			const struct tok *t = &toks[idx[i].first + k];
			printf("%s => %s\t%d\t%d\n", r, pool_str(pool, t->soff), t->pos, t->cost);
		}
	}
	return 0;
}
