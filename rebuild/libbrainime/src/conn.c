/* conn.c — connection-cost model.
 *
 * WHY THIS FILE EXISTS: the shipped BIDF v2 dictionary carries a
 * 256x256 connection section, but it is ALL ZERO (verified by
 * scanning the file: 0 non-zero bytes of 65,536).  The token stream
 * has only (surface, lid-class, word cost) — mozc's right-id was
 * dropped by convert_mozc.py.  So a Viterbi converter built on this
 * dictionary has no connection data unless the engine supplies it.
 * This file supplies it as an explicit, hand-built rule model over
 * the 52 POS classes (grouped into ~31 behaviour groups), in the
 * spirit of anthy's handcrafted costs.  If a future dictionary
 * build ships a real matrix (derived from mozc's connection data
 * remapped through the class table), bi_conn_cost() prefers it.
 *
 * SCALE: word cost = dictionary cost byte (0..255) * 16, i.e.
 * 0..4080.  Connection costs below live on the same scale:
 *   ~120-350  natural continuation
 *   ~600-1000 possible but disfavoured
 *   ~1500-2500 unlikely
 *   ~3500-6000 effectively forbidden (kept finite so the lattice
 *             always stays connected for weird input)
 *
 * KNOWN LIMITS (documented, measured by the regression suite):
 *   - Classes carry no conjugation-form distinction (mozc's lid
 *     encodes 連用形/終止形 as different ids; classify() folded
 *     them into one class per verb family).  The form check in
 *     form_adjust() recovers the most damaging cases （て/た/ます/
 *     ない …) from the phonology of the left reading's last kana
 *     plus the verb family class, but it is a heuristic.
 *   - Particle sequencing is a 10x10 subtype table, not data.
 *   - There is no word-bigram information at all; homophone choice
 *     inside a segment is pure dictionary word cost.
 */
#include "bi_internal.h"

/* behaviour groups */
enum {
	G_NOUN, G_PROP, G_SAHEN, G_NAADJ, G_NUM, G_PRON, G_NADV, G_NHIJI,
	G_SUFFIX, G_COUNTER,
	G_VERB, G_VAUX, G_ADJ, G_AAUX,
	G_ADV, G_RENTAI, G_CONJ, G_INTERJ, G_PREFIX,
	G_PCASE, G_PKAKARI, G_PFUKU, G_PSETSU, G_PFINAL, G_PFUKUKA,
	G_PRENTAI, G_PHEIRETSU, G_PSPECIAL, G_POTHER, G_AUXV,
	G_SYM, G_OTHER, G_N
};

static int class_group(int cls)
{
	switch (cls) {
	case 1: case 15: case 16: return G_NOUN;
	case 2: case 3: case 4: case 5: return G_PROP;
	case 6: return G_SAHEN;
	case 7: return G_NAADJ;
	case 8: return G_NUM;
	case 9: case 11: return G_SUFFIX;
	case 10: return G_COUNTER;
	case 12: return G_PRON;
	case 13: return G_NADV;
	case 14: return G_NHIJI;
	case 20: case 21: case 22: case 23: case 24: case 27: return G_VERB;
	case 25: case 26: return G_VAUX;
	case 30: return G_ADJ;
	case 31: case 32: return G_AAUX;
	case 40: case 41: return G_ADV;
	case 42: return G_RENTAI;
	case 43: return G_CONJ;
	case 44: case 45: case 46: return G_INTERJ;
	case 50: case 51: case 52: case 53: case 54: return G_PREFIX;
	case 60: return G_PCASE;
	case 61: return G_PKAKARI;
	case 62: case 69: return G_PFUKU;
	case 63: return G_PSETSU;
	case 64: return G_PFINAL;
	case 65: return G_PFUKUKA;
	case 66: return G_PRENTAI;
	case 67: return G_PHEIRETSU;
	case 68: return G_PSPECIAL;
	case 70: return G_POTHER;
	case 71: return G_AUXV;
	case 80: case 81: case 82: case 83: case 84: case 85: case 86:
	case 87: return G_SYM;
	default: return G_OTHER;
	}
}

static int is_particle(int g)
{
	return g >= G_PCASE && g <= G_POTHER;
}
static int is_nounfam(int g)
{
	return g == G_NOUN || g == G_PROP || g == G_SAHEN || g == G_NAADJ ||
	       g == G_NUM || g == G_PRON || g == G_NADV || g == G_NHIJI;
}

