/* bd-web: braindesk's own web browser (M1).
 * In-house engine on libbrainui: fetch with curl, a small
 * HTML parser producing blocks (headings, paragraphs, lists,
 * images), a wrapping layout with tappable link regions,
 * history, and bookmarks. Forms and a CSS subset are M2.
 * M3 embeds QuickJS (third_party/quickjs, v0.18.0): inline
 * <script> runs after the page loads; console.log and a live
 * document.title are exposed, external scripts and the DOM are
 * still out of scope. <select> (tap cycles options) and
 * <textarea> (multi-line OSK editing) are M3 follow-up.
 * M3 follow-up 2: form POST (application/x-www-form-urlencoded
 * via curl --data-binary); unknown methods report 未対応.
 */
#include "bdapp.h"
#include "bd_js.h"
#include "../bui_osk.h"
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb_image.h"

#define MAXBLOCKS 512
#define MAXRUNS 4096
#define MAXLINKS 512
#define MAXIMGS 64
#define BODYMAX (2 * 1024 * 1024)
/* content column (the window is 864 wide; 32px side margins) */
#define PAGE_W 800
#define PAGE_X 32

enum { B_PARA, B_HEAD, B_ITEM, B_PRE, B_IMG, B_HR, B_INPUT, B_SUBMIT, B_SELECT, B_TEXTAREA };

struct run {
	int block;
	int off;		/* byte offset into block text */
	int len;
	char href[512];		/* empty = not a link */
	bool bold;
	int color;		/* 0xRRGGBB from CSS, -1 = default */
	bool download;		/* <a download> (or a download-type URL) */
};

struct lineseg {
	int run;		/* run index in the block, -1 = plain text span */
	int off, len;
};

struct wline {
	struct lineseg segs[24];
	int nsegs;
};

struct block {
	int type;
	int level;		/* heading level */
	char *text;		/* malloc'd, NUL-terminated */
	int text_len;
	struct run *runs;
	int nruns;
	char img_src[512];
	/* CSS subset results (style_for at parse time) */
	int color;		/* 0xRRGGBB, -1 = default */
	int bg;			/* 0xRRGGBB, -1 = transparent */
	float font_scale;
	int align;		/* 0 left, 1 center, 2 right */
	bool hidden;		/* display:none */
	/* form membership (B_INPUT / B_SUBMIT / B_SELECT / B_TEXTAREA) */
	int form;		/* form index, -1 none */
	int field;		/* field index in the form, -1 none */
	int rows;		/* B_TEXTAREA: rows attribute (default 3) */
	/* flexbox (CSS display:flex): a block whose style says so is a
	 * container; blocks created while it is open record it as parent.
	 * Children are laid out by layout_flex(), not the main loop. */
	bool flex;
	int flex_dir;		/* 0 row, 1 column */
	int justify;		/* 0 flex-start, 1 center, 2 flex-end, 3 space-between, 4 space-around */
	int flex_align;		/* 0 stretch, 1 flex-start, 2 center, 3 flex-end */
	int gap;		/* px between items */
	float flex_grow;	/* item growth factor (from its own style) */
	int parent;		/* enclosing flex container block, -1 none */
	bool in_flex;		/* laid out by a container this pass */
	/* layout results */
	int x, y, w, h;
	struct wline *lines;
	int nlines;
	unsigned char *img_px;	/* decoded RGBA (scaled) */
	int img_w, img_h;
	bool img_tried;
};

struct linkregion {
	int x, y, w, h;
	char href[512];
	bool download;
};

struct tapregion {
	int x, y, w, h;
	int block;
};

#define MAXFORMS 4
#define MAXFIELDS 16
struct formfield {
	char name[64];
	char value[512];
	int block;		/* block index, -1 = hidden field */
	/* <select> options (max 8); value = opt_val[sel] */
	char opt_label[8][64];
	char opt_val[8][64];
	int nopts;
	int sel;
};
struct form {
	char action[1024];
	char method[8];
	struct formfield fields[MAXFIELDS];
	int nfields;
};

/* CSS subset: element / .class / #id selectors, a handful of
 * properties. Specificity order: tag < class < id < inline. */
struct cssrule {
	char sel[48];
	int kind;		/* 0 tag, 1 class, 2 id */
	int color, bg;		/* -1 = unset */
	float fscale;		/* 0 = unset */
	int align;		/* -1 = unset */
	int bold;		/* -1 unset, 0 normal, 1 bold */
	bool hide;
	/* flexbox (display:flex and friends; -1 = unset) */
	int display;		/* 0 block/other, 1 flex */
	int flex_dir;		/* 0 row, 1 column */
	float flex_grow;	/* -1 = unset */
	int justify;		/* 0 flex-start, 1 center, 2 flex-end, 3 space-between, 4 space-around */
	int flex_align;		/* 0 stretch, 1 flex-start, 2 center, 3 flex-end */
	int gap;		/* px, -1 = unset */
};

/* CSS custom properties (--name: value), collected from stylesheet
 * rules (usually :root). Stored raw; var() is expanded when a
 * declaration value is read. Scoping is global (a v1 approximation:
 * real sites define them on :root). */
struct cssvar {
	char name[48];
	char value[256];
};

struct web {
	char url[1024];
	char title[256];
	struct block blocks[MAXBLOCKS];
	int nblocks;
	struct linkregion links[MAXLINKS];
	int nlinks;
	int content_h;
	int scroll;
	char hist[32][1024];
	int hist_len, hist_pos;
	bool hist_nav;
	char status[160];
	/* url editing */
	bool editing;
	char edit[1024];
	bu_osk osk;
	char cache_dir[512];
	/* forms + CSS (M2) */
	struct form forms[MAXFORMS];
	int nforms;
	struct tapregion taps[MAXLINKS];
	int ntaps;
	int edit_field;		/* block index being edited, -1 = URL bar */
	struct cssrule rules[64];
	int nrules;
	struct cssvar vars[32];
	int nvars;
	/* page find (find-in-page): query + match list */
	bool finding;
	char find_query[256];
	struct { int block; int off; int len; } find_hits[256];
	int nfind_hits, find_cur;
	bool find_scroll_pending;	/* jump after the next layout() */
	/* inline scripts, collected at parse time (M3) */
	char js_src[65536];
	int js_len;
	int js_ext;		/* external <script src> count, skipped */
};

/* Viewport for media queries: the device display is 854x480. */
#define VIEWPORT_W 854
#define VIEWPORT_H 480

static char *body_buf;
static int body_len;

/* ---------- fetch ---------- */
/* post_body != NULL: POST the body as application/x-www-form-urlencoded
 * (curl --data-binary @file sets the content type); NULL = plain GET. */
static int fetch_impl(struct web *w, const char *url, const char *post_body,
		      char *effective, size_t en)
{
	char tmp[600], cmd[2300];
	snprintf(tmp, sizeof(tmp), "%s/fetch.html", w->cache_dir);
	if (post_body) {
		char pb[600];
		snprintf(pb, sizeof(pb), "%s/postbody.txt", w->cache_dir);
		FILE *fp = fopen(pb, "w");
		if (!fp)
			return -1;
		fputs(post_body, fp);
		fclose(fp);
		snprintf(cmd, sizeof(cmd),
			 "curl -sL --max-time 25 -A 'Mozilla/5.0 (X11; Linux) bd-web' -o '%s' -w '%%{url_effective}' --data-binary @'%s' '%s' 2>/dev/null",
			 tmp, pb, url);
	} else {
		snprintf(cmd, sizeof(cmd),
			 "curl -sL --max-time 25 -A 'Mozilla/5.0 (X11; Linux) bd-web' -o '%s' -w '%%{url_effective}' '%s' 2>/dev/null",
			 tmp, url);
	}
	FILE *pp = popen(cmd, "r");
	if (!pp)
		return -1;
	char eff[1024] = "";
	if (!fgets(eff, sizeof(eff), pp))
		eff[0] = 0;
	pclose(pp);
	snprintf(effective, en, "%s", eff[0] ? eff : url);
	FILE *fp = fopen(tmp, "rb");
	if (!fp)
		return -1;
	body_len = (int)fread(body_buf, 1, BODYMAX - 1, fp);
	fclose(fp);
	body_buf[body_len] = 0;
	return 0;
}

static int fetch(struct web *w, const char *url, char *effective, size_t en)
{
	return fetch_impl(w, url, NULL, effective, en);
}

static int fetch_post(struct web *w, const char *url, const char *body,
		      char *effective, size_t en)
{
	return fetch_impl(w, url, body, effective, en);
}

/* ---------- URL resolution ---------- */
static void resolve_url(const char *base, const char *rel, char *out, size_t n)
{
	if (strncmp(rel, "http://", 7) == 0 || strncmp(rel, "https://", 8) == 0) {
		snprintf(out, n, "%s", rel);
		return;
	}
	/* find scheme://host */
	const char *host_end = strstr(base, "://");
	if (!host_end) {
		snprintf(out, n, "%s", rel);
		return;
	}
	host_end += 3;
	const char *path = strchr(host_end, '/');
	char prefix[1024];
	if (rel[0] == '/' && rel[1] == '/') {
		snprintf(out, n, "%.*s:%s", (int)(strstr(base, "://") - base), base, rel);
		return;
	}
	if (rel[0] == '/') {
		if (path)
			snprintf(prefix, sizeof(prefix), "%.*s", (int)(path - base), base);
		else
			snprintf(prefix, sizeof(prefix), "%s", base);
		snprintf(out, n, "%s%s", prefix, rel);
		return;
	}
	/* relative to the base's directory */
	if (path) {
		const char *last = strrchr(path, '/');
		snprintf(prefix, sizeof(prefix), "%.*s", (int)(last - base) + 1, base);
	} else {
		snprintf(prefix, sizeof(prefix), "%s/", base);
	}
	/* handle ../ and ./ minimally */
	while (strncmp(rel, "../", 3) == 0) {
		rel += 3;
		int l = (int)strlen(prefix);
		if (l > 1) {
			prefix[l - 1] = 0;
			char *s = strrchr(prefix, '/');
			if (s)
				s[1] = 0;
		}
	}
	if (strncmp(rel, "./", 2) == 0)
		rel += 2;
	snprintf(out, n, "%s%s", prefix, rel);
}

/* ---------- HTML parsing ---------- */
static void decode_entities(char *s)
{
	char *r = s, *w = s;
	while (*r) {
		if (*r == '&') {
			if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; continue; }
			if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; continue; }
			if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; continue; }
			if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"'; r += 6; continue; }
			if (strncmp(r, "&nbsp;", 6) == 0) { *w++ = ' '; r += 6; continue; }
			if (strncmp(r, "&#", 2) == 0) {
				int code = atoi(r + 2);
				const char *semi = strchr(r, ';');
				if (semi && code > 0 && code < 128) {
					*w++ = (char)code;
					r = (char *)semi + 1;
					continue;
				}
			}
		}
		*w++ = *r++;
	}
	*w = 0;
}

/* Drop private-use icon-font glyphs and collapse whitespace
 * (WordPress themes sprinkle PUA icons everywhere; the device
 * font has no glyphs for them, so they would render as tofu). */
