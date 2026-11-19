/* Minimal ALSA mixer access for braindesk, straight on the
 * /dev/snd/controlC* ioctls — no alsa-lib dependency, so the
 * static device build keeps working and the shell's volume path
 * costs a few hundred bytes, not a library. Finds the first
 * writable integer mixer element named like a master/playback
 * volume (the BU26154 card exposes "Master"). */
#ifndef BUI_ALSA_H
#define BUI_ALSA_H

typedef struct bu_alsa bu_alsa;

bu_alsa *bu_alsa_open(void);		/* NULL when no usable card */
void bu_alsa_close(bu_alsa *a);
float bu_alsa_get_volume(bu_alsa *a);	/* 0..1, -1 on error */
int bu_alsa_set_volume(bu_alsa *a, float frac);	/* 0 ok */

#endif
