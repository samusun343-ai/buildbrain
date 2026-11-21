/* pkgmodel - bd-pkg package management model.
 *
 * Backends (decision record): the system package manager is
 * dpkg - Brainux is a Debian rootfs and apt would pull in a
 * much larger dependency (lists, caches, network assumptions)
 * than this offline-first terminal can justify. The model
 * therefore talks to dpkg-query (installed list), dpkg-deb
 * (catalog .deb metadata) and dpkg (install/remove), through
 * an injectable argv runner:
 *  - production: fork/exec with output capture; installation
 *    itself needs root, which on the device means a privileged
 *    helper - wiring that helper is an OS-integration-phase
 *    decision, so the app ships with dry-run + argv display
 *    fully working and real execution one runner away.
 *  - tests: the REAL dpkg, confined with --root/--admindir
 *    to a temporary tree, installing REAL .deb fixtures built
 *    with dpkg-deb. Nothing is mocked.
 *
 * The catalog is a directory of .deb files (e.g. on the SD
 * card) - the offline software source. "Updates available"
 * = catalog version strictly newer than the installed one,
 * compared with the Debian version algorithm implemented
 * here (epoch/upstream/revision, '~' sorts before empty).
 */
#ifndef PKGMODEL_H
#define PKGMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    char version[64];
    char arch[32];
    char section[64];
    char maintainer[160];
    char description[256];
    uint64_t installed_size_kb;
    char deb_path[1024];      /* catalog only */
} pkg_entry;

/* Runs argv (NULL-terminated), captures combined stdout into
 * a malloc'd buffer the caller frees. Returns exit code. */
typedef int (*pkg_runner)(void *ud, const char *const *argv,
                          char **out, size_t *out_len);
int pkg_runner_exec(void *ud, const char *const *argv,
                    char **out, size_t *out_len);

typedef struct pkg_sys pkg_sys;

/* dpkg_root/dpkg_admin: NULL for the live system; tests point
 * them at a scratch tree. */
pkg_sys *pkg_open(pkg_runner runner, void *ud,
                  const char *dpkg_root, const char *dpkg_admin);
void pkg_close(pkg_sys *sys);

int pkg_refresh_installed(pkg_sys *sys);
size_t pkg_installed_count(const pkg_sys *sys);
const pkg_entry *pkg_installed_at(const pkg_sys *sys, size_t i);
const pkg_entry *pkg_installed_find(const pkg_sys *sys,
                                    const char *name);

int pkg_scan_catalog(pkg_sys *sys, const char *dir);
size_t pkg_catalog_count(const pkg_sys *sys);
const pkg_entry *pkg_catalog_at(const pkg_sys *sys, size_t i);
const pkg_entry *pkg_catalog_find(const pkg_sys *sys,
                                  const char *name);

/* installed entries with a strictly newer catalog version */
size_t pkg_updates_count(const pkg_sys *sys);
const pkg_entry *pkg_update_at(const pkg_sys *sys, size_t i,
                               const pkg_entry **catalog_entry);

/* Debian version comparison: <0, 0, >0. */
int pkg_debver_cmp(const char *a, const char *b);

/* Action argv construction (for display / dry-run). Returns
 * the argc written (argv_out is NULL-terminated). */
size_t pkg_install_argv(const pkg_sys *sys, const char *deb_path,
                        const char **argv_out, size_t max);
size_t pkg_remove_argv(const pkg_sys *sys, const char *name,
                       const char **argv_out, size_t max);

/* Execute (or, with dry_run, only report the argv in log).
 * log receives a human-readable summary. 0 on success. */
int pkg_install(pkg_sys *sys, const char *deb_path, bool dry_run,
                char *log, size_t cap);
int pkg_remove(pkg_sys *sys, const char *name, bool dry_run,
               char *log, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* PKGMODEL_H */
