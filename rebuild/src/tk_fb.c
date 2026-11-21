/* tk_fb.c - fbdev backend (new implementation).
 * Design follows the audited "keep" parts of the old bui_fb: plain
 * /dev/fb0 mmap, format conversion at flush time, no external libs.
 * The old code is not copied; this is written against <linux/fb.h>.
 * Host builds compile this file; runtime use is on the device/guest. */
#include "tk.h"
#include "tk_internal.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct tk_fb {
    int fd;
    uint8_t *mem;
    size_t mem_len;
    int w, h;
    int bpp;          /* bytes per pixel on device */
    int line_len;
    struct fb_var_screeninfo var;
    bool is_mem;      /* M3: memory-backed (tk_fb_open_mem) */
    bool dim_warned;  /* canvas/fb size mismatch already logged */
};

tk_fb *tk_fb_open(const char *dev)
{
    if (!dev) dev = "/dev/fb0";
    tk_fb *fb = calloc(1, sizeof(*fb));
    if (!fb) return NULL;
    fb->fd = open(dev, O_RDWR);
    if (fb->fd < 0) { free(fb); return NULL; }
    struct fb_fix_screeninfo fix;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) < 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        close(fb->fd); free(fb); return NULL;
    }
    fb->w = (int)fb->var.xres;
    fb->h = (int)fb->var.yres;
    fb->bpp = (int)fb->var.bits_per_pixel / 8;
    fb->line_len = (int)fix.line_length;
    fb->mem_len = fix.smem_len;
    fb->mem = mmap(NULL, fb->mem_len, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        close(fb->fd); free(fb); return NULL;
    }
    return fb;
}

void tk_fb_close(tk_fb *fb)
{
    if (!fb) return;
    if (fb->is_mem) {
        free(fb->mem);
    } else {
        if (fb->mem && fb->mem != MAP_FAILED) munmap(fb->mem, fb->mem_len);
        if (fb->fd >= 0) close(fb->fd);
    }
    free(fb);
}

int tk_fb_width(const tk_fb *fb) { return fb->w; }
int tk_fb_height(const tk_fb *fb) { return fb->h; }
const uint8_t *tk_fb_pixels(const tk_fb *fb) { return fb ? fb->mem : NULL; }
int tk_fb_line_len(const tk_fb *fb) { return fb ? fb->line_len : 0; }

tk_fb *tk_fb_open_mem(int w, int h, int bits_per_pixel)
{
    if (w <= 0 || h <= 0) return NULL;
    if (bits_per_pixel != 16 && bits_per_pixel != 32) return NULL;
    tk_fb *fb = calloc(1, sizeof(*fb));
    if (!fb) return NULL;
    fb->is_mem = true;
    fb->fd = -1;
    fb->w = w; fb->h = h;
    fb->bpp = bits_per_pixel / 8;
    fb->line_len = w * fb->bpp;
    fb->mem_len = (size_t)fb->line_len * (size_t)h;
    fb->mem = calloc(1, fb->mem_len);
    if (!fb->mem) { free(fb); return NULL; }
    fb->var.bits_per_pixel = (unsigned)bits_per_pixel;
    return fb;
}

