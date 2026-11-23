/* BrainCE Layer -- loader module system (M3).
 *
 * The loader owns a registry of mapped CE modules: the main EXE,
 * file DLLs, and ROM (XIP) modules hosted at their native CE bases
 * (DESIGN-wince-layer.md section 5.2). coredll imports bind to the
 * shim; imports of any other DLL bind to that module's exports,
 * loading the dependency on demand (ROM first, then files next to
 * the importer / the main module). The shim reaches this API for
 * LoadLibraryW / GetProcAddress / GetModuleHandleW /
 * GetModuleFileNameW / FindResourceW.
 */
#ifndef BRAINCE_MODULE_H
#define BRAINCE_MODULE_H

#include "../shim/ce_types.h"
#include <stdint.h>
#include <stddef.h>

typedef struct ce_module ce_module;

/* One-time setup. self_path: this loader's own executable path
 * (for CreateProcessW re-exec). rom_path: ROM image source (block
 * device or file) for slot A, or NULL. fs_root: the --fs-root in
 * effect. Returns 0 on success. */
int ce_mod_init(const char *self_path, const char *fs_root,
                const char *rom_path);

/* Load the main module from a host file. On error returns NULL and
 * writes a message into errbuf. The module and its whole dependency
 * graph are mapped and bound, but DllMain / entry are NOT called
 * yet (see ce_mod_finish_new). */
ce_module *ce_mod_load_main_file(const char *host_path,
                                 const CE_WCHAR *ce_path,
                                 char *errbuf, size_t errlen);

/* Load the main module from the ROM by module name (e.g.
 * "control.exe"). Same contract as ce_mod_load_main_file. */
ce_module *ce_mod_load_main_rom(const char *modname,
                                char *errbuf, size_t errlen);

/* Run TLS callbacks + DllMain(DLL_PROCESS_ATTACH) for every module
 * whose binding completed since the last call, in dependency order.
 * Returns 0, or -1 when a DllMain reported failure. */
int ce_mod_finish_new(void);

ce_module *ce_mod_main(void);
void *ce_mod_base(ce_module *m);
void *ce_mod_entry(ce_module *m);
int   ce_mod_is_dll(ce_module *m);

/* Registry lookup by base address (HMODULE). */
ce_module *ce_mod_by_base(void *base);

/* Export resolution inside a loaded module. ordinal >= 0 selects
 * by ordinal; name != NULL selects by name. Forwarded exports are
 * followed (depth-limited). Returns NULL when not found. */
void *ce_mod_export(ce_module *m, int ordinal, const char *name);

/* ---- shim-facing API ---- */
/* LoadLibraryW backend: name is a CE path or a bare module name.
 * Returns the module base (HMODULE) or NULL. Newly loaded modules
 * are finished (DllMain run) before returning. */
void *ce_loader_load_library(const CE_WCHAR *name);
/* GetProcAddress backend: ordinal >= 0 when by ordinal. */
void *ce_loader_get_proc(void *hmod, const char *name, long ordinal);
/* GetModuleHandleW backend: NULL name -> main module base. */
void *ce_loader_module_handle(const CE_WCHAR *name);
/* GetModuleFileNameW backend: copies the module's CE path.
 * Returns the number of units copied (excluding terminator). */
int ce_loader_module_filename(void *hmod, CE_WCHAR *buf, int max);
/* Resource directory of a module. 0 = ok. */
int ce_loader_module_resdir(void *hmod, uint32_t *rva, uint32_t *size);
void *ce_loader_main_base(void);
const char *ce_loader_self_path(void);
const char *ce_loader_fs_root_arg(void);
const char *ce_loader_rom_arg(void);

#endif