/* particle -> particle, subtype order:
 * CASE KAKARI FUKU SETSU FINAL FUKUKA RENTAI HEIRETSU SPECIAL OTHER */
static const short pp_table[10][10] = {
	/* CASE    */ {3200,  220,  500, 2500, 2800, 3000,  250, 2500, 2000, 1500},
	/* KAKARI  */ {3000, 3500,  800, 2600,  900, 3000, 2600, 2600, 2000, 1500},
	/* FUKU    */ { 400,  250, 1500, 2200, 1200, 2600,  400, 2000, 1500, 1200},
	/* SETSU   */ {1800,  500, 1200, 2200,  400, 2600, 2000, 2200, 1500, 1200},
	/* FINAL   */ {4000, 4000, 4000, 4000, 4000, 4000, 4000, 4000, 4000, 4000},
	/* FUKUKA  */ {3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000},
	/* RENTAI  */ { 300,  300,  600, 2000,  700, 2600, 3500, 2000, 1500, 1200},
	/* HEIRETSU*/ {1500, 1500, 1500, 2000, 1500, 2600, 1500, 2500, 1500, 1500},
	/* SPECIAL */ {2500, 1200, 1500, 2000,  800, 2600, 2000, 2000, 1500, 1500},
	/* OTHER   */ {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500},
};
static int pp_index(int g)
{
	switch (g) {
	case G_PCASE: return 0; case G_PKAKARI: return 1;
	case G_PFUKU: return 2; case G_PSETSU: return 3;
	case G_PFINAL: return 4; case G_PFUKUKA: return 5;
	case G_PRENTAI: return 6; case G_PHEIRETSU: return 7;
	case G_PSPECIAL: return 8; default: return 9;
	}
}

/* kana row of a codepoint: 0=a-row 1=i-row 2=u-row 3=e-row 4=o-row,
 * 5=sokuon/nasal (っ/ん), -1 = not a kana we classify */
static int kana_row(uint32_t cp)
{
	/* hiragana block offsets: あ=0 row a; pattern repeats every 10
	 * for あ..ぢ rows with small kana interleaved — use an explicit
	 * table for the common endings instead. */
	static const struct { uint32_t cp; int row; } tab[] = {
		{0x3042,0},{0x3044,1},{0x3046,2},{0x3048,3},{0x304A,4}, /* あいうえお */
		{0x304B,0},{0x304D,1},{0x304F,2},{0x3051,3},{0x3053,4}, /* かきくけこ */
		{0x3055,0},{0x3057,1},{0x3059,2},{0x305B,3},{0x305D,4}, /* さしすせそこ */
		{0x305F,0},{0x3061,1},{0x3064,2},{0x3066,3},{0x3068,4}, /* たちつてと */
		{0x306A,0},{0x306B,1},{0x306C,2},{0x306D,3},{0x306E,4}, /* なにぬねの */
		{0x306F,0},{0x3072,1},{0x3075,2},{0x3078,3},{0x307B,4}, /* はひふへほ */
		{0x307E,0},{0x307F,1},{0x3080,2},{0x3081,3},{0x3082,4}, /* まみむめも */
		{0x3084,0},{0x3086,2},{0x3088,4},                       /* やゆよ */
		{0x3089,0},{0x308A,1},{0x308B,2},{0x308C,3},{0x308D,4}, /* らりるれろ */
		{0x308F,0},{0x3092,4},                                   /* わを */
		{0x304C,0},{0x304E,1},{0x3050,2},{0x3052,3},{0x3054,4}, /* がぎぐげご */
		{0x3056,0},{0x3058,1},{0x305A,2},{0x305C,3},{0x305E,4}, /* ざじずぜぞ */
		{0x3060,0},{0x3062,1},{0x3065,2},{0x3067,3},{0x3069,4}, /* だぢづでど */
		{0x3070,0},{0x3073,1},{0x3076,2},{0x3079,3},{0x307C,4}, /* ばびぶべぼ */
		{0x3071,0},{0x3074,1},{0x3077,2},{0x307A,3},{0x307D,4}, /* ぱぴぷぺぽ */
		{0x3063,5},{0x3093,5},                                   /* っ ん */
	};
	for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
		if (tab[i].cp == cp) return tab[i].row;
	return -1;
}

/* Conjugation-form recovery.  Right-side forms that demand the
 * renyou (continuative) stem: て/で/た/だ/ます/たい/ながら/つつ and
 * auxiliary verbs (class VAUX).  Forms that demand the mizen
 * (irrealis) stem: ない/ぬ/れる/られる/せる/させる.
 * left_cls_is_verb_form is the left node's class (20=godan,
 * 21=ichidan, 22=sahen, 23=kahen, 30=adjective). */
