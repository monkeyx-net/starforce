/*
 * The screen's layout (no ROM needed): the scale modes and rotations on the
 * devices' screens, and the joystick turned with the picture.
 */
#include <stdio.h>

#include "gfx/fb.h"
#include "platform/screen.h"

static int failures;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
        }                                                                   \
    } while (0)

static ScreenLayout lay(int mode, int rotate, int w, int h)
{
    ScreenSettings s = { mode, rotate };
    ScreenLayout l;
    screen_layout(&s, w, h, &l);
    return l;
}

/* The picture as it lands on the screen: inside it, centred */
static void check_on_screen(int mode, int rotate, int out_w, int out_h)
{
    ScreenLayout l = lay(mode, rotate, out_w, out_h);
    int turned = rotate == 90 || rotate == 270;
    double cx = l.x + l.w / 2.0, cy = l.y + l.h / 2.0;
    double sw = turned ? l.h : l.w, sh = turned ? l.w : l.h;
    int cropped = l.exact && l.sx == 1; /* smaller than the arcade: 1x, cropped */
    CHECK(cropped || (cx - sw / 2 >= -0.5 && cx + sw / 2 <= out_w + 0.5 && cy - sh / 2 >= -0.5 && cy + sh / 2 <= out_h + 0.5),
          "%s %d on %dx%d: %gx%g at the centre %g,%g leaves the screen", screen_mode_name(mode), rotate, out_w, out_h,
          sw, sh, cx, cy);
    CHECK(cx >= out_w / 2.0 - 1 && cx <= out_w / 2.0 + 1 && cy >= out_h / 2.0 - 1 && cy <= out_h / 2.0 + 1,
          "%s %d on %dx%d: not centred (%g,%g)", screen_mode_name(mode), rotate, out_w, out_h, cx, cy);
    if (l.exact && turned)
        CHECK(cx - sw / 2 == (int)(cx - sw / 2) && cy - sh / 2 == (int)(cy - sh / 2),
              "%s %d on %dx%d: an integer scale off the pixel grid", screen_mode_name(mode), rotate, out_w, out_h);
    CHECK(l.crisp >= 1 && l.crisp >= l.sx - 1e-9 && l.crisp >= l.sy - 1e-9, "%s %d on %dx%d: crisp %d below %gx%g",
          screen_mode_name(mode), rotate, out_w, out_h, l.crisp, l.sx, l.sy);
}

static void devices(void)
{
    static const int SIZES[][2] = { { 640, 480 }, { 720, 720 }, { 1280, 720 }, { 1280, 800 }, { 1920, 1080 },
                                    { 480, 320 }, { 1024, 768 }, { 750, 1334 }, { 100, 100 }, { 3840, 2160 } };
    static const int ROT[3] = { 0, 90, 270 };
    for (size_t i = 0; i < sizeof SIZES / sizeof SIZES[0]; i++)
        for (int m = 0; m < SCREEN_MODES; m++)
            for (int r = 0; r < 3; r++) check_on_screen(m, ROT[r], SIZES[i][0], SIZES[i][1]);
}

static void scales(void)
{
    ScreenLayout l = lay(SCREEN_AUTO, 0, 640, 480); /* 1x fills 53%: fitted */
    CHECK(!l.exact && l.sy == 1.875 && l.crisp == 2 && l.h == 480, "640x480 auto: %g crisp %d", l.sy, l.crisp);
    l = lay(SCREEN_AUTO, 90, 640, 480); /* turned: 2x fills 93% */
    CHECK(l.exact && l.sx == 2 && l.w == 448 && l.h == 512, "640x480 turned: %g", l.sx);
    l = lay(SCREEN_AUTO, 270, 1280, 720);
    CHECK(l.exact && l.sx == 3, "1280x720 turned: %g", l.sx);
    l = lay(SCREEN_INTEGER, 0, 1280, 720);
    CHECK(l.exact && l.sx == 2 && l.x == 416 && l.y == 104, "1280x720 integer: %g at %d,%d", l.sx, l.x, l.y);
    l = lay(SCREEN_FIT, 0, 720, 720);
    CHECK(!l.exact && l.sy == 2.8125 && l.crisp == 3 && l.w == 630 && l.h == 720, "720x720 fit: %g", l.sy);
    l = lay(SCREEN_FIT, 0, 672, 768); /* fits exactly 3x */
    CHECK(l.exact && l.sx == 3 && l.x == 0 && l.y == 0, "672x768 fit: %g", l.sx);
    l = lay(SCREEN_STRETCH, 0, 720, 720);
    CHECK(!l.exact && l.w == 720 && l.h == 720 && l.crisp == 4, "720x720 stretch: %dx%d crisp %d", l.w, l.h, l.crisp);
    l = lay(SCREEN_STRETCH, 90, 1280, 720); /* the arcade's x along the screen's height */
    CHECK(l.w == 720 && l.h == 1280 && l.x == 280 && l.y == -280, "1280x720 stretch turned: %dx%d at %d,%d", l.w, l.h,
          l.x, l.y);
    l = lay(SCREEN_AUTO, 0, 100, 100); /* smaller than the arcade: 1x, cropped */
    CHECK(l.exact && l.sx == 1, "100x100: %g", l.sx);
}

static void joystick(void)
{
    int x, y;
    screen_turn_input(0, -1, 0, &x, &y);
    CHECK(x == -1 && y == 0, "not turned: left is left");
    /* turned 90: the arcade's top at the screen's right, its left at the screen's top */
    screen_turn_input(90, 0, -1, &x, &y);
    CHECK(x == -1 && y == 0, "90: up is the arcade's left (%d,%d)", x, y);
    screen_turn_input(90, 1, 0, &x, &y);
    CHECK(x == 0 && y == -1, "90: right is the arcade's up (%d,%d)", x, y);
    screen_turn_input(270, 0, 1, &x, &y);
    CHECK(x == -1 && y == 0, "270: down is the arcade's left (%d,%d)", x, y);
    screen_turn_input(270, -1, 0, &x, &y);
    CHECK(x == 0 && y == -1, "270: left is the arcade's up (%d,%d)", x, y);
}

static void names(void)
{
    for (int m = 0; m < SCREEN_MODES; m++)
        CHECK(screen_mode_parse(screen_mode_name(m)) == m, "mode %d's name", m);
    CHECK(screen_mode_parse("zoom") < 0, "an unknown mode");
    CHECK(screen_rotate_parse("90") == 90 && screen_rotate_parse("270") == 270 && screen_rotate_parse("0") == 0 &&
          screen_rotate_parse("180") < 0, "rotations");
    CHECK(screen_rotate_next(0) == 90 && screen_rotate_next(90) == 270 && screen_rotate_next(270) == 0, "the next rotation");
}

int main(void)
{
    devices();
    scales();
    joystick();
    names();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("screen: all checks passed\n");
    return failures != 0;
}