static void sanitize_text(char *s, bool pre)
{
	char *r = s, *w = s;
	bool last_space = false;
	while (*r) {
		unsigned char c0 = (unsigned char)*r;
		int clen = 1;
		unsigned long cp = c0;
		if (c0 >= 0xF0) { clen = 4; cp = c0 & 0x07; }
		else if (c0 >= 0xE0) { clen = 3; cp = c0 & 0x0F; }
		else if (c0 >= 0xC0) { clen = 2; cp = c0 & 0x1F; }
		for (int i = 1; i < clen && r[i]; i++)
			cp = (cp << 6) | (r[i] & 0x3F);
		if ((cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200B) {
			r += clen;
			continue;
		}
		if (!pre && (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r')) {
			if (!last_space) {
				*w++ = ' ';
				last_space = true;
			}
			r += clen;
			continue;
		}
		last_space = false;
		for (int i = 0; i < clen; i++)
			*w++ = r[i];
		r += clen;
	}
	*w = 0;
}

/* Innermost enclosing flex container while parsing (block index,
 * -1 none). Set by the open-tag stack in parse(); new_block() reads
 * it. parse() is single-threaded (one page at a time), like the
 * other file-scope parser state. */
static int flex_parent = -1;

static struct block *new_block(struct web *w, int type, int level)
{
	if (w->nblocks >= MAXBLOCKS)
		return NULL;
	struct block *b = &w->blocks[w->nblocks++];
	memset(b, 0, sizeof(*b));
	b->type = type;
	b->level = level;
	b->text = calloc(1, 1);
	b->color = -1;
	b->bg = -1;
	b->font_scale = 1.0f;
	b->form = -1;
	b->field = -1;
	b->x = PAGE_X;
	b->w = PAGE_W;
	b->parent = flex_parent;
	b->flex_dir = 0;
	b->justify = 0;
	b->flex_align = 0;
	b->gap = 6;
	b->flex_grow = 0;
	return b;
}

static void block_append(struct block *b, const char *s, int len, const char *href, bool bold)
{
	if (!b || len <= 0)
		return;
	/* collapse whitespace */
	int cap = b->text_len + len + 2;
	b->text = realloc(b->text, cap);
	memcpy(b->text + b->text_len, s, len);
	b->text_len += len;
	b->text[b->text_len] = 0;
}

static void free_page(struct web *w)
{
	for (int i = 0; i < w->nblocks; i++) {
		free(w->blocks[i].text);
		free(w->blocks[i].runs);
		free(w->blocks[i].lines);
		free(w->blocks[i].img_px);
	}
	w->nblocks = 0;
	w->nlinks = 0;
	w->content_h = 0;
	w->scroll = 0;
}

static void get_attr(const char *tag, const char *name, char *out, size_t n)
{
	out[0] = 0;
	const char *p = strcasestr(tag, name);
	if (!p)
		return;
	p += strlen(name);
	while (*p == ' ' || *p == '=')
		p++;
	if (*p == '"' || *p == '\'') {
		char q = *p++;
		const char *e = strchr(p, q);
		if (e)
			snprintf(out, n, "%.*s", (int)(e - p), p);
	} else {
		const char *e = p;
		while (*e && *e != ' ' && *e != '>')
			e++;
		snprintf(out, n, "%.*s", (int)(e - p), p);
	}
}

/* Boolean attribute presence (e.g. <a download>): the name must
 * appear as an attribute, not inside another word or a value. */
static bool has_attr(const char *tag, const char *name)
{
	size_t nl = strlen(name);
	const char *p = tag;
	while ((p = strcasestr(p, name)) != NULL) {
		char before = p == tag ? ' ' : p[-1];
		char after = p[nl];
		if ((before == ' ' || before == '\t' || before == '/' || before == '\n') &&
		    (after == ' ' || after == '\t' || after == '=' || after == '>' ||
		     after == '/' || after == 0))
			return true;
		p += nl;
	}
	return false;
}

/* ---------- CSS subset ---------- */

static int parse_color(const char *s, int *out)
{
	while (isspace((unsigned char)*s))
		s++;
	if (*s == '#') {
		s++;
		int n = (int)strlen(s);
		char hex[8];
		if (n == 3) {
			hex[0] = s[0]; hex[1] = s[0]; hex[2] = s[1]; hex[3] = s[1];
			hex[4] = s[2]; hex[5] = s[2]; hex[6] = 0;
		} else if (n >= 6) {
			snprintf(hex, sizeof(hex), "%.6s", s);
		} else {
			return 0;
		}
		char *end;
		long v = strtol(hex, &end, 16);
		if (end == hex)
			return 0;
		*out = (int)v;
		return 1;
	}
	if (strncmp(s, "rgb", 3) == 0) {
		int r, g, b;
		const char *lp = strchr(s, '(');
		if (lp && sscanf(lp + 1, "%d,%d,%d", &r, &g, &b) == 3) {
			*out = ((r & 255) << 16) | ((g & 255) << 8) | (b & 255);
			return 1;
		}
		return 0;
	}
	static const struct { const char *name; int v; } named[] = {
		{ "black", 0x000000 }, { "white", 0xffffff }, { "red", 0xff0000 },
		{ "green", 0x008000 }, { "blue", 0x0000ff }, { "gray", 0x808080 },
		{ "grey", 0x808080 }, { "silver", 0xc0c0c0 }, { "yellow", 0xffff00 },
		{ "orange", 0xffa500 }, { "purple", 0x800080 }, { "pink", 0xffc0cb },
		{ "brown", 0xa52a2a }, { "navy", 0x000080 }, { "teal", 0x008080 },
		{ "maroon", 0x800000 }, { "lime", 0x00ff00 }, { "aqua", 0x00ffff },
	};
	for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
		size_t nl = strlen(named[i].name);
		if (strncasecmp(s, named[i].name, nl) == 0 &&
		    !isalpha((unsigned char)s[nl])) {
			*out = named[i].v;
			return 1;
		}
	}
	return 0;
}

static void css_init_rule(struct cssrule *r)
{
	memset(r, 0, sizeof(*r));
	r->color = -1;
	r->bg = -1;
	r->align = -1;
	r->bold = -1;
	r->display = -1;
	r->flex_dir = -1;
	r->flex_grow = -1;
	r->justify = -1;
	r->flex_align = -1;
	r->gap = -1;
}

/* Whole-value comparison ignoring surrounding whitespace. */
static bool val_is(const char *val, const char *word)
{
	while (isspace((unsigned char)*val))
		val++;
	size_t n = strlen(word);
	if (strncasecmp(val, word, n) != 0)
		return false;
	const char *e = val + n;
	while (isspace((unsigned char)*e))
		e++;
	return *e == 0;
}

/* Expand var(--name) and var(--name, fallback) in a declaration
 * value, using the collected custom properties. depth caps
 * self-referential chains. */
static void css_expand_vars(struct web *w, const char *val, char *out,
			    size_t n, int depth)
{
	size_t o = 0;
	const char *p = val;
	while (*p && o + 1 < n) {
		if (strncmp(p, "var(", 4) == 0) {
			const char *q = p + 4;
			while (isspace((unsigned char)*q))
				q++;
			char name[48];
			int ni = 0;
			while (*q && *q != ',' && *q != ')' && ni < 47)
				name[ni++] = *q++;
			name[ni] = 0;
			while (ni > 0 && isspace((unsigned char)name[ni - 1]))
				name[--ni] = 0;
			const char *fb = NULL;
			if (*q == ',') {
				q++;
				while (isspace((unsigned char)*q))
					q++;
				fb = q;
			}
			/* find the matching close paren (depth 1 at "var(") */
			const char *close = q;
			int d = 1;
			while (*close) {
				if (*close == '(')
					d++;
				else if (*close == ')') {
					if (--d == 0)
						break;
				}
				close++;
			}
			const char *rep = NULL;
			char repbuf[256];
			repbuf[0] = 0;
			for (int i = 0; i < w->nvars; i++)
				if (strcmp(w->vars[i].name, name) == 0) {
					rep = w->vars[i].value;
					break;
				}
			if (rep) {
				if (depth < 4)
					css_expand_vars(w, rep, repbuf, sizeof(repbuf), depth + 1);
				else
					snprintf(repbuf, sizeof(repbuf), "%s", rep);
				rep = repbuf;
			} else if (fb) {
				char fbt[256];
				int fl = (int)(close - fb);
				if (fl > 255)
					fl = 255;
				snprintf(fbt, sizeof(fbt), "%.*s", fl, fb);
				if (depth < 4)
					css_expand_vars(w, fbt, repbuf, sizeof(repbuf), depth + 1);
				else
					snprintf(repbuf, sizeof(repbuf), "%s", fbt);
				rep = repbuf;
			}
			if (rep) {
				size_t rl = strlen(rep);
				if (o + rl >= n)
					rl = n - o - 1;
				memcpy(out + o, rep, rl);
				o += rl;
			}
			p = *close ? close + 1 : close;
		} else {
			out[o++] = *p++;
		}
	}
	out[o] = 0;
}

/* Collect "--name: value" declarations into w->vars (later
 * definitions win). Called before the declarations are applied, so
 * var() works even when the definition follows its use in the
 * same rule. */
static void css_collect_vars(struct web *w, const char *decls)
{
	const char *p = decls;
	while (*p) {
		while (isspace((unsigned char)*p))
			p++;
		if (strncmp(p, "--", 2) != 0) {
			const char *s = strchr(p, ';');
			p = s ? s + 1 : p + strlen(p);
			continue;
		}
		const char *colon = strchr(p, ':');
		if (!colon)
			break;
		int nl = (int)(colon - p);
		if (nl > 47)
			nl = 47;
		char name[48];
		snprintf(name, sizeof(name), "%.*s", nl, p);
		while (nl > 0 && isspace((unsigned char)name[nl - 1]))
			name[--nl] = 0;
		const char *v = colon + 1;
		while (isspace((unsigned char)*v))
			v++;
		const char *e = strchr(v, ';');
		if (!e)
			e = v + strlen(v);
		int vl = (int)(e - v);
		while (vl > 0 && isspace((unsigned char)v[vl - 1]))
			vl--;
		if (vl > 255)
			vl = 255;
		char value[256];
		snprintf(value, sizeof(value), "%.*s", vl, v);
		int i;
		for (i = 0; i < w->nvars; i++)
			if (strcmp(w->vars[i].name, name) == 0)
				break;
		if (i >= w->nvars && w->nvars < 32)
			i = w->nvars++;
		if (i < 32) {
			snprintf(w->vars[i].name, sizeof(w->vars[i].name), "%s", name);
			snprintf(w->vars[i].value, sizeof(w->vars[i].value), "%s", value);
		}
		p = *e ? e + 1 : e;
	}
}

/* Parse a declaration list ("color: red; font-size: 20px") into r. */
static void css_decls(struct web *w, struct cssrule *r, const char *decls)
{
	css_collect_vars(w, decls);
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s", decls);
	char *save = NULL;
	for (char *d = strtok_r(buf, ";", &save); d; d = strtok_r(NULL, ";", &save)) {
		char *colon = strchr(d, ':');
		if (!colon)
			continue;
		*colon = 0;
		char *prop = d, *val = colon + 1;
		while (isspace((unsigned char)*prop)) prop++;
		while (isspace((unsigned char)*val)) val++;
		char *pe = prop + strlen(prop);
		while (pe > prop && isspace((unsigned char)pe[-1])) *--pe = 0;
		/* var() substitution (custom properties + fallback) */
		char valx[512];
		css_expand_vars(w, val, valx, sizeof(valx), 0);
		val = valx;
		if (!strncasecmp(prop, "--", 2))
			continue;	/* custom property: stored, not a style */
		if (!strcasecmp(prop, "color")) {
			int c;
			if (parse_color(val, &c)) r->color = c;
		} else if (!strcasecmp(prop, "background-color") ||
			   !strcasecmp(prop, "background")) {
			int c;
			if (parse_color(val, &c)) r->bg = c;
		} else if (!strcasecmp(prop, "font-size")) {
			float f = (float)atof(val);
			if (f > 0) {
				if (strchr(val, '%'))
					r->fscale = f / 100.0f;
				else if (strstr(val, "em") && !strstr(val, "rem"))
					r->fscale = f;
				else
					r->fscale = f / 14.5f; /* px/pt vs base size */
			}
		} else if (!strcasecmp(prop, "font-weight")) {
			if (!strncasecmp(val, "bold", 4) || atoi(val) >= 600)
				r->bold = 1;
			else if (!strncasecmp(val, "normal", 6))
				r->bold = 0;
		} else if (!strcasecmp(prop, "text-align")) {
			if (!strncasecmp(val, "center", 6)) r->align = 1;
			else if (!strncasecmp(val, "right", 5)) r->align = 2;
			else if (!strncasecmp(val, "left", 4)) r->align = 0;
		} else if (!strcasecmp(prop, "display")) {
			if (val_is(val, "none")) r->hide = true;
			else if (val_is(val, "flex")) r->display = 1;
			else r->display = 0;
		} else if (!strcasecmp(prop, "flex-direction")) {
			if (val_is(val, "column") || val_is(val, "column-reverse"))
				r->flex_dir = 1;
			else
				r->flex_dir = 0;
		} else if (!strcasecmp(prop, "flex-grow")) {
			r->flex_grow = (float)atof(val);
		} else if (!strcasecmp(prop, "flex")) {
			/* shorthand: "flex: <grow> ..." — first number wins */
			r->flex_grow = (float)atof(val);
		} else if (!strcasecmp(prop, "justify-content")) {
			if (val_is(val, "center")) r->justify = 1;
			else if (val_is(val, "flex-end") || val_is(val, "end")) r->justify = 2;
			else if (val_is(val, "space-between")) r->justify = 3;
			else if (val_is(val, "space-around") || val_is(val, "space-evenly")) r->justify = 4;
			else r->justify = 0;
		} else if (!strcasecmp(prop, "align-items")) {
			if (val_is(val, "flex-start") || val_is(val, "start")) r->flex_align = 1;
			else if (val_is(val, "center")) r->flex_align = 2;
			else if (val_is(val, "flex-end") || val_is(val, "end")) r->flex_align = 3;
			else r->flex_align = 0;
		} else if (!strcasecmp(prop, "gap")) {
			int g = atoi(val);
			if (g >= 0)
				r->gap = g;
		}
	}
}

/* One media-query condition against the 854x480 viewport.
 * Supported: min/max-width/height (px), orientation, screen/all,
 * print (never matches), prefers-color-scheme (theme is dark).
 * Unknown conditions fail closed (the block is skipped). */
/* One media-query condition against the 854x480 viewport.
 * The condition is split on the word "and" (parens may contain
 * spaces, so plain whitespace tokenising would break them). */
static bool media_one(const char *part)
{
	while (isspace((unsigned char)*part))
		part++;
	if (!strncasecmp(part, "only ", 5))
		part += 5;
	while (isspace((unsigned char)*part))
		part++;
	char *end = (char *)part + strlen(part);
	while (end > part && isspace((unsigned char)end[-1]))
		*--end = 0;
	if (strcasecmp(part, "screen") == 0 || strcasecmp(part, "all") == 0)
		return true;
	if (strcasecmp(part, "print") == 0)
		return false;
	if (strncasecmp(part, "not ", 4) == 0)
		return false;	/* "not screen" etc: unsupported */
	char *lp = strchr(part, '(');
	char *rp = strchr(part, ')');
	if (!lp || !rp)
		return false;
	char inner[128];
	snprintf(inner, sizeof(inner), "%.*s", (int)(rp - lp - 1), lp + 1);
	char *colon = strchr(inner, ':');
	if (!colon)
		return false;
	*colon = 0;
	char *key = inner, *val = colon + 1;
	while (isspace((unsigned char)*key)) key++;
	while (isspace((unsigned char)*val)) val++;
	int n = atoi(val);
	if (strcasecmp(key, "min-width") == 0)
		return VIEWPORT_W >= n;
	if (strcasecmp(key, "max-width") == 0)
		return VIEWPORT_W <= n;
	if (strcasecmp(key, "min-height") == 0)
		return VIEWPORT_H >= n;
	if (strcasecmp(key, "max-height") == 0)
		return VIEWPORT_H <= n;
	if (strcasecmp(key, "orientation") == 0) {
		bool land = VIEWPORT_W > VIEWPORT_H;
		return (strcasecmp(val, "landscape") == 0) == land;
	}
	if (strcasecmp(key, "prefers-color-scheme") == 0)
		/* the braindesk theme is always dark */
		return strcasecmp(val, "dark") == 0;
	return false;	/* unknown condition: fail closed */
}

static bool media_cond(const char *cond)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", cond);
	/* split on "and" (case-insensitive, word-boundary aware) */
	char *p = buf;
	while (*p) {
		char *andp = NULL;
		for (char *q = p; *q; q++) {
			if ((q == p || isspace((unsigned char)q[-1])) &&
			    strncasecmp(q, "and", 3) == 0 &&
			    (q[3] == 0 || isspace((unsigned char)q[3]))) {
				andp = q;
				break;
			}
		}
		char part[128];
		if (andp) {
			snprintf(part, sizeof(part), "%.*s", (int)(andp - p), p);
			p = andp + 3;
		} else {
			snprintf(part, sizeof(part), "%s", p);
			p += strlen(p);
		}
		if (!media_one(part))
			return false;
	}
	return true;
}

