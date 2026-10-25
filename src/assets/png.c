#include "assets/png.h"
#include "assets/inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- checksums ---------------------------------------------------------- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    if (crc_table[1]) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc_init();
    crc ^= 0xFFFFFFFFu;
    while (n--) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n--) {
        a = (a + *p++) % 65521u;
        b = (b + a) % 65521u;
    }
    return b << 16 | a;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* ---- PNG ---------------------------------------------------------------- */

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

int png_decode(const uint8_t *data, size_t size, Image *out, const char **err)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    const char *e = NULL;
    uint8_t *idat = NULL, *raw = NULL, *rows = NULL;
    size_t idat_len = 0;
    uint32_t w = 0, h = 0;
    int seen_ihdr = 0, seen_iend = 0, channels = 4;
    memset(out, 0, sizeof *out);

    if (size < 8 || memcmp(data, sig, 8) != 0) { e = "not a PNG file"; goto fail; }

    for (size_t pos = 8; pos < size && !seen_iend;) {
        if (pos + 12 > size) { e = "chunk header truncated"; goto fail; }
        uint32_t len = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        if (len > size - pos - 12) { e = "chunk truncated"; goto fail; }
        const uint8_t *body = data + pos + 8;
        if (crc32_update(0, type, len + 4) != be32(body + len)) { e = "chunk CRC mismatch"; goto fail; }

        if (!memcmp(type, "IHDR", 4)) {
            if (len != 13) { e = "bad IHDR"; goto fail; }
            w = be32(body);
            h = be32(body + 4);
            if (body[8] != 8 || (body[9] != 6 && body[9] != 2)) { e = "only 8-bit RGB or RGBA PNGs are supported"; goto fail; }
            channels = body[9] == 6 ? 4 : 3; /* RGB: MAME snapshots */
            if (body[10] != 0 || body[11] != 0) { e = "unknown compression or filter method"; goto fail; }
            if (body[12] != 0) { e = "interlaced PNGs are not supported"; goto fail; }
            if (w == 0 || h == 0 || w > 16384 || h > 16384) { e = "unreasonable image size"; goto fail; }
            seen_ihdr = 1;
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *p = realloc(idat, idat_len + len);
            if (!p) { e = "out of memory"; goto fail; }
            idat = p;
            memcpy(idat + idat_len, body, len);
            idat_len += len;
        } else if (!memcmp(type, "IEND", 4)) {
            seen_iend = 1;
        } else if (!(type[0] & 0x20)) {
            e = "unknown critical chunk";
            goto fail;
        }
        pos += 12 + len;
    }
    if (!seen_ihdr || !seen_iend) { e = "missing IHDR or IEND"; goto fail; }

    /* zlib wrapper (RFC 1950) */
    if (idat_len < 6) { e = "image data too short"; goto fail; }
    if ((idat[0] & 0x0F) != 8 || ((idat[0] << 8) | idat[1]) % 31 != 0 || (idat[1] & 0x20)) {
        e = "bad zlib header";
        goto fail;
    }
    size_t raw_len;
    if (inflate_raw(idat + 2, idat_len - 6, &raw, &raw_len, &e)) goto fail;
    if (adler32(raw, raw_len) != be32(idat + idat_len - 4)) { e = "zlib Adler-32 mismatch"; goto fail; }

    const size_t bpp = (size_t)channels, stride = (size_t)w * bpp;
    if (raw_len != (stride + 1) * h) { e = "image data has the wrong size"; goto fail; }

    out->rgba = malloc((size_t)w * 4 * h);
    rows = bpp == 4 ? out->rgba : malloc(stride * h);
    if (!out->rgba || !rows) { e = "out of memory"; goto fail; }
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = raw + y * (stride + 1);
        uint8_t *dst = rows + y * stride;
        const uint8_t *prev = y ? dst - stride : NULL;
        int filter = *src++;
        for (size_t x = 0; x < stride; x++) {
            int a = x >= bpp ? dst[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = prev && x >= bpp ? prev[x - bpp] : 0;
            int pred;
            switch (filter) {
            case 0: pred = 0; break;
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (a + b) / 2; break;
            case 4: pred = paeth(a, b, c); break;
            default: e = "bad row filter"; goto fail;
            }
            dst[x] = (uint8_t)(src[x] + pred);
        }
    }
    if (bpp == 3) { /* expand to opaque RGBA */
        for (size_t i = 0; i < (size_t)w * h; i++) {
            memcpy(out->rgba + i * 4, rows + i * 3, 3);
            out->rgba[i * 4 + 3] = 0xFF;
        }
        free(rows);
    }
    rows = NULL;
    out->width = (int)w;
    out->height = (int)h;
    free(idat);
    free(raw);
    return 0;

fail:
    free(idat);
    free(raw);
    if (rows != out->rgba) free(rows);
    image_free(out);
    if (err) *err = e;
    return -1;
}

int png_load(const char *path, Image *out, const char **err)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) *err = "cannot open file";
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        if (err) *err = "cannot read file";
        return -1;
    }
    fclose(f);
    int rc = png_decode(buf, (size_t)n, out, err);
    free(buf);
    return rc;
}

void image_free(Image *img)
{
    free(img->rgba);
    img->rgba = NULL;
    img->width = img->height = 0;
}

/* ---- writer ------------------------------------------------------------- */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *body, uint32_t len)
{
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc32_update(0, (const uint8_t *)type, 4);
    /* continue the CRC over the body: crc32_update pre/post-inverts, so undo that */
    crc_init();
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) c = crc_table[(c ^ body[i]) & 0xFF] ^ (c >> 8);
    crc = c ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put32(tail, crc);
    return fwrite(hdr, 1, 8, f) != 8 || (len && fwrite(body, 1, len, f) != len) || fwrite(tail, 1, 4, f) != 4;
}

int png_write(const char *path, const uint8_t *rgba, int width, int height)
{
    size_t stride = (size_t)width * 4;
    size_t raw_len = (stride + 1) * (size_t)height;
    size_t blocks = raw_len / 65535 + 1;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len), *z = malloc(z_len);
    if (!raw || !z) {
        free(raw);
        free(z);
        return -1;
    }
    for (int y = 0; y < height; y++) {
        raw[y * (stride + 1)] = 0;
        memcpy(raw + y * (stride + 1) + 1, rgba + y * stride, stride);
    }
    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    for (size_t pos = 0; pos < raw_len || pos == 0;) {
        size_t n = raw_len - pos > 65535 ? 65535 : raw_len - pos;
        z[o++] = pos + n == raw_len ? 1 : 0;
        z[o++] = (uint8_t)n;
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;
        z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + pos, n);
        o += n;
        pos += n;
        if (n == 0) break;
    }
    put32(z + o, adler32(raw, raw_len));
    o += 4;

    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)width);
    put32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    FILE *f = fopen(path, "wb");
    int bad = !f;
    if (f) {
        bad |= fwrite(sig, 1, 8, f) != 8;
        bad |= write_chunk(f, "IHDR", ihdr, 13);
        bad |= write_chunk(f, "IDAT", z, (uint32_t)o);
        bad |= write_chunk(f, "IEND", NULL, 0);
        bad |= fclose(f) != 0;
    }
    free(raw);
    free(z);
    return bad ? -1 : 0;
}
