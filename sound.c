/*
 * sound.c - sintetiza os efeitos (ondas senoidais com envelope) na inicializacao
 * e toca via audsrv. Formato: PCM 16 bits, estereo, 44100 Hz.
 *
 * Reproducao SEM bloqueio: audsrv_play_audio() trava o programa ate caber todo
 * o buffer no anel do audsrv (efeitos longos congelavam a tela e picotavam o
 * som). Aqui o efeito e enviado em pedacos, so o que cabe, a cada quadro
 * (snd_update) e tambem no momento em que o som comeca.
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

/* reproducao em andamento */
static int    cur = -1;                /* id tocando (-1 = nenhum) */
static int    curOff = 0;              /* bytes ja enviados ao audsrv */
static int    curPrio = 0;

/* prioridade: efeitos importantes nao sao cortados por cliques de navegacao */
static const int prio[SND_COUNT] = { 0, 0, 0, 1, 1, 2, 2, 2 };

/* uma nota: freq (Hz) -> freq2 (varredura opcional), ms, gap ms de silencio depois, volume 0..1 */
typedef struct { float f1, f2; int ms, gap; float vol; } Note;

static int synth(int id, const Note *n, int cnt)
{
    int total = 0, i, k;
    short *p;
    for (i = 0; i < cnt; i++) total += (n[i].ms + n[i].gap) * RATE / 1000;
    total += RATE / 50;                                  /* 20 ms de silencio no fim */
    p = (short *)calloc((size_t)total * 2, sizeof(short));
    if (!p) return 0;
    total = 0;
    for (i = 0; i < cnt; i++) {
        int ns = n[i].ms * RATE / 1000, gs = n[i].gap * RATE / 1000;
        int atk = RATE / 300;                            /* ~3 ms */
        int rel = RATE / 150;                            /* ~6 ms: fecha a nota a zero (evita estalo) */
        float ph = 0.0f;
        if (rel > ns / 2) rel = ns / 2;
        for (k = 0; k < ns; k++) {
            float t = (float)k / (float)ns;
            float f = n[i].f1 + (n[i].f2 - n[i].f1) * t;
            float att = (k < atk) ? (float)k / (float)atk : 1.0f;
            float fade = (k >= ns - rel) ? (float)(ns - 1 - k) / (float)rel : 1.0f;
            float env = att * fade * expf(-3.2f * t);
            float s;
            short v;
            ph += 6.2831853f * f / (float)RATE;
            if (ph > 6.2831853f) ph -= 6.2831853f;
            s = (sinf(ph) + 0.25f * sinf(2.0f * ph)) * (1.0f / 1.25f);   /* normaliza: nunca passa de 1.0 */
            s *= env * n[i].vol * 0.8f;
            v = (short)(s * 32767.0f);
            p[(total + k) * 2]     = v;
            p[(total + k) * 2 + 1] = v;
        }
        total += ns + gs;
    }
    buf[id] = p;
    len[id] = (total + RATE / 50) * 4;
    return 1;
}

void snd_init(void)
{
    struct audsrv_fmt_t fmt;
    int ok = 1, tries;
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

    /* o audsrv.irx pode demorar um instante para registrar o RPC depois de carregado */
    for (tries = 0; tries < 20; tries++) {
        if (audsrv_init() == 0) break;
        {   /* ~50 ms de espera */
            volatile int w;
            for (w = 0; w < 2000000; w++) ;
        }
    }
    if (tries == 20) return;

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

/* envia ao audsrv so o que cabe agora (nunca bloqueia) */
void snd_update(void)
{
    int avail, n;
    if (!ready || cur < 0) return;
    if (curOff >= len[cur]) { cur = -1; return; }

    avail = audsrv_available();
    if (avail <= 0) return;
    n = len[cur] - curOff;
    if (n > avail) n = avail;
    n &= ~3;                                   /* amostras estereo 16 bits = 4 bytes */
    if (n <= 0) return;

    audsrv_play_audio((const char *)buf[cur] + curOff, n);
    curOff += n;
    if (curOff >= len[cur]) cur = -1;
}

void snd_play(int id)
{
    if (!ready || !on || id < 0 || id >= SND_COUNT || !buf[id]) return;

    /* nao deixa um clique cortar um aviso/confirmacao que ainda esta tocando */
    if (cur >= 0 && prio[id] < curPrio) return;

    audsrv_stop_audio();                       /* o efeito novo corta o anterior (sem fila de cliques) */
    cur = id; curOff = 0; curPrio = prio[id];
    snd_update();
}

void snd_set(int v)
{
    on = v ? 1 : 0;
    if (!on && ready) { audsrv_stop_audio(); cur = -1; }
}

int snd_enabled(void) { return on; }