static void css_parse(struct web *w, const char *css)
{
	const char *p = css;
	while (*p && w->nrules < 64) {
		const char *t = p;
		while (isspace((unsigned char)*t))
			t++;
		/* @media <cond> { <rules> }: apply the inner rules only
		 * when the condition matches the viewport. Other at-rules
		 * (@import, @font-face, ...) are skipped gracefully. */
		if (*t == '@') {
			const char *ob = strchr(t, '{');
			if (!ob) {
				/* at-rule without a block: skip to ';' */
				const char *sc = strchr(t, ';');
				p = sc ? sc + 1 : t + strlen(t);
				continue;
			}
			/* find the matching close brace (inner rules nest) */
			int depth = 0;
			const char *q = ob, *cb = NULL;
			while (*q) {
				if (*q == '{')
					depth++;
				else if (*q == '}') {
					if (--depth == 0) {
						cb = q;
						break;
					}
				}
				q++;
			}
			if (!cb)
				break;
			if (strncasecmp(t, "@media", 6) == 0) {
				char cond[256];
				snprintf(cond, sizeof(cond), "%.*s",
					 (int)(ob - t - 6 > 255 ? 255 : (int)(ob - t - 6)), t + 6);
				if (media_cond(cond)) {
					char inner[8192];
					snprintf(inner, sizeof(inner), "%.*s",
						 (int)(cb - ob - 1 > 8191 ? 8191 : (int)(cb - ob - 1)),
						 ob + 1);
					css_parse(w, inner);
				}
			}
			p = cb + 1;
			continue;
		}
		const char *ob = strchr(p, '{');
		if (!ob)
			break;
		const char *cb = strchr(ob, '}');
		if (!cb)
			break;
		char sels[256];
		snprintf(sels, sizeof(sels), "%.*s", (int)(ob - p > 255 ? 255 : (int)(ob - p)), p);
		char decls[1024];
		snprintf(decls, sizeof(decls), "%.*s", (int)(cb - ob - 1 > 1023 ? 1023 : (int)(cb - ob - 1)), ob + 1);
		/* comma-separated selectors share the declarations */
		char *save = NULL;
		for (char *sel = strtok_r(sels, ",", &save); sel && w->nrules < 64;
		     sel = strtok_r(NULL, ",", &save)) {
			while (isspace((unsigned char)*sel)) sel++;
			char *se = sel + strlen(sel);
			while (se > sel && isspace((unsigned char)se[-1])) *--se = 0;
			if (!*sel)
				continue;
			/* :root holds custom properties; its other
			 * declarations have no element to attach to. */
			if (strcmp(sel, ":root") == 0) {
				struct cssrule tmp;
				css_init_rule(&tmp);
				css_decls(w, &tmp, decls);
				continue;
			}
			if (strchr(sel, ' ') || strchr(sel, '>') ||
			    strchr(sel, ':') || strchr(sel, '*'))
				continue;	/* combinators/pseudo: unsupported */
			struct cssrule *r = &w->rules[w->nrules];
			css_init_rule(r);
			if (sel[0] == '.') {
				r->kind = 1;
				snprintf(r->sel, sizeof(r->sel), "%s", sel + 1);
			} else if (sel[0] == '#') {
				r->kind = 2;
				snprintf(r->sel, sizeof(r->sel), "%s", sel + 1);
			} else {
				r->kind = 0;
				snprintf(r->sel, sizeof(r->sel), "%s", sel);
				for (char *q = r->sel; *q; q++)
					*q = tolower((unsigned char)*q);
			}
			css_decls(w, r, decls);
			w->nrules++;
		}
		p = cb + 1;
	}
}

static void css_merge(struct cssrule *dst, const struct cssrule *src)
{
	if (src->color >= 0) dst->color = src->color;
	if (src->bg >= 0) dst->bg = src->bg;
	if (src->fscale > 0) dst->fscale = src->fscale;
	if (src->align >= 0) dst->align = src->align;
	if (src->bold >= 0) dst->bold = src->bold;
	if (src->hide) dst->hide = true;
	if (src->display >= 0) dst->display = src->display;
	if (src->flex_dir >= 0) dst->flex_dir = src->flex_dir;
	if (src->flex_grow >= 0) dst->flex_grow = src->flex_grow;
	if (src->justify >= 0) dst->justify = src->justify;
	if (src->flex_align >= 0) dst->flex_align = src->flex_align;
	if (src->gap >= 0) dst->gap = src->gap;
}

/* Merged style for an element: tag rules, then class, id, inline. */
static void style_for(struct web *w, const char *tagname, const char *tag,
		      struct cssrule *out)
{
	css_init_rule(out);
	char cls[128] = "", id[64] = "", inl[512] = "";
	if (tag) {
		get_attr(tag, "class", cls, sizeof(cls));
		get_attr(tag, "id", id, sizeof(id));
		get_attr(tag, "style", inl, sizeof(inl));
	}
	for (int pass = 0; pass < 3; pass++) {
		for (int i = 0; i < w->nrules; i++) {
			struct cssrule *r = &w->rules[i];
			if (r->kind != pass)
				continue;
			bool match = false;
			if (pass == 0)
				match = strcmp(r->sel, tagname) == 0;
			else if (pass == 1) {
				/* class attr is a space-separated list */
				const char *p = cls;
				while (*p) {
					while (isspace((unsigned char)*p)) p++;
					const char *e = p;
					while (*e && !isspace((unsigned char)*e)) e++;
					if ((size_t)(e - p) == strlen(r->sel) &&
					    strncmp(p, r->sel, e - p) == 0)
						match = true;
					p = e;
				}
			} else {
				match = id[0] && strcmp(r->sel, id) == 0;
			}
			if (match)
				css_merge(out, r);
		}
	}
	if (inl[0]) {
		struct cssrule tmp;
		css_init_rule(&tmp);
		css_decls(w, &tmp, inl);
		css_merge(out, &tmp);
	}
}

static void block_apply_style(struct block *b, const struct cssrule *st)
{
	b->color = st->color;
	b->bg = st->bg;
	b->font_scale = st->fscale > 0 ? st->fscale : 1.0f;
	b->align = st->align >= 0 ? st->align : 0;
	b->hidden = st->hide;
	b->flex = st->display == 1;
	b->flex_dir = st->flex_dir >= 0 ? st->flex_dir : 0;
	b->justify = st->justify >= 0 ? st->justify : 0;
	b->flex_align = st->flex_align >= 0 ? st->flex_align : 0;
	b->gap = st->gap >= 0 ? st->gap : 6;
	b->flex_grow = st->flex_grow >= 0 ? st->flex_grow : 0;
}

/* Append the option currently being collected to the <select>
 * field. Called on </option>, on the next <option>, and on
 * </select>. The label is the option text (entities decoded,
 * surrounding whitespace trimmed); the submitted value is the
 * value attribute, or the label when it is absent. */
static void flush_option(struct web *w, int form_i, int field_i,
			 char *opt_text, const char *opt_value, bool selected)
{
	if (form_i < 0 || field_i < 0)
		return;
	struct formfield *ff = &w->forms[form_i].fields[field_i];
	if (ff->nopts >= 8)
		return;
	decode_entities(opt_text);
	/* trim surrounding whitespace */
	char *s = opt_text;
	while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\t' || e[-1] == '\r'))
		*--e = 0;
	snprintf(ff->opt_label[ff->nopts], 64, "%s", s);
	snprintf(ff->opt_val[ff->nopts], 64, "%s",
		 opt_value[0] ? opt_value : s);
	if (selected || ff->nopts == 0) {
		ff->sel = ff->nopts;
		snprintf(ff->value, sizeof(ff->value), "%s",
			 ff->opt_val[ff->nopts]);
	}
	ff->nopts++;
}

static void parse(struct web *w, const char *html, const char *page_url)
{
	struct block *cur = NULL;
	char cur_href[512] = "";
	bool cur_bold = false;
	bool block_bold = false;	/* CSS font-weight on the block */
	int cur_color = -1;		/* inline color (span/font) */
	int color_stack[8];
	int csp = 0;
	bool skip = false;	/* inside script/style/head */
	bool in_style = false;
	bool in_script = false;	/* inside an inline <script> */
	static char cssbuf[16384];
	int csslen = 0;
	int cur_form = -1;
	int pre = 0;
	/* <select> / <textarea> collection state */
	bool in_select = false;
	int sel_form = -1, sel_field = -1;
	bool in_option = false;
	char opt_text[256];
	char opt_value[64];
	bool opt_selected = false;
	bool in_textarea = false;
	int ta_form = -1;
	char ta_name[64];
	int ta_rows = 3;
	static char ta_buf[1024];
	int ta_len = 0;
	const char *p = html;
	char textbuf[8192];
	int tlen = 0;
	w->nforms = 0;
	w->nrules = 0;
	w->nvars = 0;
	w->nfind_hits = 0;
	w->find_cur = 0;
	w->js_len = 0;
	w->js_ext = 0;
	flex_parent = -1;
	/* open div/section/article stack, for flex container nesting.
	 * Only the nearest enclosing flex container matters for the
	 * parent pointer (flex_parent), but every open element is
	 * tracked so a closing tag pops the right entry. */
	struct { char name[16]; int block; bool flex; } ostack[16];
	int nstack = 0;
	bool cur_download = false;	/* inside <a download> */
	/* runs are recorded per block as (offset,len,href,bold) using a temp list */
	static struct { int off, len; char href[512]; bool bold; int color; bool download; } truns[2048];
	int ntruns = 0;
	struct block *run_block = NULL;

#define FLUSH_TEXT() do { \
		if (tlen > 0 && cur && !skip) { \
			textbuf[tlen] = 0; \
			decode_entities(textbuf); \
			sanitize_text(textbuf, pre); \
			int off = cur->text_len; \
			block_append(cur, textbuf, (int)strlen(textbuf), cur_href, cur_bold); \
			if (run_block != cur) { ntruns = 0; run_block = cur; } \
			if (ntruns < 2048) { \
				truns[ntruns].off = off; \
				truns[ntruns].len = (int)strlen(textbuf); \
				snprintf(truns[ntruns].href, 512, "%s", cur_href); \
				truns[ntruns].bold = cur_bold || block_bold; \
				truns[ntruns].color = cur_color; \
				truns[ntruns].download = cur_download; \
				ntruns++; \
			} \
		} \
		tlen = 0; \
	} while (0)
#define CLOSE_BLOCK() do { \
		FLUSH_TEXT(); \
		if (cur && ntruns > 0) { \
			cur->runs = malloc(sizeof(struct run) * ntruns); \
			cur->nruns = ntruns; \
			for (int i = 0; i < ntruns; i++) { \
				cur->runs[i].block = 0; \
				cur->runs[i].off = truns[i].off; \
				cur->runs[i].len = truns[i].len; \
				snprintf(cur->runs[i].href, 512, "%s", truns[i].href); \
				cur->runs[i].bold = truns[i].bold; \
				cur->runs[i].color = truns[i].color; \
				cur->runs[i].download = truns[i].download; \
			} \
		} \
		ntruns = 0; \
		run_block = NULL; \
		block_bold = false; \
	} while (0)
