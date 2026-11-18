/*
 * lzari.c - LZARI (Okumura, 1989) com modelo identico ao original,
 * como usado pelo Action Replay MAX (.max).
 *
 * O codificador usa cadeias de hash para achar as repeticoes (o original
 * usa arvore binaria). Isso nao altera o formato: o fluxo gerado e
 * decodificavel por qualquer decodificador LZARI padrao.
 */
#include <string.h>
#include "lzari.h"

#define N          4096
#define F          60
#define THRESHOLD  2
#define M          15
#define Q1         (1ULL << M)
#define Q2         (2 * Q1)
#define Q3         (3 * Q1)
#define Q4         (4 * Q1)
#define MAX_CUM    (Q1 - 1)
#define N_CHAR     (256 - THRESHOLD + F)

/* ---------------- modelo adaptativo ---------------- */
static int      char_to_sym[N_CHAR], sym_to_char[N_CHAR + 1];
static unsigned sym_freq[N_CHAR + 1], sym_cum[N_CHAR + 1], position_cum[N + 1];

static void start_model(void)
{
    int ch, sym, i;
    sym_cum[N_CHAR] = 0;
    for (sym = N_CHAR; sym >= 1; sym--) {
        ch = sym - 1;
        char_to_sym[ch] = sym;
        sym_to_char[sym] = ch;
        sym_freq[sym] = 1;
        sym_cum[sym - 1] = sym_cum[sym] + sym_freq[sym];
    }
    sym_freq[0] = 0;
    position_cum[N] = 0;
    for (i = N; i >= 1; i--)
        position_cum[i - 1] = position_cum[i] + 10000 / (i + 200);
}

static void update_model(int sym)
{
    int i, ch_i, ch_sym;
    unsigned c;
    if (sym_cum[0] >= MAX_CUM) {
        c = 0;
        for (i = N_CHAR; i > 0; i--) {
            sym_cum[i] = c;
            c += (sym_freq[i] = (sym_freq[i] + 1) >> 1);
        }
        sym_cum[0] = c;
    }
    for (i = sym; sym_freq[i] == sym_freq[i - 1]; i--) ;
    if (i < sym) {
        ch_i = sym_to_char[i];
        ch_sym = sym_to_char[sym];
        sym_to_char[i] = ch_sym;
        sym_to_char[sym] = ch_i;
        char_to_sym[ch_i] = sym;
        char_to_sym[ch_sym] = i;
    }
    sym_freq[i]++;
    while (--i >= 0) sym_cum[i]++;
}

/* ---------------- codificador ---------------- */
typedef struct {
    uint64_t low, high;
    int      shifts;
    unsigned buffer, mask;
    uint8_t  ob[4096];
    uint32_t on;
    long     total;
    int      err;
    lz_put_fn put;
    void    *pctx;
} Enc;

static void enc_flush(Enc *e)
{
    if (e->on && !e->err) {
        if (e->put(e->pctx, e->ob, e->on) != 0) e->err = 1;
    }
    e->total += e->on;
    e->on = 0;
}

static void enc_byte(Enc *e, uint8_t b)
{
    e->ob[e->on++] = b;
    if (e->on == sizeof(e->ob)) enc_flush(e);
}

static void put_bit(Enc *e, int bit)
{
    if (bit) e->buffer |= e->mask;
    if ((e->mask >>= 1) == 0) {
        enc_byte(e, (uint8_t)e->buffer);
        e->buffer = 0;
        e->mask = 128;
    }
}

static void enc_output(Enc *e, int bit)
{
    put_bit(e, bit);
    for (; e->shifts > 0; e->shifts--) put_bit(e, !bit);
}

static void enc_norm(Enc *e)
{
    for (;;) {
        if (e->high <= Q2) enc_output(e, 0);
        else if (e->low >= Q2) { enc_output(e, 1); e->low -= Q2; e->high -= Q2; }
        else if (e->low >= Q1 && e->high <= Q3) { e->shifts++; e->low -= Q1; e->high -= Q1; }
        else break;
        e->low += e->low;
        e->high += e->high;
    }
}

