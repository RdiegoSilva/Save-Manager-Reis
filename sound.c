/*
 * sound.c - sintetiza os efeitos (ondas senoidais com envelope) na inicializacao
 * e toca via audsrv. Formato: PCM 16 bits, estereo, 44100 Hz.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <audsrv.h>
#include "sound.h"

#define RATE 44100

static short *buf[SND_COUNT];
static int    len[SND_COUNT];          /* em bytes */
static int    ready = 0, on = 1;

/* uma nota: freq (Hz) -> freq2 (varredura opcional), ms, volume 0..1; 'gap' ms de silencio depois */
typedef struct { float f1, f2; int ms, gap; float vol; } Note;

static int synth(int id, const Note *n, int cnt)
{
    int total = 0, i, k;
    short *p;
    for (i = 0; i < cnt; i++) total += (n[i].ms + n[i].gap) * RATE / 1000;
    p = (short *)calloc((size_t)total * 2, sizeof(short));
    if (!p) return 0;
    total = 0;
    for (i = 0; i < cnt; i++) {
        int ns = n[i].ms * RATE / 1000, gs = n[i].gap * RATE / 1000;
        float ph = 0.0f;
        for (k = 0; k < ns; k++) {
            float t = (float)k / (float)ns;
            float f = n[i].f1 + (n[i].f2 - n[i].f1) * t;
            float att = (k < RATE / 300) ? (float)k / (float)(RATE / 300) : 1.0f;   /* ~3 ms: evita estalo */
            float env = att * expf(-3.2f * t);
            float s;
            ph += 6.2831853f * f / (float)RATE;
            if (ph > 6.2831853f) ph -= 6.2831853f;
            s = sinf(ph) + 0.25f * sinf(2.0f * ph);
            s *= env * n[i].vol * 0.8f;
            p[(total + k) * 2]     = (short)(s * 32767.0f);
            p[(total + k) * 2 + 1] = (short)(s * 32767.0f);
        }
        total += ns + gs;
    }
    buf[id] = p;
    len[id] = total * 4;
    return 1;
}

void snd_init(void)
{
    struct audsrv_fmt_t fmt;
    int ok = 1;
    static const Note move[]   = { { 1100, 1100, 28, 0, 0.30f } };
    static const Note select[] = { { 1500, 1700, 40, 0, 0.35f } };
    static const Note tab[]    = { { 700, 950, 55, 0, 0.35f } };
    static const Note okk[]    = { { 880, 880, 55, 0, 0.40f }, { 1320, 1320, 80, 0, 0.40f } };
    static const Note back[]   = { { 660, 660, 55, 0, 0.40f }, { 440, 440, 80, 0, 0.40f } };
    static const Note done[]   = { { 523, 523, 70, 0, 0.40f }, { 659, 659, 70, 0, 0.40f },
                                   { 784, 784, 70, 0, 0.40f }, { 1047, 1047, 160, 0, 0.40f } };
    static const Note err[]    = { { 190, 150, 110, 40, 0.45f }, { 190, 130, 160, 0, 0.45f } };
    static const Note start[]  = { { 523, 523, 90, 0, 0.30f }, { 784, 784, 90, 0, 0.30f },
                                   { 1047, 1047, 90, 0, 0.30f }, { 1319, 1319, 220, 0, 0.30f } };

    if (audsrv_init() != 0) return;
    fmt.freq = RATE; fmt.bits = 16; fmt.channels = 2;
    if (audsrv_set_format(&fmt) != 0) return;
    audsrv_set_volume(MAX_VOLUME);

    ok &= synth(SND_MOVE,   move,   1);
    ok &= synth(SND_SELECT, select, 1);
    ok &= synth(SND_TAB,    tab,    1);
    ok &= synth(SND_OK,     okk,    2);
    ok &= synth(SND_BACK,   back,   2);
    ok &= synth(SND_DONE,   done,   4);
    ok &= synth(SND_ERR,    err,    2);
    ok &= synth(SND_START,  start,  4);
    ready = ok;
}

void snd_play(int id)
{
    if (!ready || !on || id < 0 || id >= SND_COUNT || !buf[id]) return;
    audsrv_stop_audio();                      /* o efeito novo corta o anterior (sem fila de cliques) */
    audsrv_play_audio((const char *)buf[id], len[id]);
}

void snd_set(int v)     { on = v ? 1 : 0; }
int  snd_enabled(void)  { return on; }
