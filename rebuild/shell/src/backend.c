/* backend.c - see backend.h. sysfs + raw-ALSA implementation and
 * the in-memory mock. */
#define _POSIX_C_SOURCE 200809L
#include "backend.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sound/asound.h>

struct bd_backend {
    bool mock;
    char root[256];       /* sysfs root (real backend) */
    char snd_dev[128];
    /* mock state */
    int m_brightness, m_volume, m_capacity;
    bool m_charging;
    int m_writes;
    /* cached sysfs node names (found once, then reused) */
    char bl_name[256];
    char bat_name[256];
    bool scanned;
};

/* ---------------- small file helpers ---------------- */

static int read_long_file(const char *path, long *out)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v;
    int ok = fscanf(f, "%ld", &v);
    fclose(f);
    if (ok != 1) return -1;
    *out = v;
    return 0;
}

static int read_str_file(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(out, (int)n, f)) { fclose(f); return -1; }
    fclose(f);
    out[strcspn(out, "\r\n")] = 0;
    return 0;
}

static int write_long_file(const char *path, long v)
{
    /* O_TRUNC matters: without it a shorter new value leaves
     * trailing bytes of the old one on regular files (the mock
     * sysfs test caught "64" written over "128" becoming "648").
     * Real sysfs attributes accept O_TRUNC harmlessly. */
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) return -1;
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%ld", v);
    int rc = 0;
    if (write(fd, buf, (size_t)len) != len) rc = -1;
    if (close(fd) < 0) rc = -1;
    return rc;
}

/* first directory entry under <root>/<cls> whose name passes a
 * prefix test (NULL = any), skipping . and .. */
static bool find_node(bd_backend *be, const char *cls, const char *prefix,
                      char *out, size_t n)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", be->root, cls);
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (prefix && strncmp(e->d_name, prefix, strlen(prefix)) != 0)
            continue;
        snprintf(out, n, "%s", e->d_name);
        found = true;
        break;
    }
    closedir(d);
    return found;
}

static void scan_nodes(bd_backend *be)
{
    if (be->scanned) return;
    be->scanned = true;
    find_node(be, "class/backlight", NULL, be->bl_name, sizeof(be->bl_name));
    /* power_supply: prefer a BAT* node, else any node whose type
     * file says Battery */
    if (!find_node(be, "class/power_supply", "BAT", be->bat_name,
                   sizeof(be->bat_name))) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/class/power_supply", be->root);
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (e->d_name[0] == '.') continue;
                char tp[600], tv[32];
                snprintf(tp, sizeof(tp), "%s/%s/type", dir, e->d_name);
                if (read_str_file(tp, tv, sizeof(tv)) == 0 &&
                    strcmp(tv, "Battery") == 0) {
                    snprintf(be->bat_name, sizeof(be->bat_name), "%s",
                             e->d_name);
                    break;
                }
            }
            closedir(d);
        }
    }
}

/* ---------------- sysfs brightness / battery ---------------- */

static int sysfs_brightness_get(bd_backend *be)
{
    scan_nodes(be);
    if (!be->bl_name[0]) return -1;
    char p[640];
    long cur, max;
    snprintf(p, sizeof(p), "%s/class/backlight/%s/max_brightness",
             be->root, be->bl_name);
    if (read_long_file(p, &max) < 0 || max <= 0) return -1;
    snprintf(p, sizeof(p), "%s/class/backlight/%s/brightness",
             be->root, be->bl_name);
    if (read_long_file(p, &cur) < 0) return -1;
    return (int)((cur * 100 + max / 2) / max);
}

static int sysfs_brightness_set(bd_backend *be, int pct)
{
    scan_nodes(be);
    if (!be->bl_name[0]) return -1;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    char p[640];
    long max;
    snprintf(p, sizeof(p), "%s/class/backlight/%s/max_brightness",
             be->root, be->bl_name);
    if (read_long_file(p, &max) < 0 || max <= 0) return -1;
    long v = (max * pct + 50) / 100;
    snprintf(p, sizeof(p), "%s/class/backlight/%s/brightness",
             be->root, be->bl_name);
    return write_long_file(p, v);
}

static bool sysfs_battery(bd_backend *be, bd_battery_info *out)
{
    scan_nodes(be);
    memset(out, 0, sizeof(*out));
    out->capacity = -1;
    if (!be->bat_name[0]) return false;
    char p[640];
    long cap;
    snprintf(p, sizeof(p), "%s/class/power_supply/%s/capacity",
             be->root, be->bat_name);
    if (read_long_file(p, &cap) == 0) out->capacity = (int)cap;
    snprintf(p, sizeof(p), "%s/class/power_supply/%s/status",
             be->root, be->bat_name);
    if (read_str_file(p, out->status, sizeof(out->status)) < 0)
        snprintf(out->status, sizeof(out->status), "Unknown");
    out->charging = strcmp(out->status, "Charging") == 0;
    out->present = true;
    return true;
}