#define APPLY_STYLE(blk) do { \
		if (blk) { \
			struct cssrule st; \
			style_for(w, name, tag, &st); \
			block_apply_style(blk, &st); \
			if (st.bold > 0) \
				block_bold = true; \
		} \
	} while (0)

	while (*p) {
		if (*p == '<') {
			/* '<' inside a script is script text, not markup
			 * (JS comparisons, generics, '<' in strings), except
			 * for the tag that closes the script. This also keeps
			 * legacy <!-- --> inside scripts intact (Annex B
			 * HTML-like comments are valid JS).
			 * Stop at the closing tag: a '<' earlier in the script
			 * must not swallow '</script>' (its '>' would otherwise
			 * be taken as the end of this '<...>' chunk). */
			if (in_script && !(p[1] == '/' &&
					   strncasecmp(p + 2, "script", 6) == 0)) {
				const char *close = strcasestr(p, "</script");
				const char *gt2 = strchr(p, '>');
				if (close && (!gt2 || close < gt2)) {
					int tl = (int)(close - p);
					if (w->js_len + tl < (int)sizeof(w->js_src)) {
						memcpy(w->js_src + w->js_len, p, (size_t)tl);
						w->js_len += tl;
					}
					p = close;
					continue;
				}
				if (!gt2)
					break;
				int tl = (int)(gt2 - p + 1);
				if (w->js_len + tl < (int)sizeof(w->js_src)) {
					memcpy(w->js_src + w->js_len, p, (size_t)tl);
					w->js_len += tl;
				}
				p = gt2 + 1;
				continue;
			}
			/* comment */
			if (strncmp(p, "<!--", 4) == 0) {
				const char *e = strstr(p, "-->");
				p = e ? e + 3 : p + strlen(p);
				continue;
			}
			const char *gt = strchr(p, '>');
			if (!gt)
				break;
			char tag[1024];
			snprintf(tag, sizeof(tag), "%.*s", (int)(gt - p - 1 > 1023 ? 1023 : (int)(gt - p - 1)), p + 1);
			/* tag name */
			char name[32];
			int ni = 0;
			const char *q = tag;
			if (*q == '/')
				q++;
			while (*q && !isspace((unsigned char)*q) && *q != '/' && ni < 31)
				name[ni++] = tolower((unsigned char)*q++);
			name[ni] = 0;
			bool closing = tag[0] == '/';
			if (strcmp(name, "style") == 0) {
				if (!closing) {
					in_style = true;
					skip = true;
					csslen = 0;
				} else {
					in_style = false;
					skip = false;
					cssbuf[csslen] = 0;
					css_parse(w, cssbuf);
				}
				p = gt + 1;
				continue;
			}
			if (strcmp(name, "script") == 0) {
				if (!closing) {
					char src[512];
					src[0] = 0;
					get_attr(tag, "src", src, sizeof(src));
					if (src[0])
						w->js_ext++;
					else
						in_script = true;
					skip = true;
				} else {
					in_script = false;
					skip = false;
				}
				p = gt + 1;
				continue;
			}
			if (strcmp(name, "head") == 0) {
				if (!closing)
					skip = true;
				else
					skip = false;
				p = gt + 1;
				continue;
			}
			/* <title> lives inside <head> (skipped), so extract it
			 * before the skip check — otherwise w->title (and
			 * document.title) is always empty. */
			if (strcmp(name, "title") == 0 && !closing) {
				const char *e = strcasestr(gt + 1, "</title>");
				if (e) {
					snprintf(w->title, sizeof(w->title), "%.*s", (int)(e - (gt + 1)), gt + 1);
					decode_entities(w->title);
				}
			}
			if (skip) {
				p = gt + 1;
				continue;
			}
			if (!closing && (strcmp(name, "p") == 0 || strcmp(name, "div") == 0 ||
					 strcmp(name, "section") == 0 || strcmp(name, "article") == 0 ||
					 strcmp(name, "blockquote") == 0 || strcmp(name, "table") == 0)) {
				CLOSE_BLOCK();
				cur = new_block(w, B_PARA, 0);
				APPLY_STYLE(cur);
				if (cur && (strcmp(name, "div") == 0 || strcmp(name, "section") == 0 ||
					    strcmp(name, "article") == 0) && nstack < 16) {
					/* track container nesting for flex parents */
					snprintf(ostack[nstack].name, sizeof(ostack[nstack].name), "%s", name);
					ostack[nstack].block = (int)(cur - w->blocks);
					ostack[nstack].flex = cur->flex;
					nstack++;
					if (cur->flex)
						flex_parent = (int)(cur - w->blocks);
				}
			} else if (!closing && name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == 0) {
				CLOSE_BLOCK();
				cur = new_block(w, B_HEAD, name[1] - '0');
				APPLY_STYLE(cur);
			} else if (closing && (name[0] == 'h' && name[1] >= '1' && name[1] <= '6')) {
				CLOSE_BLOCK();
				cur = NULL;
			} else if (!closing && strcmp(name, "li") == 0) {
				CLOSE_BLOCK();
				cur = new_block(w, B_ITEM, 0);
				APPLY_STYLE(cur);
			} else if (!closing && strcmp(name, "pre") == 0) {
				CLOSE_BLOCK();
				cur = new_block(w, B_PRE, 0);
				APPLY_STYLE(cur);
				pre = 1;
			} else if (closing && strcmp(name, "pre") == 0) {
				CLOSE_BLOCK();
				cur = NULL;
				pre = 0;
			} else if (strcmp(name, "br") == 0) {
				FLUSH_TEXT();
				if (cur)
					block_append(cur, "\n", 1, "", false);
			} else if (!closing && strcmp(name, "hr") == 0) {
				CLOSE_BLOCK();
				new_block(w, B_HR, 0);
				cur = NULL;
			} else if (!closing && strcmp(name, "img") == 0) {
				FLUSH_TEXT();
				char src[512];
				get_attr(tag, "src", src, sizeof(src));
				if (src[0]) {
					CLOSE_BLOCK();
					struct block *ib = new_block(w, B_IMG, 0);
					if (ib)
						resolve_url(page_url, src, ib->img_src, sizeof(ib->img_src));
					cur = NULL;
				}
			} else if (!closing && strcmp(name, "form") == 0) {
				FLUSH_TEXT();
				if (w->nforms < MAXFORMS) {
					struct form *f = &w->forms[w->nforms];
					memset(f, 0, sizeof(*f));
					char act[1024];
					get_attr(tag, "action", act, sizeof(act));
					if (act[0])
						resolve_url(page_url, act, f->action, sizeof(f->action));
					else
						snprintf(f->action, sizeof(f->action), "%s", page_url);
					get_attr(tag, "method", f->method, sizeof(f->method));
					for (char *q = f->method; *q; q++)
						*q = tolower((unsigned char)*q);
					if (!f->method[0])
						snprintf(f->method, sizeof(f->method), "get");
					cur_form = w->nforms++;
				}
			} else if (closing && strcmp(name, "form") == 0) {
				FLUSH_TEXT();
				cur_form = -1;
			} else if (!closing && strcmp(name, "input") == 0) {
				FLUSH_TEXT();
				char type[32], nm[64], val[512];
				snprintf(type, sizeof(type), "text");
				get_attr(tag, "type", type, sizeof(type));
				for (char *q = type; *q; q++)
					*q = tolower((unsigned char)*q);
				nm[0] = 0;
				val[0] = 0;
				get_attr(tag, "name", nm, sizeof(nm));
				get_attr(tag, "value", val, sizeof(val));
				bool is_text = strcmp(type, "text") == 0 || strcmp(type, "search") == 0 ||
					strcmp(type, "password") == 0 || strcmp(type, "email") == 0 ||
					strcmp(type, "tel") == 0 || strcmp(type, "url") == 0 ||
					strcmp(type, "number") == 0 || type[0] == 0;
				if ((is_text || strcmp(type, "submit") == 0) && cur_form < 0 &&
				    w->nforms < MAXFORMS) {
					/* implicit form around a stray control */
					struct form *f = &w->forms[w->nforms];
					memset(f, 0, sizeof(*f));
					snprintf(f->action, sizeof(f->action), "%s", page_url);
					snprintf(f->method, sizeof(f->method), "get");
					cur_form = w->nforms++;
				}
				struct form *f = cur_form >= 0 ? &w->forms[cur_form] : NULL;
				if (strcmp(type, "hidden") == 0) {
					if (f && f->nfields < MAXFIELDS) {
						struct formfield *ff = &f->fields[f->nfields++];
						snprintf(ff->name, sizeof(ff->name), "%s", nm);
						snprintf(ff->value, sizeof(ff->value), "%s", val);
						ff->block = -1;
					}
				} else if (strcmp(type, "submit") == 0) {
					CLOSE_BLOCK();
					struct block *sb = new_block(w, B_SUBMIT, 0);
					if (sb) {
						sb->form = cur_form;
						const char *lbl = val[0] ? val : "送信";
						block_append(sb, lbl, (int)strlen(lbl), "", false);
					}
					cur = NULL;
				} else if (is_text) {
					CLOSE_BLOCK();
					struct block *ib = new_block(w, B_INPUT, 0);
					if (ib) {
						ib->form = cur_form;
						if (f && f->nfields < MAXFIELDS) {
							int fi = f->nfields++;
							struct formfield *ff = &f->fields[fi];
							snprintf(ff->name, sizeof(ff->name), "%s", nm);
							snprintf(ff->value, sizeof(ff->value), "%s", val);
							ff->block = (int)(ib - w->blocks);
							ib->field = fi;
						}
						if (val[0])
							block_append(ib, val, (int)strlen(val), "", false);
					}
					cur = NULL;
				}
				/* checkbox/radio/button/file: not rendered */
			} else if (!closing && strcmp(name, "select") == 0) {
				FLUSH_TEXT();
				char nm[64];
				nm[0] = 0;
				get_attr(tag, "name", nm, sizeof(nm));
				if (cur_form < 0 && w->nforms < MAXFORMS) {
					struct form *f = &w->forms[w->nforms];
					memset(f, 0, sizeof(*f));
					snprintf(f->action, sizeof(f->action), "%s", page_url);
					snprintf(f->method, sizeof(f->method), "get");
					cur_form = w->nforms++;
				}
				sel_form = cur_form;
				sel_field = -1;
				if (cur_form >= 0 && w->forms[cur_form].nfields < MAXFIELDS) {
					sel_field = w->forms[cur_form].nfields++;
					struct formfield *ff =
						&w->forms[cur_form].fields[sel_field];
					memset(ff, 0, sizeof(*ff));
					snprintf(ff->name, sizeof(ff->name), "%s", nm);
					ff->block = -1;
				}
				in_select = true;
				in_option = false;
			} else if (!closing && strcmp(name, "option") == 0 && in_select) {
				if (in_option)
					flush_option(w, sel_form, sel_field, opt_text,
						     opt_value, opt_selected);
				in_option = true;
				opt_text[0] = 0;
				opt_value[0] = 0;
				get_attr(tag, "value", opt_value, sizeof(opt_value));
				/* boolean attribute: presence only */
				opt_selected = strcasestr(tag, "selected") != NULL;
			} else if (closing && strcmp(name, "option") == 0 && in_select) {
				if (in_option)
					flush_option(w, sel_form, sel_field, opt_text,
						     opt_value, opt_selected);
				in_option = false;
			} else if (closing && strcmp(name, "select") == 0 && in_select) {
				if (in_option)
					flush_option(w, sel_form, sel_field, opt_text,
						     opt_value, opt_selected);
				in_option = false;
				in_select = false;
				CLOSE_BLOCK();
				struct formfield *ff = (sel_form >= 0 && sel_field >= 0)
					? &w->forms[sel_form].fields[sel_field] : NULL;
				if (ff && ff->nopts > 0) {
					struct block *sb = new_block(w, B_SELECT, 0);
					if (sb) {
						sb->form = sel_form;
						sb->field = sel_field;
						ff->block = (int)(sb - w->blocks);
						block_append(sb, ff->opt_label[ff->sel],
							     (int)strlen(ff->opt_label[ff->sel]),
							     "", false);
					}
				}
				sel_form = sel_field = -1;
				cur = NULL;
			} else if (!closing && strcmp(name, "textarea") == 0) {
				FLUSH_TEXT();
				ta_name[0] = 0;
				get_attr(tag, "name", ta_name, sizeof(ta_name));
				char rs[16];
				rs[0] = 0;
				get_attr(tag, "rows", rs, sizeof(rs));
				ta_rows = rs[0] ? atoi(rs) : 3;
				if (ta_rows < 2)
					ta_rows = 2;
				if (ta_rows > 6)
					ta_rows = 6;
				if (cur_form < 0 && w->nforms < MAXFORMS) {
					struct form *f = &w->forms[w->nforms];
					memset(f, 0, sizeof(*f));
					snprintf(f->action, sizeof(f->action), "%s", page_url);
					snprintf(f->method, sizeof(f->method), "get");
					cur_form = w->nforms++;
				}
				ta_form = cur_form;
				ta_len = 0;
				ta_buf[0] = 0;
				in_textarea = true;
			} else if (closing && strcmp(name, "textarea") == 0 && in_textarea) {
				in_textarea = false;
				ta_buf[ta_len] = 0;
				decode_entities(ta_buf);
				CLOSE_BLOCK();
				struct block *tb = new_block(w, B_TEXTAREA, 0);
				if (tb) {
					tb->form = ta_form;
					tb->rows = ta_rows;
					if (ta_form >= 0 &&
					    w->forms[ta_form].nfields < MAXFIELDS) {
						int fi = w->forms[ta_form].nfields++;
						struct formfield *ff =
							&w->forms[ta_form].fields[fi];
						memset(ff, 0, sizeof(*ff));
						snprintf(ff->name, sizeof(ff->name), "%s", ta_name);
						snprintf(ff->value, sizeof(ff->value), "%s", ta_buf);
						ff->block = (int)(tb - w->blocks);
						tb->field = fi;
					}
					block_append(tb, ta_buf, (int)strlen(ta_buf), "", false);
				}
				ta_form = -1;
				cur = NULL;
			} else if (!closing && (strcmp(name, "span") == 0 || strcmp(name, "font") == 0)) {
				FLUSH_TEXT();
				struct cssrule st;
				css_init_rule(&st);
				if (strcmp(name, "font") == 0) {
					char fc[64];
					fc[0] = 0;
					get_attr(tag, "color", fc, sizeof(fc));
					if (fc[0]) {
						int c;
						if (parse_color(fc, &c))
							st.color = c;
					}
				}
				char inl[512];
				inl[0] = 0;
				get_attr(tag, "style", inl, sizeof(inl));
				if (inl[0])
					css_decls(w, &st, inl);
				if (st.color >= 0) {
					if (csp < 8)
						color_stack[csp++] = cur_color;
					cur_color = st.color;
				}
			} else if (closing && (strcmp(name, "span") == 0 || strcmp(name, "font") == 0)) {
				FLUSH_TEXT();
				if (csp > 0)
					cur_color = color_stack[--csp];
			} else if (!closing && strcmp(name, "a") == 0) {
				FLUSH_TEXT();
				char href[512];
				get_attr(tag, "href", href, sizeof(href));
				if (href[0] && href[0] != '#')
					resolve_url(page_url, href, cur_href, sizeof(cur_href));
				else
					cur_href[0] = 0;
				cur_download = has_attr(tag, "download");
			} else if (closing && strcmp(name, "a") == 0) {
				FLUSH_TEXT();
				cur_href[0] = 0;
				cur_download = false;
			} else if (!closing && (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0)) {
				FLUSH_TEXT();
				cur_bold = true;
			} else if (closing && (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0)) {
				FLUSH_TEXT();
				cur_bold = false;
			} else if (closing && (strcmp(name, "p") == 0 || strcmp(name, "div") == 0 ||
						strcmp(name, "li") == 0 || strcmp(name, "section") == 0 ||
						strcmp(name, "article") == 0 || strcmp(name, "tr") == 0)) {
				CLOSE_BLOCK();
				cur = NULL;
				/* pop the open-tag stack when the tag matches */
				if (nstack > 0 && strcmp(ostack[nstack - 1].name, name) == 0) {
					nstack--;
					flex_parent = -1;
					for (int si = nstack - 1; si >= 0; si--)
						if (ostack[si].flex) {
							flex_parent = ostack[si].block;
							break;
						}
				}
			}
			p = gt + 1;
			continue;
		}
		/* text */
		if (in_script) {
			if (w->js_len < (int)sizeof(w->js_src) - 1)
				w->js_src[w->js_len++] = *p;
			p++;
			continue;
		}
		if (in_style) {
			if (csslen < (int)sizeof(cssbuf) - 1)
				cssbuf[csslen++] = *p;
			p++;
			continue;
		}
		if (in_option) {
			/* option label text: collected, not page content */
			size_t l = strlen(opt_text);
			if (l + 1 < sizeof(opt_text)) {
				opt_text[l] = *p;
				opt_text[l + 1] = 0;
			}
			p++;
			continue;
		}
		if (in_textarea) {
			if (ta_len + 1 < (int)sizeof(ta_buf))
				ta_buf[ta_len++] = *p;
			p++;
			continue;
		}
		if (!skip && !in_select) {
			if (!cur) {
				cur = new_block(w, pre ? B_PRE : B_PARA, 0);
				ntruns = 0;
				run_block = NULL;
			}
			if (tlen < (int)sizeof(textbuf) - 8)
				textbuf[tlen++] = *p;
		}
		p++;
	}
	w->js_src[w->js_len] = 0;
	CLOSE_BLOCK();
	/* drop empty blocks */
	int out = 0;
	static int idxmap[MAXBLOCKS];
	for (int i = 0; i < w->nblocks; i++) {
		struct block *b = &w->blocks[i];
		bool ws_only = true;
		for (int k = 0; k < b->text_len; k++)
			if (b->text[k] != ' ' && b->text[k] != '\n' && b->text[k] != '\t' && b->text[k] != '\r')
				ws_only = false;
		/* flex containers carry no text of their own; keep them */
		if (!b->flex && b->type != B_IMG && b->type != B_HR && b->type != B_INPUT &&
		    b->type != B_SUBMIT && b->type != B_SELECT &&
		    b->type != B_TEXTAREA && (b->text_len == 0 || ws_only)) {
			free(b->text);
			free(b->runs);
			idxmap[i] = -1;
			continue;
		}
		if (out != i)
			w->blocks[out] = *b;
		idxmap[i] = out;
		out++;
	}
	w->nblocks = out;
	/* form fields and flex parents point at blocks by index:
	 * remap after compaction */
	for (int fi = 0; fi < w->nforms; fi++)
		for (int k = 0; k < w->forms[fi].nfields; k++)
			if (w->forms[fi].fields[k].block >= 0)
				w->forms[fi].fields[k].block = idxmap[w->forms[fi].fields[k].block];
	for (int i = 0; i < w->nblocks; i++)
		if (w->blocks[i].parent >= 0)
			w->blocks[i].parent = idxmap[w->blocks[i].parent];
