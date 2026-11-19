/* bd_js: minimal QuickJS embedding for bd-web (M3).
 * One runtime + one context per page load; the context is thrown away
 * with the page. Exposes:
 *   - console.log(...)  (accumulated, shown in the status line)
 *   - document.title    (live getter, reads the page title)
 * No DOM yet, no fetch/XHR, no timers, no external scripts: a script
 * that touches those throws, and the error is reported in the status
 * line. The engine is capped (16 MiB heap, 256 KiB stack) so a runaway
 * page cannot blow the browser's <=65 MiB RSS budget.
 */
#ifndef BD_JS_H
#define BD_JS_H

struct bd_js;
struct bd_js *bd_js_new(void);
void bd_js_free(struct bd_js *js);
/* Page title for the document.title getter (copied). */
void bd_js_set_title(struct bd_js *js, const char *title);
/* Evaluate src as a global script. Returns 0 on success; on success
 * *result_out is malloc'd (caller frees) with the string form of the
 * completion value ("" for undefined). On failure returns -1 and
 * *result_out holds the exception text. */
int bd_js_eval(struct bd_js *js, const char *src, char **result_out);
/* console.log output since the context was created ("" if none). */
const char *bd_js_console(struct bd_js *js);

#endif
