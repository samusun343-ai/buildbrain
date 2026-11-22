/* tk_internal.h - shared widget internals (not part of the SDK). */
#ifndef TK_INTERNAL_H
#define TK_INTERNAL_H

#include "tk.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *label;
    int id;
    bool checked, checkable, enabled, separator;
    tk_widget *submenu;      /* TK_W_MENU or NULL */
    const uint32_t *icon;    /* M6: borrowed pixels or NULL */
    int icon_w, icon_h;
    char *trailing;          /* M7: trailing text (owned) or NULL */
    int badge;               /* M7: -1 dot, 0 none, >0 count */
} tk_menu_item;

typedef struct {
    char *label;
    int parent;              /* node id, -1 = root level */
    bool expanded;
} tk_tree_node;

/* Damage store (M3): merged rect list owned by a tk_root and shared
 * with its top widget (and overlays) so tk_widget_invalidate() can
 * reach it by walking the parent chain. `full` requests a complete
 * repaint at the next tk_root_paint_damage(). */
typedef struct {
    tk_rect *rects;
    size_t n, cap;
    bool full;
    tk_root *owner;       /* M5: the root that owns this store
                           * (lets a widget reach root services -
                           * animation, scroll-blit opt-in - through
                           * the parent chain's damage pointer) */
} tk_damage_store;

void tk_damage_add(tk_damage_store *d, tk_rect r);
void tk_damage_clear(tk_damage_store *d);

