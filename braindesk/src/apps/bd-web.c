/* bd-web: braindesk's own web browser (M1).
 * In-house engine on libbrainui: fetch with curl, a small
 * HTML parser producing blocks (headings, paragraphs, lists,
 * images), a wrapping layout with tappable link regions,
 * history, and bookmarks. No JavaScript yet (M3 plans a
 * Duktape embedding); forms and a CSS subset are M2.
 */
#include "bdapp.h"
#include "../bui_osk.h"
#include <ctype.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb_image.h"

#define MAXBLOCKS 512
#define MAXRUNS 4096
#define MAXLINKS 512
#define MAXIMGS 64
#define BODYMAX (2 * 1024 * 1024)

enum { B_PARA, B_HEAD, B_ITEM, B_PRE, B_IMG, B_HR };

struct run {
	int block;
	int off;		/* byte offset into block text */
	int len;
	char href[512];		/* empty = not a link */
	bool bold;
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
	/* layout results */
	int y, h;
	struct wline *lines;
	int nlines;
	unsigned char *img_px;	/* decoded RGBA (scaled) */
	int img_w, img_h;
	bool img_tried;
};

struct linkregion {
	int x, y, w, h;
	char href[512];
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
};

static char *body_buf;
static int body_len;

/* ---------- fetch ---------- */
static int fetch(struct web *w, const char *url, char *effective, size_t en)
{
	char tmp[600], cmd[2048];
	snprintf(tmp, sizeof(tmp), "%s/fetch.html", w->cache_dir);
	snprintf(cmd, sizeof(cmd),
		 "curl -sL --max-time 25 -A 'Mozilla/5.0 (X11; Linux) bd-web' -o '%s' -w '%%{url_effective}' '%s' 2>/dev/null",
		 tmp, url);
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

static struct block *new_block(struct web *w, int type, int level)
{
	if (w->nblocks >= MAXBLOCKS)
		return NULL;
	struct block *b = &w->blocks[w->nblocks++];
	memset(b, 0, sizeof(*b));
	b->type = type;
	b->level = level;
	b->text = calloc(1, 1);
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

static void parse(struct web *w, const char *html, const char *page_url)
{
	struct block *cur = NULL;
	char cur_href[512] = "";
	bool cur_bold = false;
	bool skip = false;	/* inside script/style/head */
	int pre = 0;
	const char *p = html;
	char textbuf[8192];
	int tlen = 0;
	/* runs are recorded per block as (offset,len,href,bold) using a temp list */
	static struct { int off, len; char href[512]; bool bold; } truns[2048];
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
				truns[ntruns].bold = cur_bold; \
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
			} \
		} \
		ntruns = 0; \
		run_block = NULL; \
	} while (0)

	while (*p) {
		if (*p == '<') {
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
			if (strcmp(name, "script") == 0 || strcmp(name, "style") == 0 || strcmp(name, "head") == 0) {
				if (!closing)
					skip = true;
				else
					skip = false;
				p = gt + 1;
				continue;
			}
			if (skip) {
				p = gt + 1;
				continue;
			}
			if (strcmp(name, "title") == 0 && !closing) {
				const char *e = strcasestr(gt + 1, "</title>");
				if (e) {
					snprintf(w->title, sizeof(w->title), "%.*s", (int)(e - (gt + 1)), gt + 1);
					decode_entities(w->title);
				}
			}
			if (!closing && (strcmp(name, "p") == 0 || strcmp(name, "div") == 0 ||
					 strcmp(name, "section") == 0 || strcmp(name, "article") == 0 ||
					 strcmp(name, "blockquote") == 0 || strcmp(name, "table") == 0)) {
				CLOSE_BLOCK();
				cur = new_block(w, B_PARA, 0);
			} else if (!closing && name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == 0) {
				CLOSE_BLOCK();
				cur = new_block(w, B_HEAD, name[1] - '0');
			} else if (closing && (name[0] == 'h' && name[1] >= '1' && name[1] <= '6')) {
				CLOSE_BLOCK();
				cur = NULL;
			} else if (!closing && strcmp(name, "li") == 0) {
				CLOSE_BLOCK();
				cur = new_block(w, B_ITEM, 0);
			} else if (!closing && strcmp(name, "pre") == 0) {
				CLOSE_BLOCK();
				cur = new_block(w, B_PRE, 0);
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
			} else if (!closing && strcmp(name, "a") == 0) {
				FLUSH_TEXT();
				char href[512];
				get_attr(tag, "href", href, sizeof(href));
				if (href[0] && href[0] != '#')
					resolve_url(page_url, href, cur_href, sizeof(cur_href));
				else
					cur_href[0] = 0;
			} else if (closing && strcmp(name, "a") == 0) {
				FLUSH_TEXT();
				cur_href[0] = 0;
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
			}
			p = gt + 1;
			continue;
		}
		/* text */
		if (!skip) {
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
	CLOSE_BLOCK();
	/* drop empty blocks */
	int out = 0;
	for (int i = 0; i < w->nblocks; i++) {
		struct block *b = &w->blocks[i];
		bool ws_only = true;
		for (int k = 0; k < b->text_len; k++)
			if (b->text[k] != ' ' && b->text[k] != '\n' && b->text[k] != '\t' && b->text[k] != '\r')
				ws_only = false;
		if (b->type != B_IMG && b->type != B_HR && (b->text_len == 0 || ws_only)) {
			free(b->text);
			free(b->runs);
			continue;
		}
		if (out != i)
			w->blocks[out] = *b;
		out++;
	}
	w->nblocks = out;
#undef FLUSH_TEXT
#undef CLOSE_BLOCK
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
#define PAGE_W 800
#define PAGE_X 32

static float block_font_size(const struct block *b)
{
	if (b->type == B_HEAD) {
		static const float hs[] = { 26, 22, 19, 17, 15.5, 14.5 };
		return hs[b->level < 1 ? 0 : b->level > 6 ? 5 : b->level - 1];
	}
	if (b->type == B_PRE)
		return 12.5f;
	return 14.5f;
}

static void layout_block(struct web *w, bu_font *font, struct block *b)
{
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
	int maxw = PAGE_W - indent;
	/* build runs view: if no runs recorded, one plain run */
	struct run plain;
	struct run *runs = b->runs;
	int nruns = b->nruns;
	if (nruns == 0) {
		memset(&plain, 0, sizeof(plain));
		plain.off = 0;
		plain.len = b->text_len;
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

static void layout(struct web *w, bu_font *font)
{
	int y = 8;
	w->nlinks = 0;
	for (int i = 0; i < w->nblocks; i++) {
		struct block *b = &w->blocks[i];
		b->y = y;
		layout_block(w, font, b);
		y += b->h + 6;
		/* link regions from lines */
		if (b->lines && b->runs) {
			float size = block_font_size(b);
			int indent = b->type == B_ITEM ? 22 : 0;
			for (int li = 0; li < b->nlines; li++) {
				int x = PAGE_X + indent;
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
					}
					x += tw;
				}
			}
		}
	}
	w->content_h = y;
}

/* ---------- navigation ---------- */
static void navigate(struct web *w, const char *url, bool push)
{
	char eff[1024];
	snprintf(w->status, sizeof(w->status), "読み込み中… %s", url);
	if (fetch(w, url, eff, sizeof(eff)) != 0) {
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

/* ---------- UI ---------- */
#define TOOLBAR_H 92
#define VIEW_H (480 - TOOLBAR_H)

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
	bu_rect ub = { 162, 50, 520, 38 };
	bu_fill_round_rect(c, ub, 9, w->editing ? t->surface3 : t->surface);
	const char *shown = w->editing ? w->edit : (w->url[0] ? w->url : "about:home");
	bu_draw_text(c, t->font, ub.x + 12, ub.y + 9, shown, 13, w->editing ? t->text : t->text_dim);
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
		bu_draw_text_right(c, t->font, (bu_rect){ 0, 452, 850, 18 }, w->status, 11.5, t->text_faint);
	} else {
		layout(w, t->font);
		int maxscroll = w->content_h - VIEW_H + 20;
		if (w->scroll > maxscroll)
			w->scroll = maxscroll > 0 ? maxscroll : 0;
		for (int i = 0; i < w->nblocks; i++) {
			struct block *b = &w->blocks[i];
			int by = top + b->y - w->scroll;
			if (by + b->h < top || by > 480)
				continue;
			if (b->type == B_HR) {
				bu_fill_rect(c, (bu_rect){ PAGE_X, by + 8, PAGE_W, 1 }, t->line);
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
							c->px[cy * c->w + PAGE_X + xx] =
								((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) |
								((uint32_t)px[1] << 8) | px[2];
						}
					}
				} else {
					bu_fill_round_rect(c, (bu_rect){ PAGE_X, by, 300, 26 }, 6, t->surface);
					bu_draw_text(c, t->font, PAGE_X + 10, by + 4, "[画像]", 12, t->text_faint);
				}
				continue;
			}
			float size = block_font_size(b);
			int indent = b->type == B_ITEM ? 22 : 0;
			if (b->type == B_ITEM)
				bu_fill_circle(c, PAGE_X + 8, by + 10, 3, t->accent);
			if (!b->lines)
				continue;
			for (int li = 0; li < b->nlines; li++) {
				int ly = by + (int)(li * size * 1.5f);
				if (ly + size * 1.5f < top || ly > 480)
					continue;
				int x = PAGE_X + indent;
				struct wline *ln = &b->lines[li];
				for (int si = 0; si < ln->nsegs; si++) {
					struct lineseg *sg = &ln->segs[si];
					char tmp[1024];
					snprintf(tmp, sizeof(tmp), "%.*s", sg->len, b->text + sg->off);
					bu_color ink = t->text;
					if (b->runs && sg->run < b->nruns && b->runs[sg->run].href[0])
						ink = t->accent;
					if (b->type == B_HEAD)
						ink = t->text;
					bu_draw_text(c, t->font, x, ly, tmp, size, ink);
					x += bu_text_width(t->font, tmp, size);
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
				char url[1024];
				if (strncmp(w->edit, "http", 4) != 0 && strchr(w->edit, '.'))
					snprintf(url, sizeof(url), "https://%s", w->edit);
				else if (strncmp(w->edit, "http", 4) != 0)
					snprintf(url, sizeof(url), "https://duckduckgo.com/html/?q=%s", w->edit);
				else
					snprintf(url, sizeof(url), "%s", w->edit);
				navigate(w, url, true);
			}
			return;
		}
		w->editing = false;
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
	if (inr(x, y, (bu_rect){ 162, 50, 520, 38 })) {
		w->editing = true;
		snprintf(w->edit, sizeof(w->edit), "%s", w->url);
		bu_osk_init(&w->osk);
		w->osk.kana = false;
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
	/* content links (content coordinates) */
	int cy = y - TOOLBAR_H + w->scroll;
	for (int i = 0; i < w->nlinks; i++) {
		struct linkregion *lr = &w->links[i];
		if (x >= lr->x && x < lr->x + lr->w && cy >= lr->y && cy < lr->y + lr->h) {
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
	if (shot)
		navigate(&w, argc > 2 ? argv[2] : "https://example.com", true);
	return bdapp_run("ブラウザ", shot, draw, onevent, NULL, &w);
}
