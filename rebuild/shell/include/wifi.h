/* wifi - Wi-Fi control for shell v2 (M3) through the wpa_supplicant
 * control interface.
 *
 * Why wpa_supplicant: it is the standard Linux Wi-Fi supplicant
 * and its control interface (Unix datagram sockets, text commands,
 * the protocol wpa_cli speaks) needs no extra daemon and no
 * library - the same "talk to the facility directly" approach as
 * the ALSA/sysfs backend. The shell's quick-settings toggle drove
 * only a reg value until M3; this module is what makes it real.
 *
 * Scope: liveness (PING), scan trigger + result parsing, status
 * (state/SSID/IP), connect to an open or WPA-PSK network
 * (ADD_NETWORK/SET_NETWORK/ENABLE_NETWORK/SELECT_NETWORK),
 * disconnect, reconnect. Enterprise (802.1X) configuration,
 * WPS and P2P are out of scope for this terminal.
 *
 * Failure model (same as backend.h): when no supplicant answers,
 * every call returns -1 and callers degrade silently - offline is
 * the default state of this device (DESIGN section 12.2), never
 * an error worth surfacing. No state is persisted here: network
 * definitions live in wpa_supplicant's own config (its storage,
 * its atomicity); the shell keeps only the on/off preference in
 * reg.
 *
 * Testability: the control path is a parameter. Tests point it
 * at a mock supplicant socket (tests/fake_wpa.c) and the exact
 * production client code runs against it.
 */
#ifndef BD_WIFI_H
#define BD_WIFI_H

#include <stdbool.h>

typedef struct bd_wifi bd_wifi;

typedef struct {
    char bssid[18];
    int freq;        /* MHz */
    int signal_dbm;
    char flags[64];  /* e.g. "[WPA2-PSK-CCMP][ESS]" */
    char ssid[64];   /* may be empty (hidden network) */
} bd_wifi_ap;

typedef struct {
    bool connected;  /* wpa_state == COMPLETED */
    char state[24];  /* raw wpa_state: COMPLETED, DISCONNECTED, ... */
    char ssid[64];
    char bssid[18];
    char ip[48];     /* ip_address, empty until DHCP completes */
    int network_id;  /* selected network id, -1 when none */
} bd_wifi_status;

/* ctrl_path: the supplicant's per-interface control socket, e.g.
 * "/var/run/wpa_supplicant/wlan0". NULL selects $BD_WPA_CTRL,
 * else the default path above. The object is lazy: the socket is
 * opened on the first command and reopened if it breaks. */
bd_wifi *bd_wifi_new(const char *ctrl_path);
void bd_wifi_free(bd_wifi *w);
const char *bd_wifi_ctrl_path(const bd_wifi *w);

/* 0 when the supplicant answered PONG, -1 otherwise. */
int bd_wifi_ping(bd_wifi *w);
/* Ask the supplicant to scan. Results are read separately with
 * bd_wifi_scan_results() once the scan completes (the supplicant
 * keeps the last scan's results; callers poll). */
int bd_wifi_scan(bd_wifi *w);
/* Parse the latest SCAN_RESULTS. Returns the number of APs filled
 * (<= max), or -1 on transport failure. */
int bd_wifi_scan_results(bd_wifi *w, bd_wifi_ap *out, int max);
/* 0 and *out filled, or -1 on transport failure. */
int bd_wifi_get_status(bd_wifi *w, bd_wifi_status *out);
/* Current signal strength of the connected AP (SIGNAL_POLL's
 * RSSI). 0 and *out_dbm filled (e.g. -57), or -1 when there is
 * no usable reading (not connected, supplicant silent, or the
 * -9999 "no signal" sentinel). STATUS carries no signal field,
 * so this is the only live source for the connected AP's
 * strength; scan results are per-AP snapshots from scan time. */
int bd_wifi_signal_dbm(bd_wifi *w, int *out_dbm);
/* Create/select a network and connect. psk NULL or "" means an
 * open network (key_mgmt NONE); otherwise WPA-PSK. On failure the
 * half-created network is removed again. 0 ok, -1 fail. */
int bd_wifi_connect(bd_wifi *w, const char *ssid, const char *psk);
int bd_wifi_disconnect(bd_wifi *w);
int bd_wifi_reconnect(bd_wifi *w);

#endif /* BD_WIFI_H */