static int form_adjust(int left_cls, const char *right_surface,
		       uint32_t left_last_cp, int right_group)
{
	static const char *renyou_forms[] = {
		"て", "で", "た", "だ", "ます", "たい", "ながら", "つつ",
		"ても", "でも", "たら", "だら", "たり", "だり", NULL
	};
	static const char *mizen_forms[] = {
		"ない", "ぬ", "れる", "られる", "せる", "させる", "まい", NULL
	};
	int want = 0;	/* 1 = renyou, 2 = mizen */
	for (int i = 0; renyou_forms[i]; i++)
		if (!strcmp(right_surface, renyou_forms[i])) want = 1;
	if (!want)
		for (int i = 0; mizen_forms[i]; i++)
			if (!strcmp(right_surface, mizen_forms[i])) want = 2;
	if (!want && right_group == G_VAUX) want = 1;
	if (!want) return 0;

	int row = kana_row(left_last_cp);
	if (left_cls == 20) {		/* godan: renyou = i-row/っ/ん, mizen = a-row */
		if (want == 1) return (row == 1 || row == 5) ? -150 : 2600;
		return row == 0 ? -150 : 2600;
	}
	if (left_cls == 21 || left_cls == 22 || left_cls == 23) {
		/* ichidan/sahen/kahen stems end in i/e-row kana */
		if (row == 1 || row == 3) return -150;
		return 2600;
	}
	if (left_cls == 30) {		/* adjective renyou = く-form */
		if (want == 1) return left_last_cp == 0x304F /* く */ ? -150 : 2600;
		return 0;
	}
	return 0;
}

/* Particle -> content-word connection.  A single "particle"
 * class cost cannot work: は (topic) is followed by any bunsetsu,
 * を (accusative) almost only by verbs, の (rentai-ka) exists to
 * precede nouns, で/に/へ/と/から each have their own profile.
 * The class gives the subtype; for case particles the surface
 * itself decides.  (This table replaced a flat 2600 for
 * particle->noun that made the converter flee to noun readings of
 * は (葉) to avoid paying it — see REPORT-m1.md.) */
static int particle_to_content(int lg, const char *ls, int rg)
{
	int noun = rg == G_NOUN || rg == G_PROP || rg == G_SAHEN ||
		   rg == G_NAADJ || rg == G_PRON || rg == G_NADV ||
		   rg == G_NHIJI || rg == G_NUM;
	switch (lg) {
	case G_PKAKARI:  return rg == G_VERB ? 300 : noun ? 500 : rg == G_ADJ ? 500 : 400;
	/* 副助詞 (だけ/しか/ほど/か): these FOLLOW their phrase;
	 * directly preceding a verb is the wrong order (読むだけ,
	 * not だけ読む), and a stray か + a complete verb form
	 * (か+来ました) must not undercut the compositional
	 * stem+aux path (かき+まし+た). */
	case G_PFUKU:    return rg == G_VERB ? 1400 : noun ? 500 : rg == G_ADJ ? 900 : 600;
	case G_PSETSU:   return rg == G_VERB ? 300 : noun ? 1000 : rg == G_ADJ ? 600 : 700;
	case G_PRENTAI:  return noun ? 120 : 2000;
	case G_PFUKUKA:  return rg == G_VERB ? 300 : noun ? 2000 : rg == G_ADJ ? 800 : 900;
	case G_PHEIRETSU:return noun ? 200 : rg == G_VERB ? 1500 : 1200;
	case G_PSPECIAL: return noun ? 800 : rg == G_VERB ? 1200 : 1000;
	case G_PFINAL:   return 3200;
	case G_PCASE:    break;	/* surface decides */
	default:         return rg == G_VERB ? 600 : noun ? 1500 : 900;
	}
	if (ls) {
		if (!strcmp(ls, "が")) return rg == G_VERB ? 300 : noun ? 450 : rg == G_ADJ ? 300 : 900;
		if (!strcmp(ls, "を")) return rg == G_VERB ? 220 : noun ? 2600 : rg == G_ADJ ? 1500 : 2600;
		if (!strcmp(ls, "に")) return rg == G_VERB ? 250 : noun ? 900 : rg == G_ADJ ? 700 : 1200;
		if (!strcmp(ls, "へ")) return rg == G_VERB ? 250 : noun ? 1500 : 1500;
		if (!strcmp(ls, "と")) return rg == G_VERB ? 300 : noun ? 600 : rg == G_ADJ ? 900 : 900;
		if (!strcmp(ls, "で")) return rg == G_VERB ? 280 : noun ? 800 : rg == G_ADJ ? 900 : 900;
		if (!strcmp(ls, "から")) return rg == G_VERB ? 450 : noun ? 900 : rg == G_ADJ ? 700 : 900;
		if (!strcmp(ls, "まで")) return rg == G_VERB ? 450 : noun ? 900 : 900;
		if (!strcmp(ls, "より")) return rg == G_VERB ? 500 : noun ? 900 : rg == G_ADJ ? 500 : 900;
	}
	return rg == G_VERB ? 350 : noun ? 1800 : 900;
}

