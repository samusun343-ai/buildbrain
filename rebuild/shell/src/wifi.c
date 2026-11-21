/* wifi.c - wpa_supplicant control-interface client. See wifi.h. */
#include "wifi.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BD_WPA_DEFAULT_CTRL "/var/run/wpa_supplicant/wlan0"
#define BD_WPA_REPLY_MAX 16384
#define BD_WPA_TIMEOUT_MS 3000

struct bd_wifi {
    char ctrl_path[256];
    char local_path[256];
    int fd; /* datagram socket, -1 when closed */
};

static int wifi_open(bd_wifi *w)
{
    if (w->fd >= 0) return 0;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un local = {0};
    local.sun_family = AF_UNIX;
    snprintf(local.sun_path, sizeof(local.sun_path), "%s", w->local_path);
    unlink(w->local_path);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_un remote = {0};
    remote.sun_family = AF_UNIX;
    snprintf(remote.sun_path, sizeof(remote.sun_path), "%s", w->ctrl_path);
    if (connect(fd, (struct sockaddr *)&remote, sizeof(remote)) < 0) {
        close(fd);
        unlink(w->local_path);
        return -1;
    }
    w->fd = fd;
    return 0;
}

static void wifi_close(bd_wifi *w)
{
    if (w->fd >= 0) close(w->fd);
    w->fd = -1;
    unlink(w->local_path);
}

/* Send one command and return the reply (NUL-terminated, trailing
 * newline stripped) or -1. Unsolicited event frames ("<3>...")
 * are skipped - we never ATTACH, but a supplicant that has other
 * clients attached may still broadcast to us. On any transport
 * error the socket is closed so the next call reopens it. */
static int wifi_request(bd_wifi *w, const char *cmd, char *reply, size_t cap)
{
    if (wifi_open(w) < 0) return -1;
    size_t len = strlen(cmd);
    if (send(w->fd, cmd, len, 0) != (ssize_t)len) {
        wifi_close(w);
        return -1;
    }
    int waited = 0;
    while (waited < BD_WPA_TIMEOUT_MS) {
        struct pollfd pfd = { w->fd, POLLIN, 0 };
        int slice = BD_WPA_TIMEOUT_MS - waited < 250
                        ? BD_WPA_TIMEOUT_MS - waited : 250;
        int rc = poll(&pfd, 1, slice);
        if (rc < 0) { wifi_close(w); return -1; }
        if (rc == 0) { waited += slice; continue; }
        ssize_t n = recv(w->fd, reply, cap - 1, 0);
        if (n < 0) { wifi_close(w); return -1; }
        reply[n] = 0;
        if (reply[0] == '<') { waited += slice; continue; } /* event */
        while (n > 0 && (reply[n - 1] == '\n' || reply[n - 1] == '\r'))
            reply[--n] = 0;
        return 0;
    }
    return -1; /* timed out; socket stays (supplicant may be busy) */
}

bd_wifi *bd_wifi_new(const char *ctrl_path)
{
    bd_wifi *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    if (!ctrl_path) ctrl_path = getenv("BD_WPA_CTRL");
    if (!ctrl_path || !*ctrl_path) ctrl_path = BD_WPA_DEFAULT_CTRL;
    snprintf(w->ctrl_path, sizeof(w->ctrl_path), "%s", ctrl_path);
    const char *dir = getenv("BD_WPA_LOCAL_DIR");
    if (!dir || !*dir) dir = "/tmp";
    /* the local path must be unique per client object, not
     * just per process: the socket stays open for the object's
     * life, and a second client in the same process unlinking
     * and rebinding the shared path would silently steal the
     * first client's replies (found by the two-supplicant
     * screenshot harness - REPORT-status-icons section 9) */
    static unsigned local_seq;
    snprintf(w->local_path, sizeof(w->local_path), "%s/bdwifi-%ld-%u",
             dir, (long)getpid(), (unsigned)++local_seq);
    w->fd = -1;
    return w;
}

void bd_wifi_free(bd_wifi *w)
{
    if (!w) return;
    wifi_close(w);
    free(w);
}

const char *bd_wifi_ctrl_path(const bd_wifi *w) { return w->ctrl_path; }

int bd_wifi_ping(bd_wifi *w)
{
    char reply[64];
    if (wifi_request(w, "PING", reply, sizeof(reply)) < 0) return -1;
    return strcmp(reply, "PONG") == 0 ? 0 : -1;
}

int bd_wifi_scan(bd_wifi *w)
{
    char reply[64];
    if (wifi_request(w, "SCAN", reply, sizeof(reply)) < 0) return -1;
    return strcmp(reply, "OK") == 0 ? 0 : -1;
}