/* ---------------- ALSA volume (raw ctl ioctls) ---------------- */

/* Find the "Master" playback volume element and read its range
 * and current value (channel 0). Returns 0 on success. */
static int alsa_master(int fd, long *minv, long *maxv, long *cur)
{
    struct snd_ctl_elem_id id = {0};
    struct snd_ctl_elem_info info = {0};
    id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
    snprintf((char *)id.name, sizeof(id.name), "Master");
    info.id = id;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) return -1;
    if (info.type != SNDRV_CTL_ELEM_TYPE_INTEGER) return -1;
    *minv = info.value.integer.min;
    *maxv = info.value.integer.max;
    struct snd_ctl_elem_value val = {0};
    val.id = id;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &val) < 0) return -1;
    if (cur) *cur = val.value.integer.value[0];
    return 0;
}

static int alsa_volume_get(bd_backend *be)
{
    int fd = open(be->snd_dev, O_RDWR);
    if (fd < 0) return -1;
    long minv, maxv, cur;
    int rc = alsa_master(fd, &minv, &maxv, &cur);
    close(fd);
    if (rc < 0 || maxv <= minv) return -1;
    return (int)(((cur - minv) * 100 + (maxv - minv) / 2) / (maxv - minv));
}

static int alsa_volume_set(bd_backend *be, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int fd = open(be->snd_dev, O_RDWR);
    if (fd < 0) return -1;
    long minv, maxv;
    int rc = alsa_master(fd, &minv, &maxv, NULL);
    if (rc == 0) {
        struct snd_ctl_elem_value val = {0};
        val.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
        snprintf((char *)val.id.name, sizeof(val.id.name), "Master");
        long v = minv + ((maxv - minv) * pct + 50) / 100;
        /* set every channel the element has */
        struct snd_ctl_elem_info info = {0};
        info.id = val.id;
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) == 0) {
            unsigned int n = info.count;
            if (n > 128) n = 128;
            for (unsigned int i = 0; i < n; i++)
                val.value.integer.value[i] = v;
        } else {
            val.value.integer.value[0] = v;
        }
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &val) < 0) rc = -1;
    }
    close(fd);
    return rc;
}

/* ---------------- public API ---------------- */

bd_backend *bd_backend_sysfs_new(void)
{
    bd_backend *be = calloc(1, sizeof(*be));
    if (!be) return NULL;
    const char *root = getenv("BD_SYSFS_ROOT");
    snprintf(be->root, sizeof(be->root), "%s", root && root[0] ? root : "/sys");
    const char *snd = getenv("BD_SND_DEV");
    snprintf(be->snd_dev, sizeof(be->snd_dev), "%s",
             snd && snd[0] ? snd : "/dev/snd/controlC0");
    return be;
}

bd_backend *bd_backend_mock_new(void)
{
    bd_backend *be = calloc(1, sizeof(*be));
    if (!be) return NULL;
    be->mock = true;
    be->m_brightness = 70;
    be->m_volume = 50;
    be->m_capacity = 80;
    be->m_charging = false;
    return be;
}

void bd_backend_free(bd_backend *be) { free(be); }

const char *bd_backend_name(const bd_backend *be)
{
    return be->mock ? "mock" : "sysfs+alsa";
}

int bd_backend_brightness_get(bd_backend *be)
{
    return be->mock ? be->m_brightness : sysfs_brightness_get(be);
}

int bd_backend_brightness_set(bd_backend *be, int pct)
{
    if (be->mock) {
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        be->m_brightness = pct;
        be->m_writes++;
        return 0;
    }
    return sysfs_brightness_set(be, pct);
}

int bd_backend_volume_get(bd_backend *be)
{
    return be->mock ? be->m_volume : alsa_volume_get(be);
}

int bd_backend_volume_set(bd_backend *be, int pct)
{
    if (be->mock) {
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        be->m_volume = pct;
        be->m_writes++;
        return 0;
    }
    return alsa_volume_set(be, pct);
}

bool bd_backend_battery(bd_backend *be, bd_battery_info *out)
{
    if (be->mock) {
        memset(out, 0, sizeof(*out));
        out->present = true;
        out->capacity = be->m_capacity;
        out->charging = be->m_charging;
        snprintf(out->status, sizeof(out->status), "%s",
                 be->m_charging ? "Charging" : "Discharging");
        return true;
    }
    return sysfs_battery(be, out);
}

void bd_backend_mock_set(bd_backend *be, int brightness, int volume,
                         int capacity, bool charging)
{
    if (!be->mock) return;
    be->m_brightness = brightness;
    be->m_volume = volume;
    be->m_capacity = capacity;
    be->m_charging = charging;
}

int bd_backend_mock_writes(const bd_backend *be)
{
    return be->mock ? be->m_writes : 0;
}
