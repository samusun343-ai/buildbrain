/* test_backend - sysfs backend against a mock sysfs tree, ALSA
 * absence handling, and the in-memory mock backend. */
#define _POSIX_C_SOURCE 200809L
#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void mkfile(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    fputs(content, f);
    fclose(f);
}

static void readfile(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) { out[0] = 0; return; }
    size_t r = fread(out, 1, n - 1, f);
    out[r] = 0;
    fclose(f);
}

int main(void)
{
    char dir[] = "/tmp/bdbeXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char p[512];
    snprintf(p, sizeof(p), "%s/class/backlight/pwm-backlight", dir);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/class/backlight/pwm-backlight "
             "%s/class/power_supply/BAT0", dir, dir);
    if (system(cmd) != 0) return 1;
    snprintf(p, sizeof(p), "%s/class/backlight/pwm-backlight/max_brightness", dir);
    mkfile(p, "255\n");
    snprintf(p, sizeof(p), "%s/class/backlight/pwm-backlight/brightness", dir);
    mkfile(p, "128\n");
    snprintf(p, sizeof(p), "%s/class/power_supply/BAT0/type", dir);
    mkfile(p, "Battery\n");
    snprintf(p, sizeof(p), "%s/class/power_supply/BAT0/capacity", dir);
    mkfile(p, "73\n");
    snprintf(p, sizeof(p), "%s/class/power_supply/BAT0/status", dir);
    mkfile(p, "Charging\n");

    setenv("BD_SYSFS_ROOT", dir, 1);
    setenv("BD_SND_DEV", "/nonexistent/controlC0", 1);
    bd_backend *be = bd_backend_sysfs_new();
    CHECK(be != NULL);

    /* brightness: 128/255 -> 50%, set 25% -> raw 64 */
    CHECK(bd_backend_brightness_get(be) == 50);
    CHECK(bd_backend_brightness_set(be, 25) == 0);
    char raw[64];
    snprintf(p, sizeof(p), "%s/class/backlight/pwm-backlight/brightness", dir);
    readfile(p, raw, sizeof(raw));
    CHECK(atoi(raw) == 64);
    CHECK(bd_backend_brightness_get(be) == 25);
    /* clamping */
    CHECK(bd_backend_brightness_set(be, 140) == 0);
    CHECK(bd_backend_brightness_get(be) == 100);

    /* battery */
    bd_battery_info bi;
    CHECK(bd_backend_battery(be, &bi));
    CHECK(bi.present && bi.capacity == 73 && bi.charging);
    CHECK(strcmp(bi.status, "Charging") == 0);

    /* ALSA device absent: graceful -1, never a crash or fake 0 */
    CHECK(bd_backend_volume_get(be) == -1);
    CHECK(bd_backend_volume_set(be, 40) == -1);
    bd_backend_free(be);

    /* empty sysfs root: everything absent, gracefully */
    char empty[] = "/tmp/bdbeemptyXXXXXX";
    if (!mkdtemp(empty)) return 1;
    setenv("BD_SYSFS_ROOT", empty, 1);
    be = bd_backend_sysfs_new();
    CHECK(bd_backend_brightness_get(be) == -1);
    CHECK(bd_backend_brightness_set(be, 50) == -1);
    CHECK(!bd_backend_battery(be, &bi));
    bd_backend_free(be);
    unsetenv("BD_SYSFS_ROOT");

    /* mock backend */
    be = bd_backend_mock_new();
    CHECK(be != NULL);
    CHECK(bd_backend_brightness_get(be) == 70);
    CHECK(bd_backend_volume_get(be) == 50);
    CHECK(bd_backend_brightness_set(be, 30) == 0);
    CHECK(bd_backend_volume_set(be, 90) == 0);
    CHECK(bd_backend_brightness_get(be) == 30);
    CHECK(bd_backend_volume_get(be) == 90);
    CHECK(bd_backend_mock_writes(be) == 2);
    bd_backend_mock_set(be, 55, 44, 66, true);
    CHECK(bd_backend_brightness_get(be) == 55);
    CHECK(bd_backend_battery(be, &bi) && bi.capacity == 66 && bi.charging);
    bd_backend_free(be);

    printf("test_backend: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
