/* test_greeter_auth - greeter2 auth core against fixture
 * passwd/shadow files. The fixture shadow hash is generated with
 * crypt() itself at test start (fixed salt), so the test does not
 * depend on a precomputed hash being portable. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "greeter2_auth.h"

#include <crypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

int main(void)
{
    char dir[] = "/tmp/bdgreetXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char passwd_p[256], shadow_p[256];
    snprintf(passwd_p, sizeof(passwd_p), "%s/passwd", dir);
    snprintf(shadow_p, sizeof(shadow_p), "%s/shadow", dir);

    mkfile(passwd_p,
           "root:x:0:0:root:/root:/bin/bash\n"
           "user:x:1000:1000:Brain User,,,:/home/user:/bin/bash\n"
           "guest:x:1001:1001::/home/guest:/usr/sbin/nologin\n"
           "svc:x:1002:1002::/home/svc:/bin/false\n"
           "nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin\n");

    char *hash = crypt("brain", "$6$bdsalt42$");
    CHECK(hash != NULL);
    char shadow[1024];
    snprintf(shadow, sizeof(shadow),
             "root:!:19000:0:99999:7:::\n"
             "user:%s:19000:0:99999:7:::\n"
             "guest:*:19000:0:99999:7:::\n", hash);
    mkfile(shadow_p, shadow);

    setenv("BD_GREETER_PASSWD", passwd_p, 1);
    setenv("BD_GREETER_SHADOW", shadow_p, 1);

    /* user enumeration: only "user" qualifies */
    bd_greeter_user users[8];
    int n = bd_greeter_users(users, 8);
    CHECK(n == 1);
    if (n >= 1) {
        CHECK(strcmp(users[0].name, "user") == 0);
        CHECK(strcmp(users[0].gecos, "Brain User") == 0);
        CHECK(users[0].uid == 1000);
    }

    /* verification */
    CHECK(bd_greeter_verify("user", "brain"));
    CHECK(!bd_greeter_verify("user", "wrong"));
    CHECK(!bd_greeter_verify("user", ""));
    CHECK(!bd_greeter_verify("root", "root"));   /* locked '!' */
    CHECK(!bd_greeter_verify("guest", "brain")); /* locked '*' */
    CHECK(!bd_greeter_verify("nosuch", "brain"));/* unknown user */
    CHECK(!bd_greeter_verify("user", NULL));

    /* missing shadow file verifies nothing */
    setenv("BD_GREETER_SHADOW", "/nonexistent/shadow", 1);
    CHECK(!bd_greeter_verify("user", "brain"));

    printf("test_greeter_auth: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
