#include "tests/script.h"

#include <stdlib.h>
#include <string.h>

#include "emu/board.h"

int script_parse(Script *s, const char *text)
{
    static const struct {
        const char *name;
        int port; /* 0 P1, 1 P2, 2 SYSTEM */
        uint8_t bit;
    } names[] = {
        { "coin", 2, SYS_COIN1 },  { "coin2", 2, SYS_COIN2 }, { "start", 2, SYS_START1 }, { "start2", 2, SYS_START2 },
        { "left", 0, IN_LEFT },    { "right", 0, IN_RIGHT },  { "up", 0, IN_UP },         { "down", 0, IN_DOWN },
        { "fire", 0, IN_FIRE },    { "left2", 1, IN_LEFT },   { "right2", 1, IN_RIGHT },  { "up2", 1, IN_UP },
        { "down2", 1, IN_DOWN },   { "fire2", 1, IN_FIRE },
    };
    const char *p = text;
    s->n = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (s->n == (int)(sizeof s->step / sizeof s->step[0])) return -1;
        char *end;
        memset(&s->step[s->n], 0, sizeof s->step[0]);
        s->step[s->n].frame = strtol(p, &end, 10);
        if (end == p || *end != ':') return -1;
        p = end + 1;
        while (*p && *p != ' ') {
            size_t n = strcspn(p, ", ");
            int ok = 0;
            for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
                if (strlen(names[i].name) == n && !strncmp(p, names[i].name, n)) {
                    uint8_t *port = names[i].port == 0 ? &s->step[s->n].p1 : names[i].port == 1 ? &s->step[s->n].p2 : &s->step[s->n].system;
                    *port |= names[i].bit;
                    ok = 1;
                }
            if (!ok) return -1;
            p += n;
            if (*p == ',') p++;
        }
        s->n++;
    }
    return 0;
}

void script_inputs(const Script *s, long frame, uint8_t *p1, uint8_t *p2, uint8_t *system)
{
    *p1 = *p2 = *system = 0;
    for (int i = 0; i < s->n; i++)
        if (s->step[i].frame <= frame) {
            *p1 = s->step[i].p1;
            *p2 = s->step[i].p2;
            *system = s->step[i].system;
            if (frame - s->step[i].frame >= COIN_PULSE_FRAMES) *system &= (uint8_t)~(SYS_COIN1 | SYS_COIN2);
        }
}
