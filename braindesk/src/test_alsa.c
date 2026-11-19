/* Runtime check for bui_alsa against a real card: open, read,
 * set 0.5, read back, restore. Prints machine-checkable lines. */
#include "bui_alsa.h"
#include <stdio.h>

int main(void)
{
	bu_alsa *a = bu_alsa_open();
	if (!a) {
		printf("ALSA_OPEN_FAIL\n");
		return 1;
	}
	float v0 = bu_alsa_get_volume(a);
	printf("VOL_INITIAL %.3f\n", v0);
	if (bu_alsa_set_volume(a, 0.5f) != 0) {
		printf("ALSA_SET_FAIL\n");
		return 1;
	}
	float v1 = bu_alsa_get_volume(a);
	printf("VOL_AFTER_SET %.3f\n", v1);
	if (v0 >= 0)
		bu_alsa_set_volume(a, v0);
	bu_alsa_close(a);
	printf("ALSA_TEST_DONE\n");
	return (v1 > 0.4f && v1 < 0.6f) ? 0 : 2;
}
