/*
 * Where the picture goes on the screen (no SDL, so the tests can check it):
 * the scale mode and the rotation. The picture is portrait (224x256, the
 * monitor turned 90 degrees); turned a quarter (rotate 90 or 270) it is
 * landscape, for a landscape device held on its side, the joysticks turned
 * with it (input.c).
 *
 *   auto     an integer scale where it fills at least 90% of the fit, else
 *            fitted (640x480: 1.875x, sharp bilinear)
 *   integer  the largest integer scale that fits (at least 1)
 *   fit      as large as fits, the arcade's shape kept
 *   stretch  the whole screen, the shape not kept
 */
#ifndef STARFORCE_SCREEN_H
#define STARFORCE_SCREEN_H

typedef enum { SCREEN_AUTO, SCREEN_INTEGER, SCREEN_FIT, SCREEN_STRETCH, SCREEN_MODES } ScreenMode;

typedef struct {
    int mode;   /* ScreenMode */
    int rotate; /* degrees clockwise: 0, 90 or 270 */
} ScreenSettings;

typedef struct {
    double sx, sy; /* device pixels per arcade pixel, along the arcade's x and y */
    int exact;     /* sx == sy, an integer: drawn as is */
    int crisp;     /* the integer scale drawn at before smoothing down (exact: sx) */
    int x, y, w, h; /* the picture before turning (w along the arcade's x), centred on the screen */
} ScreenLayout;

void screen_layout(const ScreenSettings *s, int out_w, int out_h, ScreenLayout *l);

/* The arcade's directions from the device's (-1, 0, 1 each), as turned. */
void screen_turn_input(int rotate, int dev_x, int dev_y, int *x, int *y);

/* "auto", "integer", ... (starforce.cfg, --scale); returns the mode, or -1. */
int screen_mode_parse(const char *name);
const char *screen_mode_name(int mode);
/* 0, 90, 270 from "0", "90", "270"; -1 otherwise. */
int screen_rotate_parse(const char *name);
/* The next rotation: 0, 90, 270, 0, ... */
int screen_rotate_next(int rotate);
/* "FIT, TURNED 90" for the notice */
const char *screen_describe(const ScreenSettings *s);

#endif
