/* iconcache - decoded/scaled icon cache for the shell.
 *
 * Why this exists (overhead/copy principle, DESIGN-shell-v2.md
 * section 1): decoding an icon file (PNG/JPEG via stb_image) and
 * scaling it costs orders of magnitude more than blitting the
 * result. The same icon is shown on the desktop grid, in launcher
 * lists and in the task area, and the shell repaints often; without
 * a cache every repaint would re-decode and re-scale. This cache
 * makes decode+scale happen exactly once per (path, target size)
 * until eviction.
 *
 * Design:
 *   - Key: (path, target_w, target_h). Value: scaled RGBA8888
 *     pixels in the toolkit canvas packing (a<<24|r<<16|g<<8|b),
 *     ready to hand to an ImageView or a blit with no conversion.
 *   - Eviction: LRU under a byte budget set at creation. An item
 *     larger than the whole budget is returned from a transient
 *     slot and never cached (documented, counted as a miss).
 *   - The decoder is injectable for tests; the default decoder uses
 *     the toolkit's exported tk_image_load_rgba (stb_image).
 *   - Returned pointers are owned by the cache and stay valid until
 *     the next ic_get/ic_cache_invalidate call on the same cache.
 *   - Scaling is nearest-neighbour: icons are shown at fixed small
 *     sizes and this matches the toolkit ImageView's own scaling,
 *     so the cached pixels are identical to what an uncached
 *     ImageView would paint.
 */
#ifndef BD_ICONCACHE_H
#define BD_ICONCACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decoder: on success returns true and hands over a malloc'd
 * RGBA8888 pixel buffer (canvas packing) via out_px. */
typedef bool (*ic_decode_fn)(void *userdata, const char *path,
                             uint32_t **out_px, int *out_w, int *out_h);

typedef struct ic_cache ic_cache;

/* budget_bytes: total pixel bytes the cache may hold. */
ic_cache *ic_cache_new(size_t budget_bytes);
void ic_cache_free(ic_cache *c);

/* Replace the decoder (default: tk_image_load_rgba). */
void ic_cache_set_decoder(ic_cache *c, ic_decode_fn fn, void *userdata);

/* Get the icon at path scaled to (target_w, target_h). Returns the
 * cached pixel buffer (cache-owned) or NULL on decode failure.
 * out_w/out_h receive the target size on success. */
const uint32_t *ic_get(ic_cache *c, const char *path,
                       int target_w, int target_h,
                       int *out_w, int *out_h);

/* Drop every size variant of path (e.g. after the file changed). */
void ic_cache_invalidate(ic_cache *c, const char *path);

size_t ic_cache_bytes(const ic_cache *c);
void ic_cache_stats(const ic_cache *c, size_t *hits, size_t *misses,
                    size_t *evictions);

/* Resolve a .desktop Icon value to an existing file path (M4).
 * An absolute/relative path is used as-is when it exists. A bare
 * name is searched, in order, in: $BD_ICON_DIR, ./assets/app and
 * ../assets/app (the bundled Papirus-derived set in the dev tree),
 * /usr/share/icons/brainux/app (the installed bundled set),
 * /usr/share/icons/hicolor/48x48/apps, /usr/share/icons/Papirus/
 * 48x48/apps and /usr/share/pixmaps - first with the name itself,
 * then with the bundled set's stem for known freedesktop names
 * (e.g. accessories-calculator -> calc). Returns false when no
 * file exists anywhere; callers fall back to a placeholder. */
bool ic_resolve_icon_path(const char *icon, char *out, size_t out_size);

#endif /* BD_ICONCACHE_H */
