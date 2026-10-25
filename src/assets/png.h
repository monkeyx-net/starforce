/*
 * Minimal PNG reader/writer, no dependencies.
 *
 * Reads 8-bit RGB or RGBA, non-interlaced PNGs (MAME snapshots, assets); verifies
 * every chunk CRC and the zlib Adler-32. Writes uncompressed RGBA PNGs
 * (stored deflate blocks), used for framebuffer screenshots in tests.
 */
#ifndef STARFORCE_PNG_H
#define STARFORCE_PNG_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int width, height;
    uint8_t *rgba; /* width * height * 4, row-major */
} Image;

/* Decode a PNG held in memory. Returns 0 on success; on failure *err names the problem. */
int png_decode(const uint8_t *data, size_t size, Image *out, const char **err);
/* Load and decode a file. */
int png_load(const char *path, Image *out, const char **err);
void image_free(Image *img);

/* Write `width` x `height` RGBA pixels as a PNG file. Returns 0 on success. */
int png_write(const char *path, const uint8_t *rgba, int width, int height);

#endif
