/* notify.c - notification IPC server + client. See notify.h. */
#include "notify.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

/* Payloads larger than this are a protocol violation (the
 * largest defined payload is bd_nt_post, well under 1 KiB). */
#define NT_MAX_PAYLOAD 4096
#define NT_MAX_CLIENTS 8

const char *bd_nt_default_path(void)
{
    static char buf[108];
    const char *e = getenv("BD_NOTIFY_SOCK");
    if (e && e[0]) return e;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && rt[0]) {
        snprintf(buf, sizeof(buf), "%s/bdnotify.sock", rt);
        return buf;
    }
    snprintf(buf, sizeof(buf), "/tmp/bdnotify-%u.sock",
             (unsigned)getuid());
    return buf;
}

/* ---------------- server ---------------- */

typedef struct {
    int fd;                       /* -1 = free slot */
    uint8_t buf[sizeof(bd_nt_hdr) + NT_MAX_PAYLOAD];
    size_t have;                  /* bytes buffered */
} nt_client;

struct bd_nt_server {
    int listen_fd;
    char path[108];
    nt_client clients[NT_MAX_CLIENTS];
    unsigned errors;
};

bd_nt_server *bd_nt_server_start(const char *sock_path)
{
    bd_nt_server *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    for (int i = 0; i < NT_MAX_CLIENTS; i++) s->clients[i].fd = -1;
    const char *path = sock_path ? sock_path : bd_nt_default_path();
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->listen_fd < 0) { free(s); return NULL; }
    unlink(s->path);
    struct sockaddr_un un = { 0 };
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", s->path);
    if (bind(s->listen_fd, (struct sockaddr *)&un, sizeof(un)) < 0 ||
        listen(s->listen_fd, 8) < 0) {
        close(s->listen_fd);
        free(s);
        return NULL;
    }
    int fl = fcntl(s->listen_fd, F_GETFL, 0);
    fcntl(s->listen_fd, F_SETFL, fl | O_NONBLOCK);
    return s;
}

void bd_nt_server_free(bd_nt_server *s)
{
    if (!s) return;
    for (int i = 0; i < NT_MAX_CLIENTS; i++)
        if (s->clients[i].fd >= 0) close(s->clients[i].fd);
    close(s->listen_fd);
    unlink(s->path);
    free(s);
}

const char *bd_nt_server_path(const bd_nt_server *s) { return s->path; }
unsigned bd_nt_server_errors(const bd_nt_server *s) { return s->errors; }

size_t bd_nt_server_clients(const bd_nt_server *s)
{
    size_t n = 0;
    for (int i = 0; i < NT_MAX_CLIENTS; i++)
        if (s->clients[i].fd >= 0) n++;
    return n;
}

static void nt_close_client(nt_client *c)
{
    close(c->fd);
    c->fd = -1;
    c->have = 0;
}

/* Apply one complete message. Returns false when the message is
 * a protocol violation (caller closes the connection). */
static bool nt_apply(nt_client *c, bd_shell *sh,
                     const bd_nt_hdr *hdr, const uint8_t *payload)
{
    switch (hdr->type) {
    case BD_NT_POST: {
        if (hdr->len != sizeof(bd_nt_post)) return false;
        bd_nt_post p;
        memcpy(&p, payload, sizeof(p));
        /* Never trust the wire to NUL-terminate. */
        p.app[BD_NT_APP_MAX - 1] = 0;
        p.title[BD_NT_TITLE_MAX - 1] = 0;
        p.body[BD_NT_BODY_MAX - 1] = 0;
        int urg = p.urgency;
        if (urg < BD_NT_LOW) urg = BD_NT_LOW;
        if (urg > BD_NT_CRITICAL) urg = BD_NT_CRITICAL;
        uint32_t id = bd_shell_notify_post(sh, p.app, p.title,
                                           p.body, urg);
        /* Acknowledge with the assigned id so the sender can
         * cancel later. A failed ack (client already gone) does
         * not undo the post. */
        bd_nt_hdr ah = { BD_NT_MAGIC, BD_NT_POSTED, sizeof(bd_nt_id) };
        bd_nt_id aid = { id };
        uint8_t out[sizeof(ah) + sizeof(aid)];
        memcpy(out, &ah, sizeof(ah));
        memcpy(out + sizeof(ah), &aid, sizeof(aid));
        (void)send(c->fd, out, sizeof(out), MSG_NOSIGNAL);
        return true;
    }
    case BD_NT_CANCEL: {
        if (hdr->len != sizeof(bd_nt_id)) return false;
        bd_nt_id cid;
        memcpy(&cid, payload, sizeof(cid));
        (void)bd_shell_notify_cancel(sh, cid.id);
        return true;
    }
    default:
        return false;
    }
}

