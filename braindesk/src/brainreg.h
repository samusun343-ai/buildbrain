/* brainreg - central settings store for Brainux (see
 * docs/brainreg.md in buildbrain). Text INI-style files:
 *   system: .conf files in /etc/brainreg.d
 *   user:   .conf files in ~/.config/brainreg.d (wins over system)
 *   env:    BRAINREG_<SECTION>_<KEY>            (wins over both)
 * Writes always go to the user layer, one file per section
 * (~/.config/brainreg.d/<section>.conf).
 * BRAINREG_SYS_DIR / BRAINREG_USER_DIR override the directories
 * (used by tests). */
#ifndef BRAINREG_H
#define BRAINREG_H

#include <stdbool.h>
#include <stdio.h>

#define BRAINREG_SRC_SYSTEM 2
#define BRAINREG_SRC_USER 1
#define BRAINREG_SRC_ENV 0

typedef struct {
	char section[64];
	char key[64];
	char value[256];
	int source; /* BRAINREG_SRC_* of the winning layer */
} brainreg_entry;

/* Malloc'd value or NULL when unset. Caller frees. */
char *brainreg_get(const char *section, const char *key);
int brainreg_get_int(const char *section, const char *key, int def);
bool brainreg_get_bool(const char *section, const char *key, bool def);

/* Write/remove in the user layer. 0 on success. */
int brainreg_set(const char *section, const char *key, const char *value);
int brainreg_unset(const char *section, const char *key);

/* Merged view of every known key (system + user + env-only keys
 * cannot be enumerated, so env overrides are applied to keys that
 * exist in files). Returns the number of entries filled. */
int brainreg_list(brainreg_entry *out, int max);

/* Shell-style export lines: BRAINREG_<SECTION>_<KEY>='value' */
void brainreg_export(FILE *out);

#endif
