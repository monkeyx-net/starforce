#include "assets/zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "assets/inflate.h"

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

/* The entry matching `name` (base name, case-insensitive), or if name is NULL
   the one whose stored CRC-32 is `want_crc`. */
static int zip_find(const char *zip_path, const char *name, uint32_t want_crc, uint8_t **data, size_t *len, char *err,
                    int errlen)
{
    size_t n;
    uint8_t *z = slurp(zip_path, &n);
    if (!z) {
        snprintf(err, (size_t)errlen, "%s: cannot read", zip_path);
        return -1;
    }
    /* end of central directory: last 0x06054b50 within the final 64 KB + 22 */
    size_t eocd = 0;
    int found = 0;
    for (size_t i = n >= 22 ? n - 22 : 0; i + 22 <= n; i--) {
        if (le32(z + i) == 0x06054b50) { eocd = i; found = 1; break; }
        if (i == 0 || n - i > 65535 + 22) break;
    }
    if (!found) {
        free(z);
        snprintf(err, (size_t)errlen, "%s: not a zip file", zip_path);
        return -1;
    }
    uint32_t entries = le16(z + eocd + 10), cd = le32(z + eocd + 16);
    int rc = -2;
    size_t p = cd;
    for (uint32_t e = 0; e < entries; e++) {
        if (p + 46 > n || le32(z + p) != 0x02014b50) {
            snprintf(err, (size_t)errlen, "%s: bad central directory", zip_path);
            rc = -1;
            break;
        }
        uint32_t method = le16(z + p + 10), crc = le32(z + p + 16), csize = le32(z + p + 20), usize = le32(z + p + 24);
        uint32_t nlen = le16(z + p + 28), xlen = le16(z + p + 30), clen = le16(z + p + 32), loff = le32(z + p + 42);
        if (p + 46 + nlen > n) { rc = -1; snprintf(err, (size_t)errlen, "%s: truncated", zip_path); break; }
        const char *ename = (const char *)z + p + 46;
        const char *base = ename;
        for (uint32_t k = 0; k < nlen; k++)
            if (ename[k] == '/') base = ename + k + 1;
        size_t blen = (size_t)(ename + nlen - base);
        p += 46 + nlen + xlen + clen;
        if (name ? blen != strlen(name) || strncasecmp(base, name, blen) : crc != want_crc) continue;
        char shown[64];
        snprintf(shown, sizeof shown, "%.*s", (int)(blen < 63 ? blen : 63), base);

        if ((size_t)loff + 30 > n || le32(z + loff) != 0x04034b50) { rc = -1; snprintf(err, (size_t)errlen, "%s: bad entry", zip_path); break; }
        size_t start = (size_t)loff + 30 + le16(z + loff + 26) + le16(z + loff + 28);
        if (start + csize > n) { rc = -1; snprintf(err, (size_t)errlen, "%s: truncated entry", zip_path); break; }
        uint8_t *out = NULL;
        size_t olen = 0;
        const char *ie = NULL;
        if (method == 0) {
            out = malloc(csize ? csize : 1);
            if (out) memcpy(out, z + start, csize);
            olen = csize;
        } else if (method == 8) {
            if (inflate_raw(z + start, csize, &out, &olen, &ie)) out = NULL;
        } else {
            snprintf(err, (size_t)errlen, "%s: %s uses an unsupported compression method", zip_path, shown);
            rc = -1;
            break;
        }
        if (!out || olen != usize || crc32(out, olen) != crc) {
            free(out);
            snprintf(err, (size_t)errlen, "%s: %s is damaged%s%s", zip_path, shown, ie ? ": " : "", ie ? ie : "");
            rc = -1;
            break;
        }
        *data = out;
        *len = olen;
        rc = 0;
        break;
    }
    free(z);
    return rc;
}

int zip_read(const char *zip_path, const char *name, uint8_t **data, size_t *len, char *err, int errlen)
{
    return zip_find(zip_path, name, 0, data, len, err, errlen);
}

int zip_read_crc(const char *zip_path, uint32_t crc, uint8_t **data, size_t *len, char *err, int errlen)
{
    return zip_find(zip_path, NULL, crc, data, len, err, errlen);
}
