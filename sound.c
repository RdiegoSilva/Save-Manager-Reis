/*
 * sound.c - efeitos (ondas senoidais com envelope, sintetizadas na inicializacao) e musica de fundo
 * (WAV de 8 ou 16 bits em loop), misturados num unico fluxo e enviados ao audsrv.
 *
 * Como funciona:
 *  - Formato de saida fixo: 22050 Hz, estereo, 16 bits. A cada quadro, snd_update() mistura musica e
 *    efeito atual e envia ao audsrv so o que falta para manter ~90 ms de audio na fila (nunca bloqueia).
 *  - O efeito novo entra por cima da musica (nao corta nada). Efeitos importantes nao sao cortados por
 *    cliques de navegacao (prioridade).
 *  - Todo efeito termina com silencio. Quando a musica/efeitos param, e enviado silencio suficiente para
 *    esvaziar a fila: assim o audsrv nunca repete um trecho antigo (som em loop/estourado).
 *  - A musica e lida inteira para a RAM (limite MUSIC_MAX_BYTES) e reamostrada na hora (interpolacao linear).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <audsrv.h>
#include "sound.h"

#define OUT_RATE        22050
#define TAIL_MS         120                    /* silencio no fim de cada efeito */
#define MIX_FRAMES      2048                   /* maximo de quadros de audio por envio */
#define MUSIC_MAX_BYTES (14 * 1024 * 1024)

static short *buf[SND_COUNT];                  /* efeitos: estereo, OUT_RATE */
static int    flen[SND_COUNT];                 /* tamanho em quadros (amostras por canal) */
static int    ready = 0, on = 1;
static int    bufCap = 16384, targetBytes = 8192, normTarget = 8192, tail = 0;

/* efeito em reproducao */
static int    cur = -1, curPos = 0, curPrio = 0;

/* prioridade: efeitos importantes nao sao cortados por cliques de navegacao */
static const int prio[SND_COUNT] = { 0, 0, 0, 1, 1, 2, 2, 2 };

/* musica */
static unsigned char *mus = NULL;               /* PCM 16 bits little-endian (heap ou embutido no ELF) */
static int    musHeap = 0;                     /* 1 = mus veio de malloc (pode dar free) */
static int    musBits = 16;                    /* 8 (sem sinal) ou 16 (com sinal) */
/* amostra numero i (intercalada por canal) como 16 bits com sinal */
#define S16(i) (musBits == 16 ? (int)(short)(mus[(i) * 2] | (mus[(i) * 2 + 1] << 8)) : (((int)mus[(i)] - 128) << 8))
static int    musFrames = 0, musCh = 2, musRate = 44100;
static int    musIdx = 0;
static unsigned musFrac = 0, musStep = 65536;  /* posicao 16.16 e passo por quadro de saida */
static int    musOn = 1, musVol = 6;           /* volume 0..10 */

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

static int synth(int id)
{
    const Note *n = defs[id].n;
    int cnt = defs[id].cnt;
    int frames = 0, i, k;
    short *p;

    frames = (def_ms(id) + TAIL_MS) * OUT_RATE / 1000;
    p = (short *)calloc((size_t)frames * 2, sizeof(short));
    if (!p) return 0;

    frames = 0;
    for (i = 0; i < cnt; i++) {
        int ns = n[i].ms * OUT_RATE / 1000, gs = n[i].gap * OUT_RATE / 1000;
        int atk = OUT_RATE / 300;                        /* ~3 ms */
        int rel = OUT_RATE / 150;                        /* ~6 ms: fecha a nota a zero (evita estalo) */
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
            ph += 6.2831853f * f / (float)OUT_RATE;
            if (ph > 6.2831853f) ph -= 6.2831853f;
            s = (sinf(ph) + 0.25f * sinf(2.0f * ph)) * (1.0f / 1.25f);
            s *= env * n[i].vol * 0.55f;                 /* amplitude moderada: sem estourar */
            v = (short)(s * 32767.0f);
            p[(frames + k) * 2]     = v;
            p[(frames + k) * 2 + 1] = v;
        }
        frames += ns + gs;
    }
    buf[id] = p;
    flen[id] = (def_ms(id) + TAIL_MS) * OUT_RATE / 1000;
    return 1;
}

