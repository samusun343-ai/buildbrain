/* train_rerank — host-side trainer for the M3 linear reranker.
 *
 * Structured (averaged) perceptron over (reading -> gold surface)
 * pairs: for each pair, the engine's top paths are collected with
 * their feature vectors (bi_internal_collect — the same feature
 * code the engine runs).  Scores are COSTS (the engine takes
 * the argmin), so when the model's argmin differs from the gold
 * path the update is w += phi(pred) - phi(gold): it raises the
 * predicted path's score and lowers the gold path's.  (Getting
 * this sign backwards makes the model learn to AVOID the gold
 * path — measured: train top-1 collapsed to 0.1%.)  The averaged
 * weights are written as BI_RERANK_NFEAT float32 values, ready
 * for tools/patch_rerank.py to embed into a .bid3 RNK section.
 *
 *   train_rerank <dict> <train.tsv> <weights-out.bin>
 *                [epochs] [dev.tsv]
 *
 * Pairs whose gold surface is not among the collected paths are
 * counted as coverage misses and skipped (the perceptron cannot
 * learn what the lattice cannot say).
 */
#define _POSIX_C_SOURCE 200809L
#include "brainime.h"
#include "../src/bi_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXPATHS BI_RERANK_MAXPATH

struct pair { char *reading, *gold; };

static struct pair *load_pairs(const char *path, int *out_n)
{
	FILE *f = fopen(path, "r");
	if (!f) { perror("fopen"); return NULL; }
	struct pair *arr = NULL;
	int n = 0, cap = 0;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = '\0';
		char *gold = tab + 1;
		gold[strcspn(gold, "\r\n")] = '\0';
		if (!line[0] || !gold[0]) continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 1024;
			arr = realloc(arr, cap * sizeof(*arr));
		}
		arr[n].reading = strdup(line);
		arr[n].gold = strdup(gold);
		n++;
	}
	fclose(f);
	*out_n = n;
	return arr;
}

static double score(const double *w, const double *f)
{
	double s = 0;
	for (int i = 0; i < BI_RERANK_NFEAT; i++) s += w[i] * f[i];
	return s;
}

/* top-1 accuracy of weight vector w over pairs (engine order on
 * ties = Viterbi order, same as the engine's stable behaviour) */