struct tk_widget {
    tk_widget_kind kind;
    tk_widget *parent;
    tk_widget **children;
    size_t n_children, cap_children;
    bool visible, enabled, can_focus;
    bool layout_stale;       /* a visibility change below this
                              * tree's top invalidated the
                              * arranged rects; the root
                              * re-lays out before the next
                              * paint (see tk_set_visible) */
    int flex;
    int min_w, min_h; /* tk_widget_set_min_size floor (0 = none) */
    int label_align;  /* TK_LABEL_ALIGN_* (labels only) */
    tk_rect fixed_rect;      /* for Fixed parents */
    tk_rect rect;            /* arranged rect, root space */
    tk_size desired;         /* last measure result */
    /* container state */
    tk_dir dir;
    int spacing, padding;
    int vspacing;            /* grid: row spacing (spacing = column) */
    tk_align cross_align;
    bool wrap;               /* box: wrap children (horizontal only) */
    int avail_w;             /* last measure constraint width (wrap) */
    int cols;                /* grid: column count */
    bool grid_uniform;       /* grid: equal tracks (star sizing) */
    /* generic text payload (label/button/checkbox/radio/switch/...).
     * ComboBox (editable) and SpinButton (while editing) reuse it as
     * their text-entry buffer; it is NULL for them otherwise. */
    char *text;
    int text_style;          /* tk_text_style (UI-REDESIGN 2.2) */
    bool accent_btn;         /* button: accent face + on_accent text */
    /* floating-surface backdrop (UI-REDESIGN 2.2/2.5): painted by
     * tk_paint before the widget itself - opaque surface fill,
     * 1px border, radius_overlay corners; floating surfaces also
     * get the offset shadow silhouette */
    bool backdrop;
    bool backdrop_shadow;
    int backdrop_radius;     /* -1 = theme radius_overlay */
    bool backdrop_has_face;  /* UI-STYLE-v2: tonal face override */
    tk_color backdrop_face;
    /* button presentation overrides (shell panel/launcher) */
    bool btn_flat;           /* no face/border unless pressed */
    bool btn_icon_only;      /* icon centered, label not painted */
    bool btn_has_face;
    tk_color btn_face;
    bool btn_has_indicator;
    tk_color btn_indicator;  /* 3px bar along the bottom edge */
    int btn_radius;          /* -1 = theme radius */
    bool text_shadow;        /* icon-cell label: 1px dark shadow
                              * (desktop labels on wallpaper) */
    /* listview two-line rows + row icons (UI-REDESIGN P1-3) */
    char **subitems;
    size_t n_subitems;
    const uint32_t **item_icons;  /* borrowed pixels, not owned */
    int item_icon_w, item_icon_h;
    size_t n_item_icons;
    /* overflowbar: index of the first child shed into the menu
     * (== n_children when nothing overflowed) + its lazily built
     * menu (owned, not a tree child) */
    int ovf_from;
    tk_widget *ovf_menu;
    char *tooltip;           /* M3: owned tooltip text or NULL */
    tk_damage_store *dmg;    /* M3: set on a root's top widget and on
                              * overlays; found via the parent chain */
    bool b2;                 /* spinbutton: text entry in progress;
                              * combobox: editable flag */
    int edit_pos;            /* combo/spin text entry caret (bytes) */
    /* state */
    bool checked;            /* checkbox / switch */
    int group_id, value;     /* radio */
    bool selected;           /* radio */
    float fvalue;            /* slider / progress / spinbutton */
    float fmin, fmax, fstep; /* spinbutton range */
    bool pressed;            /* button/slider transient */
    /* callbacks */
    tk_notify_cb cb1; void *cb1_ud;   /* click / change / select */
    tk_notify_cb cb2; void *cb2_ud;   /* activate (listview/treeview) */
    /* textfield / textview */
    tc_buffer *buffer;
    char *placeholder;
    bool password;           /* textfield: mask display (M3) */
    bool editable;           /* textview */
    bool b1;                 /* textview: has focus (caret visible) */
    /* scrollview */
    int scroll_y;
    int drag_start_y, drag_start_scroll;
    bool ptr_down, dragging;
    int last_y, vel_y;
    /* M5 scroll-blit state: the canvas is known to hold exactly
     * this scroll position's rendering of the whole viewport
     * (sv_paint_valid), no descendant content changed since
     * (sv_dirty), and the frame in progress took the shift fast
     * path (sv_fast_frame: the leaf paint must not repaint the
     * background over the shifted pixels). */
    int sv_painted_y;
    bool sv_paint_valid;
    bool sv_dirty;
    bool sv_fast_frame;
    /* M5: a shift already performed by tk_root_paint_damage's
     * pre-shift pass for the frame in progress, and the band it
     * exposed (the leaf paint repaints band + scrollbar strip). */
    bool sv_shift_done;
    tk_rect sv_band;
    tk_rect sv_strip;   /* scrollbar zone to repaint (thumb travel) */
    /* M5 switch knob animation: displayed position 0..1 (the
     * logical state is `checked`); idle == checked ? 1 : 0. */
    float knob_pos;
    bool knob_anim;       /* an animation is driving knob_pos */
    /* M5 listview checkable rows + section headers (parallel to
     * items[]; allocated only when used) */
    uint8_t *item_flags;  /* TK_LV_FLAG_* per item */
    tk_notify_cb cb3; void *cb3_ud;   /* listview: on_check */
    bool lv_checkable;    /* rows show checkboxes */
    bool lv_multi;        /* M6: multi-select mode (tap toggles) */
    int lv_last_check;    /* row of the last check toggle */
    /* M5 date/time picker state */
    int pk_year, pk_month, pk_day;    /* datepicker */
    int pk_hour, pk_min, pk_sec;      /* timepicker */
    int pk_field;                     /* active field index */
    bool pk_seconds;                  /* timepicker: show seconds */
    /* M5 animation: the running animation whose callback drives
     * this widget (switch knob / overlay slide), so a re-trigger
     * or a free can cancel it. slide_end_y/slide_span describe an
     * overlay slide-in in progress (rect.y travels to
     * slide_end_y; the swept damage band is slide_span tall). */
    struct tk_anim *anim_handle;
    int slide_end_y;
    int slide_span;
    /* M12: slide state for the generalized overlay slide
     * (tk_anim.c): slide_end_x / slide_axis_x cover horizontal
     * slides (side sheet), slide_cur is the offset currently
     * applied to the subtree, and slide_done_* is the caller's
     * completion hook for a slide-out (dialog exit). show_clock
     * is the root animation clock at show time (dialog / drawer
     * / sheet): exit animations engage only when the clock has
     * advanced past it, i.e. a live loop actually ran frames
     * while the surface was up. */
    int slide_end_x;
    int slide_cur;
    bool slide_axis_x;
    tk_anim_done_cb slide_done_cb;
    void *slide_done_ud;
    uint32_t show_clock;
    bool dlg_closing;        /* dialog: exit slide running */
    /* listview / combobox (items), treeview (sel_id) */
    char **items;
    size_t n_items;
    int sel;
    int marked;              /* app-level highlight row (listview) */
    int top_index;           /* first visible row (listview/textview) */
    /* tabs */
    int active_tab;
    char **tab_titles;
    size_t n_tab_titles;
    int i1;                  /* menu: minimum width (combobox dropdown) */
    /* menu / menubar */
    tk_menu_item *menu_items;
    size_t n_menu_items;
    int menu_hover;
    int menu_last_id;
    tk_widget *open_sub;     /* currently open submenu (menu) */
    tk_widget *owner_ref;    /* combobox that owns a dropdown menu */
    tk_widget *focus_return; /* menu/dialog: the widget that held
                              * focus when this overlay took it;
                              * focus returns there when the
                              * overlay closes (M14) */
    bool auto_free;          /* overlay freed when closed (combo) */
    char **bar_titles;
    tk_widget **bar_menus;
    size_t n_bar;
    /* treeview */
    tk_tree_node *nodes;
    size_t n_nodes, cap_nodes;
    int sel_id;
    /* imageview */
    uint32_t *image_px;
    int image_w, image_h;
    /* separator */
    bool vertical;
    /* per-widget text color override (label/button): for themed
     * widgets sitting on a contrasting surface, e.g. desktop icon
     * labels on a dark wallpaper */
    tk_color fg_override;
    bool has_fg_override;
    /* Audit M6 additions */
    int btn_variant;         /* tk_button_variant; 0 = legacy */
    int btn_icon_layout;     /* tk_button_icon_layout; 0 = cell */
    int btn_text_align;      /* 0 start, 1 center, 2 end;
                              * tk_widget_new sets 1 (center) */
    bool sld_range;          /* slider: range mode; the range lives
                              * in fmin/fmax/fstep (shared with the
                              * spinbutton, which is a different
                              * widget, so there is no clash) */
    bool prog_indet;         /* progress: indeterminate sweep */
    int prog_phase;          /* sweep/spinner phase, permille */
    struct tk_anim *prog_anim; /* running sweep animation */
    int chip_kind;           /* tk_chip_kind */
    bool chip_selected;      /* chip (filter) / tabbar unused */
    tk_notify_cb cb_close; void *cb_close_ud; /* chip input close */
    int badge_count;         /* badge: -1 dot, 0 hidden, >0 count */
    int *tab_badges;         /* tabbar: per-item badge counts,
                              * parallel to items[] */
    char *support_text;      /* textfield supporting/error line */
    bool support_error;
    tk_line_cb line_cb; void *line_cb_ud;  /* textview line tap */
    int span_cols, span_rows; /* grid child spans (<=1 = 1) */
    int cell_x, cell_y;      /* grid placement cache (on child) */
    int float_anchor;        /* floating overlay anchor, -1 none */
    int float_mx, float_my;  /* floating overlay margins */
    int bb_selected;         /* bottombar: selected cell index */
    float ind_pos;           /* M16: nav indicator DISPLAYED
                              * position, in item units (bottombar
                              * box / navrail): equals the logical
                              * selection at rest, fractional while
                              * the selection animation runs */
    /* M17: the M3 ripple (Android mode). Geometry in root
     * coordinates; ripple_alpha is 0..256. ripple_on covers
     * every phase (expanding / held / fading); ripple_static
     * is the no-live-clock form (final geometry, released
     * synchronously). */
    struct tk_anim *ripple_anim;
    int ripple_ox, ripple_oy;   /* press origin */
    int ripple_tx, ripple_ty;   /* target centre (rect centre) */
    int ripple_r0, ripple_r1;   /* start / end radius */
    int ripple_cx, ripple_cy;   /* displayed centre */
    int ripple_radius;          /* displayed radius */
    int ripple_alpha;           /* displayed alpha, 0..256 */
    bool ripple_on, ripple_released, ripple_static;
    /* M17: the WinUI background-brush transition (83 ms,
     * Button themeresources): the DISPLAYED face colour
     * blends face_from -> face_to over face_blend (permille). */
    struct tk_anim *face_anim;
    tk_color face_from, face_to;
    int face_blend;
    bool face_init;
    /* M17: menu fade + exit state. paint_alpha scales the
     * menu surface's colours (255 = opaque; initialised in
     * tk_widget_new, only menus animate it). menu_closing
     * marks the deferred-removal exit run. seg_anim_* is the
     * segmented button's per-segment check fade (M17). */
    int paint_alpha;
    struct tk_anim *fade_anim;
    bool menu_closing;
    /* M20: the menu entrance/exit scale component (material3
     * Menu: scale 0.8 <-> 1.0 about the anchor-side origin,
     * driven by the same animation value as paint_alpha).
     * paint_scale is per-mille (1000 = rest form; initialised
     * in tk_widget_new, only menus animate it). scale_ox /
     * scale_oy are the transform origin in per-mille of the
     * widget rect, set at popup time. At rest the overlay
     * painter takes the direct path and no scratch surface
     * exists (see overlay_paint in tk_core.c). */
    int paint_scale;
    int scale_ox, scale_oy;
    int seg_anim_idx;           /* segment fading, -1 = none */
    bool seg_anim_in;           /* direction of seg_anim_idx */
    struct tk_anim *seg_anim;   /* its animation handle */
    int size_px;             /* spinner: requested size (0 = 40) */
    tk_event_cb ev_hook; void *ev_hook_ud; /* M6: generic hook */
    /* M7 additions (audit completion pass) */
    char *a11y_label;        /* content description (owned) */
    int a11y_role;           /* tk_a11y_role override, 0 = auto */
    bool a11y_heading;
    bool btn_toggle;         /* button: toggle form */
    bool btn_face_pill;      /* button: face paints as an icon pill
                              * (bottombar M3 indicator mode) */
    bool btn_face_container; /* button: the face is the PAINT
                              * theme's container token, resolved
                              * at paint time (M7: bottombar
                              * selection - follows runtime theme
                              * switches instead of baking the
                              * default theme's colour) */
    float fvalue2;           /* slider dual: the lo value */
    bool sld_dual;           /* slider: two-handle Range form */
    int sld_active;          /* slider: active handle 0 lo / 1 hi */
    bool sld_grabbed;        /* slider: a drag is in progress */
    char *tf_prefix, *tf_suffix;  /* textfield adornments (owned) */
    int tf_max_len;          /* textfield: cap in chars, 0 = none */
    bool tf_counter;         /* textfield: show the char counter */
    tk_text_validator_cb tf_validator; void *tf_validator_ud;
    char *tf_error_msg;      /* textfield: validator message */
    bool tf_valid;           /* textfield: last validation result */
    int tf_threshold;        /* textfield: autocomplete threshold */
    tk_widget *tf_ac_menu;   /* textfield: autocomplete dropdown
                              * (owned overlay, not a tree child) */
    int card_variant;        /* card: tk_card_variant */
    uint32_t seg_mask;       /* segmented: multi-select bitset */
    bool seg_multi;          /* segmented: multi-select form */
    int seg_cursor;          /* segmented (multi): keyboard
                              * cursor segment (M14) */
    bool seg_focused;        /* segmented (multi): the cursor
                              * layer paints while focused (M14) */
    int car_start_x;         /* carousel: drag start pointer x */
    int car_start_off;       /* carousel: scroll offset at drag start */
    bool appbar_leading;     /* appbar: children[0] is the leading */
    bool appbar_scrolled;    /* appbar: scrolled (container colour) */
    bool dlg_fullscreen;     /* dialog: full-screen form */
    int sheet_edge;          /* sheet: 0 bottom, 1 end (side) */
    bool sheet_modal;        /* sheet: shown modally */
    int sheet_off;           /* sheet: drag offset px (dismiss axis) */
    bool sheet_drag;         /* sheet: handle drag in progress */
    int sheet_start;         /* sheet: drag start pointer pos */
    bool drawer_modal;       /* drawer: shown modally (scrim) */
    bool drawer_standard;    /* M9: drawer attached in-layout
                              * (the M3 standard form) */
    int drawer_off;          /* M12: panel x offset while the
                              * entrance / exit slide runs
                              * (<= 0: panel shifted left) */
    bool drawer_closing;     /* M12: exit slide running */
    bool sheet_closing;      /* M12: exit slide running */
    const tk_tree_model *tree_model;  /* treeview: model or NULL */
    int *model_expanded;     /* treeview: expanded model ids */
    size_t n_model_expanded, cap_model_expanded;
    int *node_model_id;      /* treeview: model id per materialised
                              * node (parallel to nodes[]) */
    size_t n_node_model_id, cap_node_model_id;
    char *drag_mime;         /* DnD source: MIME type (owned) */
    void *drag_data;         /* DnD source: payload copy (owned) */
    size_t drag_len;
    char *drag_label;        /* DnD source: shadow label (owned) */
    tk_drop_cb drop_cb; void *drop_ud;
    bool lv_drag_reorder;    /* listview: row drag enabled */
    int lv_drag_row;         /* listview: row being dragged, -1 */
    int lv_drag_y;           /* listview: drag pointer y (root) */
    int lv_drag_start_y;
    /* M8: split button chevron rotation. On the arrow button:
     * split_menu is the borrowed menu it pops and chev_pos is
     * the rotation position (0 = pointing down/closed, 1 = 180
     * degrees/open). On the menu: split_arrow points back at
     * the arrow button so any close path can drive the chevron
     * home (tk_split_on_menu_closed). */
    tk_widget *split_menu;
    tk_widget *split_arrow;
    float chev_pos;
    bool chev_anim;
    /* M13 additions */
    bool btn_fab;            /* button: FAB form - in the M3 mode
                              * the face is primaryContainer with
                              * onPrimaryContainer content, not the
                              * filled-button primary face */
    bool btn_face_indicator; /* button: the icon pill is a
                              * navigation indicator - in the M3
                              * mode it takes secondaryContainer
                              * (selection), not primaryContainer */
    int btn_split_side;      /* split button: 0 = not a split
                              * half, 1 = leading half (inner
                              * corner on the right), 2 = trailing
                              * half (inner corner on the left) */
    int btn_split_inner;     /* split button: inner corner radius
                              * px (M3: the 4dp inner corner) */
    float glyph_pos;         /* checkbox/radio: displayed glyph
                              * fraction 0..1 (animates on state
                              * change; equals the logical state
                              * at rest) */
    float sld_disp, sld_disp2; /* slider: displayed fractions
                              * (programmatic sets animate these
                              * toward fvalue/fvalue2 when the
                              * widget opted in, sld_anim_prog) */
    float sld_from, sld_from2; /* slider: animation start values */
    bool sld_anim_prog;      /* slider: animate programmatic
                              * value changes (opt-in; default
                              * off = snap, the pre-M13 contract
                              * reflective consumers rely on) */
    bool prog_anim_values;   /* progress: same opt-in for the
                              * determinate transition */
    float prog_disp;         /* progress: displayed determinate
                              * fraction (M3 determinate sets
                              * transition when prog_anim_values is set;
                              * other modes snap) */
    float prog_from;         /* progress: animation start value */
    int tab_ind_x;           /* tabbar: displayed indicator left
                              * edge px (widget coords); -1 =
                              * derive from the selection */
    /* osk: opaque per-widget state owned by src/tk_osk.c
     * (single allocation; tk_osk_free_state releases its innards,
     * the core frees the block itself). NULL for other kinds. */
    void *ext;
};

