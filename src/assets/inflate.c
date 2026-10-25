#include "assets/inflate.h"

#include <stdlib.h>
#include <string.h>

/* ---- inflate (RFC 1951) ------------------------------------------------- */

typedef struct {
    const uint8_t *in;
    size_t in_len, in_pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t out_len, out_cap;
    const char *err;
} Inflate;

#define MAXBITS 15

typedef struct {
    short count[MAXBITS + 1]; /* codes of each length */
    short symbol[288];        /* symbols ordered by code */
} Huffman;

static int bits(Inflate *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) {
            s->err = "deflate stream truncated";
            return -1;
        }
        val |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int put(Inflate *s, uint8_t b)
{
    if (s->out_len == s->out_cap) {
        size_t cap = s->out_cap ? s->out_cap * 2 : 65536;
        uint8_t *p = realloc(s->out, cap);
        if (!p) {
            s->err = "out of memory";
            return -1;
        }
        s->out = p;
        s->out_cap = cap;
    }
    s->out[s->out_len++] = b;
    return 0;
}

static int stored(Inflate *s)
{
    s->bitbuf = 0; /* discard to byte boundary */
    s->bitcnt = 0;
    if (s->in_pos + 4 > s->in_len) {
        s->err = "stored block truncated";
        return -1;
    }
    unsigned len = s->in[s->in_pos] | s->in[s->in_pos + 1] << 8;
    unsigned nlen = s->in[s->in_pos + 2] | s->in[s->in_pos + 3] << 8;
    s->in_pos += 4;
    if (len != (~nlen & 0xFFFFu)) {
        s->err = "stored block length check failed";
        return -1;
    }
    if (s->in_pos + len > s->in_len) {
        s->err = "stored block truncated";
        return -1;
    }
    while (len--)
        if (put(s, s->in[s->in_pos++])) return -1;
    return 0;
}

/* Canonical Huffman decode, one bit at a time (inputs are small). */
static int decode(Inflate *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        int b = bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = "invalid Huffman code";
    return -1;
}

/* Build a decoder from code lengths. Returns 0, or -1 for an over-subscribed set.
   Incomplete sets are allowed (a single-code distance tree is legal). */
static int construct(Huffman *h, const short *length, int n)
{
    memset(h->count, 0, sizeof h->count);
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    short offs[MAXBITS + 1];
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int sym = 0; sym < n; sym++)
        if (length[sym]) h->symbol[offs[length[sym]]++] = (short)sym;
    return 0;
}

static const short len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                     3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                     257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                     8193, 12289, 16385, 24577 };
static const short dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                      7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(Inflate *s, const Huffman *lencode, const Huffman *distcode)
{
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (put(s, (uint8_t)sym)) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) {
                s->err = "invalid length symbol";
                return -1;
            }
            int e = bits(s, len_extra[sym]);
            if (e < 0) return -1;
            int len = len_base[sym] + e;
            int dsym = decode(s, distcode);
            if (dsym < 0) return -1;
            if (dsym >= 30) {
                s->err = "invalid distance symbol";
                return -1;
            }
            e = bits(s, dist_extra[dsym]);
            if (e < 0) return -1;
            size_t dist = (size_t)dist_base[dsym] + (size_t)e;
            if (dist > s->out_len) {
                s->err = "distance too far back";
                return -1;
            }
            while (len--)
                if (put(s, s->out[s->out_len - dist])) return -1;
        }
    }
}

static int fixed(Inflate *s)
{
    static Huffman lencode, distcode;
    static int built;
    if (!built) {
        short lengths[288];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < 288; sym++) lengths[sym] = 8;
        construct(&lencode, lengths, 288);
        for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
        construct(&distcode, lengths, 30);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(Inflate *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen = bits(s, 5), ndist = bits(s, 5), ncode = bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return -1;
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > 30) {
        s->err = "bad dynamic block counts";
        return -1;
    }

    short lengths[320] = {0};
    for (int i = 0; i < ncode; i++) {
        int b = bits(s, 3);
        if (b < 0) return -1;
        lengths[order[i]] = (short)b;
    }
    Huffman lencode, distcode;
    if (construct(&lencode, lengths, 19)) {
        s->err = "bad code-length code";
        return -1;
    }

    int index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &lencode);
        if (sym < 0) return -1;
        if (sym < 16) {
            lengths[index++] = (short)sym;
            continue;
        }
        short len = 0;
        int rep;
        if (sym == 16) {
            if (index == 0) {
                s->err = "repeat with no previous length";
                return -1;
            }
            len = lengths[index - 1];
            rep = bits(s, 2);
            if (rep < 0) return -1;
            rep += 3;
        } else if (sym == 17) {
            rep = bits(s, 3);
            if (rep < 0) return -1;
            rep += 3;
        } else {
            rep = bits(s, 7);
            if (rep < 0) return -1;
            rep += 11;
        }
        if (index + rep > nlen + ndist) {
            s->err = "too many code lengths";
            return -1;
        }
        while (rep--) lengths[index++] = len;
    }
    if (lengths[256] == 0) {
        s->err = "no end-of-block code";
        return -1;
    }
    if (construct(&lencode, lengths, nlen) || construct(&distcode, lengths + nlen, ndist)) {
        s->err = "bad literal/length or distance code";
        return -1;
    }
    return codes(s, &lencode, &distcode);
}

static int inflate_all(Inflate *s)
{
    int last;
    do {
        last = bits(s, 1);
        int type = bits(s, 2);
        if (last < 0 || type < 0) return -1;
        int rc = type == 0 ? stored(s) : type == 1 ? fixed(s) : type == 2 ? dynamic(s) : -1;
        if (type == 3) s->err = "invalid block type";
        if (rc) return -1;
    } while (!last);
    return 0;
}


int inflate_raw(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len, const char **err)
{
    Inflate s = { .in = in, .in_len = len };
    if (inflate_all(&s)) {
        free(s.out);
        if (err) *err = s.err ? s.err : "deflate error";
        return -1;
    }
    *out = s.out;
    *out_len = s.out_len;
    return 0;
}
