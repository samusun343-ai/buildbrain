#include <stdio.h>
#include <stdlib.h>
#include <cairo.h>
#include <glib.h>
/* librsvg prototypes (headers not installed; ABI is stable) */
typedef struct _RsvgHandle RsvgHandle;
typedef struct { int width, height; double em, ex; } RsvgDimensionData;
extern RsvgHandle *rsvg_handle_new_from_file(const char *file_name, void **error);
extern void rsvg_handle_get_dimensions(RsvgHandle *handle, RsvgDimensionData *data);
extern gboolean rsvg_handle_render_cairo(RsvgHandle *handle, cairo_t *cr);
extern void g_object_unref(void *obj);
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: svg2png in.svg out.png size\n"); return 2; }
    int size = atoi(argv[3]);
    void *err = NULL;
    RsvgHandle *h = rsvg_handle_new_from_file(argv[1], &err);
    if (!h) { fprintf(stderr, "load failed\n"); return 1; }
    RsvgDimensionData d; rsvg_handle_get_dimensions(h, &d);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(s);
    double sc = (double)size / (d.width > 0 ? d.width : size);
    cairo_scale(cr, sc, sc);
    rsvg_handle_render_cairo(h, cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(s, argv[2]);
    cairo_surface_destroy(s);
    g_object_unref(h);
    return 0;
}