struct tk_anim;              /* tk_anim.c: one running animation */

struct tk_root {
    tk_widget *top;
    tk_widget *focused;
    tk_widget *capture;      /* pointer capture target */
    /* M5: scroll-blit opt-in (see tk_root_set_scroll_blit) and the
     * animation engine's state */
    bool scroll_blit;
    bool anims_enabled;
    struct tk_anim *anims;
    uint32_t anim_now;       /* last tick time animations saw */
    /* M2: overlay stack (menus, dialogs) painted/hit above the tree */
    tk_widget **overlays;
    size_t n_overlays, cap_overlays;
    tk_widget *modal;        /* overlay that owns all input, or NULL */
    bool suppress_click;     /* a drag-scroll ate this pointer sequence */
    int w, h;                /* last layout size */
    /* capture-phase drag-scroll gesture recognition: the nearest
     * ScrollView ancestor of the pointer-down hit, so a drag can
     * take over even when a child consumed the DOWN (buttons do) */
    tk_widget *drag_sv;
    int down_y;
    /* M3: damage-driven repaint + tooltips */
    tk_damage_store dmg;
    /* M20: scratch surfaces for the scaled-overlay paint path
     * (menu entrance/exit scale). Lazily created at the size
     * of the largest scaled overlay rect seen, reused across
     * paints, freed with the root; never created while every
     * overlay is at rest (paint_scale == 1000). The pair is
     * bounded by the root surface size by construction. */
    tk_canvas *scale_save;    /* backdrop copy */
    tk_canvas *scale_surf;    /* composited overlay copy */
    /* per-event damage narrowing (tk_root_damage_hint): a
     * handler may declare that the event's visual change is
     * smaller than the target's full rect - up to three rects
     * (a changed band plus a changed cell, say); reset on every
     * tk_root_event entry, consumed at its tail */
    tk_rect dmg_hint[3];
    int dmg_hint_n;
    tk_widget *hover;        /* widget under the pointer (MOVE) */
    int hover_x, hover_y;    /* rest anchor for the tooltip timer */
    uint32_t hover_since;    /* root-clock time the rest began */
    uint32_t now_ms;         /* last tk_root_tick() value */
    bool tt_visible;
    tk_rect tt_rect;
    char tt_buf[128];        /* tooltip text snapshot (owned copy) */
    /* M6: pointer long-press tracking (set on POINTER_DOWN,
     * fired from tk_root_tick, cancelled by slop/UP) */
    tk_widget *lp_target;
    uint32_t lp_since;
    int lp_x, lp_y;
    bool lp_fired;
    /* M6: toast (non-interactive, auto-expiring) */
    bool toast_active;
    char toast_buf[160];
    uint32_t toast_until;
    tk_rect toast_rect;
    /* M13: toast fade - the displayed alpha 0..255, computed
     * from the show / expiry timeline in transients_tick (the
     * fades occupy the head and tail of the display duration).
     * toast_show_clock is the animation-clock reading at show
     * time (the explicit-hide fade gate mirrors the dialog
     * exit gate). */
    int toast_alpha;
    bool toast_fading_out;
    uint32_t toast_show_clock;
    uint32_t toast_show_ms;
    uint32_t toast_hide_end;
    int toast_hide_from;
    int toast_fade_ms;
    /* M6: snackbar (one optional action, auto-expiring) */
    bool snack_active;
    char snack_buf[160];
    char snack_action[40];
    tk_notify_cb snack_cb;
    void *snack_ud;
    uint32_t snack_until;
    bool snack_forever;
    tk_rect snack_rect;
    tk_rect snack_action_rect; /* valid while snack_active */
    tk_widget *snack_widget;   /* lazily created callback target
                                * (never added to a tree) */
    /* M7: snackbar swipe-to-dismiss tracking (a DOWN on the bar
     * body arms a pending tap; a MOVE past the swipe threshold
     * dismisses immediately, an UP within it dismisses as a tap;
     * the action zone keeps its fire-on-DOWN behaviour) */
    bool snack_down_on;
    int snack_down_x, snack_down_y;
    int snack_off;           /* M12: entrance slide offset px
                              * (bar painted this far below its
                              * rect; 0 once settled) */
    tk_anim *snack_anim;     /* M12: running entrance animation */
    int snack_severity;      /* M9: tk_snack_severity of the
                              * current bar (InfoBar form) */
    tk_widget *down_dismissed_menu;  /* M9: the menu the current
                              * press dismissed (anchor toggle) */
    tk_widget *down_dismissed_owner; /* M9: its owner_ref, for
                              * anchors whose menus are recreated
                              * per open (combobox dropdowns) */
    /* M7: drag & drop state (root-driven; the payload is copied
     * at DOWN so the source widget may change during the drag) */
    tk_widget *drag_src;
    bool drag_armed;           /* press is on a source, slop pending */
    bool drag_on;              /* the drag shadow is live */
    int drag_x, drag_y;        /* shadow anchor (pointer pos) */
    int drag_down_x, drag_down_y;
    char *drag_mime;
    void *drag_data;
    size_t drag_len;
    char drag_label[64];
    /* M7: accessibility live-region announcements */
    char announce_buf[160];
};

