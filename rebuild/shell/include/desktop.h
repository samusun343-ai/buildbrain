/* desktop - freedesktop Desktop Entry parsing + application catalog.
 *
 * Implements the model layer the old braindesk never had (audit
 * section 0-6: the old launcher was a 16-entry hardcoded table, 5 of
 * them decorative, and existing apps could not be launched at all).
 *
 * Scope (Desktop Entry Specification 1.5):
 *   - The first group of a file must be [Desktop Entry]; a file whose
 *     first group is something else is invalid and skipped.
 *   - Keys used: Type, Name, GenericName, Comment, Keywords, Icon,
 *     Exec, TryExec, Terminal, Hidden, NoDisplay, Categories.
 *   - Localized keys: Name[ja] etc. The catalog is created with a
 *     locale ("ja", "ja_JP", ...); an exact locale match wins, then
 *     the bare language, then the unlocalized key.
 *   - String escapes \s \n \t \r \\ are decoded.
 *   - Hidden=true means "deleted": the entry never appears, and it
 *     masks a same-id entry from a lower-priority directory.
 *   - NoDisplay=true entries are parsed and findable by id but are
 *     excluded from visible listings/queries by default.
 *   - Only Type=Application entries enter the catalog. Entries with
 *     no Exec are kept but flagged not-launchable (de_entry.launchable
 *     is false) - a real desktop shows them greyed/with an error;
 *     silently dropping them would hide broken installs.
 *   - Directories are loaded in priority order: the first directory
 *     to provide a desktop-file id (the file basename) wins.
 *   - Categories are mapped onto the freedesktop main categories;
 *     well-known additional categories map to their documented main
 *     category (e.g. WebBrowser -> Network, FileManager -> System).
 *
 * Caching / memory (overhead principle):
 *   - Files are parsed exactly once at load. Queries and launches
 *     work purely from memory (see the no-reparse test).
 *   - de_catalog_refresh() rescans the loaded directories and
 *     re-parses ONLY files whose mtime (sec+nsec) changed; new
 *     files are added, deleted files are dropped, a file that
 *     became Hidden/invalid drops its entry. Unchanged files are
 *     never re-read.
 *   - Resident memory is budgeted: de_catalog_memory() estimates
 *     the catalog's bytes; when the estimate exceeds the budget
 *     (default 1 MiB), non-essential strings are trimmed in a fixed
 *     order (Comment first, then Keywords) - Name/Exec/Icon/
 *     Categories are never trimmed, so every app stays launchable
 *     and searchable by name. de_catalog_trimmed() reports whether
 *     trimming happened.
 *   - String storage is a GENERATION ARENA (M3): all strings and
 *     string lists of the entries parsed in one load/refresh batch
 *     live in ONE malloc'd block (the batch's generation), bump-
 *     allocated with no per-string malloc header or rounding, so
 *     a batch of N entries costs 1 allocation instead of ~14N.
 *     An entry holds a reference to its generation; when the last
 *     entry of a generation is replaced or removed, the whole
 *     block is freed at once (no fragmentation, no per-string
 *     free). entry.id / entry.path are borrowed from the catalog's
 *     seen-record (the same strings, not copies). Trimming under
 *     the budget is logical (pointers are dropped); the physical
 *     bytes return when the generation dies. de_catalog_arena_
 *     bytes() reports the physical bytes held in live generations.
 *
 * Exec handling: de_exec_argv() tokenizes an Exec line following the
 * spec's quoting rules and expands field codes. %i expands to
 * "--icon <Icon>" when an icon is set (removed when there is no
 * Icon), %c to the entry name, %k to the .desktop path, %% to a
 * literal %. The file/URL codes expand in de_exec_argv_files():
 * %f substitutes the first file into the current token, %F expands
 * to one argument per file, %u/%U do the same for URLs; a code
 * whose list is empty is removed (so plain de_exec_argv(), which
 * passes no files/URLs, removes them). Deprecated codes (%d %D %n
 * %N %v %m) and unknown codes are removed. Field codes inside a
 * quoted argument are not expanded.
 */
#ifndef BD_DESKTOP_H
#define BD_DESKTOP_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DE_CAT_ALL = -1,
    DE_CAT_ACCESSORIES = 0, /* Utility */
    DE_CAT_DEVELOPMENT,
    DE_CAT_EDUCATION,
    DE_CAT_GAME,
    DE_CAT_GRAPHICS,
    DE_CAT_NETWORK,
    DE_CAT_OFFICE,
    DE_CAT_AUDIOVIDEO,
    DE_CAT_SETTINGS,
    DE_CAT_SYSTEM,
    DE_CAT_OTHER,
    DE_CAT_COUNT
} de_category;

