/* test_wifi - the production wpa_supplicant client (src/wifi.c)
 * driven against a mock supplicant socket (tests/fake_wpa.c):
 * liveness, status parsing, scan-result parsing (including an
 * SSID with a space), the full connect command sequence for PSK
 * and open networks (verified against the fake's command log),
 * failure cleanup (REMOVE_NETWORK after a rejected SET_NETWORK),
 * and graceful -1 behaviour when no supplicant exists. */
#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond) do { \
    if (cond) { passes++; } \
    else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static char logbuf[8192];

static void read_log(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { logbuf[0] = 0; return; }
    size_t n = fread(logbuf, 1, sizeof(logbuf) - 1, f);
    logbuf[n] = 0;
    fclose(f);
}

static bool log_has(const char *needle) { return strstr(logbuf, needle) != NULL; }

static bool wait_file(const char *path, int timeout_ms)
{
    struct stat st;
    while (timeout_ms > 0) {
        if (stat(path, &st) == 0) return true;
        usleep(10000);
        timeout_ms -= 10;
    }
    return false;
}

int main(void)
{
    char dir[] = "/tmp/bdwifitestXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char sock[256], logp[256];
    snprintf(sock, sizeof(sock), "%s/wpa.sock", dir);
    snprintf(logp, sizeof(logp), "%s/cmds.log", dir);
    setenv("BD_WPA_LOCAL_DIR", dir, 1);

    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = { "./fake_wpa", sock, logp, NULL };
        execv(args[0], args);
        _exit(127);
    }
    CHECK(wait_file(sock, 3000));

    /* --- default-path selection via environment --- */
    setenv("BD_WPA_CTRL", sock, 1);
    bd_wifi *w = bd_wifi_new(NULL);
    CHECK(w != NULL);
    CHECK(strcmp(bd_wifi_ctrl_path(w), sock) == 0);

    /* --- liveness + status parsing --- */
    CHECK(bd_wifi_ping(w) == 0);
    bd_wifi_status st;
    CHECK(bd_wifi_get_status(w, &st) == 0);
    CHECK(st.connected);
    CHECK(strcmp(st.state, "COMPLETED") == 0);
    CHECK(strcmp(st.ssid, "TestNet") == 0);
    CHECK(strcmp(st.bssid, "aa:bb:cc:dd:ee:ff") == 0);
    CHECK(strcmp(st.ip, "192.168.1.50") == 0);
    CHECK(st.network_id == 0);

    /* --- scan + results parsing --- */
    CHECK(bd_wifi_scan(w) == 0);
    bd_wifi_ap aps[8];
    int n = bd_wifi_scan_results(w, aps, 8);
    CHECK(n == 3);
    CHECK(strcmp(aps[0].ssid, "TestNet") == 0);
    CHECK(aps[0].freq == 2437 && aps[0].signal_dbm == -42);
    CHECK(strstr(aps[0].flags, "WPA2-PSK") != NULL);
    CHECK(strcmp(aps[1].ssid, "Office 5G") == 0); /* SSID with space */
    CHECK(strcmp(aps[2].ssid, "OpenNet") == 0);
    CHECK(strstr(aps[2].flags, "WPA") == NULL);   /* open network */

    /* --- live signal of the connected AP (SIGNAL_POLL) --- */
    int dbm = 0;
    CHECK(bd_wifi_signal_dbm(w, &dbm) == 0);
    CHECK(dbm == -42);
    CHECK(bd_wifi_signal_dbm(w, NULL) == -1);

    /* --- connect (PSK): full command sequence in the log --- */
    CHECK(bd_wifi_connect(w, "HomeNet", "secret123") == 0);
    read_log(logp);
    CHECK(log_has("ADD_NETWORK\n"));
    CHECK(log_has("SET_NETWORK 0 ssid \"HomeNet\"\n"));
    CHECK(log_has("SET_NETWORK 0 psk \"secret123\"\n"));
    CHECK(log_has("ENABLE_NETWORK 0\n"));
    CHECK(log_has("SELECT_NETWORK 0\n"));

    /* --- connect (open): key_mgmt NONE, no psk --- */
    CHECK(bd_wifi_connect(w, "OpenNet", NULL) == 0);
    read_log(logp);
    CHECK(log_has("SET_NETWORK 0 ssid \"OpenNet\"\n"));
    CHECK(log_has("SET_NETWORK 0 key_mgmt NONE\n"));

    /* --- connect failure: rejected SET_NETWORK removes the
     * half-created network and reports -1 --- */
    CHECK(bd_wifi_connect(w, "BADNET", "x") == -1);
    read_log(logp);
    CHECK(log_has("REMOVE_NETWORK 0\n"));

    /* --- disconnect / reconnect --- */
    CHECK(bd_wifi_disconnect(w) == 0);
    CHECK(bd_wifi_reconnect(w) == 0);
    bd_wifi_free(w);

    /* --- no supplicant: everything degrades to -1, no crash --- */
    char nosock[256];
    snprintf(nosock, sizeof(nosock), "%s/absent.sock", dir);
    bd_wifi *dead = bd_wifi_new(nosock);
    CHECK(dead != NULL);
    CHECK(bd_wifi_ping(dead) == -1);
    CHECK(bd_wifi_get_status(dead, &st) == -1);
    CHECK(bd_wifi_scan(dead) == -1);
    CHECK(bd_wifi_scan_results(dead, aps, 8) == -1);
    CHECK(bd_wifi_connect(dead, "X", "y") == -1);
    CHECK(bd_wifi_disconnect(dead) == -1);
    bd_wifi_free(dead);

    /* --- two clients in one process, two supplicants: the
     * per-object local socket paths must not collide (a shared
     * path lets one client's bind steal the other's replies -
     * regression test for the wifi.c local_seq fix) --- */
    {
        char sock2[256], logp2[256];
        snprintf(sock2, sizeof(sock2), "%s/wpa2.sock", dir);
        snprintf(logp2, sizeof(logp2), "%s/cmds2.log", dir);
        pid_t pid2 = fork();
        if (pid2 == 0) {
            char *args[] = { "./fake_wpa", sock2, logp2, NULL };
            execv(args[0], args);
            _exit(127);
        }
        CHECK(wait_file(sock2, 3000));
        bd_wifi *wA = bd_wifi_new(sock);
        bd_wifi *wB = bd_wifi_new(sock2);
        bd_wifi_status stA, stB;
        for (int i = 0; i < 2; i++) {
            CHECK(bd_wifi_get_status(wA, &stA) == 0 && stA.connected);
            CHECK(bd_wifi_get_status(wB, &stB) == 0 && stB.connected);
        }
        bd_wifi_free(wA);
        bd_wifi_free(wB);
        kill(pid2, SIGTERM);
        waitpid(pid2, NULL, 0);
    }

    /* stop the fake via its QUIT command */
    {
        bd_wifi *q = bd_wifi_new(sock);
        /* bd_wifi has no raw-command API; use a bare datagram */
        bd_wifi_free(q);
        int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        struct sockaddr_un un = {0};
        un.sun_family = AF_UNIX;
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", sock);
        (void)sendto(fd, "QUIT", 4, 0, (struct sockaddr *)&un, sizeof(un));
        close(fd);
        int status = 0;
        bool exited = false;
        for (int t = 0; t < 3000 && !exited; t += 20) {
            if (waitpid(pid, &status, WNOHANG) == pid) exited = true;
            else usleep(20000);
        }
        CHECK(exited && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    printf("test_wifi: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