int bd_nt_server_poll(bd_nt_server *srv, bd_shell *sh)
{
    int applied = 0;
    /* Accept everything pending. */
    for (;;) {
        int fd = accept(srv->listen_fd, NULL, NULL);
        if (fd < 0) break;
        int slot = -1;
        for (int i = 0; i < NT_MAX_CLIENTS; i++)
            if (srv->clients[i].fd < 0) { slot = i; break; }
        if (slot < 0) { close(fd); continue; }
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        srv->clients[slot].fd = fd;
        srv->clients[slot].have = 0;
    }
    /* Drain each client and apply complete messages. */
    for (int i = 0; i < NT_MAX_CLIENTS; i++) {
        nt_client *c = &srv->clients[i];
        if (c->fd < 0) continue;
        for (;;) {
            ssize_t n = recv(c->fd, c->buf + c->have,
                             sizeof(c->buf) - c->have, 0);
            if (n > 0) {
                c->have += (size_t)n;
            } else if (n == 0) {
                /* EOF: a partial message at EOF is truncated. */
                if (c->have > 0) srv->errors++;
                nt_close_client(c);
                break;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    nt_close_client(c);
                break;
            }
            /* Apply every complete message now buffered. */
            bool dead = false;
            while (c->have >= sizeof(bd_nt_hdr)) {
                bd_nt_hdr hdr;
                memcpy(&hdr, c->buf, sizeof(hdr));
                if (hdr.magic != BD_NT_MAGIC ||
                    hdr.len > NT_MAX_PAYLOAD) {
                    srv->errors++;
                    dead = true;
                    break;
                }
                if (c->have < sizeof(hdr) + hdr.len) break;
                if (!nt_apply(c, sh, &hdr,
                              c->buf + sizeof(hdr))) {
                    srv->errors++;
                    dead = true;
                    break;
                }
                applied++;
                size_t used = sizeof(hdr) + hdr.len;
                memmove(c->buf, c->buf + used, c->have - used);
                c->have -= used;
            }
            if (dead) { nt_close_client(c); break; }
            if (c->fd < 0) break;
            /* Loop to recv more only when the buffer is empty;
             * a partial message waits for the next poll. */
            if (c->have > 0) break;
        }
    }
    return applied;
}

/* ---------------- client ---------------- */

static int nt_connect(const char *sock_path)
{
    const char *path = sock_path ? sock_path : bd_nt_default_path();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_un un = { 0 };
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&un, sizeof(un)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool nt_write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) return false;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return true;
}

static bool nt_read_all(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0) return false;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return true;
}

static void nt_copy_field(char *dst, size_t cap, const char *src)
{
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

int bd_notify_send(const char *sock_path, const char *app,
                   const char *title, const char *body, int urgency)
{
    if (!title || !*title) return -1;
    int fd = nt_connect(sock_path);
    if (fd < 0) return -1;
    bd_nt_post p;
    memset(&p, 0, sizeof(p));
    p.urgency = urgency;
    nt_copy_field(p.app, sizeof(p.app), app);
    nt_copy_field(p.title, sizeof(p.title), title);
    nt_copy_field(p.body, sizeof(p.body), body);
    bd_nt_hdr hdr = { BD_NT_MAGIC, BD_NT_POST, sizeof(p) };
    int id = -1;
    if (nt_write_all(fd, &hdr, sizeof(hdr)) &&
        nt_write_all(fd, &p, sizeof(p))) {
        bd_nt_hdr ah;
        bd_nt_id aid;
        if (nt_read_all(fd, &ah, sizeof(ah)) &&
            ah.magic == BD_NT_MAGIC && ah.type == BD_NT_POSTED &&
            ah.len == sizeof(aid) &&
            nt_read_all(fd, &aid, sizeof(aid)) && aid.id > 0)
            id = (int)aid.id;
    }
    close(fd);
    return id;
}

int bd_notify_cancel(const char *sock_path, uint32_t id)
{
    if (id == 0) return -1;
    int fd = nt_connect(sock_path);
    if (fd < 0) return -1;
    bd_nt_hdr hdr = { BD_NT_MAGIC, BD_NT_CANCEL, sizeof(bd_nt_id) };
    bd_nt_id cid = { id };
    bool ok = nt_write_all(fd, &hdr, sizeof(hdr)) &&
              nt_write_all(fd, &cid, sizeof(cid));
    close(fd);
    return ok ? 0 : -1;
}
