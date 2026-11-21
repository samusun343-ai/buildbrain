/* test_pkg - bd-pkg model tests.
 *
 * Version comparison is checked against the Debian policy
 * ordering rules. Everything else runs the REAL dpkg in a
 * scratch --root/--admindir tree with REAL .deb fixtures
 * built by dpkg-deb: install 1.0, see the 1.1 update, apply
 * it, dry-run a third package (must NOT land), remove.
 */
#define _POSIX_C_SOURCE 200809L
#include "pkgmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passes, fails;
#define CHECK(cond, ...) do { \
    if (cond) { passes++; printf("PASS "); } \
    else { fails++; printf("FAIL "); } \
    printf(__VA_ARGS__); putchar('\n'); } while (0)

static void sh(const char *cmd)
{
    if (system(cmd) != 0) {
        printf("setup command failed: %s\n", cmd);
        exit(2);
    }
}

/* Build a .deb fixture: name, version -> out path. */
static void make_deb(const char *dir, const char *name,
                     const char *version, char *out, size_t cap)
{
    char cmd[2400], sub[1200];
    snprintf(sub, sizeof(sub), "%s/build-%s-%s", dir, name, version);
    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s/DEBIAN %s/usr/share/%s && chmod 755 %s/DEBIAN",
             sub, sub, name, sub);
    sh(cmd);
    char ctl[1200];
    snprintf(ctl, sizeof(ctl), "%s/DEBIAN/control", sub);
    FILE *f = fopen(ctl, "w");
    fprintf(f,
            "Package: %s\n"
            "Version: %s\n"
            "Architecture: all\n"
            "Maintainer: Brain Test <test@example.jp>\n"
            "Section: utils\n"
            "Installed-Size: 4\n"
            "Description: %s fixture package\n"
            " A long description line.\n",
            name, version, name);
    fclose(f);
    char payload[1200];
    snprintf(payload, sizeof(payload), "%s/usr/share/%s/data.txt",
             sub, name);
    f = fopen(payload, "w");
    fprintf(f, "%s %s\n", name, version);
    fclose(f);
    snprintf(out, cap, "%s/%s_%s_all.deb", dir, name, version);
    snprintf(cmd, sizeof(cmd), "dpkg-deb --build %s %s >/dev/null",
             sub, out);
    sh(cmd);
}

static void check_cmp(const char *a, const char *b, int want_sign)
{
    int c = pkg_debver_cmp(a, b);
    int sign = (c > 0) - (c < 0);
    CHECK(sign == want_sign, "debver %s vs %s -> %d (want %d)",
          a, b, sign, want_sign);
}

