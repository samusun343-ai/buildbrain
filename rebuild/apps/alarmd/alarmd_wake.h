/* alarmd_wake - RTC wakealarm interface for bd-alarmd.
 *
 * STATUS: INTERFACE ONLY - UNVERIFIED. Programming
 * /sys/class/rtc/rtc0/wakealarm so the device resumes from
 * suspend in time for an alarm requires the i.MX28 RTC to
 * support wakealarm semantics under the Brainux kernel, which
 * can only be established on real hardware (or the QEMU guest
 * with an RTC model that implements it). This development
 * machine has no /sys/class/rtc at all. Nothing in this phase
 * claims suspend-resume alarms work; what exists is the seam:
 *
 *   - bd_alarmd, when started with BD_ALARMD_WAKEALARM=1 in the
 *     environment, programs the next alarm epoch through
 *     bd_wakealarm_program_default() after every evaluation.
 *   - The write sequence follows the kernel's documented
 *     wakealarm protocol: write "0" first (clears any previous
 *     setting; writing a new value without clearing fails with
 *     EBUSY when one is already set), then the epoch seconds.
 *
 * The program/clear logic is written against an injectable
 * writer so the sequence itself is unit-tested; only the final
 * sysfs write is unverifiable here.
 */
#ifndef ALARMD_WAKE_H
#define ALARMD_WAKE_H

#include <stdint.h>

#define BD_WAKEALARM_PATH "/sys/class/rtc/rtc0/wakealarm"

/* One sysfs write: 0 on success, -1 on failure. */
typedef int (*bd_wake_write_fn)(void *ud, const char *path,
                                const char *value);

/* Program the wakealarm for `epoch` (wall seconds). An epoch
 * <= 0 just clears. Returns 0 on success, -1 on the first
 * failed write. */
int bd_wakealarm_program(int64_t epoch, bd_wake_write_fn fn,
                         void *ud, const char *path);
int bd_wakealarm_clear(bd_wake_write_fn fn, void *ud,
                       const char *path);

/* Production entry points: write to BD_WAKEALARM_PATH. */
int bd_wakealarm_program_default(int64_t epoch);
int bd_wakealarm_clear_default(void);

#endif /* ALARMD_WAKE_H */