/* Tab strip height (UI-REDESIGN 2.2: tabs are 48). Shared by the
 * core's tabs measure/arrange and the widgets' tabs paint/events. */
#define TK_TAB_BAR_H 48

/* Style-aware text helpers for widget code (tk_widgets.c defines
 * the widget-side wrappers; these are the raw forms). */
int tk_w_text_w(const tk_widget *w, const tk_theme *t, const char *s);
int tk_w_text_lh(const tk_widget *w, const tk_theme *t);
void tk_w_draw_text(tk_canvas *c, const tk_widget *w, const tk_theme *t,
                    int x, int y, const char *s, tk_color col);

/* vtable-ish dispatch used by tk_core.c */
tk_size tk_w_measure(tk_widget *w, tk_constraints c);
void tk_w_paint(tk_widget *w, tk_canvas *c, const tk_theme *t);
bool tk_w_event(tk_widget *w, tk_root *root, tk_event *ev);

/* M9: the formal IME preedit painter (defined in tk_widgets.c,
 * shared with the SearchBar so every editor renders the
 * composition the same way: at the caret, segmented, 1px dim
 * underline on inactive segments, 2px accent on the active
 * one, selection wash while browsing candidates). Returns the
 * painted width in px. mask = password form. */
int tk_draw_composition(tk_canvas *c, const tk_theme *t,
                        tc_buffer *b, int x, int y, tk_color fg,
                        bool mask);