int main(void)
{
    /* ---- Debian version ordering (policy manual 5.6.12) ---- */
    check_cmp("1.0", "1.0", 0);
    check_cmp("1.0", "1.1", -1);
    check_cmp("1.0~beta", "1.0", -1);     /* tilde before empty */
    check_cmp("1.0~~", "1.0~", -1);
    check_cmp("1.0~rc1", "1.0~rc2", -1);
    check_cmp("1:1.0", "2.0", 1);         /* epoch wins */
    check_cmp("2:1.0-1", "1:9.9-9", 1);
    check_cmp("1.0-1", "1.0-2", -1);      /* revision */
    check_cmp("1.0", "1.0-0", 0);         /* missing revision = 0 */
    check_cmp("1.0a", "1.0b", -1);
    check_cmp("1.0", "1.0.1", -1);
    check_cmp("1.0+dfsg", "1.0", 1);
    check_cmp("1.09", "1.1", 1);          /* numeric parts: 9 > 1 */
    check_cmp("1.0-1", "1.0-1.1", -1);
    check_cmp("0.9.8", "1.0~alpha", -1);
    check_cmp("1.0~", "1.0", -1);
    check_cmp("1.0", "1.0~", 1);

    /* ---- real dpkg in a scratch root ---- */
    char tmpl[] = "/tmp/bdpkg-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 2; }
    char cmd[2400], root[1100], admin[1100], debs[1100];
    snprintf(root, sizeof(root), "%s/root", tmpl);
    snprintf(admin, sizeof(admin), "%s/root/var/lib/dpkg", tmpl);
    snprintf(debs, sizeof(debs), "%s/debs", tmpl);
    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s %s && touch %s/status %s/available",
             admin, debs, admin, admin);
    sh(cmd);

    char deb_hello10[1200], deb_hello11[1200], deb_other[1200];
    make_deb(debs, "bd-hello", "1.0", deb_hello10, sizeof(deb_hello10));
    make_deb(debs, "bd-hello", "1.1", deb_hello11, sizeof(deb_hello11));
    make_deb(debs, "bd-other", "2.0", deb_other, sizeof(deb_other));

    pkg_sys *sys = pkg_open(NULL, NULL, root, admin);
    CHECK(sys != NULL, "sys open");
    CHECK(pkg_refresh_installed(sys) == 0, "refresh empty root");
    CHECK(pkg_installed_count(sys) == 0, "nothing installed yet");

    CHECK(pkg_scan_catalog(sys, debs) == 0, "catalog scan");
    CHECK(pkg_catalog_count(sys) == 3, "catalog 3, got %zu",
          pkg_catalog_count(sys));
    const pkg_entry *ce = pkg_catalog_find(sys, "bd-hello");
    CHECK(ce && strcmp(ce->version, "1.1") == 0,
          "catalog find returns newest (1.1)");
    CHECK(ce && strstr(ce->description, "fixture") != NULL,
          "catalog description parsed");
    CHECK(pkg_updates_count(sys) == 0, "no updates before install");

    /* argv inspection (dry-run surface) */
    const char *argv[12];
    size_t argc = pkg_install_argv(sys, deb_hello10, argv, 12);
    CHECK(argc >= 4 && strcmp(argv[0], "dpkg") == 0,
          "install argv starts with dpkg");
    bool has_i = false, has_path = false, has_root = false;
    for (size_t i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) has_i = true;
        if (strcmp(argv[i], deb_hello10) == 0) has_path = true;
        if (strcmp(argv[i], "--root") == 0) has_root = true;
    }
    CHECK(has_i && has_path && has_root, "install argv complete");
    char log[2048];
    CHECK(pkg_install(sys, deb_other, true, log, sizeof(log)) == 0,
          "dry-run install ok");
    CHECK(strstr(log, "dry-run") != NULL && strstr(log, "dpkg") != NULL,
          "dry-run log shows argv: %s", log);
    CHECK(pkg_refresh_installed(sys) == 0 &&
          pkg_installed_count(sys) == 0,
          "dry-run installed nothing");

    /* real install of 1.0 */
    CHECK(pkg_install(sys, deb_hello10, false, log, sizeof(log)) == 0,
          "install 1.0: %s", log);
    CHECK(pkg_refresh_installed(sys) == 0, "refresh after install");
    CHECK(pkg_installed_count(sys) == 1, "1 installed");
    const pkg_entry *ie = pkg_installed_find(sys, "bd-hello");
    CHECK(ie && strcmp(ie->version, "1.0") == 0, "installed version 1.0");
    CHECK(ie && strcmp(ie->section, "utils") == 0, "section parsed");
    CHECK(pkg_updates_count(sys) == 1, "one update available (1.1)");
    const pkg_entry *cat_up = NULL;
    const pkg_entry *upd = pkg_update_at(sys, 0, &cat_up);
    CHECK(upd && cat_up && strcmp(cat_up->version, "1.1") == 0,
          "update points at catalog 1.1");

    /* apply the update */
    CHECK(pkg_install(sys, cat_up->deb_path, false, log, sizeof(log)) == 0,
          "install update: %s", log);
    CHECK(pkg_refresh_installed(sys) == 0, "refresh after update");
    ie = pkg_installed_find(sys, "bd-hello");
    CHECK(ie && strcmp(ie->version, "1.1") == 0, "now at 1.1");
    CHECK(pkg_updates_count(sys) == 0, "no updates left");

    /* dry-run remove keeps the package; real remove drops it */
    CHECK(pkg_remove(sys, "bd-hello", true, log, sizeof(log)) == 0,
          "dry-run remove ok");
    CHECK(pkg_refresh_installed(sys) == 0 &&
          pkg_installed_count(sys) == 1, "dry-run removed nothing");
    CHECK(pkg_remove(sys, "bd-hello", false, log, sizeof(log)) == 0,
          "remove: %s", log);
    CHECK(pkg_refresh_installed(sys) == 0 &&
          pkg_installed_count(sys) == 0, "removed");
    pkg_close(sys);

    printf("test_pkg: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
