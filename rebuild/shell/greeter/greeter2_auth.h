/* greeter2_auth - user enumeration and password verification for
 * braingreeter2. The verification core is the part of the old
 * greeter.c the audit kept ("照合核は部品取り"): read the shadow
 * entry and compare crypt() output. Everything else about the old
 * greeter (fbcon rendering, skin following) was discarded.
 *
 * File locations default to /etc/passwd and /etc/shadow and can be
 * overridden with BD_GREETER_PASSWD / BD_GREETER_SHADOW (tests use
 * fixtures; on device the greeter runs as root so shadow is
 * readable). Locked accounts ('!'/'*' hash, empty hash) never
 * verify. A missing/unreadable shadow file verifies nothing.
 */
#ifndef BD_GREETER2_AUTH_H
#define BD_GREETER2_AUTH_H

#include <stdbool.h>

typedef struct {
    char name[64];
    char gecos[128]; /* display name part of GECOS, may be empty */
    int uid;
} bd_greeter_user;

/* List login-capable users: uid >= 1000, shell not ending in
 * nologin/false, excluding "nobody". Returns the count filled. */
int bd_greeter_users(bd_greeter_user *out, int max);

/* Verify `password` for `user` against the shadow file.
 * Constant-work for unknown users is NOT guaranteed (the lookup
 * itself differs); the greeter is a local console, and this limit
 * is documented rather than hidden. */
bool bd_greeter_verify(const char *user, const char *password);

#endif /* BD_GREETER2_AUTH_H */
