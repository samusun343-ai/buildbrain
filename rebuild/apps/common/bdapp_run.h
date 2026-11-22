/* bdapp_run - shared display host for the bd-* applications.
 *
 * Every bd-* app main used to open /dev/fb0 itself, read evdev
 * itself and paint the whole panel. Under braindesk2 that made
 * the app and the shell two uncoordinated owners of one
 * framebuffer: the shell had to be frozen (kill -STOP, the
 * integration harness's workaround) for an app to own the
 * screen, the shell panel vanished under every app, the app
 * existed in no task list, and both processes read the same
 * evdev stream. Integration phase 6 (REPORT-integration-8)
 * replaces that with one host that runs the app's toolkit root
 * in exactly one of three ways, in this order:
 *
 *   1. WM client of the running shell (bd_wm_client). The app
 *      is a real window: confined to the work area above the
 *      panel (the panel stays visible and operable, apps are
 *      switchable from it), input routed by the shell alone,
 *      OSK/IME through the shell. This is the normal run
 *      inside a Brainux session. Apps ask for the panel size
 *      and open maximized (see wm.c handle_hello).
 *   2. Direct fbdev - the historical run, kept for sessions
 *      where no shell WM is listening (console use, recovery).
 *      The app owns the panel exactly as before.
 *   3. Headless: neither is available. The display is not
 *      "live"; the caller paints once into its own canvas and
 *      prints its "UI built (headless...)" line - the
 *      make-check smoke contract.
 *
 * The probe happens BEFORE the app builds its UI, because the
 * UI is built at the display's size (the phase-2 lesson: build
 * at the real size, never flush a mismatched canvas).
 */
#ifndef BDAPP_RUN_H
#define BDAPP_RUN_H

#include <stdbool.h>
#include <stdint.h>
#include "tk.h"

typedef struct bd_app_display bd_app_display;

/* Per-iteration hook (may be NULL). Runs once per loop
 * iteration in both live modes with a monotonic millisecond
 * timestamp - the place the old fb loops called their
 * clock/player/recorder/admin tick or the notes autosave. */
typedef void (*bd_app_tick_fn)(void *ud, uint64_t mono_ms);

/* Probe WM, then fbdev. *out_w / *out_h receive the size the
 * app must build at: the WM content size, the fb size, or
 * fb_w / fb_h when nothing is live (headless). title is the
 * WM window title (the app's .desktop Name). req_w / req_h
 * is what the app asks the WM for. Never returns NULL. */
bd_app_display *bd_app_display_open(const char *title,
                                    int req_w, int req_h,
                                    int fb_w, int fb_h,
                                    int *out_w, int *out_h);

/* Extended probe (DESIGN-layout-contract.md section 2):
 * req 0x0 states the "open maximized" intent explicitly -
 * apps must not encode it as the panel's dimensions - and
 * min_w / min_h declare the smallest content size the app
 * can lay out in, which the WM enforces on resize. The bd-*
 * apps use this form. */
bd_app_display *bd_app_display_open_ex(const char *title,
                                       int req_w, int req_h,
                                       int min_w, int min_h,
                                       int fb_w, int fb_h,
                                       int *out_w, int *out_h);

/* True when a real display (WM or fbdev) backs the display. */
bool bd_app_display_live(const bd_app_display *d);

/* The event loop for a live display. poll_ms is the iteration
 * cadence (the value the app's fb loop used to sleep; it is
 * also the tick period). Returns when the app should exit:
 * the shell asked the window to close, the connection died,
 * or (fbdev) the user pressed the HOME action key - the exit
 * conditions the old per-app loops each open-coded. */
void bd_app_display_run(bd_app_display *d, tk_root *root,
                        bd_app_tick_fn tick, void *tick_ud,
                        int poll_ms);

void bd_app_display_close(bd_app_display *d);

#endif /* BDAPP_RUN_H */