/* OverflowBar (src/tk_widgets.c): measure is reached through
 * tk_w2_measure; arrange needs a core branch because it positions
 * children. */
tk_size tk_ob_measure(tk_widget *w, tk_constraints c);
void tk_ob_arrange(tk_widget *w, tk_rect r);

/* OSK v2 dispatch (src/tk_osk.c), called from tk_leaf_* for
 * TK_W_OSK. tk_osk_free_state is called by the core's free path
 * before it frees w->ext. */
tk_size tk_osk_measure(tk_widget *w, tk_constraints c);
void tk_osk_paint(tk_widget *w, tk_canvas *c, const tk_theme *t);
bool tk_osk_event(tk_widget *w, tk_root *root, tk_event *ev);
void tk_osk_free_state(void *ext);

/* overlay management (tk_core.c) */
void tk_root_add_overlay(tk_root *r, tk_widget *w);
void tk_root_remove_overlay(tk_root *r, tk_widget *w);
bool tk_root_has_overlay(const tk_root *r, const tk_widget *w);
void tk_close_all_menus(tk_root *r);
bool tk_is_descendant(const tk_widget *ancestor, const tk_widget *w);

/* canvas helpers for the font/image code (tk_draw.c) */
void tk_canvas_blend_pixel(tk_canvas *c, int x, int y, tk_color col);
void tk_canvas_put_pixel(tk_canvas *c, int x, int y, tk_color col);
/* M5: current clip (intersection of the clip stack), for paint
 * culling and row-level skips. */