static void enc_char(Enc *e, int ch)
{
    int sym = char_to_sym[ch];
    uint64_t range = e->high - e->low;
    e->high = e->low + (range * sym_cum[sym - 1]) / sym_cum[0];
    e->low +=          (range * sym_cum[sym])     / sym_cum[0];
    enc_norm(e);
    update_model(sym);
}

static void enc_position(Enc *e, int position)
{
    uint64_t range = e->high - e->low;
    e->high = e->low + (range * position_cum[position])     / position_cum[0];
    e->low +=          (range * position_cum[position + 1]) / position_cum[0];
    enc_norm(e);
}

static void enc_end(Enc *e)
{
    e->shifts++;
    if (e->low < Q1) enc_output(e, 0); else enc_output(e, 1);
    enc_byte(e, (uint8_t)e->buffer);
    enc_flush(e);
}

#define HBITS 15
#define HSIZE (1 << HBITS)
static int32_t head[HSIZE];
static int32_t prevp[N];

static uint32_t hash3(const uint8_t *p)
{
    return (((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ p[2]) & (HSIZE - 1);
}

long lzari_encode(const uint8_t *in, uint32_t n,
                  lz_put_fn put, void *pctx,
                  lz_tick_fn tick, void *tctx)
{
    static Enc e;
    uint8_t hdr[4];
    uint32_t pos = 0, nextTick = 8192, i;

    memset(&e, 0, sizeof(e));
    e.low = 0; e.high = Q4; e.mask = 128;
    e.put = put; e.pctx = pctx;

    hdr[0] = (uint8_t)n; hdr[1] = (uint8_t)(n >> 8);
    hdr[2] = (uint8_t)(n >> 16); hdr[3] = (uint8_t)(n >> 24);
    for (i = 0; i < 4; i++) enc_byte(&e, hdr[i]);

    start_model();
    for (i = 0; i < HSIZE; i++) head[i] = -1;

    while (pos < n && !e.err) {
        uint32_t maxlen = n - pos, bestLen = 0, bestDist = 0, len;
        if (maxlen > F) maxlen = F;

        if (maxlen > THRESHOLD) {
            int32_t cand = head[hash3(in + pos)];
            int chain = 0;
            while (cand >= 0 && (pos - (uint32_t)cand) < N && chain++ < 48) {
                uint32_t l = 0;
                while (l < maxlen && in[cand + l] == in[pos + l]) l++;
                if (l > bestLen || (l == bestLen && (pos - (uint32_t)cand) < bestDist)) {
                    bestLen = l;
                    bestDist = pos - (uint32_t)cand;
                    if (l == maxlen) break;
                }
                {
                    int32_t nx = prevp[cand & (N - 1)];
                    if (nx >= cand) break;
                    cand = nx;
                }
            }
        }

        if (bestLen > THRESHOLD) {
            len = bestLen;
            enc_char(&e, 255 - THRESHOLD + (int)len);
            enc_position(&e, (int)bestDist - 1);
        } else {
            len = 1;
            enc_char(&e, in[pos]);
        }

        for (i = 0; i < len; i++, pos++) {
            if (n - pos >= 3) {
                uint32_t h = hash3(in + pos);
                prevp[pos & (N - 1)] = head[h];
                head[h] = (int32_t)pos;
            }
        }
        if (tick && pos >= nextTick) {
            tick(tctx, pos);
            nextTick = pos + 8192;
        }
    }
    if (e.err) return -1;
    enc_end(&e);
    if (e.err) return -1;
    return e.total;
}

/* ---------------- decodificador ---------------- */
typedef struct {
    uint64_t low, high, value;
    unsigned buffer, mask;
    lz_get_fn get;
    void *gctx;
} Dec;

static int get_bit(Dec *d)
{
    if ((d->mask >>= 1) == 0) {
        int c = d->get(d->gctx);
        d->buffer = (c < 0) ? 0xFFFFFFFFu : (unsigned)c;     /* EOF = bits 1, como no original */
        d->mask = 128;
    }
    return (d->buffer & d->mask) != 0;
}

static int bsearch_sym(unsigned x)
{
    int i = 1, j = N_CHAR, k;
    while (i < j) {
        k = (i + j) / 2;
        if (sym_cum[k] > x) i = k + 1; else j = k;
    }
    return i;
}

static int bsearch_pos(unsigned x)
{
    int i = 1, j = N, k;
    while (i < j) {
        k = (i + j) / 2;
        if (position_cum[k] > x) i = k + 1; else j = k;
    }
    return i - 1;
}

static void dec_norm(Dec *d)
{
    for (;;) {
        if (d->low >= Q2) { d->value -= Q2; d->low -= Q2; d->high -= Q2; }
        else if (d->low >= Q1 && d->high <= Q3) { d->value -= Q1; d->low -= Q1; d->high -= Q1; }
        else if (d->high > Q2) break;
        d->low += d->low;
        d->high += d->high;
        d->value = 2 * d->value + get_bit(d);
    }
}

static int dec_char(Dec *d)
{
    int sym, ch;
    uint64_t range = d->high - d->low;
    sym = bsearch_sym((unsigned)(((d->value - d->low + 1) * sym_cum[0] - 1) / range));
    d->high = d->low + (range * sym_cum[sym - 1]) / sym_cum[0];
    d->low +=          (range * sym_cum[sym])     / sym_cum[0];
    dec_norm(d);
    ch = sym_to_char[sym];
    update_model(sym);
    return ch;
}

static int dec_position(Dec *d)
{
    int position;
    uint64_t range = d->high - d->low;
    position = bsearch_pos((unsigned)(((d->value - d->low + 1) * position_cum[0] - 1) / range));
    d->high = d->low + (range * position_cum[position])     / position_cum[0];
    d->low +=          (range * position_cum[position + 1]) / position_cum[0];
    dec_norm(d);
    return position;
}

long lzari_decode(lz_get_fn get, void *gctx,
                  uint8_t *out, uint32_t cap,
                  lz_tick_fn tick, void *tctx)
{
    static uint8_t text_buf[N];
    static Dec d;
    uint32_t size = 0, count = 0, nextTick = 8192;
    int i, r, k, c;

    for (i = 0; i < 4; i++) {
        c = get(gctx);
        if (c < 0) return -1;
        size |= (uint32_t)c << (8 * i);
    }
    if (size > cap) return -1;

    memset(&d, 0, sizeof(d));
    d.low = 0; d.high = Q4; d.value = 0;
    d.mask = 0; d.buffer = 0;
    d.get = get; d.gctx = gctx;
    for (i = 0; i < M + 2; i++) d.value = 2 * d.value + get_bit(&d);

    start_model();
    memset(text_buf, ' ', N - F);
    r = N - F;

    while (count < size) {
        c = dec_char(&d);
        if (c < 256) {
            out[count++] = (uint8_t)c;
            text_buf[r++] = (uint8_t)c;
            r &= (N - 1);
        } else {
            int dist = dec_position(&d);
            int src = (r - dist - 1) & (N - 1);
            int len = c - 255 + THRESHOLD;
            for (k = 0; k < len && count < size; k++) {
                uint8_t b = text_buf[(src + k) & (N - 1)];
                out[count++] = b;
                text_buf[r++] = b;
                r &= (N - 1);
            }
        }
        if (tick && count >= nextTick) {
            tick(tctx, count);
            nextTick = count + 8192;
        }
    }
    return (long)size;
}

/* ---------------- CRC32 ---------------- */
uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    static uint32_t table[256];
    static int init = 0;
    if (!init) {
        uint32_t i, j, c;
        for (i = 0; i < 256; i++) {
            c = i;
            for (j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = 1;
    }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
