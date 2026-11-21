/* osk_brainime - libbrainime adapter for OSK v2.
 *
 * Exposes the libbrainime session API (include/brainime.h) as an
 * osk_ime_backend vtable (include/osk.h).  Usage:
 *
 *   bi_engine *e = bi_engine_open("brainux2.bidf");
 *   tk_osk_set_backend(osk, osk_brainime_backend(), e);
 *
 * The engine handle stays owned by the caller; sessions created
 * through the vtable are bi_session objects freed by the vtable.
 */
#ifndef OSK_BRAINIME_H
#define OSK_BRAINIME_H

#include "osk.h"

const osk_ime_backend *osk_brainime_backend(void);

#endif /* OSK_BRAINIME_H */