static uint16_t to_rgb565(uint32_t px)
{
    uint8_t r = (uint8_t)(px >> 16), g = (uint8_t)(px >> 8), b = (uint8_t)px;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Convert one canvas rect to the device; returns device bytes
 * written. The rect is clipped to the framebuffer. M5: the pixel
 * loop is specialised per device depth (the depth branch used to
 * be re-taken for every pixel). */
static size_t flush_region(tk_fb *fb, tk_canvas *c, tk_rect r)
{
    int x0 = r.x > 0 ? r.x : 0;
    int y0 = r.y > 0 ? r.y : 0;
    int x1 = r.x + r.w < fb->w ? r.x + r.w : fb->w;
    int y1 = r.y + r.h < fb->h ? r.y + r.h : fb->h;
    if (x1 <= x0 || y1 <= y0) return 0;
    uint32_t *src = tk_canvas_pixels(c);
    int cw = tk_canvas_width(c);
    size_t yoff = (size_t)fb->var.yoffset * (size_t)fb->line_len;
    if (fb->bpp == 2) {
        for (int y = y0; y < y1; y++) {
            uint16_t *row = (uint16_t *)(fb->mem + yoff +
                                         (size_t)y * fb->line_len);
            const uint32_t *srow = &src[(size_t)y * cw];
            for (int x = x0; x < x1; x++)
                row[x] = to_rgb565(srow[x]);
        }
    } else if (fb->bpp == 4) {
        for (int y = y0; y < y1; y++) {
            uint32_t *row = (uint32_t *)(fb->mem + yoff +
                                         (size_t)y * fb->line_len);
            const uint32_t *srow = &src[(size_t)y * cw];
            for (int x = x0; x < x1; x++)
                row[x] = srow[x] & 0x00FFFFFFu;
        }
    } else if (fb->bpp == 3) {
        for (int y = y0; y < y1; y++) {
            uint8_t *row = fb->mem + yoff + (size_t)y * fb->line_len;
            const uint32_t *srow = &src[(size_t)y * cw];
            for (int x = x0; x < x1; x++) {
                uint32_t px = srow[x];
                uint8_t *p = row + (size_t)x * 3;
                p[0] = (uint8_t)px; p[1] = (uint8_t)(px >> 8);
                p[2] = (uint8_t)(px >> 16);
            }
        }
    }
    return (size_t)(x1 - x0) * (size_t)(y1 - y0) * (size_t)fb->bpp;
}

/* Replay one canvas shift in device memory (M5 scroll blit): the
 * same memmove the canvas performed, in device format. */
static void fb_shift_region(tk_fb *fb, tk_rect r, int dy)
{
    int x0 = r.x > 0 ? r.x : 0;
    int y0 = r.y > 0 ? r.y : 0;
    int x1 = r.x + r.w < fb->w ? r.x + r.w : fb->w;
    int y1 = r.y + r.h < fb->h ? r.y + r.h : fb->h;
    if (x1 <= x0 || y1 <= y0 || dy == 0) return;
    int h = y1 - y0;
    if (dy >= h || dy <= -h) return;
    size_t yoff = (size_t)fb->var.yoffset * (size_t)fb->line_len;
    size_t seg = (size_t)(x1 - x0) * (size_t)fb->bpp;
    size_t xoff = (size_t)x0 * (size_t)fb->bpp;
    if (dy > 0) {
        for (int y = h - 1; y >= dy; y--)
            memmove(fb->mem + yoff + (size_t)(y0 + y) * fb->line_len + xoff,
                    fb->mem + yoff + (size_t)(y0 + y - dy) * fb->line_len + xoff,
                    seg);
    } else {
        for (int y = 0; y < h + dy; y++)
            memmove(fb->mem + yoff + (size_t)(y0 + y) * fb->line_len + xoff,
                    fb->mem + yoff + (size_t)(y0 + y - dy) * fb->line_len + xoff,
                    seg);
    }
}

/* The flush entry points require the canvas to match the
 * framebuffer exactly. A mismatch used to fail silently, which
 * hid an integration defect for a long time (apps built at a
 * hardcoded size never drew anything on the real panel). Log the
 * first mismatch per fb instance so the failure is discoverable,
 * but keep the no-transfer behaviour: shipping a wrong-sized
 * canvas would corrupt the frame. */
static bool fb_dims_match(tk_fb *fb, tk_canvas *c, const char *fn)
{
    if (tk_canvas_width(c) == fb->w && tk_canvas_height(c) == fb->h)
        return true;
    if (!fb->dim_warned) {
        fb->dim_warned = true;
        fprintf(stderr,
                "tk_fb: %s: canvas %dx%d does not match framebuffer "
                "%dx%d; nothing transferred (further mismatches "
                "suppressed)\n",
                fn, tk_canvas_width(c), tk_canvas_height(c),
                fb->w, fb->h);
    }
    return false;
}

bool tk_fb_flush(tk_fb *fb, tk_canvas *c)
{
    if (!fb || !c) return false;
    if (!fb_dims_match(fb, c, "tk_fb_flush"))
        return false;
    (void)flush_region(fb, c, (tk_rect){0, 0, fb->w, fb->h});
    tk_canvas_damage_clear(c);
    tk_canvas_blits_clear(c); /* full copy supersedes the shifts */
    return true;
}

size_t tk_fb_flush_damage(tk_fb *fb, tk_canvas *c)
{
    if (!fb || !c) return 0;
    if (!fb_dims_match(fb, c, "tk_fb_flush_damage"))
        return 0;
    /* M5: replay recorded shifts in device memory first, so the
     * retained area needs no conversion traffic at all; only the
     * exposed bands (in the damage list) are converted below. */
    size_t nb = tk_canvas_blit_count(c);
    for (size_t i = 0; i < nb; i++) {
        tk_blit b = tk_canvas_blit_at(c, i);
        fb_shift_region(fb, b.rect, b.dy);
    }
    tk_canvas_blits_clear(c);
    size_t total = 0;
    size_t n = tk_canvas_damage_count(c);
    for (size_t i = 0; i < n; i++)
        total += flush_region(fb, c, tk_canvas_damage_rect(c, i));
    tk_canvas_damage_clear(c);
    return total;
}
