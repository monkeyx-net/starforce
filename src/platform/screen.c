#include "platform/screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx/fb.h"

/* Auto uses an integer scale when it fills at least this much of the fit. */
#define INTEGER_FILL 0.9

static const char *const MODE_NAMES[SCREEN_MODES] = { "auto", "integer", "fit", "stretch" };

void screen_layout(const ScreenSettings *s, int out_w, int out_h, ScreenLayout *l)
{
    int turned = s->rotate == 90 || s->rotate == 270;
    double ax = (double)(turned ? out_h : out_w) / FB_WIDTH; /* room along the arcade's x */
    double ay = (double)(turned ? out_w : out_h) / FB_HEIGHT;
    double fit = ax < ay ? ax : ay;
    int whole = fit < 1 ? 1 : (int)fit;
    switch (s->mode) {
    case SCREEN_STRETCH: l->sx = ax; l->sy = ay; break;
    case SCREEN_FIT: l->sx = l->sy = fit; break;
    case SCREEN_INTEGER: l->sx = l->sy = whole; break;
    default: l->sx = l->sy = fit < 1 || whole >= fit * INTEGER_FILL ? whole : fit; break;
    }
    l->exact = l->sx == l->sy && l->sx == (int)l->sx;
    double big = l->sx > l->sy ? l->sx : l->sy;
    l->crisp = l->exact ? (int)l->sx : (int)ceil(big);
    if (l->crisp < 1) l->crisp = 1;

    l->w = (int)(FB_WIDTH * l->sx + 0.5);
    l->h = (int)(FB_HEIGHT * l->sy + 0.5);
    int sw = turned ? l->h : l->w, sh = turned ? l->w : l->h; /* as it lands on the screen */
    /* turned about its centre, which is the screen's */
    l->x = (out_w - sw) / 2 + (sw - l->w) / 2;
    l->y = (out_h - sh) / 2 + (sh - l->h) / 2;
}

void screen_turn_input(int rotate, int dev_x, int dev_y, int *x, int *y)
{
    switch (rotate) {
    case 90: /* the arcade's top is at the screen's right */
        *x = dev_y;
        *y = -dev_x;
        break;
    case 270: /* at its left */
        *x = -dev_y;
        *y = dev_x;
        break;
    default:
        *x = dev_x;
        *y = dev_y;
        break;
    }
}

int screen_mode_parse(const char *name)
{
    for (int m = 0; m < SCREEN_MODES; m++)
        if (!strcmp(name, MODE_NAMES[m])) return m;
    return -1;
}

const char *screen_mode_name(int mode)
{
    return mode >= 0 && mode < SCREEN_MODES ? MODE_NAMES[mode] : MODE_NAMES[0];
}

int screen_rotate_parse(const char *name)
{
    if (!strcmp(name, "0")) return 0;
    if (!strcmp(name, "90")) return 90;
    if (!strcmp(name, "270")) return 270;
    return -1;
}

int screen_rotate_next(int rotate)
{
    return rotate == 0 ? 90 : rotate == 90 ? 270 : 0;
}

const char *screen_describe(const ScreenSettings *s)
{
    static char text[40];
    static const char *const UPPER[SCREEN_MODES] = { "AUTO", "INTEGER", "FIT", "STRETCH" };
    const char *mode = UPPER[s->mode >= 0 && s->mode < SCREEN_MODES ? s->mode : 0];
    if (s->rotate) snprintf(text, sizeof text, "%s, TURNED %d", mode, s->rotate);
    else snprintf(text, sizeof text, "%s", mode);
    return text;
}
