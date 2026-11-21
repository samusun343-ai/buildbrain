/* greeter2_auth.c - see greeter2_auth.h. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "greeter2_auth.h"

#include <crypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *passwd_path(void)
{
    const char *p = getenv("BD_GREETER_PASSWD");
    return p && p[0] ? p : "/etc/passwd";
}

static const char *shadow_path(void)
{
    const char *p = getenv("BD_GREETER_SHADOW");
    return p && p[0] ? p : "/etc/shadow";
}

static bool shell_is_login(const char *shell)
{
    if (!shell || !shell[0]) return true; /* default shell */
    size_t n = strlen(shell);
    if (n >= 7 && strcmp(shell + n - 7, "nologin") == 0) return false;
    if (n >= 5 && strcmp(shell + n - 5, "false") == 0) return false;
    return true;
}

int bd_greeter_users(bd_greeter_user *out, int max)
{
    FILE *f = fopen(passwd_path(), "r");
    if (!f) return 0;
    char line[1024];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        /* name:passwd:uid:gid:gecos:home:shell - split manually:
         * strtok would collapse empty fields (an empty GECOS is
         * common) and shift every later field, misreading the
         * shell of exactly the accounts this filter must reject. */
        char *fields[7];
        int nf = 0;
        char *p = line;
        fields[nf++] = p;
        while (*p && nf < 7) {
            if (*p == ':') { *p = 0; fields[nf++] = p + 1; }
            p++;
        }
        if (nf < 7) continue;
        char *name = fields[0];
        char *uid_s = fields[2];
        char *gecos = fields[4];
        char *shell = fields[6];
        shell[strcspn(shell, "\r\n")] = 0;
        int uid = atoi(uid_s);
        if (uid < 1000) continue;
        if (strcmp(name, "nobody") == 0) continue;
        if (!shell_is_login(shell)) continue;
        bd_greeter_user *u = &out[n++];
        snprintf(u->name, sizeof(u->name), "%s", name);
        u->uid = uid;
        u->gecos[0] = 0;
        if (gecos) {
            /* GECOS display name is the part before the first ',' */
            const char *comma = strchr(gecos, ',');
            size_t len = comma ? (size_t)(comma - gecos) : strlen(gecos);
            if (len >= sizeof(u->gecos)) len = sizeof(u->gecos) - 1;
            memcpy(u->gecos, gecos, len);
            u->gecos[len] = 0;
        }
    }
    fclose(f);
    return n;
}

bool bd_greeter_verify(const char *user, const char *password)
{
    if (!user || !password) return false;
    FILE *f = fopen(shadow_path(), "r");
    if (!f) return false;
    char line[2048];
    char hash[512] = "";
    bool found = false;
    size_t ulen = strlen(user);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, user, ulen) == 0 && line[ulen] == ':') {
            const char *h = line + ulen + 1;
            const char *end = strchr(h, ':');
            size_t len = end ? (size_t)(end - h) : strcspn(h, "\r\n");
            if (len >= sizeof(hash)) len = sizeof(hash) - 1;
            memcpy(hash, h, len);
            hash[len] = 0;
            found = true;
            break;
        }
    }
    fclose(f);
    if (!found) return false;
    if (hash[0] == 0 || hash[0] == '!' || hash[0] == '*') return false;
    char *enc = crypt(password, hash);
    if (!enc) return false;
    /* full-string compare against the stored hash (the salt and
     * parameters are inside it) */
    size_t a = strlen(enc), b = strlen(hash);
    if (a != b) return false;
    unsigned int diff = 0;
    for (size_t i = 0; i < a; i++) diff |= (unsigned char)(enc[i] ^ hash[i]);
    return diff == 0;
}
