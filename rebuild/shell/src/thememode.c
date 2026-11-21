/* thememode.c - see thememode.h for the contract. */
#include "thememode.h"
#include "reg.h"

#include <stdlib.h>
#include <string.h>

const tk_theme *bd_theme_from_reg(void)
{
    char *mode = reg_get("ui", "mode");
    bool winui = mode && strcmp(mode, "winui") == 0;
    free(mode);
    bool dark = reg_get_bool("ui", "dark", false);
    if (winui) return dark ? &tk_theme_winui_dark : &tk_theme_winui;
    return dark ? &tk_theme_v2_dark : &tk_theme_v2;
}
