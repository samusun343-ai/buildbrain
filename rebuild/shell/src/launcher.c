/* launcher.c - pins/recents store + launch. See launcher.h. */
#include "launcher.h"
#include "reg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

struct ln_store {
    char **pins;
    size_t n_pins, cap_pins;
    char **recents;
    size_t n_recents, cap_recents;
};

static void list_push(char ***arr, size_t *n, size_t *cap, const char *id)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *arr = realloc(*arr, *cap * sizeof(char *));
    }
    (*arr)[(*n)++] = strdup(id);
}

static bool list_contains(char **arr, size_t n, const char *id)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(arr[i], id) == 0) return true;
    return false;
}

static bool list_remove(char **arr, size_t *n, const char *id)
{
    for (size_t i = 0; i < *n; i++) {
        if (strcmp(arr[i], id) == 0) {
            free(arr[i]);
            memmove(&arr[i], &arr[i + 1], (*n - i - 1) * sizeof(char *));
            (*n)--;
            return true;
        }
    }
    return false;
}

/* A persisted id is only trusted if it looks like a desktop-file
 * basename: non-empty, no '/', not "." or "..", sane length.
 * Anything else is treated as corruption (torn write, hand-edited
 * or foreign content) and dropped at load - the store degrades to
 * fewer pins/recents instead of acting on garbage. Duplicates are
 * also dropped (first occurrence wins). */
static bool id_sane(const char *id)
{
    if (!id || !*id) return false;
    if (strchr(id, '/')) return false;
    if (strcmp(id, ".") == 0 || strcmp(id, "..") == 0) return false;
    if (strlen(id) > 128) return false;
    return true;
}

static void load_list(char ***arr, size_t *n, size_t *cap, const char *key)
{
    char *v = reg_get("shell", key);
    if (!v) return;
    char *p = v;
    while (p && *p) {
        char *semi = strchr(p, ';');
        if (semi) *semi = 0;
        if (id_sane(p) && !list_contains(*arr, *n, p))
            list_push(arr, n, cap, p);
        p = semi ? semi + 1 : NULL;
    }
    free(v);
}

static void save_list(char **arr, size_t n, const char *key)
{
    size_t total = 1;
    for (size_t i = 0; i < n; i++) total += strlen(arr[i]) + 1;
    char *buf = malloc(total);
    buf[0] = 0;
    for (size_t i = 0; i < n; i++) {
        if (i) strcat(buf, ";");
        strcat(buf, arr[i]);
    }
    reg_set("shell", key, buf);
    free(buf);
}

ln_store *ln_store_new(void)
{
    ln_store *s = calloc(1, sizeof(*s));
    load_list(&s->pins, &s->n_pins, &s->cap_pins, "pinned");
    load_list(&s->recents, &s->n_recents, &s->cap_recents, "recent");
    if (s->n_recents > LN_RECENT_MAX) {
        for (size_t i = LN_RECENT_MAX; i < s->n_recents; i++) free(s->recents[i]);
        s->n_recents = LN_RECENT_MAX;
    }
    return s;
}

void ln_store_free(ln_store *s)
{
    if (!s) return;
    for (size_t i = 0; i < s->n_pins; i++) free(s->pins[i]);
    for (size_t i = 0; i < s->n_recents; i++) free(s->recents[i]);
    free(s->pins); free(s->recents); free(s);
}

size_t ln_pin_count(const ln_store *s) { return s->n_pins; }
const char *ln_pin_at(const ln_store *s, size_t i) { return i < s->n_pins ? s->pins[i] : NULL; }
bool ln_is_pinned(const ln_store *s, const char *id) { return list_contains(s->pins, s->n_pins, id); }

bool ln_pin(ln_store *s, const char *id)
{
    if (ln_is_pinned(s, id)) return false;
    list_push(&s->pins, &s->n_pins, &s->cap_pins, id);
    save_list(s->pins, s->n_pins, "pinned");
    return true;
}

bool ln_unpin(ln_store *s, const char *id)
{
    if (!list_remove(s->pins, &s->n_pins, id)) return false;
    save_list(s->pins, s->n_pins, "pinned");
    return true;
}

size_t ln_recent_count(const ln_store *s) { return s->n_recents; }
const char *ln_recent_at(const ln_store *s, size_t i) { return i < s->n_recents ? s->recents[i] : NULL; }

void ln_record_launch(ln_store *s, const char *id)
{
    list_remove(s->recents, &s->n_recents, id);
    /* insert at front */
    if (s->n_recents == s->cap_recents) {
        s->cap_recents = s->cap_recents ? s->cap_recents * 2 : 8;
        s->recents = realloc(s->recents, s->cap_recents * sizeof(char *));
    }
    memmove(&s->recents[1], &s->recents[0], s->n_recents * sizeof(char *));
    s->recents[0] = strdup(id);
    s->n_recents++;
    if (s->n_recents > LN_RECENT_MAX) {
        s->n_recents = LN_RECENT_MAX;
        free(s->recents[s->n_recents]);
    }
    save_list(s->recents, s->n_recents, "recent");
}

bool ln_launch_files(ln_store *s, const de_entry *entry,
                     char *const *files, size_t n_files,
                     char *const *urls, size_t n_urls,
                     ln_exec_fn fn, void *userdata)
{
    if (!entry || !entry->launchable || !fn) return false;
    char **argv = de_exec_argv_files(entry, files, n_files, urls, n_urls);
    if (!argv) return false;
    bool ok = fn(userdata, entry, argv, entry->terminal);
    de_argv_free(argv);
    if (ok && s) ln_record_launch(s, entry->id);
    return ok;
}

bool ln_launch(ln_store *s, const de_entry *entry, ln_exec_fn fn, void *userdata)
{
    return ln_launch_files(s, entry, NULL, 0, NULL, 0, fn, userdata);
}

bool ln_exec_real(void *userdata, const de_entry *entry,
                  char *const argv[], bool terminal)
{
    (void)userdata; (void)entry;
    /* Exec-failure reporting: the child writes errno to a
     * CLOEXEC pipe if execvp fails; a successful exec closes the
     * pipe silently. Without this, fork() success would be
     * reported as launch success even when the binary is gone -
     * a real case when the SD card holding the app was removed
     * or used on the WinCE side (DESIGN section 12). */
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;
    /* set close-on-exec on the write end via fcntl */
    int flags = fcntl(pipefd[1], F_GETFD);
    if (flags >= 0) fcntl(pipefd[1], F_SETFD, flags | FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        close(pipefd[0]);
        setsid();
        if (terminal) {
            char **targv = malloc(sizeof(char *) * 64);
            int i = 0;
            targv[i++] = (char *)"xterm";
            targv[i++] = (char *)"-e";
            for (int j = 0; argv[j] && i < 62; j++) targv[i++] = argv[j];
            targv[i] = NULL;
            execvp(targv[0], targv);
        } else {
            execvp(argv[0], argv);
        }
        int e = errno;
        ssize_t unused = write(pipefd[1], &e, sizeof(e));
        (void)unused;
        _exit(127);
    }
    close(pipefd[1]);
    int child_errno = 0;
    ssize_t got = read(pipefd[0], &child_errno, sizeof(child_errno));
    close(pipefd[0]);
    /* got > 0: the child reported an exec failure.
     * got == 0 (EOF): exec succeeded, the write end closed. */
    return got <= 0;
}