int bi_conn_cost(const struct bi_dict *d, int left_cls, int right_cls,
		 uint32_t left_last_cp, uint32_t right_last_cp,
		 const char *left_surface, const char *right_surface)
{
	/* A future dictionary with a real matrix wins.  BIDF stores
	 * int8 costs at resolution ~64 per unit (mozc scheme); scale
	 * them onto the engine's scale. */
	if (d && d->conn_from_dict && d->conn &&
	    left_cls != BI_CLS_BOS) {
		int v = d->conn[left_cls * 256 + right_cls];
		if (v != 0) return v * 64;
	}

	if (left_cls == BI_CLS_BOS) {
		int rg = class_group(right_cls);
		if (is_nounfam(rg) || rg == G_VERB || rg == G_ADJ ||
		    rg == G_ADV || rg == G_RENTAI || rg == G_CONJ ||
		    rg == G_INTERJ || rg == G_PREFIX || rg == G_NAADJ)
			return 120;
		if (rg == G_SYM) return 800;
		return 4500;
	}

	int lg = class_group(left_cls), rg = class_group(right_cls);
	int cost;

	if (is_particle(lg) && is_particle(rg))
		return pp_table[pp_index(lg)][pp_index(rg)];

	if (is_particle(rg)) {
		if (is_nounfam(lg) || lg == G_VERB || lg == G_ADJ ||
		    lg == G_ADV || lg == G_VAUX || lg == G_AAUX)
			cost = 260;
		else if (lg == G_SUFFIX || lg == G_COUNTER)
			cost = 300;
		else if (lg == G_AUXV)
			cost = rg == G_PFINAL ? 250 : 400;
		else if (lg == G_SYM)
			cost = 3000;
		else
			cost = 4200;
		if (rg == G_PRENTAI) {	/* の: needs a rentai-capable left */
			if (lg == G_NOUN || lg == G_PROP || lg == G_SAHEN ||
			    lg == G_VERB || lg == G_ADJ || lg == G_NADV)
				cost = 200;
			else if (!is_particle(lg))
				cost = 3500;
		}
		if (rg == G_PFUKUKA) {	/* に (adverbial): 静かに / 本当に */
			if (lg == G_NAADJ) cost = 120;
			else if (lg == G_ADJ) cost = 250;
		}
		/* て/で are connection particles (class 63) but still
		 * demand the renyou stem — apply the form check here
		 * too, not only in the switch below. */
		if (((left_cls >= 20 && left_cls <= 27) || left_cls == 30) &&
		    right_surface)
			cost += form_adjust(left_cls, right_surface,
					    left_last_cp, rg);
		if (cost < 0) cost = 0;
		return cost;
	}

	switch (rg) {
	case G_AUXV:
		if (lg == G_VERB) cost = 200;
		else if (is_nounfam(lg)) cost = 220;	/* だ/です after nouns */
		else if (lg == G_ADJ) cost = 250;
		else if (lg == G_AUXV) cost = 300;
		else if (lg == G_VAUX || lg == G_AAUX) cost = 400;
		else if (is_particle(lg)) cost = 3500;
		else cost = 1500;
		break;
	case G_VERB:
		if (lg == G_SAHEN) cost = 150;		/* 勉強する */
		else if (lg == G_ADV) cost = 200;
		else if (is_particle(lg))
			cost = particle_to_content(lg, left_surface, rg);
		else if (lg == G_PREFIX) cost = 300;
		else if (lg == G_ADJ) cost = 700;	/* 高くなる/する */
		else if (lg == G_VERB) cost = 900;	/* compound verbs */
		else if (lg == G_CONJ) cost = 1200;
		else if (is_nounfam(lg)) cost = 2600;
		else cost = 2000;
		break;
	case G_VAUX:
		if (lg == G_VERB) cost = 180;
		else if (lg == G_ADJ) cost = 900;
		else cost = 2800;
		break;
	case G_NOUN: case G_PROP: case G_SAHEN: case G_NAADJ:
	case G_PRON: case G_NADV: case G_NHIJI:
		if (lg == G_PRENTAI) cost = 120;	/* の + noun */
		else if (lg == G_RENTAI) cost = 150;
		else if (lg == G_PREFIX) cost = 200;
		else if (lg == G_VERB) cost = 400;	/* rentai: 食べる人 */
		else if (lg == G_ADJ) cost = 300;	/* rentai: 高い山 */
		else if (lg == G_NOUN) cost = 1050;	/* two separate noun
							   entries are
							   usually two
							   bunsetsu, not a
							   compound */
		else if (lg == G_PROP) cost = 850;
		else if (lg == G_SAHEN) cost = 700;
		else if (lg == G_NUM) cost = 800;
		else if (lg == G_ADV) cost = 1800;
		else if (is_particle(lg))
			cost = particle_to_content(lg, left_surface, rg);
		else if (lg == G_SYM) cost = 1200;
		else cost = 1600;
		break;
	case G_NUM:
		if (lg == G_PREFIX) cost = 150;
		else if (lg == G_NUM) cost = 500;
		else cost = 1800;
		break;
	case G_COUNTER:
		cost = lg == G_NUM ? 120 : 2500;
		break;
	case G_SUFFIX:
		if (lg == G_PROP) cost = 150;		/* 田中さん */
		else if (is_nounfam(lg)) cost = 200;
		else if (lg == G_ADJ) cost = 300;	/* 高さ */
		else if (lg == G_VERB) cost = 400;	/* 書き方 */
		else cost = 2200;
		break;
	case G_ADJ:
		if (lg == G_ADV) cost = 250;
		else if (is_particle(lg))
			cost = particle_to_content(lg, left_surface, rg);
		else if (lg == G_PREFIX) cost = 300;
		else if (lg == G_CONJ) cost = 1000;
		else cost = 2400;
		break;
	case G_AAUX:
		if (lg == G_VERB) cost = 250;		/* 食べたい/すぎる系 */
		else if (lg == G_ADJ) cost = 300;
		else cost = 2400;
		break;
	case G_ADV:
		if (lg == G_CONJ) cost = 500;
		else if (is_particle(lg))
			cost = particle_to_content(lg, left_surface, rg);
		else if (lg == G_ADV) cost = 900;
		else if (lg == G_INTERJ) cost = 800;
		else cost = 2200;
		break;
	case G_RENTAI:
		cost = lg == G_CONJ ? 2000 : 3500;
		break;
	case G_CONJ:
		if (lg == G_AUXV) cost = 500;
		else if (lg == G_VERB || lg == G_ADJ) cost = 600;
		else if (is_nounfam(lg) || is_particle(lg)) cost = 700;
		else cost = 1500;
		break;
	case G_INTERJ:
		cost = 2500;
		break;
	case G_PREFIX:
		cost = is_particle(lg) ? 1500 : 2500;
		break;
	case G_SYM:
		cost = 400;
		break;
	default:
		cost = 1600;
		break;
	}

	/* conjugation-form recovery for verb/adjective lefts */
	if ((left_cls >= 20 && left_cls <= 27) || left_cls == 30) {
		if (right_surface)
			cost += form_adjust(left_cls, right_surface,
					    left_last_cp, rg);
	}

	/* NOTE: a particle->renyou-stem penalty (by right kana
	 * row) was tried and REJECTED by A/B measurement on the
	 * regression corpus: it fixed nothing and broke
	 * は+休み (にちようびはやすみです).  See REPORT-m1.md. */
	(void)right_last_cp;
	if (cost < 0) cost = 0;
	return cost;
}

int bi_conn_eos(int left_cls)
{
	int lg = class_group(left_cls);
	if (is_nounfam(lg) || lg == G_VERB || lg == G_ADJ ||
	    lg == G_AUXV || lg == G_PFINAL || lg == G_SYM ||
	    lg == G_INTERJ)
		return 150;
	if (lg == G_VAUX || lg == G_AAUX) return 300;
	if (lg == G_ADV) return 900;
	if (lg == G_PSETSU) return 1200;	/* 行って。 (imperative) */
	if (lg == G_PHEIRETSU) return 2000;
	return 4500;
}
