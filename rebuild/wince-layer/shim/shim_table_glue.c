/* BrainCE Layer -- shim function table definition.
 *
 * The generated table (shim_fns_table.h) takes the addresses of all
 * implemented ce_* functions through unprototyped extern declarations.
 * Those declarations would clash with the prototyped definitions in
 * coredll_shim.c / gwe.c / gdi.c, so the table lives in its own
 * translation unit; coredll_shim.c sees only the declaration header
 * shim_fns.h. Regenerate both headers with tools/gen_shim_table.py.
 */
#include "shim_fns.h"
#include "shim_fns_table.h"