#undef FLUSH_TEXT
#undef CLOSE_BLOCK
#undef APPLY_STYLE
}

/* ---------- images ---------- */
static void load_image(struct web *w, struct block *b)
{
	b->img_tried = true;
	/* cache file name from a simple hash of the URL */
	unsigned long h = 5381;
	for (const char *p = b->img_src; *p; p++)
		h = h * 33 + (unsigned char)*p;
	char path[640];
	snprintf(path, sizeof(path), "%s/img-%lu", w->cache_dir, h);
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		char cmd[1400];
		snprintf(cmd, sizeof(cmd), "curl -sL --max-time 15 -A 'bd-web' -o '%s' '%s' 2>/dev/null", path, b->img_src);
		if (system(cmd) != 0)
			return;
	}
	int iw, ih, ch;
	unsigned char *data = stbi_load(path, &iw, &ih, &ch, 4);
	if (!data)
		return;
	int maxw = 560, maxh = 420;
	int sw = iw, sh = ih;
	if (sw > maxw) {
		sh = sh * maxw / sw;
		sw = maxw;
	}
	if (sh > maxh) {
		sw = sw * maxh / sh;
		sh = maxh;
	}
	b->img_px = malloc((size_t)sw * sh * 4);
	if (!b->img_px) {
		stbi_image_free(data);
		return;
	}
	for (int y = 0; y < sh; y++)
		for (int x = 0; x < sw; x++) {
			const unsigned char *src = data + ((size_t)(y * ih / sh) * iw + (x * iw / sw)) * 4;
			memcpy(b->img_px + ((size_t)y * sw + x) * 4, src, 4);
		}
	stbi_image_free(data);
	b->img_w = sw;
	b->img_h = sh;
}

/* ---------- layout ---------- */
static float block_font_size(const struct block *b)
{
	float base;
	if (b->type == B_HEAD) {
		static const float hs[] = { 26, 22, 19, 17, 15.5, 14.5 };
		base = hs[b->level < 1 ? 0 : b->level > 6 ? 5 : b->level - 1];
	} else if (b->type == B_PRE) {
		base = 12.5f;
	} else {
		base = 14.5f;
	}
	return base * (b->font_scale > 0 ? b->font_scale : 1.0f);
}

/* Preferred (unwrapped) width of a block, for flex row sizing.
 * Text blocks measure their whole text; controls/images use their
 * natural size; flex containers recurse into their children. */
static int block_pref_width(struct web *w, bu_font *font, struct block *b)
{
	if (b->type == B_INPUT || b->type == B_SUBMIT || b->type == B_SELECT)
		return 240;
	if (b->type == B_TEXTAREA || b->type == B_HR)
		return PAGE_W;
	if (b->type == B_IMG)
		return b->img_w > 0 ? b->img_w : 300;
	if (b->flex) {
		int ci = (int)(b - w->blocks);
		int sum = 0, mx = 0, n = 0;
		for (int i = 0; i < w->nblocks; i++) {
			if (w->blocks[i].parent != ci)
				continue;
			int pw = block_pref_width(w, font, &w->blocks[i]);
			sum += pw;
			if (pw > mx)
				mx = pw;
			n++;
		}
		int tw = b->flex_dir == 0 ? sum + (n > 1 ? b->gap * (n - 1) : 0) : mx;
		if (tw < 60)
			tw = 60;
		if (tw > PAGE_W)
			tw = PAGE_W;
		return tw;
	}
	float size = block_font_size(b);
	int tw = bu_text_width(font, b->text ? b->text : "", size);
	if (tw < 60)
		tw = 60;
	if (tw > PAGE_W)
		tw = PAGE_W;
	return tw;
}

static void layout_block_w(struct web *w, bu_font *font, struct block *b, int maxw_in);
static void layout_flex(struct web *w, bu_font *font, struct block *c, int avail_w);
static void layout_block_regions(struct web *w, bu_font *font, struct block *b);

static void layout_block(struct web *w, bu_font *font, struct block *b)
{
	layout_block_w(w, font, b, PAGE_W);
}

static void layout_block_w(struct web *w, bu_font *font, struct block *b, int maxw_in)
{
	/* layout() runs on every draw: drop the previous wrap first
	 * (the old code leaked one lines array per frame). */
	free(b->lines);
	b->lines = NULL;
	b->nlines = 0;
	b->w = maxw_in;
	if (b->hidden) {
		b->h = 0;
		return;
	}
	if (b->type == B_INPUT || b->type == B_SUBMIT || b->type == B_SELECT) {
		b->h = 44;
		return;
	}
	if (b->type == B_TEXTAREA) {
		int rows = b->rows > 0 ? b->rows : 3;
		b->h = 12 + rows * 22;
		return;
	}
	if (b->type == B_HR) {
		b->h = 18;
		return;
	}
	if (b->type == B_IMG) {
		if (!b->img_tried)
			load_image(w, b);
		b->h = b->img_px ? b->img_h + 10 : 34;
		return;
	}
	float size = block_font_size(b);
	int indent = b->type == B_ITEM ? 22 : 0;
	int maxw = maxw_in - indent;
	if (maxw < 40)
		maxw = 40;
	/* build runs view: if no runs recorded, one plain run */
	struct run plain;
	struct run *runs = b->runs;
	int nruns = b->nruns;
	if (nruns == 0) {
		memset(&plain, 0, sizeof(plain));
		plain.off = 0;
		plain.len = b->text_len;
		plain.color = -1;
		runs = &plain;
		nruns = 1;
	}
	int cap_lines = 16;
	b->lines = malloc(sizeof(struct wline) * cap_lines);
	b->nlines = 0;
	struct wline *line = &b->lines[b->nlines];
	line->nsegs = 0;
	int pen_w = 0;
	for (int ri = 0; ri < nruns; ri++) {
		struct run *r = &runs[ri];
		int pos = r->off;
		int end = r->off + r->len;
		while (pos < end) {
			/* take chars until width would overflow */
			int start = pos;
			int wsum = pen_w;
			while (pos < end) {
				int clen = 1;
				unsigned char ch0 = (unsigned char)b->text[pos];
				if (ch0 >= 0xF0) clen = 4;
				else if (ch0 >= 0xE0) clen = 3;
				else if (ch0 >= 0xC0) clen = 2;
				if (pos + clen > end)
					clen = end - pos;
				if (b->text[pos] == '\n') {
					pos++;
					break;
				}
				char tmp[8];
				snprintf(tmp, sizeof(tmp), "%.*s", clen, b->text + pos);
				int cw = bu_text_width(font, tmp, size);
				if (wsum + cw > maxw && pos > start)
					break;
				wsum += cw;
				pos += clen;
				if (wsum >= maxw)
					break;
			}
			if (pos > start || line->nsegs == 0) {
				if (line->nsegs < 24) {
					struct lineseg *sg = &line->segs[line->nsegs++];
					sg->run = ri;
					sg->off = start;
					sg->len = pos - start;
				}
				pen_w = wsum;
			}
			if (pos < end || (end > 0 && b->text[end - 1] == '\n')) {
				b->nlines++;
				if (b->nlines >= cap_lines) {
					cap_lines *= 2;
					b->lines = realloc(b->lines, sizeof(struct wline) * cap_lines);
				}
				line = &b->lines[b->nlines];
				line->nsegs = 0;
				pen_w = 0;
				if (pos >= end)
					break;
			} else {
				break;
			}
		}
	}
	if (line->nsegs > 0)
		b->nlines++;
	b->h = (int)(b->nlines * size * 1.5f) + 8;
}

/* Tap + link regions for one laid-out block (shared by the main
 * loop and layout_flex, which lays out children itself). */
