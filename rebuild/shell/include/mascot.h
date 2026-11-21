/* mascot - 2D desktop mascot runtime (Brainux rebuild).
 *
 * Renders a full-body character on the desktop from a .bdm pack
 * produced by shell/mascot/convert_shimeji.py (see
 * shell/mascot/FORMAT.md). The pack is mmap'd: frames live in a
 * pre-scaled, premultiplied ARGB8888 atlas, actions/behaviors
 * are binary tables, so the runtime does no decoding, no
 * scaling and no XML parsing - per frame it blends only the
 * current sprite rect, and only into the canvas damage.
 *
 * Behaviour model (a deliberate subset of Shimeji's, flattened
 * at convert time): the mascot spawns in the air and falls to
 * the floor (the top edge of the panel/Shelf, i.e. the bottom
 * of the shell's main area), then picks weighted actions from
 * the pack's floor pool - Stay/Animate clips in place, Move
 * clips walking along the floor, turning at the edges. A tap
 * triggers a reaction clip; a drag picks the mascot up (drag
 * clip) and releasing drops/throws it (gravity + fall clip).
 *
 * The session owns visibility policy (desktop view only, hidden
 * over fullscreen windows unless configured) and calls
 * bd_mascot_tick() once per step; painting happens in the
 * session frame, above the WM windows and below the OSK.
 */
#ifndef BD_MASCOT_H
#define BD_MASCOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tk.h"

typedef struct bd_mascot bd_mascot;

typedef struct {
    const char *pack_path;  /* .bdm file */
    int size_px;            /* wanted display height; the nearest
                             * baked size in the pack is used */
    tk_rect floor_area;     /* walk bounds; floor line = y + h */
    uint32_t seed;          /* behaviour RNG seed (0 = default) */
} bd_mascot_config;

/* Load a pack (mmap). Returns NULL when the file is missing or
 * not a valid pack - the feature is simply off then. */
bd_mascot *bd_mascot_new(const bd_mascot_config *cfg);
void bd_mascot_free(bd_mascot *m);

/* The baked display height actually in use, and pack stats for
 * diagnostics: mapped bytes, clip count, frame count. */
int bd_mascot_size(const bd_mascot *m);

/* v3 common format: pack version (1/2/3), whether the pack
 * carries the precomputed region section, and decoding of one
 * pose's region list (canvas coordinates) for cross-verification
 * with the WinCE profile. Returns the rect count or -1. */
int bd_mascot_pack_version(const bd_mascot *m);
bool bd_mascot_has_regions(const bd_mascot *m);
int bd_mascot_regions_at(bd_mascot *m, int clip, int pose,
                         tk_rect *out, int max);
void bd_mascot_stats(const bd_mascot *m, size_t *map_bytes,
                     int *clips, int *frames, int *pool);

/* Update the floor geometry (panel position / resize). */
void bd_mascot_set_floor(bd_mascot *m, tk_rect floor_area);

/* Visibility gate from the session's policy. Hiding reports the
 * last sprite rect as damage on the next tick so the area is
 * repainted clean. */
void bd_mascot_set_visible(bd_mascot *m, bool visible);
bool bd_mascot_visible(const bd_mascot *m);

/* Advance the simulation to now_ms (monotonic). When the
 * on-screen image changed, fills up to max damage rects (the
 * vacated and the new sprite rects) and returns their count;
 * 0 = nothing to repaint. Call once per session step. */
int bd_mascot_tick(bd_mascot *m, long now_ms, tk_rect *dmg, int max);

/* Current sprite rect in screen coordinates (empty when
 * hidden). */
tk_rect bd_mascot_rect(const bd_mascot *m);

/* Blend the current frame into the canvas, clipped to the
 * given rects (usually the frame's canvas damage). clips ==
 * NULL paints the whole sprite (tests / screenshots). Does not
 * add canvas damage itself - the caller already invalidated
 * the area through bd_mascot_tick's damage. */
void bd_mascot_paint(bd_mascot *m, tk_canvas *c,
                     const tk_rect *clips, size_t nclips);

/* Pointer input in screen coordinates (TK_EV_POINTER_*).
 * A DOWN inside the sprite starts a potential drag/tap and
 * returns true; while captured, MOVE/UP return true. The
 * session routes the gesture here before the WM. */
bool bd_mascot_pointer(bd_mascot *m, int type, int x, int y,
                       long now_ms);

/* Behaviour states, for tests/diagnostics. */
enum {
    BD_MASCOT_FALL = 0, BD_MASCOT_IDLE, BD_MASCOT_WALK,
    BD_MASCOT_DRAG, BD_MASCOT_REACT
};
int bd_mascot_state(const bd_mascot *m);
const char *bd_mascot_clip_name(const bd_mascot *m);

/* True while the mascot is visible and its current clip has
 * more than one pose - the embedder uses it to pick a finer
 * step cadence (33 ms) so 24-30 fps clips play at rate. */
bool bd_mascot_animating(const bd_mascot *m);

#endif /* BD_MASCOT_H */
