#include "bui_fb.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>

struct bu_display {
	int w, h;
	bool real;
	int fd;
	unsigned char *mem;
	long screensize;
	int bpp;
	int line_length;
	int xres, yres, yres_virtual;
	int cur_page;
	uint32_t *shadow;	/* last presented frame (memory backend) */
	int tty_fd;		/* console tty held in KD_GRAPHICS mode */
};

bu_display *bu_display_open(int w, int h)
{
	bu_display *d = calloc(1, sizeof(*d));
	d->w = w; d->h = h;
	d->fd = -1;
	d->tty_fd = -1;
	const char *path = getenv("BUI_FB");
	if (!path) path = "/dev/fb0";
	int fd = open(path, O_RDWR);
	if (fd >= 0) {
		struct fb_var_screeninfo v;
		struct fb_fix_screeninfo f;
		if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0 &&
		    ioctl(fd, FBIOGET_FSCREENINFO, &f) == 0 &&
		    (v.bits_per_pixel == 16 || v.bits_per_pixel == 32)) {
			d->real = true;
			d->fd = fd;
			d->bpp = v.bits_per_pixel;
			d->line_length = f.line_length;
			d->xres = v.xres; d->yres = v.yres;
			d->yres_virtual = v.yres_virtual;
			d->screensize = f.smem_len;
			d->mem = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE,
				      MAP_SHARED, fd, 0);
			if (d->mem == MAP_FAILED) {
				d->real = false;
				close(fd);
				d->fd = -1;
				d->mem = NULL;
			} else {
				d->w = v.xres; d->h = v.yres;
				/*
				 * Take the console tty into graphics mode so
				 * fbcon stops painting boot text over our
				 * frames. Restored in bu_display_close().
				 */
				d->tty_fd = open("/dev/tty1", O_RDWR);
				if (d->tty_fd < 0)
					d->tty_fd = open("/dev/console", O_RDWR);
				if (d->tty_fd >= 0 &&
				    ioctl(d->tty_fd, KDSETMODE, KD_GRAPHICS) < 0) {
					close(d->tty_fd);
					d->tty_fd = -1;
				}
				return d;
			}
		} else {
			close(fd);
		}
	}
	d->shadow = calloc((size_t)w * h, sizeof(uint32_t));
	return d;
}

void bu_display_close(bu_display *d)
{
	if (!d) return;
	if (d->tty_fd >= 0) {
		ioctl(d->tty_fd, KDSETMODE, KD_TEXT);
		close(d->tty_fd);
	}
	if (d->mem) munmap(d->mem, d->screensize);
	if (d->fd >= 0) close(d->fd);
	free(d->shadow);
	free(d);
}

int bu_display_width(bu_display *d) { return d->w; }
int bu_display_height(bu_display *d) { return d->h; }
bool bu_display_is_real(bu_display *d) { return d->real; }

void bu_display_present(bu_display *d, bu_canvas *c, bu_rect damage)
{
	if (!d->real) {
		memcpy(d->shadow, c->px, (size_t)d->w * d->h * sizeof(uint32_t));
		return;
	}
	int x0 = damage.x < 0 ? 0 : damage.x;
	int y0 = damage.y < 0 ? 0 : damage.y;
	int x1 = damage.x + damage.w > d->xres ? d->xres : damage.x + damage.w;
	int y1 = damage.y + damage.h > d->yres ? d->yres : damage.y + damage.h;
	/* double buffering when the virtual height allows it */
	int page = 0;
	if (d->yres_virtual >= d->yres * 2) {
		page = d->cur_page ^ 1;
	}
	long page_off = (long)page * d->yres * d->line_length;
	for (int y = y0; y < y1; y++) {
		unsigned char *row = d->mem + page_off + (long)y * d->line_length;
		for (int x = x0; x < x1; x++) {
			uint32_t v = c->px[y * c->w + x];
			uint32_t r = (v >> 16) & 0xff, g = (v >> 8) & 0xff, b = v & 0xff;
			if (d->bpp == 16) {
				uint16_t *p = (uint16_t *)(row + x * 2);
				*p = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
			} else {
				uint32_t *p = (uint32_t *)(row + x * 4);
				*p = v;
			}
		}
	}
	if (page != d->cur_page || d->yres_virtual >= d->yres * 2) {
		struct fb_var_screeninfo v;
		if (ioctl(d->fd, FBIOGET_VSCREENINFO, &v) == 0) {
			v.yoffset = page * d->yres;
			v.activate = FB_ACTIVATE_VBL;
			ioctl(d->fd, FBIOPAN_DISPLAY, &v);
			d->cur_page = page;
		}
	}
}

bool bu_display_dump_ppm(bu_display *d, const char *path)
{
	if (d->real || !d->shadow) return false;
	bu_canvas tmp = { d->w, d->h, d->shadow, { 0, 0, d->w, d->h } };
	return bu_canvas_write_ppm(&tmp, path);
}