static void layout_block_regions(struct web *w, bu_font *font, struct block *b)
{
	if ((b->type == B_INPUT || b->type == B_SUBMIT ||
	     b->type == B_SELECT || b->type == B_TEXTAREA) && !b->hidden &&
	    w->ntaps < MAXLINKS) {
		struct tapregion *tr = &w->taps[w->ntaps++];
		tr->x = b->x;
		tr->y = b->y;
		tr->w = b->type == B_SUBMIT ? 240 : b->w;
		tr->h = b->h;
		tr->block = (int)(b - w->blocks);
	}
	/* link regions from lines */
	if (b->lines && b->runs) {
		float size = block_font_size(b);
		int indent = b->type == B_ITEM ? 22 : 0;
		for (int li = 0; li < b->nlines; li++) {
			int x = b->x + indent;
			struct wline *ln = &b->lines[li];
			for (int si = 0; si < ln->nsegs; si++) {
				struct lineseg *sg = &ln->segs[si];
				struct run *r = &b->runs[sg->run];
				char tmp[1024];
				snprintf(tmp, sizeof(tmp), "%.*s", sg->len, b->text + sg->off);
				int tw = bu_text_width(font, tmp, size);
				if (r->href[0] && w->nlinks < MAXLINKS) {
					struct linkregion *lr = &w->links[w->nlinks++];
					lr->x = x;
					lr->y = b->y + (int)(li * size * 1.5f);
					lr->w = tw;
					lr->h = (int)(size * 1.5f);
					snprintf(lr->href, sizeof(lr->href), "%s", r->href);
					lr->download = r->download;
				}
				x += tw;
			}
		}
	}
}

/* Lay out a flex container's children (blocks whose parent is c).
 * The container's own direct text, if any, is item 0. avail_w is
 * the width the container may use (PAGE_W at top level, the item
 * width when nested). */
static void layout_flex(struct web *w, bu_font *font, struct block *c, int avail_w)
{
	int ci = (int)(c - w->blocks);
	int kids[64];
	int nk = 0;
	bool own_text = false;
	if (c->text_len > 0) {
		bool ws = true;
		for (int k = 0; k < c->text_len; k++)
			if (c->text[k] != ' ' && c->text[k] != '\n' &&
			    c->text[k] != '\t' && c->text[k] != '\r')
				ws = false;
		own_text = !ws;
	}
	for (int i = 0; i < w->nblocks && nk < 64; i++)
		if (w->blocks[i].parent == ci)
			kids[nk++] = i;
	int n = nk + (own_text ? 1 : 0);
	if (n == 0) {
		c->h = 0;
		return;
	}
	int gap = c->gap >= 0 ? c->gap : 6;
	/* items: [0] is the container's own text (if any), the rest
	 * are kids[] in document order. */
	struct block *items[65];
	for (int k = 0; k < n; k++)
		items[k] = (k == 0 && own_text) ? c : &w->blocks[kids[k - (own_text ? 1 : 0)]];
	if (c->flex_dir == 0) {
		/* row: preferred widths, flex-grow distributes leftover */
		int prefs[65], widths[65], xs[65];
		long total = 0;
		double grows = 0;
		for (int k = 0; k < n; k++) {
			prefs[k] = block_pref_width(w, font, items[k]);
			total += prefs[k];
			grows += items[k]->flex_grow;
		}
		total += (long)gap * (n - 1);
		long left = avail_w - total;
		for (int k = 0; k < n; k++) {
			if (left > 0 && grows > 0)
				widths[k] = prefs[k] + (int)(left * items[k]->flex_grow / grows);
			else if (left < 0)
				widths[k] = (int)(prefs[k] * (double)avail_w / (total > 0 ? total : 1));
			else
				widths[k] = prefs[k];
			if (widths[k] < 60)
				widths[k] = 60;
		}
		long used = 0;
		for (int k = 0; k < n; k++)
			used += widths[k];
		used += (long)gap * (n - 1);
		long slack = avail_w - used;
		if (slack < 0)
			slack = 0;
		int x = c->x;
		int egap = gap;
		if (c->justify == 1)
			x = c->x + (int)(slack / 2);
		else if (c->justify == 2)
			x = c->x + (int)slack;
		else if (c->justify == 3 && n > 1)
			egap = gap + (int)(slack / (n - 1));
		else if (c->justify == 4 && n > 0) {
			egap = gap + (int)(slack / n);
			x = c->x + (int)(slack / (2 * n));
		}
		for (int k = 0; k < n; k++) {
			xs[k] = x;
			x += widths[k] + egap;
		}
		int hs[65];
		int maxh = 0;
		for (int k = 0; k < n; k++) {
			struct block *it = items[k];
			it->x = xs[k];	/* set before measuring: nested
					 * containers lay out relative to it */
			if (it->flex && it != c)
				layout_flex(w, font, it, widths[k]);
			else
				layout_block_w(w, font, it, widths[k]);
			hs[k] = it->h;
			if (hs[k] > maxh)
				maxh = hs[k];
		}
		for (int k = 0; k < n; k++) {
			struct block *it = items[k];
			if (c->flex_align == 2)
				it->y = c->y + (maxh - hs[k]) / 2;
			else if (c->flex_align == 3)
				it->y = c->y + maxh - hs[k];
			else
				it->y = c->y;
			if (c->flex_align == 0)
				it->h = maxh;
			if (it != c)	/* the container itself stays on the
					 * main loop; only children are owned */
				it->in_flex = true;
			layout_block_regions(w, font, it);
		}
		c->h = maxh;
	} else {
		/* column: items stack vertically */
		int y = c->y;
		for (int k = 0; k < n; k++) {
			struct block *it = items[k];
			int iw = c->flex_align == 0 ? avail_w : block_pref_width(w, font, it);
			it->x = c->flex_align == 2 ? c->x + (avail_w - iw) / 2
				: c->flex_align == 3 ? c->x + avail_w - iw : c->x;
			it->y = y;	/* set before measuring (nested flex) */
			if (it->flex && it != c)
				layout_flex(w, font, it, iw);
			else
				layout_block_w(w, font, it, iw);
			if (it != c)
				it->in_flex = true;
			layout_block_regions(w, font, it);
			y += it->h + gap;
		}
		c->h = y - c->y - gap;
		if (c->h < 0)
			c->h = 0;
	}
}

static void layout(struct web *w, bu_font *font)
{
	int y = 8;
	w->nlinks = 0;
	w->ntaps = 0;
	for (int i = 0; i < w->nblocks; i++) {
		struct block *b = &w->blocks[i];
		if (b->in_flex)
			continue;	/* positioned by its container */
		b->x = PAGE_X;
		b->y = y;
		if (b->flex && !b->hidden) {
			/* layout_flex also registers the container's own
			 * text regions (item 0), so no extra call here */
			layout_flex(w, font, b, PAGE_W);
		} else {
			layout_block(w, font, b);
			layout_block_regions(w, font, b);
		}
		y += b->h + 6;
	}
	w->content_h = y;
}

/* ---------- JavaScript (M3) ---------- */
/* Run the inline scripts collected by parse(). The status line only
 * changes when the scripts had a visible effect (console output, a
 * non-empty completion value, or an error); otherwise the title
 * status set by navigate() stays. */
static void run_scripts(struct web *w)
{
	if (w->js_len == 0 && w->js_ext == 0)
		return;
	if (w->js_len == 0) {
		snprintf(w->status, sizeof(w->status),
			 "JS: 外部スクリプト %d 件は未対応です", w->js_ext);
		return;
	}
	struct bd_js *js = bd_js_new();
	if (!js) {
		snprintf(w->status, sizeof(w->status), "JS: エンジン初期化失敗");
		return;
	}
	bd_js_set_title(js, w->title);
	char *out = NULL;
	int rc = bd_js_eval(js, w->js_src, &out);
	const char *con = bd_js_console(js);
	if (rc != 0) {
		snprintf(w->status, sizeof(w->status), "JS エラー: %.120s",
			 out ? out : "?");
	} else if (con[0]) {
		/* flatten console lines into the one-line status */
		char flat[141];
		size_t j = 0;
		for (size_t i = 0; con[i] && j < sizeof(flat) - 1; i++) {
			char c = con[i] == '\n' ? ' ' : con[i];
			if (c == ' ' && j > 0 && flat[j - 1] == ' ')
				continue;
			flat[j++] = c;
		}
		while (j > 0 && flat[j - 1] == ' ')
			j--;
		flat[j] = 0;
		snprintf(w->status, sizeof(w->status), "JS: %s", flat);
	} else if (out && out[0]) {
		snprintf(w->status, sizeof(w->status), "JS: %.140s", out);
	}
	free(out);
	bd_js_free(js);
}

/* ---------- navigation ---------- */
/* post_body != NULL: submit the form body with POST instead of GET. */
static void navigate_impl(struct web *w, const char *url, const char *post_body, bool push)
{
	char eff[1024];
	w->finding = false;
	w->nfind_hits = 0;
	snprintf(w->status, sizeof(w->status), "読み込み中… %s", url);
	int rc = post_body ? fetch_post(w, url, post_body, eff, sizeof(eff))
			    : fetch(w, url, eff, sizeof(eff));
	if (rc != 0) {
		snprintf(w->status, sizeof(w->status), "読み込めませんでした: %s", url);
		return;
	}
	free_page(w);
	w->title[0] = 0;
	parse(w, body_buf, eff);
	if (getenv("BD_WEB_DEBUG")) {
		for (int i = 0; i < w->nblocks; i++)
			fprintf(stderr, "block %d type %d lvl %d len %d: %.60s\n", i, w->blocks[i].type,
				w->blocks[i].level, w->blocks[i].text_len, w->blocks[i].text ? w->blocks[i].text : "");
	}
	snprintf(w->url, sizeof(w->url), "%s", eff);
	snprintf(w->status, sizeof(w->status), "%s", w->title[0] ? w->title : eff);
	run_scripts(w);
	if (push && !w->hist_nav) {
		if (w->hist_pos < w->hist_len - 1)
			w->hist_len = w->hist_pos + 1;
		if (w->hist_len < 32) {
			snprintf(w->hist[w->hist_len], 1024, "%s", eff);
			w->hist_pos = w->hist_len;
			w->hist_len++;
		}
	}
	w->hist_nav = false;
}

static void navigate(struct web *w, const char *url, bool push)
{
	navigate_impl(w, url, NULL, push);
}

static void navigate_post(struct web *w, const char *url, const char *body, bool push)
{
	navigate_impl(w, url, body, push);
}

static void bookmark_path(struct web *w, char *out, size_t n)
{
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/.config/braindesk/bookmarks.txt", home ? home : "/home/user");
	(void)w;
}

static void add_bookmark(struct web *w)
{
	char path[600], dir[600];
	bookmark_path(w, path, sizeof(path));
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = 0;
		char cmd[700];
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
		if (system(cmd) != 0)
			return;
	}
	FILE *fp = fopen(path, "a");
	if (fp) {
		fprintf(fp, "%s\n", w->url);
		fclose(fp);
		snprintf(w->status, sizeof(w->status), "ブックマークに追加しました");
	}
}

/* ---------- downloads ---------- */
static void downloads_dir(char *out, size_t n)
{
	const char *home = getenv("HOME");
	snprintf(out, n, "%s/Downloads", home ? home : "/home/user");
}

/* Binary-ish suffixes that are downloaded instead of rendered. */
static bool is_download_url(const char *url)
{
	/* strip query/fragment, then look at the last path segment */
	const char *p = url;
	const char *q = strchr(p, '?');
	const char *f = strchr(p, '#');
	size_t len = strlen(p);
	if (q && (size_t)(q - p) < len)
		len = (size_t)(q - p);
	if (f && (size_t)(f - p) < len)
		len = (size_t)(f - p);
	const char *slash = NULL;
	for (size_t i = 0; i < len; i++)
		if (p[i] == '/')
			slash = p + i;
	const char *base = slash ? slash + 1 : p;
	const char *dot = NULL;
	for (const char *s = base; s < p + len; s++)
		if (*s == '.')
			dot = s;
	if (!dot || dot == base)
		return false;
	static const char *exts[] = {
		"pdf", "zip", "gz", "tgz", "bz2", "xz", "7z", "apk",
		"mp3", "ogg", "wav", "flac", "mp4", "webm", "mkv",
		"epub", "mobi", "doc", "docx", "xls", "xlsx", "ppt",
		"pptx", "deb", "rpm", "iso", "img", "bin", "exe", "dmg",
		"torrent", NULL
	};
	size_t elen = (p + len) - dot - 1;
	for (int i = 0; exts[i]; i++)
		if (strlen(exts[i]) == elen && strncasecmp(dot + 1, exts[i], elen) == 0)
			return true;
	return false;
}

static void download_file(struct web *w, const char *url)
{
	char dir[600];
	downloads_dir(dir, sizeof(dir));
	char cmd[700];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
	if (system(cmd) != 0)
		return;
	/* file name = last path segment, sanitised */
	const char *base = strrchr(url, '/');
	base = base ? base + 1 : url;
	char name[256];
	int ni = 0;
	for (const char *p = base; *p && *p != '?' && *p != '#' && ni < 255; p++) {
		char ch = *p;
		if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '"' ||
		    ch == '<' || ch == '>' || ch == '|')
			ch = '_';
		name[ni++] = ch;
	}
	name[ni] = 0;
	if (ni == 0)
		snprintf(name, sizeof(name), "download");
	char path[900];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	snprintf(cmd, sizeof(cmd),
		 "curl -sL --max-time 60 -A 'Mozilla/5.0 (X11; Linux) bd-web' -o '%s' '%s' 2>/dev/null",
		 path, url);
	if (system(cmd) != 0) {
		snprintf(w->status, sizeof(w->status), "ダウンロードできませんでした");
		return;
	}
	struct stat st;
	if (stat(path, &st) != 0 || st.st_size == 0) {
		snprintf(w->status, sizeof(w->status), "ダウンロードできませんでした");
		return;
	}
	snprintf(w->status, sizeof(w->status), "ダウンロードしました: %s (%lld KB)",
		 name, (long long)(st.st_size / 1024));
}

