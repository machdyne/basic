/* Werkzeug's file area for every flash size (layout.h). */

#include <stdio.h>
#include "../layout.h"

int main(void) {
    static const struct { uint8_t code; uint32_t fw, ok, off, size; } t[] = {
        { 0x14, 70000, 1, 512u << 10, 512u << 10 },     /* 1MB */
        { 0x15, 70000, 1, 1u << 20, 1u << 20 },         /* 2MB */
        { 0x16, 70000, 1, 2u << 20, 2u << 20 },         /* 4MB: V3C */
        { 0x17, 70000, 1, 6u << 20, 2u << 20 },         /* 8MB */
        { 0x18, 70000, 1, 14u << 20, 2u << 20 },        /* 16MB */
        { 0x00, 70000, 1, 512u << 10, 512u << 10 },     /* unknown: as 1MB */
        { 0xFF, 70000, 1, 512u << 10, 512u << 10 },
        { 0x14, 600000, 0, 0, 0 },      /* firmware into the area: no files */
        { 0x16, 600000, 1, 2u << 20, 2u << 20 },
    };
    int bad = 0;
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        uint32_t off = 0, size = 0;
        int ok = wz_layout(t[i].code, t[i].fw, &off, &size);
        if (ok != (int)t[i].ok || (ok && (off != t[i].off || size != t[i].size))) {
            printf("FAIL code %02x firmware %u: %d %u %u\n", t[i].code, t[i].fw, ok, off, size);
            bad++;
        }
    }
    puts(bad ? "FAILED" : "layout ok");
    return bad != 0;
}
