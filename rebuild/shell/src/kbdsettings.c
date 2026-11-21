/* kbdsettings.c - see kbdsettings.h. */
#include "kbdsettings.h"
#include "reg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const bd_kbd_repeat_preset PRESETS[] = {
    { "遅い", 600, 80 },
    { "標準", 400, 50 },
    { "速い", 250, 30 },
};

int bd_kbd_repeat_preset_count(void)
{
    return (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
}

const bd_kbd_repeat_preset *bd_kbd_repeat_preset_at(int i)
{
    if (i < 0 || i >= bd_kbd_repeat_preset_count()) return NULL;
    return &PRESETS[i];
}

int bd_kbd_repeat_preset_of(const tk_kbd_config *cfg)
{
    for (int i = 0; i < bd_kbd_repeat_preset_count(); i++)
        if (PRESETS[i].delay_ms == cfg->repeat_delay_ms &&
            PRESETS[i].interval_ms == cfg->repeat_interval_ms)
            return i;
    return -1;
}

void bd_kbd_settings_load(tk_kbd_config *out)
{
    tk_kbd_config_defaults(out);
    /* "layout" is deliberately not read: the built-in keyboard
     * has one fixed keymap, and any stored value (including
     * legacy "jis"/"us") is meaningless. sanitize() below pins
     * the field to TK_KBD_LAYOUT_BRAIN regardless. */
    char *v = reg_get("keyboard", "repeat_source");
    if (v) {
        if (strcmp(v, "kernel") == 0)
            out->repeat_mode = TK_KBD_REPEAT_KERNEL;
        else if (strcmp(v, "synthetic") == 0)
            out->repeat_mode = TK_KBD_REPEAT_SYNTHETIC;
        free(v);
    }
    out->repeat_delay_ms = reg_get_int("keyboard", "repeat_delay_ms",
                                       out->repeat_delay_ms);
    out->repeat_interval_ms = reg_get_int("keyboard",
                                          "repeat_interval_ms",
                                          out->repeat_interval_ms);
    out->longpress_ms = reg_get_int("keyboard", "longpress_ms",
                                    out->longpress_ms);
    tk_kbd_config_sanitize(out);
}

void bd_kbd_settings_save(const tk_kbd_config *cfg)
{
    tk_kbd_config c = *cfg;
    tk_kbd_config_sanitize(&c);
    char buf[32];
    reg_set("keyboard", "layout", "brain");  /* fixed keymap, see .h */
    reg_set("keyboard", "repeat_source",
            c.repeat_mode == TK_KBD_REPEAT_KERNEL ? "kernel"
                                                  : "synthetic");
    snprintf(buf, sizeof(buf), "%d", c.repeat_delay_ms);
    reg_set("keyboard", "repeat_delay_ms", buf);
    snprintf(buf, sizeof(buf), "%d", c.repeat_interval_ms);
    reg_set("keyboard", "repeat_interval_ms", buf);
    snprintf(buf, sizeof(buf), "%d", c.longpress_ms);
    reg_set("keyboard", "longpress_ms", buf);
}