/* ---------- page find ---------- */
/* Case-insensitive substring search over the parsed blocks.
 * (ASCII case folding; multibyte text matches byte-exactly.) */
static int find_run(struct web *w, const char *query)
{
	w->nfind_hits = 0;
	w->find_cur = 0;
	snprintf(w->find_query, sizeof(w->find_query), "%s", query);
	if (!query[0])
		return 0;
	size_t qlen = strlen(query);
	for (int i = 0; i < w->nblocks && w->nfind_hits < 256; i++) {
		struct block *b = &w->blocks[i];
		if (!b->text || b->text_len == 0)
			continue;
		const char *p = b->text;
		const char *end = b->text + b->text_len;
		while (p + qlen <= end && w->nfind_hits < 256) {
			const char *m = strcasestr(p, query);
			if (!m || m + qlen > end)
				break;
			w->find_hits[w->nfind_hits].block = i;
			w->find_hits[w->nfind_hits].off = (int)(m - b->text);
			w->find_hits[w->nfind_hits].len = (int)qlen;
			w->nfind_hits++;
			p = m + 1;
		}
	}
	if (w->nfind_hits > 0)
		snprintf(w->status, sizeof(w->status), "検索: %d 件中 1 番目", w->nfind_hits);
	else
		snprintf(w->status, sizeof(w->status), "検索: 見つかりませんでした");
	return w->nfind_hits;
}

/* Scroll so the current match is visible (block y is from the
 * last layout() pass, which draw() runs before presenting). */
static void find_scroll(struct web *w)
{
	if (w->nfind_hits == 0)
		return;
	int bi = w->find_hits[w->find_cur].block;
	int y = w->blocks[bi].y - 120;
	w->scroll = y > 0 ? y : 0;
	snprintf(w->status, sizeof(w->status), "検索: %d 件中 %d 番目",
		 w->nfind_hits, w->find_cur + 1);
}

static void find_next(struct web *w)
{
	if (w->nfind_hits == 0)
		return;
	w->find_cur = (w->find_cur + 1) % w->nfind_hits;
	find_scroll(w);
}

/* ---------- UI ---------- */
#define TOOLBAR_H 92
#define VIEW_H (480 - TOOLBAR_H)

static bu_color css_color(int packed)
{
	bu_color col = { (uint8_t)(packed >> 16), (uint8_t)(packed >> 8),
			 (uint8_t)packed, 255 };
	return col;
}

static void draw(bdapp *a, void *ctx)
{
	struct web *w = ctx;
	bu_canvas *c = a->canvas;
	bu_theme *t = a->theme;
	bdapp_frame(a);
	/* toolbar */
	bu_button(c, t, (bu_rect){ 10, 50, 44, 38 }, "◀", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 60, 50, 44, 38 }, "▶", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 110, 50, 44, 38 }, "更新", BU_BTN_NORMAL, false);
	bu_rect ub = { 162, 50, 438, 38 };
	bu_fill_round_rect(c, ub, 9, w->editing ? t->surface3 : t->surface);
	/* while finding, the address bar shows the query + position */
	char findbar[1100];
	const char *shown;
	if (w->finding && !w->editing) {
		if (w->nfind_hits > 0)
			snprintf(findbar, sizeof(findbar), "検索: %s  (%d/%d)",
				 w->find_query, w->find_cur + 1, w->nfind_hits);
		else if (w->find_query[0])
			snprintf(findbar, sizeof(findbar), "検索: %s  (0件)",
				 w->find_query);
		else
			snprintf(findbar, sizeof(findbar), "検索: ");
		shown = findbar;
	} else {
		shown = w->editing ? w->edit : (w->url[0] ? w->url : "about:home");
	}
	bu_draw_text(c, t->font, ub.x + 12, ub.y + 9, shown, 13, w->editing ? t->text : t->text_dim);
	bu_button(c, t, (bu_rect){ 608, 50, 74, 38 },
		  w->finding ? "次へ" : "検索", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 690, 50, 52, 38 }, "★", BU_BTN_NORMAL, false);
	bu_button(c, t, (bu_rect){ 750, 50, 104, 38 }, "ホーム", BU_BTN_NORMAL, false);
	/* content */
	int top = TOOLBAR_H;
	if (w->nblocks == 0 && !w->editing) {
		/* home page: bookmarks + history */
		bu_draw_text(c, t->font, 32, top + 10, "ブックマーク", 17, t->text);
		char path[600];
		bookmark_path(w, path, sizeof(path));
		FILE *fp = fopen(path, "r");
		int y = top + 44;
		if (fp) {
			char line[1024];
			while (fgets(line, sizeof(line), fp) && y < 460) {
				line[strcspn(line, "\n")] = 0;
				bu_draw_text(c, t->font, 48, y, line, 13, t->accent);
				y += 26;
			}
			fclose(fp);
		} else {
			bu_draw_text(c, t->font, 48, y, "まだありません (★で追加)", 13, t->text_dim);
		}
		/* downloads (~/Downloads) */
		y += 12;
		bu_draw_text(c, t->font, 32, y, "ダウンロード", 17, t->text);
		y += 34;
		char dld[600];
		downloads_dir(dld, sizeof(dld));
		DIR *dd = opendir(dld);
		if (dd) {
			struct dirent *de;
			int shown_dl = 0;
			while ((de = readdir(dd)) != NULL && y < 452 && shown_dl < 8) {
				if (de->d_name[0] == '.')
					continue;
				char fp2[700];
				snprintf(fp2, sizeof(fp2), "%s/%s", dld, de->d_name);
				struct stat st;
				char line[1100];
				if (stat(fp2, &st) == 0)
					snprintf(line, sizeof(line), "%s  (%lld KB)",
						 de->d_name, (long long)(st.st_size / 1024));
				else
					snprintf(line, sizeof(line), "%s", de->d_name);
				bu_draw_text(c, t->font, 48, y, line, 13, t->accent);
				y += 26;
				shown_dl++;
			}
			closedir(dd);
			if (shown_dl == 0)
				bu_draw_text(c, t->font, 48, y, "まだありません", 13, t->text_dim);
		} else {
			bu_draw_text(c, t->font, 48, y, "まだありません", 13, t->text_dim);
		}
		bu_draw_text_right(c, t->font, (bu_rect){ 0, 452, 850, 18 }, w->status, 11.5, t->text_faint);
	} else {
		layout(w, t->font);
		if (w->find_scroll_pending) {
			w->find_scroll_pending = false;
			find_scroll(w);
		}
		int maxscroll = w->content_h - VIEW_H + 20;
		if (w->scroll > maxscroll)
			w->scroll = maxscroll > 0 ? maxscroll : 0;
		for (int i = 0; i < w->nblocks; i++) {
			struct block *b = &w->blocks[i];
			int by = top + b->y - w->scroll;
			if (by + b->h < top || by > 480)
				continue;
			if (b->type == B_HR) {
				bu_fill_rect(c, (bu_rect){ b->x, by + 8, b->w, 1 }, t->line);
				continue;
			}
			if (b->type == B_IMG) {
				if (b->img_px) {
					/* blit scaled RGBA */
					for (int yy = 0; yy < b->img_h; yy++) {
						int cy = by + yy;
						if (cy < top || cy >= 480)
							continue;
						for (int xx = 0; xx < b->img_w; xx++) {
							const unsigned char *px = b->img_px + ((size_t)yy * b->img_w + xx) * 4;
							c->px[cy * c->w + b->x + xx] =
								((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) |
								((uint32_t)px[1] << 8) | px[2];
						}
					}
				} else {
					bu_fill_round_rect(c, (bu_rect){ b->x, by, 300, 26 }, 6, t->surface);
					bu_draw_text(c, t->font, b->x + 10, by + 4, "[画像]", 12, t->text_faint);
				}
				continue;
			}
			if (b->bg >= 0)
				bu_fill_rect(c, (bu_rect){ b->x - 6, by - 2, b->w + 12, b->h + 2 },
					     css_color(b->bg));
			if (b->type == B_INPUT) {
				const char *val = "", *nm = "";
				if (b->form >= 0 && b->field >= 0) {
					val = w->forms[b->form].fields[b->field].value;
					nm = w->forms[b->form].fields[b->field].name;
				}
				bool active = w->editing && w->edit_field == i;
				bu_rect box = { b->x, by, b->w, 38 };
				bu_fill_round_rect(c, box, 8, active ? t->surface3 : t->surface);
				bu_stroke_round_rect(c, box, 8, 1, active ? t->accent : t->line);
				const char *show = active ? w->edit : val;
				if (show[0])
					bu_draw_text(c, t->font, box.x + 12, by + 10, show, 14, t->text);
				else if (nm[0]) {
					char ph[96];
					snprintf(ph, sizeof(ph), "(%s)", nm);
					bu_draw_text(c, t->font, box.x + 12, by + 10, ph, 14, t->text_faint);
				}
				continue;
			}
			if (b->type == B_SUBMIT) {
				bu_button(c, t, (bu_rect){ b->x, by, 240, 38 },
					  b->text ? b->text : "送信", BU_BTN_PRIMARY, false);
				continue;
			}
			if (b->type == B_SELECT) {
				const char *lbl = b->text ? b->text : "";
				bu_rect box = { b->x, by, b->w, 38 };
				bu_fill_round_rect(c, box, 8, t->surface);
				bu_stroke_round_rect(c, box, 8, 1, t->line);
				if (lbl[0])
					bu_draw_text(c, t->font, box.x + 12, by + 10, lbl, 14, t->text);
				bu_draw_text(c, t->font, box.x + box.w - 30, by + 10, "▼", 12, t->text_faint);
				continue;
			}
			if (b->type == B_TEXTAREA) {
				const char *val = "", *nm = "";
				if (b->form >= 0 && b->field >= 0) {
					val = w->forms[b->form].fields[b->field].value;
					nm = w->forms[b->form].fields[b->field].name;
				}
				bool active = w->editing && w->edit_field == i;
				bu_rect box = { b->x, by, b->w, b->h };
				bu_fill_round_rect(c, box, 8, active ? t->surface3 : t->surface);
				bu_stroke_round_rect(c, box, 8, 1, active ? t->accent : t->line);
				const char *show = active ? w->edit : val;
				int rows = b->rows > 0 ? b->rows : 3;
				if (show[0]) {
					/* wrap on width and on '\n', at most rows lines */
					int ln = 0, pos = 0, len = (int)strlen(show);
					int maxw = b->w - 24;
					while (pos < len && ln < rows) {
						int start = pos, wsum = 0;
						while (pos < len && show[pos] != '\n') {
							int clen = 1;
							unsigned char ch0 = (unsigned char)show[pos];
							if (ch0 >= 0xF0) clen = 4;
							else if (ch0 >= 0xE0) clen = 3;
							else if (ch0 >= 0xC0) clen = 2;
							if (pos + clen > len)
								clen = len - pos;
							char tmp[8];
							snprintf(tmp, sizeof(tmp), "%.*s", clen, show + pos);
							int cw = bu_text_width(t->font, tmp, 14);
							if (wsum + cw > maxw && pos > start)
								break;
							wsum += cw;
							pos += clen;
						}
						if (pos > start) {
							char tmp[1024];
							snprintf(tmp, sizeof(tmp), "%.*s",
							 pos - start < 1023 ? pos - start : 1023,
							 show + start);
							bu_draw_text(c, t->font, box.x + 12,
								     by + 6 + ln * 22, tmp, 14, t->text);
							ln++;
						}
						if (pos < len && show[pos] == '\n') {
							pos++;
							if (pos >= len)
								break;
							/* empty line still consumes a row */
							if (pos == start + 1)
								ln++;
						}
						if (pos == start)
							break; /* no progress: avoid a loop */
					}
				} else if (nm[0]) {
					char ph[96];
					snprintf(ph, sizeof(ph), "(%s)", nm);
					bu_draw_text(c, t->font, box.x + 12, by + 6, ph, 14, t->text_faint);
				}
				continue;
			}
			float size = block_font_size(b);
			int indent = b->type == B_ITEM ? 22 : 0;
			if (b->type == B_ITEM)
				bu_fill_circle(c, b->x + 8, by + 10, 3, t->accent);
			if (!b->lines)
				continue;
			for (int li = 0; li < b->nlines; li++) {
				int ly = by + (int)(li * size * 1.5f);
				if (ly + size * 1.5f < top || ly > 480)
					continue;
				struct wline *ln = &b->lines[li];
				int segw[24];
				int linew = 0;
				for (int si = 0; si < ln->nsegs; si++) {
					struct lineseg *sg = &ln->segs[si];
					char tmp[1024];
					snprintf(tmp, sizeof(tmp), "%.*s", sg->len, b->text + sg->off);
					segw[si] = bu_text_width(t->font, tmp, size);
					linew += segw[si];
				}
				int x = b->x + indent;
				if (b->align == 1)
					x = b->x + (b->w - linew) / 2;
				else if (b->align == 2)
					x = b->x + b->w - linew;
				for (int si = 0; si < ln->nsegs; si++) {
					struct lineseg *sg = &ln->segs[si];
					char tmp[1024];
					snprintf(tmp, sizeof(tmp), "%.*s", sg->len, b->text + sg->off);
					/* find-in-page highlight: paint the match
					 * background before the glyphs */
					if (w->nfind_hits > 0) {
						for (int hi = 0; hi < w->nfind_hits; hi++) {
							if (w->find_hits[hi].block != i)
								continue;
							int mo = w->find_hits[hi].off;
							int ml = w->find_hits[hi].len;
							int s0 = sg->off, s1 = sg->off + sg->len;
							if (mo >= s1 || mo + ml <= s0)
								continue;
							int po = mo > s0 ? mo - s0 : 0;
							int pe = mo + ml < s1 ? mo + ml - s0 : sg->len;
							char pre[1024], mch[1024];
							snprintf(pre, sizeof(pre), "%.*s", po, tmp);
							snprintf(mch, sizeof(mch), "%.*s", pe - po, tmp + po);
							int px = x + bu_text_width(t->font, pre, size);
							int mw = bu_text_width(t->font, mch, size);
							bu_color hl = hi == w->find_cur
								? BU_RGB(255, 140, 0)
								: BU_RGB(255, 230, 128);
							bu_fill_rect(c, (bu_rect){ px, ly, mw,
								(int)(size * 1.5f) }, hl);
						}
					}
					bu_color ink = t->text;
					bool ink_default = true;
					if (b->runs && sg->run < b->nruns) {
						struct run *r = &b->runs[sg->run];
						if (r->href[0]) {
							ink = t->accent;
							ink_default = false;
						} else if (r->color >= 0) {
							ink = css_color(r->color);
							ink_default = false;
						} else if (b->color >= 0) {
							ink = css_color(b->color);
							ink_default = false;
						}
					} else if (b->color >= 0) {
						ink = css_color(b->color);
						ink_default = false;
					}
					/* No explicit color on a light background:
					 * pages assume the web default (dark) text
					 * unless they say otherwise; the theme's
					 * white text would be unreadable. Links
					 * keep their accent colour. */
					if (ink_default && b->bg >= 0) {
						int rr = (b->bg >> 16) & 255;
						int gg = (b->bg >> 8) & 255;
						int bb = b->bg & 255;
						if (rr * 299 + gg * 587 + bb * 114 > 140 * 1000)
							ink = BU_RGB(32, 32, 32);
					}
					bu_draw_text(c, t->font, x, ly, tmp, size, ink);
					x += segw[si];
				}
			}
		}
		/* scroll buttons */
		bu_button(c, t, (bu_rect){ 812, 100, 44, 52 }, "▲", BU_BTN_NORMAL, false);
		bu_button(c, t, (bu_rect){ 812, 404, 44, 52 }, "▼", BU_BTN_NORMAL, false);
		bu_draw_text(c, t->font, 10, 458, w->status, 11.5, t->text_faint);
	}
	if (w->editing) {
		int oskh = bu_osk_height();
		bu_osk_draw(c, t, &w->osk, (bu_rect){ 0, a->h - oskh, a->w, oskh });
	}
}