/* Human-readable name of a main category ("Network", ...). */
const char *de_category_name(de_category cat);

typedef struct {
    char *id;          /* desktop-file id: the file basename */
    char *path;        /* full path of the .desktop file */
    char *name;        /* localized Name */
    char *generic_name;
    char *comment;
    char *icon;        /* may be NULL */
    char *exec_raw;    /* raw Exec string, may be NULL */
    char *try_exec;    /* may be NULL */
    char **categories; /* raw Categories list */
    size_t n_categories;
    char **keywords;
    size_t n_keywords;
    de_category main_category;
    bool no_display;
    bool terminal;
    bool launchable;   /* has a non-empty Exec */
    /* Internal (M3): generation-arena handle owning this entry's
     * strings (opaque; NULL for an entry not yet committed). The
     * id/path pointers above are borrowed from the catalog's seen
     * record, NOT owned by the entry. */
    void *arena;
    /* Source file mtime at parse time (internal: refresh compares
     * against these; not part of the Desktop Entry data). */
    long src_mtime_sec;
    long src_mtime_nsec;
} de_entry;

typedef struct de_catalog de_catalog;

/* locale may be NULL (unlocalized names only). */
de_catalog *de_catalog_new(const char *locale);
void de_catalog_free(de_catalog *c);

/* Load one applications directory (non-recursive, *.desktop only).
 * Returns the number of entries newly added. Repeated calls load in
 * priority order: earlier directories win id conflicts. Loaded
 * directories are remembered for de_catalog_refresh(). */
int de_catalog_load_dir(de_catalog *c, const char *dir);

/* Rescan the loaded directories, applying mtime-based invalidation
 * (see the caching note above). Returns the number of changes
 * (entries added, replaced or removed). */
int de_catalog_refresh(de_catalog *c);

/* Resident-memory estimate of the catalog in bytes (entries, their
 * strings and lists; an estimate, not an allocator measurement). */
size_t de_catalog_memory(const de_catalog *c);
/* Set the memory budget (0 = unlimited). Enforced after load and
 * refresh by trimming non-essential strings (see above). */
void de_catalog_set_budget(de_catalog *c, size_t bytes);
bool de_catalog_trimmed(const de_catalog *c);
/* Physical bytes held by live generation-arena blocks (M3), and
 * how many generations are alive. Diagnostics for the resident-
 * memory accounting in DESIGN-shell-v2.md section 18. */
size_t de_catalog_arena_bytes(const de_catalog *c);
size_t de_catalog_generations(const de_catalog *c);

/* Total entries in the catalog (including NoDisplay ones). */
size_t de_catalog_count(const de_catalog *c);
/* Entries that would be shown in a launcher (excludes NoDisplay). */
size_t de_catalog_visible_count(const de_catalog *c);

const de_entry *de_catalog_at(const de_catalog *c, size_t i);
const de_entry *de_catalog_find_id(const de_catalog *c, const char *id);

/* Query visible entries: category DE_CAT_ALL for all, query NULL or
 * "" for no text filter. Text search is a case-insensitive substring
 * match over Name, GenericName, Comment and Keywords. Results are
 * sorted by name (case-insensitive). Fills out[] with up to max
 * pointers (owned by the catalog) and returns the total match count
 * (which may exceed max). */
size_t de_catalog_query(const de_catalog *c, de_category cat,
                        const char *query,
                        const de_entry **out, size_t max);

/* Build an argv from an entry's Exec line. Returns a NULL-terminated
 * malloc'd array of malloc'd strings, or NULL when the entry is not
 * launchable. Free with de_argv_free(). */
char **de_exec_argv(const de_entry *e);
/* As de_exec_argv(), expanding %f/%F from files[0..n_files) and
 * %u/%U from urls[0..n_urls). Either list may be NULL/empty, in
 * which case its codes are removed. This is the regular way to
 * open files/URLs with an application (e.g. from the file
 * manager); ln_launch_files() is the launcher-level wrapper. */
char **de_exec_argv_files(const de_entry *e,
                          char *const *files, size_t n_files,
                          char *const *urls, size_t n_urls);
void de_argv_free(char **argv);

#endif /* BD_DESKTOP_H */
