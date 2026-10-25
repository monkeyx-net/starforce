/*
 * A scripted run's inputs, in the MAME probes' PROBE_SCRIPT format:
 * "frame:input,input frame:..." where an input is coin, coin2, start,
 * start2, left, right, up, down, fire (player one) or left2, right2, up2,
 * down2, fire2 (player two); each entry holds from its frame until the next.
 * The coins are pulses, as MAME's coin inputs: set for the entry's first
 * COIN_PULSE_FRAMES frames only.
 */
#ifndef STARFORCE_SCRIPT_H
#define STARFORCE_SCRIPT_H

#include <stdint.h>

typedef struct {
    struct {
        long frame;
        uint8_t p1, p2, system; /* the bits pressed (active high, MAME's ports) */
    } step[4096];
    int n;
} Script;

/* Returns 0, or -1 on a malformed script. */
int script_parse(Script *s, const char *text);
#define COIN_PULSE_FRAMES 2

/* The inputs at a frame. */
void script_inputs(const Script *s, long frame, uint8_t *p1, uint8_t *p2, uint8_t *system);

#endif
