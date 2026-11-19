/*
 * savefmt.c - leitura de saves PS2 nos formatos CodeBreaker (.cbs) e SharkPort / X-Port (.sps / .xps).
 * Baseado na especificacao do mymc (Ross Ridge, dominio publico). Sem dependencias do PS2SDK.
 */
#include <stdlib.h>
#include <string.h>
#include "savefmt.h"

/* ---------------- inflate (RFC 1951) no estilo "puff" ---------------- */
typedef struct {
    const uint8_t *in; uint32_t n, pos;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; uint32_t cap, o;
    int err, full;
} Inf;

typedef struct { uint16_t count[16]; uint16_t symbol[288]; } Huff;

static int get_bits(Inf *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->n) { s->err = 1; return 0; }
        val |= (uint32_t)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int huff_build(Huff *h, const uint16_t *length, int n)
{
    int sym, len, left;
    uint16_t offs[16];
    for (len = 0; len < 16; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (uint16_t)sym;
    return left;
}

static int huff_decode(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0, len, count;
    for (len = 1; len < 16; len++) {
        code |= get_bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint16_t LEXT[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint16_t DEXT[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int inflate_codes(Inf *s, const Huff *lc, const Huff *dc)
{
    int sym, len, dist;
    for (;;) {
        sym = huff_decode(s, lc);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->o >= s->cap) { s->full = 1; return 0; }
            s->out[s->o++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            len = LBASE[sym] + get_bits(s, LEXT[sym]);
            sym = huff_decode(s, dc);
            if (sym < 0 || sym >= 30) return -1;
            dist = DBASE[sym] + get_bits(s, DEXT[sym]);
            if (s->err || (uint32_t)dist > s->o) return -1;
            while (len--) {
                if (s->o >= s->cap) { s->full = 1; return 0; }
                s->out[s->o] = s->out[s->o - dist];
                s->o++;
            }
        }
        if (s->err) return -1;
    }
}

static Huff g_lc, g_dc;                       /* estaticos: nao usa pilha */

static int inflate_fixed(Inf *s)
{
    static int built = 0;
    static Huff flc, fdc;
    if (!built) {
        uint16_t l[288];
        int i;
        for (i = 0; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        huff_build(&flc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        huff_build(&fdc, l, 30);
        built = 1;
    }
    return inflate_codes(s, &flc, &fdc);
}

static int inflate_dynamic(Inf *s)
{
    static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    uint16_t lengths[320];
    int nlen = get_bits(s, 5) + 257, ndist = get_bits(s, 5) + 1, ncode = get_bits(s, 4) + 4;
    int index, err;
    Huff lencode;
    if (s->err || nlen > 286 || ndist > 30) return -1;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (uint16_t)get_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    err = huff_build(&lencode, lengths, 19);
    if (err != 0) return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = huff_decode(s, &lencode), len, rep;
        if (sym < 0) return -1;
        if (sym < 16) lengths[index++] = (uint16_t)sym;
        else {
            len = 0;
            if (sym == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                rep = 3 + get_bits(s, 2);
            } else if (sym == 17) rep = 3 + get_bits(s, 3);
            else                  rep = 11 + get_bits(s, 7);
            if (index + rep > nlen + ndist) return -1;
            while (rep--) lengths[index++] = (uint16_t)len;
        }
        if (s->err) return -1;
    }
    if (lengths[256] == 0) return -1;
    err = huff_build(&g_lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - g_lc.count[0] != 1)) return -1;
    err = huff_build(&g_dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - g_dc.count[0] != 1)) return -1;
    return inflate_codes(s, &g_lc, &g_dc);
}

long sv_zlib_inflate(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    Inf s;
    int last, type, rc = 0;
    if (n < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return -1;
    memset(&s, 0, sizeof(s));
    s.in = in; s.n = n; s.pos = 2;
    s.out = out; s.cap = cap;
    do {
        last = get_bits(&s, 1);
        type = get_bits(&s, 2);
        if (s.err) return -1;
        if (type == 0) {
            uint32_t len;
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.pos + 4 > s.n) return -1;
            len = in[s.pos] | (in[s.pos + 1] << 8);
            if ((len ^ 0xFFFFu) != (uint32_t)(in[s.pos + 2] | (in[s.pos + 3] << 8))) return -1;
            s.pos += 4;
            if (s.pos + len > s.n) return -1;
            while (len--) {
                if (s.o >= s.cap) { s.full = 1; break; }
                s.out[s.o++] = in[s.pos++];
            }
        } else if (type == 1) rc = inflate_fixed(&s);
        else if (type == 2)   rc = inflate_dynamic(&s);
        else return -1;
        if (rc != 0 || s.err) return -1;
        if (s.full) break;                       /* saida cheia: como o mymc, ignora o resto */
    } while (!last);
    return (long)s.o;
}

/* ---------------- RC4 (permutacao inicial fixa do CodeBreaker) ---------------- */
static const uint8_t CBS_S[256] = {
    0x5f, 0x1f, 0x85, 0x6f, 0x31, 0xaa, 0x3b, 0x18,
    0x21, 0xb9, 0xce, 0x1c, 0x07, 0x4c, 0x9c, 0xb4,
    0x81, 0xb8, 0xef, 0x98, 0x59, 0xae, 0xf9, 0x26,
    0xe3, 0x80, 0xa3, 0x29, 0x2d, 0x73, 0x51, 0x62,
    0x7c, 0x64, 0x46, 0xf4, 0x34, 0x1a, 0xf6, 0xe1,
    0xba, 0x3a, 0x0d, 0x82, 0x79, 0x0a, 0x5c, 0x16,
    0x71, 0x49, 0x8e, 0xac, 0x8c, 0x9f, 0x35, 0x19,
    0x45, 0x94, 0x3f, 0x56, 0x0c, 0x91, 0x00, 0x0b,
    0xd7, 0xb0, 0xdd, 0x39, 0x66, 0xa1, 0x76, 0x52,
    0x13, 0x57, 0xf3, 0xbb, 0x4e, 0xe5, 0xdc, 0xf0,
    0x65, 0x84, 0xb2, 0xd6, 0xdf, 0x15, 0x3c, 0x63,
    0x1d, 0x89, 0x14, 0xbd, 0xd2, 0x36, 0xfe, 0xb1,
    0xca, 0x8b, 0xa4, 0xc6, 0x9e, 0x67, 0x47, 0x37,
    0x42, 0x6d, 0x6a, 0x03, 0x92, 0x70, 0x05, 0x7d,
    0x96, 0x2f, 0x40, 0x90, 0xc4, 0xf1, 0x3e, 0x3d,
    0x01, 0xf7, 0x68, 0x1e, 0xc3, 0xfc, 0x72, 0xb5,
    0x54, 0xcf, 0xe7, 0x41, 0xe4, 0x4d, 0x83, 0x55,
    0x12, 0x22, 0x09, 0x78, 0xfa, 0xde, 0xa7, 0x06,
    0x08, 0x23, 0xbf, 0x0f, 0xcc, 0xc1, 0x97, 0x61,
    0xc5, 0x4a, 0xe6, 0xa0, 0x11, 0xc2, 0xea, 0x74,
    0x02, 0x87, 0xd5, 0xd1, 0x9d, 0xb7, 0x7e, 0x38,
    0x60, 0x53, 0x95, 0x8d, 0x25, 0x77, 0x10, 0x5e,
    0x9b, 0x7f, 0xd8, 0x6e, 0xda, 0xa2, 0x2e, 0x20,
    0x4f, 0xcd, 0x8f, 0xcb, 0xbe, 0x5a, 0xe0, 0xed,
    0x2c, 0x9a, 0xd4, 0xe2, 0xaf, 0xd0, 0xa9, 0xe8,
    0xad, 0x7a, 0xbc, 0xa8, 0xf2, 0xee, 0xeb, 0xf5,
    0xa6, 0x99, 0x28, 0x24, 0x6c, 0x2b, 0x75, 0x5d,
    0xf8, 0xd3, 0x86, 0x17, 0xfb, 0xc0, 0x7b, 0xb3,
    0x58, 0xdb, 0xc7, 0x4b, 0xff, 0x04, 0x50, 0xe9,
    0x88, 0x69, 0xc9, 0x2a, 0xab, 0xfd, 0x5b, 0x1b,
    0x8a, 0xd9, 0xec, 0x27, 0x44, 0x0e, 0x33, 0xc8,
    0x6b, 0x93, 0x32, 0x48, 0xb6, 0x30, 0x43, 0xa5
};

static void cbs_rc4(uint8_t *t, uint32_t n)
{
    uint8_t s[256], tmp;
    uint32_t ii, i, j = 0;
    memcpy(s, CBS_S, 256);
    for (ii = 0; ii < n; ii++) {
        i = (ii + 1) & 255;
        j = (j + s[i]) & 255;
        tmp = s[i]; s[i] = s[j]; s[j] = tmp;
        t[ii] ^= s[(uint8_t)(s[i] + s[j])];
    }
}

static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t u16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static void zcopy(char *dst, int max, const uint8_t *src, int n)
{
    int i;
    for (i = 0; i < n && i < max - 1 && src[i]; i++) dst[i] = (char)src[i];
    dst[i] = 0;
}

/* ---------------- CodeBreaker (.cbs) ---------------- */
int sv_peek_cbs(const uint8_t *h, uint32_t n, char *dirname, int max)
{
    if (n < 52 || memcmp(h, "CFU\0", 4) != 0) return -1;
    zcopy(dirname, max, h + 20, 32);
    return dirname[0] ? 0 : -1;
}

int sv_parse_cbs(const uint8_t *in, uint32_t n, uint32_t max_out, char *dirname, sv_file_fn fn, void *ctx)
{
    uint32_t hlen, dlen, flen, rem, blen, off = 0;
    uint8_t *body, *dec;
    long got;
    int rc = 0, nfiles = 0;

    if (n < 12 || memcmp(in, "CFU\0", 4) != 0) return -1;
    hlen = u32(in + 8);
    if (hlen < 92 + 32 || hlen > n) return -2;
    dlen = u32(in + 12);
    flen = u32(in + 16);
    zcopy(dirname, 64, in + 20, 32);
    if (!dirname[0]) return -2;
    if (dlen == 0 || dlen > max_out) return -3;

    rem = n - hlen;
    if (rem >= flen)                          blen = flen;          /* flen = tamanho do corpo */
    else if (flen >= hlen && rem == flen - hlen) blen = rem;        /* flen = tamanho do arquivo todo */
    else return -4;
    if (blen == 0) return -4;

    body = (uint8_t *)malloc(blen);
    dec  = (uint8_t *)malloc(dlen);
    if (!body || !dec) { free(body); free(dec); return -5; }
    memcpy(body, in + hlen, blen);
    cbs_rc4(body, blen);
    got = sv_zlib_inflate(body, blen, dec, dlen);
    free(body);
    if (got < 0) { free(dec); return -6; }

    while (off < (uint32_t)got) {
        uint32_t size, mode;
        char name[33];
        if ((uint32_t)got - off < 64) { rc = -7; break; }
        size = u32(dec + off + 16);
        mode = u16(dec + off + 20);
        zcopy(name, sizeof(name), dec + off + 32, 32);
        if ((uint32_t)got - off - 64 < size) { rc = -7; break; }
        if (!(mode & 0x10)) { rc = -8; break; }            /* subpasta: nao suportado */
        if (fn(ctx, name, dec + off + 64, size, dec + off, dec + off + 8) != 0) { rc = -9; break; }
        off += 64 + size;
        nfiles++;
    }
    free(dec);
    if (rc == 0 && nfiles == 0) rc = -10;
    return rc;
}

/* ---------------- SharkPort / X-Port (.sps / .xps) ---------------- */
static const uint8_t SPS_MAGIC[17] = { 0x0D, 0, 0, 0, 'S','h','a','r','k','P','o','r','t','S','a','v','e' };

/* le uma string com prefixo de 32 bits; avanca *off. 0 = ok */
static int sps_skip_str(const uint8_t *in, uint32_t n, uint32_t *off)
{
    uint32_t l;
    if (*off + 4 > n) return -1;
    l = u32(in + *off);
    *off += 4;
    if (l > n - *off) return -1;
    *off += l;
    return 0;
}

int sv_peek_sps(const uint8_t *h, uint32_t n, char *dirname, int max)
{
    uint32_t off = 17 + 4;
    if (n < 21 || memcmp(h, SPS_MAGIC, 17) != 0) return -1;
    if (sps_skip_str(h, n, &off) || sps_skip_str(h, n, &off) || sps_skip_str(h, n, &off)) return -1;
    off += 4;                                              /* tamanho */
    if (off + 98 > n) return -1;
    zcopy(dirname, max, h + off + 2, 64);
    return dirname[0] ? 0 : -1;
}

int sv_parse_sps(const uint8_t *in, uint32_t n, char *dirname, sv_file_fn fn, void *ctx)
{
    uint32_t off = 17 + 4, hlen, dirlen, k;
    uint32_t dmode;
    int nfiles = 0;

    if (n < 21 || memcmp(in, SPS_MAGIC, 17) != 0) return -1;
    if (sps_skip_str(in, n, &off) || sps_skip_str(in, n, &off) || sps_skip_str(in, n, &off)) return -2;
    if (off + 4 > n) return -2;
    off += 4;                                              /* tamanho total */
    if (off + 98 > n) return -2;
    hlen    = u16(in + off);
    zcopy(dirname, 64, in + off + 2, 64);
    dirlen  = u32(in + off + 66);
    dmode   = u16(in + off + 78);
    dmode   = ((dmode >> 8) & 0xFF) | ((dmode & 0xFF) << 8);       /* valores com bytes trocados */
    if (hlen < 98 || hlen > n - off) return -2;
    off += hlen;
    if (!(dmode & 0x20) || dirlen < 2 || !dirname[0]) return -3;     /* precisa ser pasta */
    dirlen -= 2;

    for (k = 0; k < dirlen; k++) {
        uint32_t fl, mode;
        char name[33];
        if (off + 98 > n) return -4;
        hlen = u16(in + off);
        zcopy(name, sizeof(name), in + off + 2, 32);
        fl   = u32(in + off + 66);
        mode = u16(in + off + 78);
        mode = ((mode >> 8) & 0xFF) | ((mode & 0xFF) << 8);
        if (hlen < 98 || hlen > n - off) return -4;
        if (!(mode & 0x10)) return -5;                     /* subpasta */
        {
            const uint8_t *cr = in + off + 82, *mo = in + off + 90;
            off += hlen;
            if (fl > n - off) return -4;
            if (fn(ctx, name, in + off, fl, cr, mo) != 0) return -6;
            off += fl;
        }
        nfiles++;
    }
    return nfiles ? 0 : -7;
}
