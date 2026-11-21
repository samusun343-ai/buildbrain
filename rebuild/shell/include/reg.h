/* reg - brainreg-compatible settings store for the shell v2.
 *
 * Compatibility contract (the old brainreg format was judged
 * "salvageable" in audit-2026-11-20 section 4, so the format and
 * semantics are kept bit-for-bit at the file level):
 *
 *   - INI-style text files, one file per section in the user layer.
 *   - Layers, highest priority first:
 *       env  BRAINREG_<SECTION>_<KEY>   (non-alnum -> '_', uppercased)
 *       user ~/.config/brainreg.d/<section>.conf   (writes land here)
 *       sys  .conf files in /etc/brainreg.d
 *   - BRAINREG_SYS_DIR / BRAINREG_USER_DIR override the directories
 *     (used by tests, same as the old implementation).
 *   - File syntax: [section] headers, key = value lines, '#' and ';'
 *     comments, whitespace trimmed. Later definition in a layer wins.
 *
 * This is a fresh implementation of that contract for the rebuild
 * tree; it intentionally exposes the same function names/semantics
 * as the old brainreg.h so existing keys (e.g. written by brainctl)
 * keep working. Known limits inherited from the format (no schema,
 * no types, no change notification) are documented in
 * DESIGN-shell-v2.md section 10.
 *
 * Durability: writes are atomic against sudden reset/power loss -
 * the new content is written to <section>.conf.tmp, fsynced,
 * renamed over <section>.conf, and the directory is fsynced. A
 * crash therefore leaves the old or the new file, never a torn
 * one. Readers are line-based and lenient: a damaged line is
 * skipped and typed getters fall back to caller defaults, so a
 * corrupt store degrades to defaults instead of breaking callers.
 */
#ifndef BD_REG_H
#define BD_REG_H

#include <stdbool.h>
#include <stdio.h>

#define REG_SRC_SYSTEM 2
#define REG_SRC_USER   1
#define REG_SRC_ENV    0

typedef struct {
    char section[64];
    char key[64];
    char value[256];
    int source; /* REG_SRC_* of the winning layer */
} reg_entry;

/* Malloc'd value or NULL when unset. Caller frees. */
char *reg_get(const char *section, const char *key);
int   reg_get_int(const char *section, const char *key, int def);
bool  reg_get_bool(const char *section, const char *key, bool def);

/* Write/remove in the user layer. 0 on success. */
int reg_set(const char *section, const char *key, const char *value);
int reg_unset(const char *section, const char *key);

/* Merged view of every known key (env overrides applied to keys
 * that exist in files). Returns the number of entries filled. */
int reg_list(reg_entry *out, int max);

/* Shell-style export lines: BRAINREG_<SECTION>_<KEY>='value' */
void reg_export(FILE *out);

#endif /* BD_REG_H */
