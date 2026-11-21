/* launcher - launch model on top of the desktop catalog:
 * pinned entries, recent entries, and the exec abstraction.
 *
 * Persistence goes through the brainreg-compatible store (reg.h):
 *   section "shell", key "pinned" : ';'-separated desktop-file ids
 *   section "shell", key "recent" : ';'-separated ids, newest first
 * Desktop-file ids are file basenames (no ';' or '/'), so the
 * separator is unambiguous. Recents are capped by COUNT
 * (LN_RECENT_MAX) only - never by age: the device's clock is not
 * trustworthy and it is offline by default, so day-based expiry
 * is meaningless here (DESIGN-shell-v2.md section 12). Persisted
 * ids are sanitized at load (basename shape, deduplicated); a
 * corrupt store degrades to fewer entries, never to bad launches.
 *
 * Launching never calls exec directly in the model: the caller
 * supplies an exec callback. Production code uses ln_exec_real();
 * tests inject a mock and assert on the argv that would be run.
 * This is the seam the old braindesk lacked entirely (its launcher
 * entries had exec = NULL for decoration).
 */
#ifndef BD_LAUNCHER_H
#define BD_LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>
#include "desktop.h"

#define LN_RECENT_MAX 10

typedef struct ln_store ln_store;

/* Loads pins/recents from the reg store. */
ln_store *ln_store_new(void);
void ln_store_free(ln_store *s);

/* --- pins --- */
size_t ln_pin_count(const ln_store *s);
const char *ln_pin_at(const ln_store *s, size_t i); /* desktop id */
bool ln_is_pinned(const ln_store *s, const char *id);
bool ln_pin(ln_store *s, const char *id);    /* false if already pinned */
bool ln_unpin(ln_store *s, const char *id);  /* false if not pinned */

/* --- recents (newest first) --- */
size_t ln_recent_count(const ln_store *s);
const char *ln_recent_at(const ln_store *s, size_t i);
/* Record a launch: id moves to the front, duplicates removed,
 * list capped at LN_RECENT_MAX. Persists immediately. */
void ln_record_launch(ln_store *s, const char *id);

/* --- launching --- */

/* Exec callback: receives the entry, the expanded argv (owned by
 * the caller of ln_launch, valid only during the callback) and the
 * entry's Terminal flag. Returns true on successful spawn. */
typedef bool (*ln_exec_fn)(void *userdata, const de_entry *entry,
                           char *const argv[], bool terminal);

/* Expand the entry's Exec and invoke the exec callback. Also
 * records the launch in recents (only when the callback reports
 * success). Returns false for non-launchable entries. */
bool ln_launch(ln_store *s, const de_entry *entry,
               ln_exec_fn fn, void *userdata);

/* As ln_launch(), but the Exec file/URL field codes (%f %F %u %U)
 * expand from the given lists - the regular way to open files or
 * URLs with an application (e.g. "open with" from the file
 * manager). Either list may be NULL/empty. */
bool ln_launch_files(ln_store *s, const de_entry *entry,
                     char *const *files, size_t n_files,
                     char *const *urls, size_t n_urls,
                     ln_exec_fn fn, void *userdata);

/* Production exec: fork + execvp, detached from the shell.
 * Terminal entries are run via xterm -e as a fallback (documented
 * in DESIGN-shell-v2.md; the on-device terminal choice is an
 * integration decision). Returns false when the exec itself fails
 * (e.g. the binary vanished with a removed SD card), detected via
 * a close-on-exec error pipe - fork success alone is NOT reported
 * as launch success, so failed launches never enter recents. */
bool ln_exec_real(void *userdata, const de_entry *entry,
                  char *const argv[], bool terminal);

#endif /* BD_LAUNCHER_H */