tk_rect tk_canvas_clip_bounds(const tk_canvas *c);
/* M17: a disc clipped to a rounded rect with per-corner
 * radii (the ripple shape; see tk_draw.c). */
void tk_canvas_fill_circle_rrect4(tk_canvas *c, tk_rect r,
                                  int rtl, int rtr, int rbr,
                                  int rbl, float cx, float cy,
                                  float radius, tk_color col);
/* M5: bulk alpha blit (glyph hot path; see tk_draw.c). */
void tk_canvas_blit_alpha(tk_canvas *c, int x, int y, int w, int h,
                          const uint8_t *alpha, int stride, tk_color col);
/* M5 scroll shifts: a recorded in-canvas pixel shift that a
 * separate-buffer backend must replay in its own memory. */
typedef struct { tk_rect rect; int dy; } tk_blit;
tk_rect tk_canvas_shift_region(tk_canvas *c, tk_rect r, int dy);
size_t tk_canvas_blit_count(const tk_canvas *c);
tk_blit tk_canvas_blit_at(const tk_canvas *c, size_t i);
void tk_canvas_blits_clear(tk_canvas *c);
/* M5: find the root that owns the tree w belongs to (through the
 * top widget's damage store), or NULL for a detached tree. */
tk_root *tk_widget_root(const tk_widget *w);
/* M5: mark every ScrollView ancestor of w (strict: not w itself)
 * content-dirty; called from the invalidation/event paths. */