static double eval_acc(bi_engine *e, const struct pair *pairs, int n,
		       const double *w, int *out_cover)
{
	int hit = 0, cover = 0;
	for (int i = 0; i < n; i++) {
		bi_pathinfo paths[MAXPATHS];
		int np = bi_internal_collect(e, pairs[i].reading,
					     paths, MAXPATHS);
		int gold = -1, pred = -1;
		double best = 0;
		for (int j = 0; j < np; j++) {
			if (gold < 0 && !strcmp(paths[j].text, pairs[i].gold))
				gold = j;
			double s = score(w, paths[j].feat);
			if (pred < 0 || s < best) { best = s; pred = j; }
		}
		if (gold >= 0) cover++;
		if (pred >= 0 && gold == pred) hit++;
		bi_internal_free_paths(paths, np);
	}
	if (out_cover) *out_cover = cover;
	return n ? (double)hit / n : 0;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s <dict> <train.tsv> <weights-out> "
			"[epochs] [dev.tsv]\n", argv[0]);
		return 2;
	}
	int epochs = argc > 4 ? atoi(argv[4]) : 8;
	bi_engine *e = bi_engine_open(argv[1]);
	if (!e) { fprintf(stderr, "cannot open dictionary\n"); return 1; }
	int ntrain = 0;
	struct pair *train = load_pairs(argv[2], &ntrain);
	if (!train) return 1;
	printf("train: %d pairs, %d epochs\n", ntrain, epochs);
	fflush(stdout);
	/* M5 checkpointing variant: dev pairs are loaded once and
	 * evaluated after EVERY epoch with the averaged weights so
	 * far, which are also written to the output file after every
	 * epoch.  If the host kills the run, the last completed
	 * epoch's weights survive. */
	struct pair *devp = NULL;
	int ndevp = 0;
	if (argc > 5) devp = load_pairs(argv[5], &ndevp);

	double w[BI_RERANK_NFEAT] = { 0 };
	double wsum[BI_RERANK_NFEAT] = { 0 };
	w[0] = 1.0;	/* start from the M2 Viterbi order */
	long steps = 0;
	for (int ep = 0; ep < epochs; ep++) {
		int hit = 0, cover = 0, upd = 0;
		for (int i = 0; i < ntrain; i++) {
			bi_pathinfo paths[MAXPATHS];
			int np = bi_internal_collect(e, train[i].reading,
						     paths, MAXPATHS);
			int gold = -1, pred = -1;
			double best = 0;
			for (int j = 0; j < np; j++) {
				if (gold < 0 &&
				    !strcmp(paths[j].text, train[i].gold))
					gold = j;
				double s = score(w, paths[j].feat);
				if (pred < 0 || s < best) { best = s; pred = j; }
			}
			if (gold >= 0) {
				cover++;
				if (pred == gold) {
					hit++;
				} else if (pred >= 0) {
					/* argmin convention: penalise the
					 * predicted path, reward the gold */
					for (int k = 0; k < BI_RERANK_NFEAT; k++)
						w[k] += paths[pred].feat[k] -
							paths[gold].feat[k];
					upd++;
				}
			}
			for (int k = 0; k < BI_RERANK_NFEAT; k++)
				wsum[k] += w[k];
			steps++;
			bi_internal_free_paths(paths, np);
		}
		printf("epoch %d: train top-1 %.1f%% (coverage %d/%d, "
		       "%d updates)\n", ep + 1,
		       ntrain ? 100.0 * hit / ntrain : 0,
		       cover, ntrain, upd);
		fflush(stdout);
		{	/* checkpoint: averaged weights so far */
			double wa[BI_RERANK_NFEAT];
			for (int k = 0; k < BI_RERANK_NFEAT; k++)
				wa[k] = steps ? wsum[k] / steps : w[k];
			FILE *o = fopen(argv[3], "wb");
			if (o) {
				for (int k = 0; k < BI_RERANK_NFEAT; k++) {
					float f = (float)wa[k];
					fwrite(&f, 4, 1, o);
				}
				fclose(o);
			}
			if (devp) {
				double a1 = eval_acc(e, devp, ndevp, wa, NULL);
				printf("  ckpt epoch %d: dev top-1 %.1f%% "
				       "(weights saved)\n", ep + 1, 100.0 * a1);
				fflush(stdout);
			}
		}
	}
	double wavg[BI_RERANK_NFEAT];
	for (int k = 0; k < BI_RERANK_NFEAT; k++)
		wavg[k] = steps ? wsum[k] / steps : w[k];

	FILE *o = fopen(argv[3], "wb");
	if (!o) { perror("fopen out"); return 1; }
	for (int k = 0; k < BI_RERANK_NFEAT; k++) {
		float f = (float)wavg[k];
		fwrite(&f, 4, 1, o);
	}
	fclose(o);
	printf("weights (averaged):");
	for (int k = 0; k < BI_RERANK_NFEAT; k++) printf(" %.4f", wavg[k]);
	printf("\n");

	if (argc > 5) {
		int ndev = 0;
		struct pair *dev = load_pairs(argv[5], &ndev);
		if (dev) {
			double w0[BI_RERANK_NFEAT] = { 0 };
			w0[0] = 1.0;
			int cov = 0;
			double a0 = eval_acc(e, dev, ndev, w0, &cov);
			double a1 = eval_acc(e, dev, ndev, wavg, NULL);
			printf("dev: %d pairs, coverage %d, top-1 "
			       "viterbi %.1f%% -> reranked %.1f%%\n",
			       ndev, cov, 100.0 * a0, 100.0 * a1);
		}
	}
	bi_engine_close(e);
	return 0;
}
