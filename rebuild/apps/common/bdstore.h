/* bdstore - shared durability + path helpers for the v2 apps.
 *
 * Every persistent write in the wave-1 apps goes through here so
 * the crash-safety contract lives in exactly one place:
 *
 *   - Atomic replace: data is written to "<path>.tmp.<pid>" in the
 *     SAME directory, fsynced, renamed over <path>, and the
 *     directory is fsynced. A power cut at any point leaves the old
 *     file or the new file, never a torn one. A leftover ".tmp.*"
 *     file is garbage from an interrupted write; readers ignore it
 *     and bd_sweep_tmp() removes it at store open.
 *   - No time-based expiry anywhere: eviction policies in the apps
 *     are capacity (bytes) / count / insertion-sequence based. The
 *     device clock is untrusted and the device is offline by
 *     default, so "N days old" is meaningless on this hardware.
 *   - Writes stay inside the Brainux area on the microSD. Nothing
 *     here (or in the apps) ever touches the WinCE eMMC.
 */
#ifndef BDSTORE_H
#define BDSTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Write `len` bytes to `path` atomically (see header). Parent
 * directories are NOT created implicitly; see bd_mkdir_p(). */
int bd_write_atomic(const char *path, const void *data, size_t len);
int bd_write_atomic_str(const char *path, const char *text);

/* Read a whole file. Returns a malloc'd NUL-terminated buffer (the
 * file may itself contain NULs; *len_out reports the true length)
 * or NULL when the file cannot be opened. Caller frees. */
char *bd_read_all(const char *path, size_t *len_out);

/* mkdir -p for a directory path. 0 on success (or already exists). */
int bd_mkdir_p(const char *path);

/* fsync a directory (durability of renames inside it). */
int bd_fsync_dir_of(const char *file_path);

/* Copy a regular file to `dst` atomically: bytes land in a temp
 * file in dst's directory, are fsynced, then renamed over dst.
 * A crash mid-copy leaves dst untouched (old content or absent). */
int bd_copy_atomic(const char *src, const char *dst);

/* Move a file or directory. rename() first; for regular files a
 * cross-device move falls back to bd_copy_atomic() + unlink.
 * Directories across devices return -1 with errno EXDEV (callers
 * that need it implement recursive copy themselves). */
int bd_move_path(const char *src, const char *dst);

/* Recursively remove a file or directory tree. Returns the number
 * of entries removed, or -1 on the first hard error. */
int bd_remove_recursive(const char *path);

/* Remove leftover "<name>.tmp.*" files inside `dir` (interrupted
 * atomic writes). Returns how many were removed. */
int bd_sweep_tmp(const char *dir);

/* If `path` exists, derive "name 2.ext", "name 3.ext", ... (the
 * suffix goes before the last extension for files). Writes the
 * first free candidate into out. Returns 0, or -1 if none fit. */
int bd_unique_path(const char *path, char *out, size_t cap);

/* Join two path components with exactly one '/'. */
int bd_path_join(char *out, size_t cap, const char *a, const char *b);

/* Basename pointer into `path` (no allocation). */
const char *bd_basename(const char *path);

/* Parent directory of `path` into out ("." when there is none). */
int bd_dirname(const char *path, char *out, size_t cap);

/* Human-readable size: "812 B", "12.3 KiB", "4.0 MiB", ... */
void bd_format_size(uint64_t bytes, char *out, size_t cap);

/* Case-insensitive substring test. Folding is ASCII-only plus a
 * byte-wise compare for the rest, which is exact for UTF-8 text
 * whose case pairs are ASCII (the search fields in these apps are
 * name/title searches; full Unicode case folding is out of scope
 * and documented as such). Empty needle matches everything. */
bool bd_contains_ci(const char *haystack, const char *needle);

/* Escape a field for the TSV stores: '\\' -> "\\\\", TAB -> "\\t",
 * LF -> "\\n", CR -> "\\r". Writes into out (always NUL-terminated
 * when cap > 0). Returns bytes needed excluding NUL. */
size_t bd_escape(const char *in, char *out, size_t cap);
/* Inverse of bd_escape. Returns bytes written excluding NUL. */
size_t bd_unescape(const char *in, char *out, size_t cap);

#endif /* BDSTORE_H */