void tk_sv_mark_dirty_from(tk_widget *w);
/* M5: after a full repaint, every ScrollView in the tree holds a
 * valid painted state at its current scroll offset. */
void tk_sv_note_full_paint(tk_widget *top);
/* Scrollbar thumb rect for a given scroll offset (tk_widgets.c),
 * and the strip zone covering the thumb's travel between two
 * offsets (what a shift fast frame must repaint of the strip). */
tk_rect tk_sv_thumb_rect(const tk_widget *w, int scroll_y);
tk_rect tk_sv_thumb_zone(const tk_widget *w, int old_y, int new_y);
/* M5 animation engine hooks (src/tk_anim.c) */
void tk_anims_advance(tk_root *r, uint32_t now_ms);
void tk_anims_free_all(tk_root *r);
/* Start the standard floating-surface slide-in on an overlay
 * that has just been arranged at its final rect: it starts
 * `dy` px away (signed) and eases back. Duration / easing are
 * the mode's overlay-entrance pair (M12: M3 short4 200 ms,
 * WinUI ControlFast 167 ms, both with the (0,0,0,1) decelerate
 * curve). The whole subtree travels with the face. No-op when
 * the root has no live clock or animations are disabled. */
/* M15: move keyboard focus to the first focusable widget in
 * w's subtree (w itself when it has no focusable descendant
 * and can take focus); returns the widget focused, or NULL.
 * Overlay presenters that take over the screen (the modal
 * sheet) use it to put focus inside on show. */
tk_widget *tk_focus_first(tk_root *r, tk_widget *w);
void tk_overlay_slide_in(tk_root *r, tk_widget *w, int dy);
/* M12: explicit-parameter forms. tk_overlay_slide_in_dur is
 * the vertical entrance with a caller-chosen duration / easing
 * (large surfaces: the bottom sheet). tk_overlay_slide_in_x is
 * the horizontal entrance (the side sheet). tk_overlay_slide_out
 * runs the reverse: the subtree travels (dx, dy) away from its
 * arranged rect, the geometry is restored, and only then is
 * `done` delivered (the dialog exit path removes the overlay
 * there). */
void tk_overlay_slide_in_dur(tk_root *r, tk_widget *w, int dy,
                             uint32_t dur_ms, tk_ease_fn ease);
void tk_overlay_slide_in_x(tk_root *r, tk_widget *w, int dx,
                           uint32_t dur_ms, tk_ease_fn ease);
void tk_overlay_slide_out(tk_root *r, tk_widget *w, int dx, int dy,
                          uint32_t dur_ms, tk_ease_fn ease,
                          tk_anim_done_cb done, void *ud);
/* M12: the mode's animation pair for an overlay surface, from
 * the current default theme: small overlays (dialog / menu)
 * use the short pair, large traversing surfaces (drawer /
 * sheet) the medium pair. Values are the fetched token values
 * recorded in REPORT-toolkit-m12.md SS0. */
void tk_anim_mode_pair(bool large, uint32_t *dur_ms,
                       tk_ease_fn *ease);
/* M13: the selection-control pair (the Switch knob's pairing):
 * M3 200 ms Standard (0.2, 0, 0, 1), WinUI 167 ms with the
 * (0, 0, 0, 1) Direct Entrance curve; the classic theme keeps
 * the M3 numbers. Used by the checkbox / radio glyph
 * transitions. */
void tk_anim_selection_pair(uint32_t *dur_ms, tk_ease_fn *ease);
void tk_ind_anim_to(tk_widget *w, int to_idx); /* M16: nav
                              * indicator position transition
                              * (bottombar / navrail) */
/* M17: the M3 ripple (src/tk_widgets.c). press/release are
 * called from the owning widget's pointer handlers; paint
 * draws the pressed visual (the animated disc while a ripple
 * runs, the caller's uniform layer otherwise); circle draws
 * only the disc (components whose rest face has no uniform
 * pressed layer: chip, card). */
