/* notify - notification IPC between apps and the shell (OS
 * internals phase 2, following the design in
 * REPORT-os-internals-1.md section 3.3, option B).
 *
 * Transport: a Unix stream socket, wire format modelled on the
 * WM protocol (wm.h): every message is a fixed-size header
 * (magic, type, len) followed by `len` payload bytes, native
 * endian (both ends are the same machine by construction).
 * Payloads are fixed-layout structs - no text parsing, no
 * escaping, one bounded copy from the socket buffer into the
 * shell model.
 *
 * The server lives in the shell process (bd_nt_server, polled
 * from the session step like the WM server). The client is
 * connect-per-message: an app opens the socket, sends one POST
 * or CANCEL, reads the POSTED acknowledgement for a POST (it
 * carries the assigned notification id, needed for a later
 * CANCEL), and closes. No persistent connection state exists in
 * apps, so a crashed app can never wedge the server, and a
 * missing server is a plain -1 from the client calls.
 *
 * Robustness contract (server side): a bad magic, an oversized
 * payload length, or a truncated message closes just that
 * connection and bumps bd_nt_server_errors(); the shell keeps
 * running and keeps serving other clients.
 *
 * Socket path: $BD_NOTIFY_SOCK if set, else
 * $XDG_RUNTIME_DIR/bdnotify.sock, else /tmp/bdnotify-<uid>.sock
 * (same scheme as bd_wm_default_path()).
 */
#ifndef BD_NOTIFY_H
#define BD_NOTIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shellui.h"

#define BD_NT_MAGIC 0x42444e54u /* "BDNT" */

enum bd_nt_msg {
    BD_NT_POST   = 1, /* c->s bd_nt_post                       */
    BD_NT_CANCEL = 2, /* c->s bd_nt_id                         */
    BD_NT_POSTED = 3  /* s->c bd_nt_id (ack: assigned id)      */
};

typedef struct { uint32_t magic, type, len; } bd_nt_hdr;

enum bd_nt_urgency { BD_NT_LOW = 0, BD_NT_NORMAL = 1, BD_NT_CRITICAL = 2 };

#define BD_NT_APP_MAX   32
#define BD_NT_TITLE_MAX 128
#define BD_NT_BODY_MAX  512

typedef struct {
    int32_t urgency;              /* enum bd_nt_urgency */
    char app[BD_NT_APP_MAX];      /* sender app name, NUL-terminated */
    char title[BD_NT_TITLE_MAX];
    char body[BD_NT_BODY_MAX];    /* may be empty */
} bd_nt_post;

typedef struct { uint32_t id; } bd_nt_id;

const char *bd_nt_default_path(void);

/* ---------------- server (shell side) ---------------- */

typedef struct bd_nt_server bd_nt_server;

/* Start listening. sock_path NULL = bd_nt_default_path().
 * A stale socket file at the path is unlinked first (same as
 * the WM server). Returns NULL on failure; the session treats
 * that as "notifications unavailable" and runs on. */
bd_nt_server *bd_nt_server_start(const char *sock_path);
void bd_nt_server_free(bd_nt_server *srv);
const char *bd_nt_server_path(const bd_nt_server *srv);

/* Accept connections and apply every complete message already
 * received to the shell's notification model. Never blocks.
 * Returns the number of messages applied. Partial messages stay
 * buffered per connection until the rest arrives. */
int bd_nt_server_poll(bd_nt_server *srv, bd_shell *sh);

/* Malformed messages / protocol violations dropped so far. */
unsigned bd_nt_server_errors(const bd_nt_server *srv);
/* Live client connections (for tests/diagnostics). */
size_t bd_nt_server_clients(const bd_nt_server *srv);

/* ---------------- client (app side) ---------------- */

/* Post a notification. sock_path NULL = default. Returns the
 * assigned notification id (> 0), or -1 when the shell is not
 * reachable or the ack did not arrive. title must be non-empty;
 * body may be NULL/empty; app may be NULL (stored as ""). */
int bd_notify_send(const char *sock_path, const char *app,
                   const char *title, const char *body, int urgency);
/* Cancel a notification previously posted (by any client - the
 * id is the capability). Returns 0 when the message was handed
 * to the server, -1 when the shell is not reachable. */
int bd_notify_cancel(const char *sock_path, uint32_t id);

#endif /* BD_NOTIFY_H */
