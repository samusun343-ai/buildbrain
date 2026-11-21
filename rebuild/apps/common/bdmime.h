/* bdmime - file-type -> application association for the v2 apps.
 *
 * Associations:
 *   - Associations live in the brainreg store, section "mime",
 *     key = lowercase extension without the dot ("txt"), value =
 *     desktop-file id ("bd-notes.desktop"). User associations win
 *     over the built-in defaults below.
 *
 * Opening a file (migrated to the regular field-code path):
 *   - bd_mime_argv() / bd_mime_argv_files() expand the handler's
 *     Exec through the catalog's de_exec_argv_files(): %f is
 *     substituted in place, %F expands to one argument per file.
 *   - This replaces the wave-1 workaround, which appended the
 *     path as the final argument because the catalog then dropped
 *     the file codes. The workaround is gone; the semantics are
 *     now exactly the freedesktop ones the shell's own launcher
 *     (ln_launch_files) uses.
 *   - Consequence: a handler whose Exec carries NO file code
 *     receives NO path. Handlers that open files must declare
 *     %f/%F in their Exec line - every in-tree handler does
 *     (bd-notes %f, bd-files %f, ...). The handler is still
 *     launched; it simply starts empty, like on any desktop.
 */
#ifndef BDMIME_H
#define BDMIME_H

#include <stdbool.h>
#include <stddef.h>
#include "desktop.h"

/* Lowercase extension of `path` without the dot ("txt"), or ""
 * when the file has no extension. Writes into out. */
void bd_mime_extension(const char *path, char *out, size_t cap);

/* Desktop id of the handler for `path`: user association from the
 * reg store first, then the built-in default table, else NULL.
 * The returned pointer is to static storage or a caller-owned
 * buffer filled from the reg store; see bd_mime_handler_copy(). */
const char *bd_mime_handler(const char *path);

/* Same lookup, copying the id into out. Returns false when there
 * is no handler. */
bool bd_mime_handler_copy(const char *path, char *out, size_t cap);

/* Record a user association (persists via brainreg, atomically). */
int bd_mime_set_handler(const char *extension, const char *desktop_id);

/* Build the argv to open `path` with its associated handler found
 * in catalog `cat`: the handler's Exec expanded by
 * de_exec_argv_files() with `path` as the single file (%f is
 * substituted in place; %F expands to the one file; an Exec with
 * no file code yields an argv without the path - see the header
 * note). Returns a malloc'd NULL-terminated argv (free with
 * bd_mime_argv_free), or NULL when there is no handler or the
 * entry is not launchable. */
char **bd_mime_argv(const de_catalog *cat, const char *path);

/* Multi-file variant: the handler is chosen by the FIRST path's
 * extension and its %F expands to all paths (%f receives only
 * the first). Same return/free contract as bd_mime_argv(). */
char **bd_mime_argv_files(const de_catalog *cat, char *const *paths,
                          size_t n_paths);
void bd_mime_argv_free(char **argv);

/* Human-readable kind string for a path, from its extension
 * ("Text document", "PNG image", "Folder", ...). Static storage. */
const char *bd_mime_kind(const char *path, bool is_dir);

#endif /* BDMIME_H */
