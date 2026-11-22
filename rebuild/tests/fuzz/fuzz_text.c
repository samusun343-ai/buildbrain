/* Fuzz harness: textcore editing/layout entry points
 * (src/textcore.c) with arbitrary byte strings as text. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "textcore.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *buf = malloc(size + 1);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = 0;
    tc_buffer *b = tc_new();
    if (b) {
        tc_set_text(b, buf);
        tc_move_to(b, size / 2, false);
        tc_move_to(b, size, true);
        char out[256];
        tc_range_text(b, 0, size, out, sizeof(out));
        tc_append(b, "x");
        tc_free(b);
    }
    free(buf);
    return 0;
}
