#include "bui_alsa.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

struct bu_alsa {
	int fd;
	struct snd_ctl_elem_id id;
	long min, max;
	unsigned int count;
};

static int name_score(const char *name)
{
	if (strcmp(name, "Master") == 0) return 100;
	if (strstr(name, "Master")) return 80;
	if (strstr(name, "Speaker")) return 60;
	if (strstr(name, "Headphone")) return 55;
	if (strstr(name, "PCM")) return 40;
	if (strstr(name, "Playback")) return 30;
	return -1;
}

bu_alsa *bu_alsa_open(void)
{
	for (int card = 0; card < 4; card++) {
		char path[64];
		snprintf(path, sizeof(path), "/dev/snd/controlC%d", card);
		int fd = open(path, O_RDWR);
		if (fd < 0)
			continue;
		struct snd_ctl_elem_list list;
		memset(&list, 0, sizeof(list));
		if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0) {
			close(fd);
			continue;
		}
		unsigned int n = list.count;
		struct snd_ctl_elem_id *ids = calloc(n ? n : 1, sizeof(*ids));
		list.space = n;
		list.pids = ids;
		if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0) {
			free(ids);
			close(fd);
			continue;
		}
		int best = -1;
		struct snd_ctl_elem_info best_info;
		memset(&best_info, 0, sizeof(best_info));
		for (unsigned int i = 0; i < list.used; i++) {
			struct snd_ctl_elem_info info;
			memset(&info, 0, sizeof(info));
			info.id = ids[i];
			if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0)
				continue;
			if (info.type != SNDRV_CTL_ELEM_TYPE_INTEGER)
				continue;
			if (!(info.access & SNDRV_CTL_ELEM_ACCESS_WRITE))
				continue;
			if (info.value.integer.min == info.value.integer.max)
				continue;
			int sc = name_score((const char *)info.id.name);
			if (sc > best) {
				best = sc;
				best_info = info;
			}
		}
		free(ids);
		if (best < 0) {
			close(fd);
			continue;
		}
		bu_alsa *a = calloc(1, sizeof(*a));
		a->fd = fd;
		a->id = best_info.id;
		a->min = best_info.value.integer.min;
		a->max = best_info.value.integer.max;
		a->count = best_info.count;
		return a;
	}
	return NULL;
}

void bu_alsa_close(bu_alsa *a)
{
	if (!a) return;
	close(a->fd);
	free(a);
}

float bu_alsa_get_volume(bu_alsa *a)
{
	if (!a) return -1;
	struct snd_ctl_elem_value v;
	memset(&v, 0, sizeof(v));
	v.id = a->id;
	if (ioctl(a->fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) < 0)
		return -1;
	long val = v.value.integer.value[0];
	if (a->max <= a->min) return -1;
	return (float)(val - a->min) / (float)(a->max - a->min);
}

int bu_alsa_set_volume(bu_alsa *a, float frac)
{
	if (!a) return -1;
	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	struct snd_ctl_elem_value v;
	memset(&v, 0, sizeof(v));
	v.id = a->id;
	long val = a->min + (long)((a->max - a->min) * frac + 0.5f);
	for (unsigned int i = 0; i < a->count && i < 128; i++)
		v.value.integer.value[i] = val;
	return ioctl(a->fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v) < 0 ? -1 : 0;
}
