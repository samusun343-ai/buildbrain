#define _POSIX_C_SOURCE 200809L
#include "bdmime.h"
#include "bdstore.h"
#include "reg.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void bd_mime_extension(const char *path, char *out, size_t cap)
{
    if (cap == 0) return;
    out[0] = '\0';
    const char *base = bd_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base || !dot[1]) return;
    size_t i = 0;
    for (const char *p = dot + 1; *p && i + 1 < cap; p++)
        out[i++] = (char)tolower((unsigned char)*p);
    out[i] = '\0';
}

typedef struct { const char *ext; const char *id; const char *kind; } assoc;

/* Built-in defaults. The desktop ids are the wave-1 apps' own
 * entries plus the browser from the browser workstream; a missing
 * entry in the catalog simply makes the open fail cleanly. */
static const assoc k_defaults[] = {
    { "txt",  "bd-notes.desktop",   "Text document" },
    { "md",   "bd-notes.desktop",   "Text document" },
    { "text", "bd-notes.desktop",   "Text document" },
    { "png",  "bd-image.desktop",   "PNG image" },
    { "jpg",  "bd-image.desktop",   "JPEG image" },
    { "jpeg", "bd-image.desktop",   "JPEG image" },
    { "gif",  "bd-image.desktop",   "GIF image" },
    { "bmp",  "bd-image.desktop",   "Bitmap image" },
    { "html", "bd-web.desktop",     "HTML document" },
    { "htm",  "bd-web.desktop",     "HTML document" },
    { "mp3",  "bd-player.desktop",  "Audio file" },
    { "wav",  "bd-player.desktop",  "Audio file" },
    { "ogg",  "bd-player.desktop",  "Audio file" },
    { "flac", "bd-player.desktop",  "Audio file" },
    { "mp4",  "bd-player.desktop",  "Video file" },
    { "bin",  NULL,                 "Binary data" },
    { "c",    NULL,                 "C source" },
    { "h",    NULL,                 "C header" },
    { "sh",   NULL,                 "Shell script" },
    { "pdf",  NULL,                 "PDF document" },
    { "zip",  NULL,                 "Zip archive" },
    { NULL, NULL, NULL }
};

static const assoc *find_default(const char *ext)
{
    for (const assoc *a = k_defaults; a->ext; a++)
        if (strcmp(a->ext, ext) == 0) return a;
    return NULL;
}

bool bd_mime_handler_copy(const char *path, char *out, size_t cap)
{
    char ext[32];
    bd_mime_extension(path, ext, sizeof(ext));
    if (!ext[0]) return false;
    char *user = reg_get("mime", ext);
    if (user && *user) {
        size_t n = strlen(user);
        if (n + 1 > cap) { free(user); return false; }
        memcpy(out, user, n + 1);
        free(user);
        return true;
    }
    free(user);
    const assoc *a = find_default(ext);
    if (!a || !a->id) return false;
    size_t n = strlen(a->id);
    if (n + 1 > cap) return false;
    memcpy(out, a->id, n + 1);
    return true;
}

const char *bd_mime_handler(const char *path)
{
    static char buf[128];
    if (!bd_mime_handler_copy(path, buf, sizeof(buf))) return NULL;
    return buf;
}

int bd_mime_set_handler(const char *extension, const char *desktop_id)
{
    char ext[32];
    size_t i = 0;
    for (const char *p = extension; *p && i + 1 < sizeof(ext); p++) {
        if (*p == '.') continue;
        ext[i++] = (char)tolower((unsigned char)*p);
    }
    ext[i] = '\0';
    if (!ext[0]) return -1;
    return reg_set("mime", ext, desktop_id);
}

char **bd_mime_argv_files(const de_catalog *cat, char *const *paths,
                          size_t n_paths)
{
    if (!paths || n_paths == 0 || !paths[0]) return NULL;
    char id[128];
    if (!bd_mime_handler_copy(paths[0], id, sizeof(id))) return NULL;
    const de_entry *e = de_catalog_find_id(cat, id);
    if (!e || !e->launchable) return NULL;
    /* The regular field-code path: %f/%F expand from the file
     * list inside the catalog's expansion, exactly as the
     * shell's ln_launch_files() does. No trailing-argument
     * appending (that was the wave-1 workaround). */
    return de_exec_argv_files(e, paths, n_paths, NULL, 0);
}

char **bd_mime_argv(const de_catalog *cat, const char *path)
{
    if (!path) return NULL;
    char *one = (char *)path;
    return bd_mime_argv_files(cat, &one, 1);
}

void bd_mime_argv_free(char **argv)
{
    if (!argv) return;
    for (size_t i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

const char *bd_mime_kind(const char *path, bool is_dir)
{
    if (is_dir) return "Folder";
    char ext[32];
    bd_mime_extension(path, ext, sizeof(ext));
    const assoc *a = ext[0] ? find_default(ext) : NULL;
    if (a) return a->kind;
    return "File";
}
