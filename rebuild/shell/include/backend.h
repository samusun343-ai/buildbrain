/* backend - hardware backends for the shell v2 (M2): backlight
 * brightness, ALSA master volume, battery state, behind one
 * abstraction so the shell and tests never touch /sys or /dev
 * directly.
 *
 * Implementations:
 *   - sysfs/ALSA ("real"): brightness and battery come from
 *     <root>/class/backlight and <root>/class/power_supply, where
 *     root is $BD_SYSFS_ROOT or /sys (the constant is the only
 *     switch; tests point it at a mock sysfs tree). Volume goes
 *     through the ALSA control device ($BD_SND_DEV or
 *     /dev/snd/controlC0) with raw ioctls - no alsa-lib, the same
 *     approach as the audited bui_alsa part that was kept.
 *   - mock: in-memory values, for headless tests and for running
 *     the shell where hardware is absent.
 *
 * All getters return -1 (or false) when the facility is absent;
 * callers must degrade gracefully (the panel shows "--"), never
 * fail. Percentages are 0..100 everywhere; the sysfs backend maps
 * through max_brightness so callers never see raw ranges.
 */
#ifndef BD_BACKEND_H
#define BD_BACKEND_H

#include <stdbool.h>

typedef struct bd_backend bd_backend;

typedef struct {
    bool present;
    int capacity;   /* 0..100, -1 unknown */
    bool charging;  /* status == Charging (Full counts as not charging) */
    char status[24];/* raw status string, e.g. "Discharging" */
} bd_battery_info;

bd_backend *bd_backend_sysfs_new(void);
bd_backend *bd_backend_mock_new(void);
void bd_backend_free(bd_backend *be);
const char *bd_backend_name(const bd_backend *be);

int bd_backend_brightness_get(bd_backend *be);          /* 0..100 or -1 */
int bd_backend_brightness_set(bd_backend *be, int pct); /* 0 ok, -1 fail */
int bd_backend_volume_get(bd_backend *be);              /* 0..100 or -1 */
int bd_backend_volume_set(bd_backend *be, int pct);     /* 0 ok, -1 fail */
bool bd_backend_battery(bd_backend *be, bd_battery_info *out);

/* Mock-only controls (no-ops on the real backend): preset the
 * values the mock reports, and read back what was written. */
void bd_backend_mock_set(bd_backend *be, int brightness, int volume,
                         int capacity, bool charging);
int bd_backend_mock_writes(const bd_backend *be); /* set-call count */

#endif /* BD_BACKEND_H */