void snd_init(void)
{
    struct audsrv_fmt_t fmt;
    int ok = 1, tries, id, cap;

    /* o audsrv.irx pode demorar um instante para registrar o RPC depois de carregado */
    for (tries = 0; tries < 20; tries++) {
        if (audsrv_init() == 0) break;
        {   /* ~50 ms de espera */
            volatile int w;
            for (w = 0; w < 2000000; w++) ;
        }
    }
    if (tries == 20) return;

    fmt.freq = OUT_RATE; fmt.bits = 16; fmt.channels = 2;
    if (audsrv_set_format(&fmt) != 0) return;
    cap = audsrv_available();                            /* buffer vazio: capacidade total */
    if (cap < 4096) cap = 16384;                         /* leitura estranha: assume 16 KB */
    bufCap = cap & ~3;
    targetBytes = bufCap / 2;                            /* ~90 ms de audio na fila (menos latencia nos efeitos) */
    if (targetBytes > 8192) targetBytes = 8192;
    if (targetBytes < 2048) targetBytes = 2048;
    normTarget = targetBytes;
    audsrv_set_volume(MAX_VOLUME / 2 + MAX_VOLUME / 4);  /* 75%: evita saturar o amplificador */

    for (id = 0; id < SND_COUNT; id++) ok &= synth(id);
    ready = ok;
}

static int clamp16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

/* mistura musica + efeito e envia ao audsrv so o que cabe agora (nunca bloqueia) */
void snd_update(void)
{
    static short mix[MIX_FRAMES * 2];
    int avail, queued, want, n, i, mact, eact, gain;

    if (!ready) return;
    mact = (musOn && mus && musFrames > 0 && musVol > 0);
    eact = (cur >= 0);
    if (!mact && !eact && tail <= 0) return;             /* nada tocando e fila ja silenciosa */

    avail = audsrv_available();
    if (avail <= 0) return;
    queued = bufCap - avail;
    if (queued < 0) queued = 0;
    want = targetBytes - queued;
    if (want < 512) return;                              /* fila ainda cheia o bastante */
    n = want >> 2;
    if (n > MIX_FRAMES) n = MIX_FRAMES;
    if ((n << 2) > avail) n = avail >> 2;
    if (n <= 0) return;

    gain = musVol * 180 / 10;                            /* ate ~70%: deixa folga para os efeitos */
    for (i = 0; i < n; i++) {
        int l = 0, r = 0;
        if (mact) {
            int i1 = musIdx + 1, c0, c1;
            if (i1 >= musFrames) i1 = 0;
            if (musCh == 2) {
                int a0 = S16(musIdx * 2), a1 = S16(i1 * 2), b0 = S16(musIdx * 2 + 1), b1 = S16(i1 * 2 + 1);
                c0 = a0 + (int)(((long long)(a1 - a0) * (long long)musFrac) >> 16);
                c1 = b0 + (int)(((long long)(b1 - b0) * (long long)musFrac) >> 16);
            } else {
                int a0 = S16(musIdx), a1 = S16(i1);
                c0 = c1 = a0 + (int)(((long long)(a1 - a0) * (long long)musFrac) >> 16);
            }
            l = (c0 * gain) >> 8;
            r = (c1 * gain) >> 8;
            musFrac += musStep;
            musIdx += (int)(musFrac >> 16);
            musFrac &= 0xFFFFu;
            while (musIdx >= musFrames) musIdx -= musFrames;      /* loop */
        }
        if (cur >= 0) {
            l += buf[cur][curPos * 2];
            r += buf[cur][curPos * 2 + 1];
            if (++curPos >= flen[cur]) cur = -1;
        }
        mix[i * 2]     = (short)clamp16(l);
        mix[i * 2 + 1] = (short)clamp16(r);
    }
    if (mact || eact) tail = bufCap / 4;                 /* depois de parar: silencio ate esvaziar a fila */
    else              tail -= n;

    if (audsrv_play_audio((const char *)mix, n << 2) < 0) { cur = -1; }
}

void snd_play(int id)
{
    if (!ready || !on || id < 0 || id >= SND_COUNT || !buf[id]) return;

    /* nao deixa um clique cortar um aviso/confirmacao que ainda esta tocando */
    if (cur >= 0 && prio[id] < curPrio) return;

    cur = id; curPos = 0; curPrio = prio[id];            /* o efeito novo substitui o anterior; a musica segue */
    snd_update();
}

void snd_set(int v)
{
    on = v ? 1 : 0;
    if (!on) cur = -1;
}

int snd_enabled(void) { return on; }

/* operacao longa (o quadro demora): enche mais a fila do audsrv para a musica nao falhar. 0 = volta ao normal */
void snd_boost(int b)
{
    if (!ready) return;
    targetBytes = b ? (bufCap - 1024) : normTarget;
    if (targetBytes < normTarget) targetBytes = normTarget;
}

/* ---------------- musica de fundo ---------------- */
static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }

