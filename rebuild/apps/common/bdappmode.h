/* bdappmode - the apps' single door to the display-mode contract.
 *
 * The contract itself (REPORT-status-icons section 4) is owned by
 * the shell line: brainreg "ui"/"mode" = "android" (default) |
 * "winui", plus "ui"/"dark". The one reader implementation is
 * bd_theme_from_reg() in shell/src/thememode.c (libbdshell.a,
 * linked by every app). This header is the ONLY place in apps/
 * that calls it: product main()s call bd_app_theme() instead of
 * hardcoding a theme, and constructors branch on the style of
 * tk_theme_default() (set by main, or by tests / shot harnesses,
 * which keep working exactly as before).
 *
 * Timing (contract): the mode takes effect at the next process
 * start; there is no live switching.
 */
#ifndef BD_APPMODE_H
#define BD_APPMODE_H

#include "thememode.h" /* bd_theme_from_reg() */

#include <stdbool.h>

/* Read the mode from brainreg, install it as the toolkit default
 * theme and return it. Never fails: unknown values fall back to
 * the Android form (thememode's contract). */
static inline const tk_theme *bd_app_theme(void)
{
    const tk_theme *t = bd_theme_from_reg();
    tk_theme_set_default(t);
    return t;
}

/* True when the theme is the Windows-equivalent (WinUI) form. */
static inline bool bd_app_winui(const tk_theme *t)
{
    return t != NULL && t->style == 2;
}

/* Page margin (integration phase 11): every app's root box gets
 * this horizontal padding, so page content never starts closer
 * than 18px to the window's side edges (evaluation CHECKLIST
 * C7; Material's 16dp screen-edge margin at this panel's
 * ~179ppi is 18px).  Before this, each app invented its own
 * root padding (0/4/6/8px) and blind round 3 counted ~29 C7
 * findings per evaluator.  Vertical padding stays each app's
 * own rhythm - the rule is horizontal. */
#define BD_PAGE_PAD_X 18
static inline void bd_page_pad_x(tk_widget *root_box)
{
    tk_box_set_pad_x(root_box, BD_PAGE_PAD_X);
}

#endif /* BD_APPMODE_H */