int bd_wifi_scan_results(bd_wifi *w, bd_wifi_ap *out, int max)
{
    if (!out || max <= 0) return -1;
    char *reply = malloc(BD_WPA_REPLY_MAX);
    if (!reply) return -1;
    if (wifi_request(w, "SCAN_RESULTS", reply, BD_WPA_REPLY_MAX) < 0) {
        free(reply);
        return -1;
    }
    int n = 0;
    char *save = NULL;
    bool first = true;
    for (char *line = strtok_r(reply, "\n", &save); line && n < max;
         line = strtok_r(NULL, "\n", &save)) {
        if (first) { first = false; continue; } /* header row */
        bd_wifi_ap *ap = &out[n];
        memset(ap, 0, sizeof(*ap));
        /* bssid \t freq \t signal \t flags \t ssid(rest of line) */
        char *f[4];
        char *p = line;
        bool ok = true;
        for (int i = 0; i < 4; i++) {
            char *tab = strchr(p, '\t');
            if (!tab) { ok = false; break; }
            *tab = 0;
            f[i] = p;
            p = tab + 1;
        }
        if (!ok) continue;
        snprintf(ap->bssid, sizeof(ap->bssid), "%s", f[0]);
        ap->freq = atoi(f[1]);
        ap->signal_dbm = atoi(f[2]);
        snprintf(ap->flags, sizeof(ap->flags), "%s", f[3]);
        snprintf(ap->ssid, sizeof(ap->ssid), "%s", p);
        n++;
    }
    free(reply);
    return n;
}

int bd_wifi_get_status(bd_wifi *w, bd_wifi_status *out)
{
    char reply[2048];
    if (wifi_request(w, "STATUS", reply, sizeof(reply)) < 0) return -1;
    memset(out, 0, sizeof(*out));
    out->network_id = -1;
    char *save = NULL;
    for (char *line = strtok_r(reply, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *val = eq + 1;
        if (strcmp(line, "wpa_state") == 0)
            snprintf(out->state, sizeof(out->state), "%s", val);
        else if (strcmp(line, "ssid") == 0)
            snprintf(out->ssid, sizeof(out->ssid), "%s", val);
        else if (strcmp(line, "bssid") == 0)
            snprintf(out->bssid, sizeof(out->bssid), "%s", val);
        else if (strcmp(line, "ip_address") == 0)
            snprintf(out->ip, sizeof(out->ip), "%s", val);
        else if (strcmp(line, "id") == 0)
            out->network_id = atoi(val);
    }
    out->connected = strcmp(out->state, "COMPLETED") == 0;
    return 0;
}

int bd_wifi_signal_dbm(bd_wifi *w, int *out_dbm)
{
    if (!out_dbm) return -1;
    char reply[1024];
    if (wifi_request(w, "SIGNAL_POLL", reply, sizeof(reply)) < 0)
        return -1;
    char *save = NULL;
    for (char *line = strtok_r(reply, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (strncmp(line, "RSSI=", 5) == 0) {
            int dbm = atoi(line + 5);
            /* the supplicant reports -9999 when it has no
             * reading (e.g. not associated) */
            if (dbm <= -100 || dbm >= 0) return -1;
            *out_dbm = dbm;
            return 0;
        }
    }
    return -1;
}

/* Escape a value for a wpa_supplicant quoted string ("..."): the
 * supplicant unescapes \" and \\ inside double quotes. */
static void wpa_escape(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    for (const char *p = src; *p && o + 3 < cap; p++) {
        if (*p == '"' || *p == '\\') dst[o++] = '\\';
        dst[o++] = *p;
    }
    dst[o] = 0;
}

static int wifi_cmd_ok(bd_wifi *w, const char *cmd)
{
    char reply[128];
    if (wifi_request(w, cmd, reply, sizeof(reply)) < 0) return -1;
    return strcmp(reply, "OK") == 0 ? 0 : -1;
}

int bd_wifi_connect(bd_wifi *w, const char *ssid, const char *psk)
{
    if (!ssid || !*ssid) return -1;
    char reply[128];
    if (wifi_request(w, "ADD_NETWORK", reply, sizeof(reply)) < 0)
        return -1;
    int id = atoi(reply);
    if (id < 0) return -1;
    char esc[160], cmd[512];
    wpa_escape(esc, sizeof(esc), ssid);
    snprintf(cmd, sizeof(cmd), "SET_NETWORK %d ssid \"%s\"", id, esc);
    if (wifi_cmd_ok(w, cmd) < 0) goto fail;
    if (psk && *psk) {
        wpa_escape(esc, sizeof(esc), psk);
        snprintf(cmd, sizeof(cmd), "SET_NETWORK %d psk \"%s\"", id, esc);
        if (wifi_cmd_ok(w, cmd) < 0) goto fail;
    } else {
        snprintf(cmd, sizeof(cmd), "SET_NETWORK %d key_mgmt NONE", id);
        if (wifi_cmd_ok(w, cmd) < 0) goto fail;
    }
    snprintf(cmd, sizeof(cmd), "ENABLE_NETWORK %d", id);
    if (wifi_cmd_ok(w, cmd) < 0) goto fail;
    snprintf(cmd, sizeof(cmd), "SELECT_NETWORK %d", id);
    if (wifi_cmd_ok(w, cmd) < 0) goto fail;
    return 0;
fail:
    snprintf(cmd, sizeof(cmd), "REMOVE_NETWORK %d", id);
    (void)wifi_cmd_ok(w, cmd);
    return -1;
}

int bd_wifi_disconnect(bd_wifi *w)
{
    return wifi_cmd_ok(w, "DISCONNECT");
}

int bd_wifi_reconnect(bd_wifi *w)
{
    return wifi_cmd_ok(w, "RECONNECT");
}