/* troca a musica atual (libera a anterior se estava no heap) */
static void music_use(unsigned char *p, int heap, int ch, int bits, unsigned rate, unsigned dataLen)
{
    musFrames = 0;                                       /* para o mixer antes de trocar o buffer */
    if (mus && musHeap) free(mus);
    mus = p; musHeap = heap;
    musCh = ch; musRate = (int)rate; musBits = bits;
    musFrames = (int)(dataLen / (unsigned)((bits / 8) * ch));
    musIdx = 0; musFrac = 0;
    musStep = (unsigned)(((unsigned long long)rate << 16) / OUT_RATE);
    if (musStep == 0) musStep = 1;
}

/* musica embutida no ELF (nao copia): WAV PCM 8 ou 16 bits. 0 = ok, -2 = formato invalido */
int snd_music_embed(const unsigned char *d, unsigned size)
{
    unsigned pos = 12, dataOff = 0, dataLen = 0, rate = 0;
    int ch = 0, bits = 0, tag = 0, gotFmt = 0;

    if (!d || size < 44 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return -2;
    while (pos + 8 <= size) {
        unsigned csz = rd32(d + pos + 4);
        if (!memcmp(d + pos, "fmt ", 4) && csz >= 16 && pos + 8 + 16 <= size) {
            const unsigned char *f = d + pos + 8;
            tag = (int)rd16(f); ch = (int)rd16(f + 2); rate = rd32(f + 4); bits = (int)rd16(f + 14);
            gotFmt = 1;
        } else if (!memcmp(d + pos, "data", 4)) {
            dataOff = pos + 8;
            dataLen = csz;
            if (dataOff > size) return -2;
            if (dataLen > size - dataOff) dataLen = size - dataOff;
            break;
        }
        if (csz > size) break;
        pos += 8 + csz + (csz & 1);
    }
    if (!gotFmt || (tag != 1 && tag != 0xFFFE) || (bits != 16 && bits != 8) || (ch != 1 && ch != 2) || rate < 8000 || rate > 96000 || dataLen < 4) return -2;
    dataLen -= dataLen % (unsigned)((bits / 8) * ch);
    music_use((unsigned char *)d + dataOff, 0, ch, bits, rate, dataLen);
    return 0;
}

/* musica de um arquivo WAV (ex.: no pendrive): le inteiro para a RAM. Substitui a embutida. */
int snd_music_load(const char *path)
{
    int fd, ch = 0, bits = 0, tag = 0, gotFmt = 0;
    unsigned rate = 0, dataLen = 0;
    off_t size, pos, dataOff = 0;
    unsigned char h[16];
    unsigned char *p;
    unsigned got;

    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (size < 44 || read(fd, h, 12) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) { close(fd); return -2; }

    pos = 12;
    while (pos + 8 <= size) {
        unsigned csz;
        if (lseek(fd, pos, SEEK_SET) != pos || read(fd, h, 8) != 8) break;
        csz = rd32(h + 4);
        if (!memcmp(h, "fmt ", 4) && csz >= 16) {
            unsigned char f[16];
            if (read(fd, f, 16) != 16) break;
            tag = (int)rd16(f); ch = (int)rd16(f + 2); rate = rd32(f + 4); bits = (int)rd16(f + 14);
            gotFmt = 1;
        } else if (!memcmp(h, "data", 4)) {
            dataOff = pos + 8;
            dataLen = csz;
            if ((off_t)dataOff + (off_t)dataLen > size) dataLen = (unsigned)(size - dataOff);
            break;
        }
        pos += 8 + (off_t)csz + (off_t)(csz & 1);
    }
    if (!gotFmt || (tag != 1 && tag != 0xFFFE) || (bits != 16 && bits != 8) || (ch != 1 && ch != 2) || rate < 8000 || rate > 96000 || dataLen < 4) {
        close(fd);
        return -2;
    }
    if (dataLen > MUSIC_MAX_BYTES) { close(fd); return -3; }
    dataLen -= dataLen % (unsigned)((bits / 8) * ch);

    p = (unsigned char *)malloc(dataLen);
    if (!p) { close(fd); return -3; }
    if (lseek(fd, dataOff, SEEK_SET) != dataOff) { free(p); close(fd); return -2; }
    got = 0;
    while (got < dataLen) {
        unsigned n = dataLen - got;
        int r;
        if (n > 32768) n = 32768;
        r = read(fd, p + got, n);
        if (r <= 0) break;
        got += (unsigned)r;
    }
    close(fd);
    if (got < dataLen) { free(p); return -2; }
    music_use(p, 1, ch, bits, rate, dataLen);
    return 0;
}

int  snd_music_present(void)       { return mus != NULL && musFrames > 0; }
void snd_music_set(int v)          { musOn = v ? 1 : 0; }
int  snd_music_enabled(void)       { return musOn; }
void snd_music_volume_set(int v)   { musVol = v < 0 ? 0 : (v > 10 ? 10 : v); }
int  snd_music_volume(void)        { return musVol; }
