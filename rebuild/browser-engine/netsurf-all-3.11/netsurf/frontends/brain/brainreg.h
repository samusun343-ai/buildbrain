/*
 * Brainux "brain" frontend: brainreg bridge.
 *
 * DESIGN-brain-frontend.md section 5: the browser's persistent
 * settings live in brainreg (the shared settings store) under the
 * "browser" section, not in a private Choices file.  At startup
 * the registry values are pushed into the NetSurf options; at exit
 * the current values are written back to the registry.  The memory
 * budget derivation (llcache / image cache / JS heap from
 * browser.budget_mb) is the M0 model from REPORT-browser-m0.md and
 * is shared with the framebuffer frontend's behaviour.
 */

#ifndef BRAIN_BRAINREG_H
#define BRAIN_BRAINREG_H

/** Read brainreg "browser" section into the NetSurf options.
 * Call after nsoption_init(), before netsurf_init().  Also derives
 * the cache budgets from browser.budget_mb. */
void brain_brainreg_apply(void);

/** Write the current option values back to brainreg.  Call before
 * exit; the write is atomic (brainreg guarantee). */
void brain_brainreg_store(void);

#endif /* BRAIN_BRAINREG_H */
