/*
 * sound.c - sintetiza os efeitos (ondas senoidais com envelope) na inicializacao
 * e toca via audsrv.
 *
 * Por que assim (corrige o "loop estourado"):
 *  - O buffer do audsrv e pequeno. Se ele esvazia no meio de um efeito, o audio
 *    repete o ultimo trecho (som em loop/estourado). Por isso o formato e escolhido
 *    no inicio de modo que CADA efeito caiba inteiro no buffer (44.1 kHz estereo se
 *    couber; senao 22 kHz / 11 kHz mono) e o efeito e enviado de uma vez so.
 *  - Todo efeito termina com silencio, para que qualquer trecho "preso" seja mudo.
 *  - O volume e as amplitudes sao moderados (sem estourar).
 *  - O envio nunca bloqueia: se algo nao couber, o resto vai em snd_update().
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <audsrv.h>
#include "sound.h"

#define TAIL_MS 120                    /* silencio no fim de cada efeito */

static int    rate = 44100, chn = 2;   /* formato escolhido em snd_init */
static short *buf[SND_COUNT];
static int    len[SND_COUNT];          /* em bytes */
static int    ready = 0, on = 1;

/* reproducao em andamento */
static int    cur = -1;                /* id tocando (-1 = nenhum) */
static int    curOff = 0;              /* bytes ja enviados ao audsrv */
static int    curPrio = 0;
static int    hold = 0;                /* quadros ate o efeito terminar de tocar */
static int    holdFrames[SND_COUNT];

/* prioridade: efeitos importantes nao sao cortados por cliques de navegacao */
static const int prio[SND_COUNT] = { 0, 0, 0, 1, 1, 2, 2, 2 };

/* uma nota: freq (Hz) -> freq2 (varredura opcional), ms, gap ms de silencio depois, volume 0..1 */
typedef struct { float f1, f2; int ms, gap; float vol; } Note;

static const Note n_move[]   = { { 1100, 1100, 28, 0, 0.30f } };
static const Note n_select[] = { { 1500, 1700, 40, 0, 0.35f } };
static const Note n_tab[]    = { { 700, 950, 55, 0, 0.35f } };
static const Note n_ok[]     = { { 880, 880, 55, 0, 0.40f }, { 1320, 1320, 80, 0, 0.40f } };
static const Note n_back[]   = { { 660, 660, 55, 0, 0.40f }, { 440, 440, 80, 0, 0.40f } };
static const Note n_done[]   = { { 523, 523, 70, 0, 0.40f }, { 659, 659, 70, 0, 0.40f },
                                 { 784, 784, 70, 0, 0.40f }, { 1047, 1047, 160, 0, 0.40f } };
static const Note n_err[]    = { { 190, 150, 110, 40, 0.45f }, { 190, 130, 160, 0, 0.45f } };
static const Note n_start[]  = { { 523, 523, 90, 0, 0.30f }, { 784, 784, 90, 0, 0.30f },
                                 { 1047, 1047, 90, 0, 0.30f }, { 1319, 1319, 220, 0, 0.30f } };

typedef struct { const Note *n; int cnt; } Def;
static const Def defs[SND_COUNT] = {
    { n_move, 1 }, { n_select, 1 }, { n_tab, 1 }, { n_ok, 2 },
    { n_back, 2 }, { n_done, 4 }, { n_err, 2 }, { n_start, 4 }
};

static int def_ms(int id)              /* duracao do efeito, sem o silencio final */
{
    int i, ms = 0;
    for (i = 0; i < defs[id].cnt; i++) ms += defs[id].n[i].ms + defs[id].n[i].gap;
    return ms;
}

static int max_bytes(int r, int c)     /* maior efeito neste formato */
{
    int id, m = 0;
    for (id = 0; id < SND_COUNT; id++) {
        int b = (def_ms(id) + TAIL_MS) * r / 1000 * c * 2;
        if (b > m) m = b;
    }
    return m;
}