void tk_ripple_press(tk_widget *w, tk_root *r, int x, int y);
void tk_ripple_release(tk_widget *w, tk_root *r);
void tk_ripple_paint(tk_widget *w, tk_canvas *c, tk_rect r,
                     int rad, tk_color layer);
void tk_ripple_circle(tk_widget *w, tk_canvas *c, tk_rect r,
                      int rad, tk_color layer);
/* M17: the WinUI 83 ms brush transition for button-family
 * faces (src/tk_widgets.c): returns the colour to fill with
 * this frame for target under theme t. */
tk_color tk_face_brush(tk_widget *w, const tk_theme *t,
                       tk_color target);
/* M17: menu close with the exit run (src/tk_widgets.c);
 * falls back to tk_menu_close's synchronous removal when no
 * live clock ran while the menu was up. force = complete a
 * running exit immediately. */
void tk_menu_close_animated(tk_root *r, tk_widget *menu,
                            bool force);
void tk_bottombar_paint_indicator(tk_widget *bar, tk_canvas *c,
                              const tk_theme *t); /* M16 */
/* M13: the animation engine's cubic-bezier solver, exposed for
 * the progress indicator's keyframe evaluation (the same curve
 * the easing functions wrap). */
float tk_bezier_y(float x1, float y1, float x2, float y2, float x);

/* ListView item flags (M5) */
#define TK_LV_FLAG_HEADER  0x01  /* section header: not selectable */
#define TK_LV_FLAG_CHECKED 0x02  /* checkable row, currently on */

/* M7 widget dispatch (src/tk_m7.c): kinds >= TK_W_CARD that are
 * leaves (CARD/APPBAR/SHEET get container layout in tk_core.c
 * but still paint and take events through here). */
int tk_dialog_title_h(const tk_widget *w, const tk_theme *t);
tk_size tk_m7_measure(tk_widget *w, tk_constraints c);
void tk_m7_paint(tk_widget *w, tk_canvas *c, const tk_theme *t);
bool tk_m7_event(tk_widget *w, tk_root *root, tk_event *ev);
/* M7 per-widget state cleanup, called from free_subtree before
 * the generic ext free (the new kinds keep state in fields, so
 * this releases the owned strings/menus/models). */
void tk_m7_free_widget(tk_widget *w);
/* M7: the drag shadow, painted with the transients (tk_core.c). */
void tk_m7_paint_drag(tk_root *r, tk_canvas *c, const tk_theme *t);
/* M8: a menu that a split button pops is closing - drive its
 * chevron back to the closed position (tk_m7.c). Called from
 * every menu-removal path (tk_menu_close, tk_close_all_menus). */
void tk_split_on_menu_closed(tk_widget *menu);
tk_rect tk_drag_shadow_rect(const tk_root *r);
/* M7: a pointer-down that modality swallowed outside the modal
 * widget (a modal sheet's scrim dismisses the sheet). */
void tk_m7_overlay_outside_down(tk_root *r, tk_widget *modal);
/* M7: shared text-edit key handling, exported from tk_widgets.c
 * so the SearchBar edits exactly like a TextField. */
bool tk_text_edit_key(tk_widget *w, tk_event *ev, bool multiline);
/* M7: caret placement by pixel offset (tk_widgets.c). */
size_t tk_offset_at_x(const tk_theme *t, const char *utf8, int x_px);
/* M7: the prefix-match suggestion dropdown shared by the
 * TextField autocomplete and the SearchBar (tk_m7.c). Opens,
 * refreshes or closes w->open_sub from the buffer text. */
void tk_m7_suggest_refresh(tk_root *r, tk_widget *w);
/* M7: the toolkit shared clipboard as a textcore provider
 * (tk_m7.c); installed by default on new text buffers. */
extern const tc_clipboard tk_shared_clipboard;

/* M7: baseline JPEG with scaled IDCT (tk_jpeg.c) */
bool tk_jpeg_probe(const unsigned char *data, size_t len, int *w,
                   int *h);
bool tk_jpeg_decode_scaled(const unsigned char *data, size_t len,
                           int scale, uint32_t **out_px, int *ow,
                           int *oh, size_t *peak);

/* image decode (tk_image.c, stb_image backend) */
bool tk_image_load_rgba(const char *path, uint32_t **out_px, int *w, int *h);

/* helpers */
char *tk_strdup(const char *s);
bool tk_rect_contains(tk_rect r, int x, int y);
tk_rect tk_rect_intersect(tk_rect a, tk_rect b);
void tk_invoke(tk_notify_cb cb, tk_widget *w, void *ud);

#endif /* TK_INTERNAL_H */
