/* thememode - the display-mode contract (REPORT-status-icons
 * section 4). This header is the canonical definition of how the
 * UI mode is chosen; the settings app WRITES the key, product
 * processes READ it once at startup through this helper.
 *
 *   namespace "ui", key "mode":  "android" (default) | "winui"
 *   namespace "ui", key "dark":  "true" | "false" (default)
 *
 * android -> tk_theme_v2 / tk_theme_v2_dark
 * winui   -> tk_theme_winui / tk_theme_winui_dark
 *
 * Timing: the value takes effect at the next process start
 * (next login / session start). The toolkit's default theme is
 * process-wide and widget trees are built against it, so there
 * is no live switching. An unknown mode value falls back to
 * android; the reader never fails. */
#ifndef BD_THEMEMODE_H
#define BD_THEMEMODE_H

#include "tk.h"

const tk_theme *bd_theme_from_reg(void);

#endif /* BD_THEMEMODE_H */
