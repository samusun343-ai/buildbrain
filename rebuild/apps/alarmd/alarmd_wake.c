/* alarmd_wake.c - see alarmd_wake.h. Interface only, unverified
 * on hardware in this phase. */
#include "alarmd_wake.h"

#include <stdio.h>

int bd_wakealarm_clear(bd_wake_write_fn fn, void *ud,
                       const char *path)
{
    if (!fn || !path) return -1;
    return fn(ud, path, "0");
}

int bd_wakealarm_program(int64_t epoch, bd_wake_write_fn fn,
                         void *ud, const char *path)
{
    if (!fn || !path) return -1;
    if (fn(ud, path, "0") != 0) return -1;
    if (epoch <= 0) return 0;
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", (long long)epoch);
    return fn(ud, path, buf);
}

static int sysfs_write(void *ud, const char *path, const char *value)
{
    (void)ud;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int rc = fputs(value, f) < 0 ? -1 : 0;
    if (fclose(f) != 0) rc = -1;
    return rc;
}

int bd_wakealarm_program_default(int64_t epoch)
{
    return bd_wakealarm_program(epoch, sysfs_write, NULL,
                                BD_WAKEALARM_PATH);
}

int bd_wakealarm_clear_default(void)
{
    return bd_wakealarm_clear(sysfs_write, NULL, BD_WAKEALARM_PATH);
}