static int synth(int id)
{
    const Note *n = defs[id].n;
    int cnt = defs[id].cnt;
    int frames = 0, i, k, c;
    short *p;

    frames = (def_ms(id) + TAIL_MS) * rate / 1000;
    p = (short *)calloc((size_t)frames * chn, sizeof(short));
    if (!p) return 0;

    frames = 0;
    for (i = 0; i < cnt; i++) {
        int ns = n[i].ms * rate / 1000, gs = n[i].gap * rate / 1000;
        int atk = rate / 300;                            /* ~3 ms */
        int rel = rate / 150;                            /* ~6 ms: fecha a nota a zero (evita estalo) */
        float ph = 0.0f;
        if (atk < 1) atk = 1;
        if (rel < 1) rel = 1;
        if (rel > ns / 2) rel = ns / 2;
        for (k = 0; k < ns; k++) {
            float t = (float)k / (float)ns;
            float f = n[i].f1 + (n[i].f2 - n[i].f1) * t;
            float att = (k < atk) ? (float)k / (float)atk : 1.0f;
            float fade = (k >= ns - rel) ? (float)(ns - 1 - k) / (float)rel : 1.0f;
            float env = att * fade * expf(-3.2f * t);
            float s;
            short v;
            ph += 6.2831853f * f / (float)rate;
            if (ph > 6.2831853f) ph -= 6.2831853f;
            s = (sinf(ph) + 0.25f * sinf(2.0f * ph)) * (1.0f / 1.25f);
            s *= env * n[i].vol * 0.55f;                 /* amplitude moderada: sem estourar */
            v = (short)(s * 32767.0f);
            for (c = 0; c < chn; c++) p[(frames + k) * chn + c] = v;
        }
        frames += ns + gs;
    }
    buf[id] = p;
    len[id] = (def_ms(id) + TAIL_MS) * rate / 1000 * chn * 2;
    holdFrames[id] = def_ms(id) * 60 / 1000 + 2;
    return 1;
}

void snd_init(void)
{
    static const int opt_rate[] = { 44100, 22050, 11025 };
    static const int opt_chn[]  = {     2,     1,     1 };
    struct audsrv_fmt_t fmt;
    int ok = 1, tries, o, cap, id, set = 0;

    /* o audsrv.irx pode demorar um instante para registrar o RPC depois de carregado */
    for (tries = 0; tries < 20; tries++) {
        if (audsrv_init() == 0) break;
        {   /* ~50 ms de espera */
            volatile int w;
            for (w = 0; w < 2000000; w++) ;
        }
    }
    if (tries == 20) return;

    /* capacidade do buffer (vazio): escolhe o melhor formato em que todo efeito cabe inteiro */
    fmt.freq = 44100; fmt.bits = 16; fmt.channels = 2;
    if (audsrv_set_format(&fmt) != 0) return;
    cap = audsrv_available();
    if (cap < 4096) cap = 16384;                         /* leitura estranha: assume 16 KB */

    for (o = 0; o < 3; o++) {
        fmt.freq = opt_rate[o]; fmt.bits = 16; fmt.channels = opt_chn[o];
        if (max_bytes(opt_rate[o], opt_chn[o]) > cap && o < 2) continue;   /* nao cabe: tenta menor */
        if (audsrv_set_format(&fmt) == 0) { rate = opt_rate[o]; chn = opt_chn[o]; set = 1; break; }
    }
    if (!set) return;
    audsrv_set_volume(MAX_VOLUME / 2 + MAX_VOLUME / 4);  /* 75%: evita saturar o amplificador */

    for (id = 0; id < SND_COUNT; id++) ok &= synth(id);
    ready = ok;
}

/* envia ao audsrv so o que cabe agora (nunca bloqueia) */
void snd_update(void)
{
    int avail, n, sent;
    if (!ready || cur < 0) return;

    if (curOff >= len[cur]) {                  /* tudo enviado: espera o efeito terminar de tocar */
        if (--hold <= 0) cur = -1;
        return;
    }

    avail = audsrv_available();
    if (avail <= 0) return;
    n = len[cur] - curOff;
    if (n > avail) n = avail;
    n &= ~3;                                   /* multiplo de 4: alinhado em qualquer formato */
    if (n <= 0) return;

    sent = audsrv_play_audio((const char *)buf[cur] + curOff, n);
    if (sent < 0) { cur = -1; return; }        /* erro: abandona o efeito */
    curOff += sent;
}

void snd_play(int id)
{
    if (!ready || !on || id < 0 || id >= SND_COUNT || !buf[id]) return;

    /* nao deixa um clique cortar um aviso/confirmacao que ainda esta tocando */
    if (cur >= 0 && prio[id] < curPrio) return;

    audsrv_stop_audio();                       /* o efeito novo corta o anterior (sem fila de cliques) */
    cur = id; curOff = 0; curPrio = prio[id]; hold = holdFrames[id];
    snd_update();
}

void snd_set(int v)
{
    on = v ? 1 : 0;
    if (!on && ready) { audsrv_stop_audio(); cur = -1; }
}

int snd_enabled(void) { return on; }
