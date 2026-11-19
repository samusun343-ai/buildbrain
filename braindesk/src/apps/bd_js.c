/* bd_js: minimal QuickJS embedding for bd-web (M3). See bd_js.h. */
#include "bd_js.h"
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"

#define BD_JS_HEAP_MAX (16u * 1024 * 1024)
#define BD_JS_STACK_MAX (256 * 1024)
#define BD_JS_CONSOLE_CAP 4096

struct bd_js {
	JSRuntime *rt;
	JSContext *ctx;
	char *title;
	char *console;
	size_t console_len;
	bool console_trunc;
};

static void js_console_append(struct bd_js *js, const char *s)
{
	size_t n = strlen(s);
	if (js->console_len >= BD_JS_CONSOLE_CAP) {
		if (!js->console_trunc) {
			js->console_trunc = true;
			const char *t = "…";
			size_t tn = strlen(t);
			if (js->console_len + tn < BD_JS_CONSOLE_CAP) {
				memcpy(js->console + js->console_len, t, tn);
				js->console_len += tn;
			}
			js->console[js->console_len] = 0;
		}
		return;
	}
	if (js->console_len + n >= BD_JS_CONSOLE_CAP)
		n = BD_JS_CONSOLE_CAP - 1 - js->console_len;
	memcpy(js->console + js->console_len, s, n);
	js->console_len += n;
	js->console[js->console_len] = 0;
}

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val,
			      int argc, JSValueConst *argv)
{
	struct bd_js *js = JS_GetContextOpaque(ctx);
	for (int i = 0; i < argc; i++) {
		if (i > 0)
			js_console_append(js, " ");
		if (JS_IsUndefined(argv[i])) {
			js_console_append(js, "undefined");
		} else if (JS_IsNull(argv[i])) {
			js_console_append(js, "null");
		} else {
			const char *s = JS_ToCString(ctx, argv[i]);
			if (s) {
				js_console_append(js, s);
				JS_FreeCString(ctx, s);
			}
		}
	}
	js_console_append(js, "\n");
	return JS_UNDEFINED;
}

static JSValue js_document_title_get(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	struct bd_js *js = JS_GetContextOpaque(ctx);
	return JS_NewString(ctx, js->title ? js->title : "");
}

struct bd_js *bd_js_new(void)
{
	struct bd_js *js = calloc(1, sizeof(*js));
	if (!js)
		return NULL;
	js->console = malloc(BD_JS_CONSOLE_CAP);
	if (!js->console) {
		free(js);
		return NULL;
	}
	js->console[0] = 0;
	js->title = strdup("");
	if (!js->title) {
		free(js->console);
		free(js);
		return NULL;
	}
	js->rt = JS_NewRuntime();
	if (!js->rt) {
		free(js->title);
		free(js->console);
		free(js);
		return NULL;
	}
	JS_SetMemoryLimit(js->rt, BD_JS_HEAP_MAX);
	js->ctx = JS_NewContext(js->rt);
	if (!js->ctx) {
		JS_FreeRuntime(js->rt);
		free(js->title);
		free(js->console);
		free(js);
		return NULL;
	}
	JS_SetMaxStackSize(js->rt, BD_JS_STACK_MAX);
	JS_SetContextOpaque(js->ctx, js);

	JSValue global = JS_GetGlobalObject(js->ctx);
	JSValue console = JS_NewObject(js->ctx);
	JS_SetPropertyStr(js->ctx, console, "log",
			  JS_NewCFunction(js->ctx, js_console_log, "log", 1));
	JS_SetPropertyStr(js->ctx, global, "console", console);

	JSValue document = JS_NewObject(js->ctx);
	JSValue getter = JS_NewCFunction(js->ctx, js_document_title_get,
					 "get title", 0);
	JSAtom atom = JS_NewAtom(js->ctx, "title");
	JS_DefinePropertyGetSet(js->ctx, document, atom, getter,
				JS_UNDEFINED, JS_PROP_CONFIGURABLE);
	JS_FreeAtom(js->ctx, atom);
	JS_SetPropertyStr(js->ctx, global, "document", document);
	JS_FreeValue(js->ctx, global);
	return js;
}

void bd_js_free(struct bd_js *js)
{
	if (!js)
		return;
	JS_FreeContext(js->ctx);
	JS_FreeRuntime(js->rt);
	free(js->title);
	free(js->console);
	free(js);
}

void bd_js_set_title(struct bd_js *js, const char *title)
{
	if (!js)
		return;
	free(js->title);
	js->title = strdup(title ? title : "");
	if (!js->title)
		js->title = strdup("");
}

int bd_js_eval(struct bd_js *js, const char *src, char **result_out)
{
	*result_out = NULL;
	JSValue v = JS_Eval(js->ctx, src, strlen(src), "<script>",
			    JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(v)) {
		JSValue ex = JS_GetException(js->ctx);
		const char *s = JS_ToCString(js->ctx, ex);
		*result_out = strdup(s ? s : "JS error");
		if (s)
			JS_FreeCString(js->ctx, s);
		JS_FreeValue(js->ctx, ex);
		return -1;
	}
	if (JS_IsUndefined(v)) {
		*result_out = strdup("");
	} else {
		const char *s = JS_ToCString(js->ctx, v);
		*result_out = strdup(s ? s : "");
		if (s)
			JS_FreeCString(js->ctx, s);
	}
	JS_FreeValue(js->ctx, v);
	return 0;
}

const char *bd_js_console(struct bd_js *js)
{
	return js ? js->console : "";
}