static bool inr(int x, int y, bu_rect r)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void url_encode(char *out, size_t n, const char *s)
{
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p && o + 4 < n; p++) {
		if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
		    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
		    *p == '.' || *p == '~') {
			out[o++] = *p;
		} else if (*p == ' ') {
			out[o++] = '+';
		} else {
			o += snprintf(out + o, n - o, "%%%02X", *p);
		}
	}
	out[o] = 0;
}

static void submit_form(struct web *w, int fi)
{
	if (fi < 0 || fi >= w->nforms)
		return;
	struct form *f = &w->forms[fi];
	/* Build the urlencoded body once; GET appends it to the URL,
	 * POST sends it as the request body. */
	char qs[8192];
	qs[0] = 0;
	bool first = true;
	for (int i = 0; i < f->nfields; i++) {
		struct formfield *ff = &f->fields[i];
		if (!ff->name[0])
			continue;
		char en[600], ev[2200];
		url_encode(en, sizeof(en), ff->name);
		url_encode(ev, sizeof(ev), ff->value);
		strncat(qs, first ? "" : "&", sizeof(qs) - strlen(qs) - 1);
		strncat(qs, en, sizeof(qs) - strlen(qs) - 1);
		strncat(qs, "=", sizeof(qs) - strlen(qs) - 1);
		strncat(qs, ev, sizeof(qs) - strlen(qs) - 1);
		first = false;
	}
	if (strcmp(f->method, "get") == 0) {
		char url[4096];
		snprintf(url, sizeof(url), "%s", f->action);
		if (qs[0])
			strncat(url, "?", sizeof(url) - strlen(url) - 1);
		strncat(url, qs, sizeof(url) - strlen(url) - 1);
		navigate(w, url, true);
	} else if (strcmp(f->method, "post") == 0) {
		navigate_post(w, f->action, qs, true);
	} else {
		snprintf(w->status, sizeof(w->status),
			 "この method は未対応です (method=%s)", f->method);
	}
}

static void onevent(bdapp *a, void *ctx, bu_event *ev, bool *changed, bool *quit)
{
	struct web *w = ctx;
	if (ev->type == BU_EV_KEY && ev->value == 1) {
		if (ev->code == 109) { /* PageDown */
			w->scroll += VIEW_H - 40;
			*changed = true;
		} else if (ev->code == 104) { /* PageUp */
			w->scroll -= VIEW_H - 40;
			if (w->scroll < 0)
				w->scroll = 0;
			*changed = true;
		}
		return;
	}
	if (ev->type != BU_EV_POINTER_DOWN)
		return;
	int x = ev->x, y = ev->y;
	*changed = true;
	if (w->editing) {
		int oskh = bu_osk_height();
		bu_rect area = { 0, a->h - oskh, a->w, oskh };
		if (y >= area.y) {
			char out[32];
			int r = bu_osk_hit(&w->osk, area, x, y, out, sizeof(out));
			if (r == OSK_TEXT && out[0])
				strncat(w->edit, out, sizeof(w->edit) - strlen(w->edit) - 1);
			else if (r == OSK_BACKSPACE) {
				int l = (int)strlen(w->edit);
				if (l > 0)
					w->edit[l - 1] = 0;
			} else if (r == OSK_ENTER) {
				w->editing = false;
				if (w->edit_field == -2) {
					/* find query: commit, highlight, jump */
					find_run(w, w->edit);
					find_scroll(w);
					w->edit_field = -1;
				} else if (w->edit_field >= 0) {
					/* form field edit: write back, no navigation */
					struct block *b = &w->blocks[w->edit_field];
					if (b->form >= 0 && b->field >= 0) {
						struct formfield *ff =
							&w->forms[b->form].fields[b->field];
						snprintf(ff->value, sizeof(ff->value), "%s", w->edit);
						free(b->text);
						b->text = strdup(w->edit);
						b->text_len = (int)strlen(w->edit);
						free(b->lines);
						b->lines = NULL;
						b->nlines = 0;
					}
					w->edit_field = -1;
				} else {
					char url[1024];
					if (strncmp(w->edit, "http", 4) != 0 && strchr(w->edit, '.'))
						snprintf(url, sizeof(url), "https://%s", w->edit);
					else if (strncmp(w->edit, "http", 4) != 0)
						snprintf(url, sizeof(url), "https://duckduckgo.com/html/?q=%s", w->edit);
					else
						snprintf(url, sizeof(url), "%s", w->edit);
					navigate(w, url, true);
				}
			}
			return;
		}
		w->editing = false;
		w->edit_field = -1;
		return;
	}
	if (inr(x, y, (bu_rect){ 10, 50, 44, 38 })) {
		if (w->hist_pos > 0) {
			w->hist_pos--;
			w->hist_nav = true;
			navigate(w, w->hist[w->hist_pos], true);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 60, 50, 44, 38 })) {
		if (w->hist_pos < w->hist_len - 1) {
			w->hist_pos++;
			w->hist_nav = true;
			navigate(w, w->hist[w->hist_pos], true);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 110, 50, 44, 38 })) {
		if (w->url[0])
			navigate(w, w->url, false);
		return;
	}
	if (inr(x, y, (bu_rect){ 162, 50, 438, 38 })) {
		/* leaving find mode clears the highlights */
		w->finding = false;
		w->nfind_hits = 0;
		w->editing = true;
		w->edit_field = -1;
		snprintf(w->edit, sizeof(w->edit), "%s", w->url);
		bu_osk_init(&w->osk);
		w->osk.kana = false;
		return;
	}
	if (inr(x, y, (bu_rect){ 608, 50, 74, 38 })) {
		if (!w->finding) {
			/* open the find bar (OSK) */
			w->finding = true;
			w->editing = true;
			w->edit_field = -2;
			snprintf(w->edit, sizeof(w->edit), "%s", w->find_query);
			bu_osk_init(&w->osk);
			w->osk.kana = false;
		} else if (!w->editing && w->nfind_hits == 0) {
			/* no matches yet: re-open the query entry */
			w->editing = true;
			w->edit_field = -2;
			snprintf(w->edit, sizeof(w->edit), "%s", w->find_query);
			bu_osk_init(&w->osk);
			w->osk.kana = false;
		} else if (!w->editing) {
			find_next(w);
		}
		return;
	}
	if (inr(x, y, (bu_rect){ 690, 50, 52, 38 })) {
		if (w->url[0])
			add_bookmark(w);
		return;
	}
	if (inr(x, y, (bu_rect){ 750, 50, 104, 38 })) {
		free_page(w);
		w->url[0] = 0;
		w->finding = false;
		w->nfind_hits = 0;
		snprintf(w->status, sizeof(w->status), "ホーム");
		return;
	}
	if (inr(x, y, (bu_rect){ 812, 100, 44, 52 })) {
		w->scroll -= 200;
		if (w->scroll < 0)
			w->scroll = 0;
		return;
	}
	if (inr(x, y, (bu_rect){ 812, 404, 44, 52 })) {
		w->scroll += 200;
		return;
	}
	/* home page bookmark links */
	if (w->nblocks == 0) {
		char path[600];
		bookmark_path(w, path, sizeof(path));
		FILE *fp = fopen(path, "r");
		if (fp) {
			char line[1024];
			int yy = TOOLBAR_H + 44;
			while (fgets(line, sizeof(line), fp)) {
				line[strcspn(line, "\n")] = 0;
				if (y >= yy && y < yy + 26 && line[0]) {
					fclose(fp);
					navigate(w, line, true);
					return;
				}
				yy += 26;
			}
			fclose(fp);
		}
		return;
	}
	/* form controls (content coordinates) */
	int cy = y - TOOLBAR_H + w->scroll;
	for (int i = 0; i < w->ntaps; i++) {
		struct tapregion *tr = &w->taps[i];
		if (x >= tr->x && x < tr->x + tr->w && cy >= tr->y && cy < tr->y + tr->h) {
			struct block *b = &w->blocks[tr->block];
			if (b->type == B_INPUT || b->type == B_TEXTAREA) {
				w->editing = true;
				w->edit_field = tr->block;
				const char *v = "";
				if (b->form >= 0 && b->field >= 0)
					v = w->forms[b->form].fields[b->field].value;
				snprintf(w->edit, sizeof(w->edit), "%s", v);
				bu_osk_init(&w->osk);
				w->osk.kana = false;
			} else if (b->type == B_SELECT) {
				/* tap cycles to the next option */
				if (b->form >= 0 && b->field >= 0) {
					struct formfield *ff =
						&w->forms[b->form].fields[b->field];
					if (ff->nopts > 0) {
						ff->sel = (ff->sel + 1) % ff->nopts;
						snprintf(ff->value, sizeof(ff->value), "%s",
							 ff->opt_val[ff->sel]);
						free(b->text);
						b->text = strdup(ff->opt_label[ff->sel]);
						b->text_len = (int)strlen(b->text);
					}
				}
			} else if (b->type == B_SUBMIT) {
				submit_form(w, b->form);
			}
			return;
		}
	}
	/* content links (content coordinates) */
	for (int i = 0; i < w->nlinks; i++) {
		struct linkregion *lr = &w->links[i];
		if (x >= lr->x && x < lr->x + lr->w && cy >= lr->y && cy < lr->y + lr->h) {
			if (lr->download || is_download_url(lr->href))
				download_file(w, lr->href);
			else
				navigate(w, lr->href, true);
			return;
		}
	}
}

int main(int argc, char **argv)
{
	struct web w;
	memset(&w, 0, sizeof(w));
	w.hist_pos = -1;
	w.edit_field = -1;
	body_buf = malloc(BODYMAX);
	const char *home = getenv("HOME");
	snprintf(w.cache_dir, sizeof(w.cache_dir), "%s/.cache/bd-web", home ? home : "/home/user");
	char cmd[700];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", w.cache_dir);
	if (system(cmd) != 0)
		return 1;
	snprintf(w.status, sizeof(w.status), "bd-web: URL をタップして入力");
	if (argc > 1 && strcmp(argv[1], "--shot") != 0)
		navigate(&w, argv[1], true);
	const char *shot = argc > 1 && strcmp(argv[1], "--shot") == 0 ? "bd-web.ppm" : NULL;
	if (shot) {
		navigate(&w, argc > 2 ? argv[2] : "https://example.com", true);
		/* test hook (like BD_WEB_DEBUG): run a find query so the
		 * --shot frame shows the highlights + first match. The
		 * scroll happens in draw(), after layout() (the font is
		 * only loaded inside bdapp_run). */
		const char *fq = getenv("BD_WEB_FIND");
		if (fq && fq[0]) {
			w.finding = true;
			find_run(&w, fq);
			w.find_scroll_pending = true;
		}
	}
	return bdapp_run("ブラウザ", shot, draw, onevent, NULL, &w);
}
