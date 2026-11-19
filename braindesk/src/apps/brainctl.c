/* brainctl - command-line front end for the brainreg settings
 * store. See docs/brainreg.md in buildbrain.
 *   brainctl get <section.key>
 *   brainctl set <section.key> <value>
 *   brainctl unset <section.key>
 *   brainctl list [section]
 *   brainctl export */
#include "../brainreg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int split_key(const char *arg, char *sec, size_t sn, char *key,
		     size_t kn)
{
	const char *dot = strchr(arg, '.');
	if (!dot || dot == arg || !dot[1])
		return -1;
	snprintf(sec, sn, "%.*s", (int)(dot - arg), arg);
	snprintf(key, kn, "%s", dot + 1);
	return 0;
}

static int usage(void)
{
	fprintf(stderr,
		"usage: brainctl get|set|unset <section.key> [value]\n"
		"       brainctl list [section]\n"
		"       brainctl export\n");
	return 2;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return usage();
	char sec[64], key[64];

	if (!strcmp(argv[1], "get") && argc == 3) {
		if (split_key(argv[2], sec, sizeof(sec), key, sizeof(key)))
			return usage();
		char *v = brainreg_get(sec, key);
		if (!v)
			return 1;
		printf("%s\n", v);
		free(v);
		return 0;
	}
	if (!strcmp(argv[1], "set") && argc == 4) {
		if (split_key(argv[2], sec, sizeof(sec), key, sizeof(key)))
			return usage();
		return brainreg_set(sec, key, argv[3]) == 0 ? 0 : 1;
	}
	if (!strcmp(argv[1], "unset") && argc == 3) {
		if (split_key(argv[2], sec, sizeof(sec), key, sizeof(key)))
			return usage();
		return brainreg_unset(sec, key) == 0 ? 0 : 1;
	}
	if (!strcmp(argv[1], "list")) {
		brainreg_entry entries[256];
		int n = brainreg_list(entries, 256);
		for (int i = 0; i < n; i++) {
			if (argc == 3 && strcmp(argv[2], entries[i].section))
				continue;
			printf("%s.%s = %s\n", entries[i].section,
			       entries[i].key, entries[i].value);
		}
		return 0;
	}
	if (!strcmp(argv[1], "export") && argc == 2) {
		brainreg_export(stdout);
		return 0;
	}
	return usage();
}
