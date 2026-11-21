/* fake_wpa - mock wpa_supplicant for test_wifi: binds a Unix
 * datagram socket and answers control commands the way the real
 * supplicant does (subset used by wifi.c), logging every command
 * it receives so tests can verify the client's command sequence.
 *
 * usage: fake_wpa <sockpath> <logpath>
 * Exits on SIGTERM or a QUIT command. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char *reply_for(const char *cmd)
{
    if (strcmp(cmd, "PING") == 0) return "PONG\n";
    if (strcmp(cmd, "STATUS") == 0) {
        /* FAKE_WPA_STATE=DISCONNECTED poses an unconnected
         * supplicant for tests / screenshot harnesses */
        const char *st = getenv("FAKE_WPA_STATE");
        if (st && strcmp(st, "DISCONNECTED") == 0)
            return "wpa_state=DISCONNECTED\n";
        return "bssid=aa:bb:cc:dd:ee:ff\n"
               "freq=2437\n"
               "ssid=TestNet\n"
               "id=0\n"
               "mode=station\n"
               "pairwise_cipher=CCMP\n"
               "group_cipher=CCMP\n"
               "key_mgmt=WPA2-PSK\n"
               "wpa_state=COMPLETED\n"
               "ip_address=192.168.1.50\n";
    }
    if (strcmp(cmd, "SIGNAL_POLL") == 0) {
        /* FAKE_WPA_RSSI overrides the reading so screenshot
         * harnesses can pose weak/strong links */
        const char *r = getenv("FAKE_WPA_RSSI");
        static char buf[128];
        snprintf(buf, sizeof(buf),
                 "RSSI=%s\nLINKSPEED=54\nNOISE=9999\nFREQUENCY=2437\n",
                 r && *r ? r : "-42");
        return buf;
    }
    if (strcmp(cmd, "SCAN") == 0) return "OK\n";
    if (strcmp(cmd, "SCAN_RESULTS") == 0)
        return "bssid / frequency / signal level / flags / ssid\n"
               "aa:bb:cc:dd:ee:ff\t2437\t-42\t[WPA2-PSK-CCMP][ESS]\tTestNet\n"
               "11:22:33:44:55:66\t5180\t-70\t[WPA2-PSK-CCMP][ESS]\tOffice 5G\n"
               "22:33:44:55:66:77\t2412\t-81\t[ESS]\tOpenNet\n";
    if (strcmp(cmd, "ADD_NETWORK") == 0) return "0\n";
    if (strncmp(cmd, "SET_NETWORK", 11) == 0) {
        /* a network whose SSID contains BAD fails, like a real
         * supplicant rejecting an invalid value */
        return strstr(cmd, "BAD") ? "FAIL\n" : "OK\n";
    }
    if (strncmp(cmd, "ENABLE_NETWORK", 14) == 0) return "OK\n";
    if (strncmp(cmd, "SELECT_NETWORK", 14) == 0) return "OK\n";
    if (strncmp(cmd, "REMOVE_NETWORK", 14) == 0) return "OK\n";
    if (strcmp(cmd, "DISCONNECT") == 0) return "OK\n";
    if (strcmp(cmd, "RECONNECT") == 0) return "OK\n";
    return "FAIL\n";
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const char *sockpath = argv[1], *logpath = argv[2];
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return 1;
    struct sockaddr_un un = {0};
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", sockpath);
    unlink(sockpath);
    if (bind(fd, (struct sockaddr *)&un, sizeof(un)) < 0) return 1;
    FILE *log = fopen(logpath, "w");
    if (!log) return 1;
    fclose(log);
    for (;;) {
        char buf[2048];
        struct sockaddr_un from = {0};
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&from, &flen);
        if (n < 0) continue;
        buf[n] = 0;
        log = fopen(logpath, "a");
        if (log) { fprintf(log, "%s\n", buf); fclose(log); }
        if (strcmp(buf, "QUIT") == 0) break;
        const char *rep = reply_for(buf);
        (void)sendto(fd, rep, strlen(rep), 0,
                     (struct sockaddr *)&from, flen);
    }
    close(fd);
    unlink(sockpath);
    return 0;
}
