/*
 * ============================================================
 *   SAVE MANAGER REIS  -  homebrew para PlayStation 2
 *   Criado por Diego Reis
 * ------------------------------------------------------------
 *   - Lista os saves do Memory Card (slot 1 / slot 2) em um
 *     dashboard com cards organizados (icone, nome, tamanho,
 *     data e hora).
 *   - Exporta saves do Memory Card para pendrive / HD USB:
 *         mass:/SaveManagerReis/<PASTA_DO_SAVE>/<AAAA-MM-DD_HH-MM-SS>/
 *   - Importa backups do pendrive / HD USB para o Memory Card.
 *   - Converte direto: exporta/importa saves em .PSU e .MAX (alem de pasta).
 *
 *   Compilar com PS2SDK (veja README.md).
 * ============================================================
 */

#ifndef NEWLIB_PORT_AWARE
#define NEWLIB_PORT_AWARE   /* I/O de arquivos via POSIX; fileXio so para fileXioInit() */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
#include <libmc.h>
#include <libmtap.h>
#include <libpad.h>
#include <libcdvd.h>
#include <osd_config.h>
#include <gsKit.h>
#include <dmaKit.h>

#include "lzari.h"
#include "savefmt.h"
#include "vmcfs.h"
#include "sound.h"
#include "btnicons.h"

/* ---------------- IRX embutidos (gerados pelo Makefile) ---------------- */
extern unsigned char iomanX_irx[];       extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];      extern unsigned int size_fileXio_irx;
extern unsigned char usbd_irx[];         extern unsigned int size_usbd_irx;
extern unsigned char bdm_irx[];          extern unsigned int size_bdm_irx;
extern unsigned char bdmfs_fatfs_irx[];  extern unsigned int size_bdmfs_fatfs_irx;
extern unsigned char usbmass_bd_irx[];   extern unsigned int size_usbmass_bd_irx;
extern unsigned char freesd_irx[];       extern unsigned int size_freesd_irx;
extern unsigned char audsrv_irx[];       extern unsigned int size_audsrv_irx;
#ifdef EMBED_MUSIC
extern unsigned char musica_wav[];       extern unsigned int size_musica_wav;     /* musica embutida (musica.wav na pasta do projeto) */
#endif

/* ---------------- Constantes ---------------- */
#define APP_NAME      "SAVE MANAGER REIS"
#define APP_VERSION   "v2.0"
#define ROOT_DIR      "mass:/SaveManagerReis"
#define MAX_ENT       128
#define CHUNK         (32 * 1024)
#define MC_ATTR_DIR   0x0020
/* flags do mcOpen (valores do IOP, diferentes dos do newlib) */
#define MC_RDONLY     0x0001
#define MC_WRONLY     0x0002
#define MC_CREAT      0x0200
#define MC_TRUNC      0x0400
#define FW            16.0f           /* largura de um caractere na escala 1.0 (Spacing 0.62) */

enum { TAB_MC = 0, TAB_USB = 1, TAB_CONV = 2 };
enum { FMT_DIR = 0, FMT_PSU = 1, FMT_MAX = 2, FMT_CBS = 3, FMT_SPS = 4, FMT_XPS = 5, FMT_VMC = 6, FMT_ZIP = 7 };   /* 3..6 so de entrada; ZIP so de saida */
#define MAX_ARCH      (24u * 1024 * 1024)   /* limite do arquivo descomprimido de um .max */

/* ---------------- Cores ---------------- */
#define RGBA(r, g, b, a) GS_SETREG_RGBAQ((r), (g), (b), (a), 0x00)
#define C_BG_T    RGBA(16, 18, 42, 0x80)
#define C_BG_B    RGBA(7, 8, 20, 0x80)
#define C_BAR     RGBA(22, 25, 54, 0x80)
#define C_CARD    RGBA(30, 34, 68, 0x80)
#define C_CARD_S  RGBA(30, 78, 100, 0x80)
#define C_SHADOW  RGBA(0, 0, 0, 0x40)
#define C_ACC     RGBA(0, 200, 255, 0x80)
#define C_ACC2    RGBA(150, 90, 255, 0x80)
#define C_GREEN   RGBA(60, 220, 140, 0x80)
#define C_RED     RGBA(255, 90, 100, 0x80)
#define C_TXT     RGBA(235, 238, 255, 0x80)
#define C_MUTED   RGBA(140, 148, 190, 0x80)
#define C_DARK    RGBA(10, 12, 28, 0x80)
#define C_DIM     RGBA(0, 0, 0, 0x50)
#define C_BLUE   RGBA(90, 140, 255, 0x80)
#define C_PINK   RGBA(240, 120, 200, 0x80)

/* ---------------- Estruturas ---------------- */
typedef struct {
    char name[40];     /* pasta do save (ex.: BASLUS-20552GTA3) */
    char stamp[40];    /* backup: AAAA-MM-DD_HH-MM-SS (+ nome)   */
    char date[12];     /* dd/mm/aa                               */
    char time[8];      /* hh:mm                                  */
    u32  size;         /* bytes                                  */
    int  sel;          /* marcado                                */
    u8   kind;         /* FMT_DIR / PSU / MAX / CBS / SPS / XPS / VMC (aba USB) */
    char path[128];    /* caminho completo do backup (aba USB)   */
    char title[36];    /* nome do jogo (titulo do icon.sys), ou vazio */
    u8   titleDone;    /* ja tentou ler o titulo                 */
    u8   ps1;          /* save de PlayStation 1                  */
    u8   sizeDone;     /* Memory Card: tamanho ja calculado (calculo em segundo plano) */
} Entry;

typedef struct {
    int  busy;
    int  modal;
    char title[48];
    char msg[96];
    u32  done, total;
    char toast[96];
    int    toastT;
    int    toastErr;
    int    menu, mn, msel;          /* menu de opcoes (modal com lista) */
    const char *mitem[20];
    int    extraN;                  /* linhas extras no modal de confirmacao (0..3) */
    char   extra[3][96];
    u64    extraCol[3];
} UI;

/* ---------------- Globais ---------------- */
static GSGLOBAL *gsGlobal;
static GSFONTM  *font;
static UI        ui;

static Entry mcList[MAX_ENT],  usbList[MAX_ENT];
static int   mcCount, usbCount;
static int   tab = TAB_MC, mcSlot = 0, mcReady = 0, usbOk = 0, mcFreeKB = 0;
/* Memory Cards: sem multitap sao 2 (porta 1 e 2); com multitap cada porta tem 4 slots (1A..1D, 2A..2D).
 * mcSlot e o INDICE nesta lista (0..nCards-1). */
static int   cardPort[8] = { 0, 1 }, cardSlot[8] = { 0, 0 }, cardTap[8], nCards = 2;
/* cancelar operacoes longas: segure O (checado entre um save e outro) */
static int g_cancel = 0, g_multi = 0;
static int cancel_poll(void);

#define MP(i) cardPort[i]
#define MS(i) cardSlot[i]

/* lista completa (mcFull / usbFull) e vista filtrada (mcList / usbList) */
static int   sortMode = 1, filtReg = -1;                 /* ordem: 0 nome, 1 data, 2 tamanho, 3 regiao; filtro -1 = todas */
static char  searchStr[24];
static Entry mcFull[MAX_ENT], usbFull[MAX_ENT];
static int   mcFullN = 0, usbFullN = 0;
static const char *SORT_NAME[4] = { "Nome", "Data", "Tamanho", "Regiao" };
static const char *FILT_NAME[6] = { "US", "EU", "JP", "KR", "Outras", "PS1" };

static void card_name(int i, char *b, int n)
{
    if (cardTap[i]) snprintf(b, n, "%d%c", cardPort[i] + 1, 'A' + cardSlot[i]);
    else            snprintf(b, n, "%d", cardPort[i] + 1);
}
static int   cursorPos[4], topRow[4];
static int   listMode = 0;                            /* 0 = grade de cards (4 colunas), 1 = lista (1 por linha) */
#define COLS (listMode ? 1 : 4)
static int   splash = 0;                              /* 1 = tela de abertura com a logo */
static u32   frameNo;

static sceMcTblGetDir mctbl[MAX_ENT]  __attribute__((aligned(64)));
static sceMcTblGetDir subtbl[MAX_ENT] __attribute__((aligned(64)));
static u8   iobuf[CHUNK]              __attribute__((aligned(64)));
static char padbuf[256]               __attribute__((aligned(64)));

static int quitReq = 0;
static int expFmt = FMT_DIR, convFmt = FMT_PSU, mcRawN = 0, mcNoFmt = 0;
static const char *FMT_NAME[] = { "Pasta", "PSU", "MAX", "CBS", "SPS", "XPS", "VMC", "ZIP" };
/* proximo formato de exportacao no ciclo do R2: Pasta -> PSU -> MAX -> CBS -> ZIP */
static int next_exp_fmt(int f) { return f == FMT_DIR ? FMT_PSU : (f == FMT_PSU ? FMT_MAX : (f == FMT_MAX ? FMT_CBS : (f == FMT_CBS ? FMT_ZIP : FMT_DIR))); }

static u32 g_done, g_total;
static u32 pad_new, pad_old;
static int pad_hold;

/* ============================================================
 *  Relogio
 * ============================================================ */
typedef struct { int y, mo, d, h, mi, s; } Now;
#define BCD(x) ((((x) >> 4) & 0xF) * 10 + ((x) & 0xF))

/* Fuso configurado NO PS2 (minutos em relacao ao GMT, ja com horario de verao).
 * O relogio do PS2 e sempre de Toquio (540); o console aplica o fuso escolhido no menu dele. */
static int tzMin = 540;
static void tz_refresh(void)
{
    tzMin = configGetTimezone();
    if (configIsDaylightSavingEnabled()) tzMin += 60;
}

static long days_from_civil(int y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, int *y, int *m, int *d)
{
    long era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (int)(yoe + era * 400);
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (*m <= 2) (*y)++;
}

/* Soma "mins" minutos a uma data/hora */
static void shift_now(Now *n, long mins)
{
    long days = days_from_civil(n->y, n->mo, n->d);
    long secs = days * 86400L + n->h * 3600L + n->mi * 60L + n->s;
    secs += mins * 60L;
    days = secs / 86400L;
    secs = secs % 86400L;
    if (secs < 0) { secs += 86400L; days--; }
    civil_from_days(days, &n->y, &n->mo, &n->d);
    n->h  = (int)(secs / 3600);
    n->mi = (int)((secs % 3600) / 60);
    n->s  = (int)(secs % 60);
}

/* O relogio e os cartoes do PS2 guardam a hora de TOQUIO (UTC+9). Converte para o fuso local (e vice-versa). */
static void jst_to_local(Now *n) { shift_now(n, (long)(tzMin - 540)); }
static void local_to_jst(Now *n) { shift_now(n, -(long)(tzMin - 540)); }

static void get_now(Now *n)
{
    sceCdCLOCK c;
    memset(n, 0, sizeof(*n));
    tz_refresh();
    if (sceCdReadClock(&c) && c.stat == 0) {
        n->y  = 2000 + BCD(c.year);
        n->mo = BCD(c.month & 0x1F);
        n->d  = BCD(c.day);
        n->h  = BCD(c.hour);
        n->mi = BCD(c.minute);
        n->s  = BCD(c.second);
        jst_to_local(n);
    }
}

/* ============================================================
 *  Desenho
 * ============================================================ */
static Now clk;
static int clkT;
static int volEditing = 0, volVal = 6;                 /* seletor de volume da musica */
static int clkEditing = 0, clkSel = 0, clkEd[5];     /* editor: dia, mes, ano, hora, minuto */
/* entrada de texto (busca) */
static int  inEditing = 0, inLen = 0, inKr = 0, inKc = 0, inCaps = 1;    /* teclado na tela */
static char inBuf[40];
static const char *inTitle = "";
static const char *KB_ROW[4] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789_-." };
static const char *KB_ACT[6] = { "Aa", "ESPACO", "APAGAR", "LIMPAR", "OK", "CANCELAR" };
static const float KB_ACTW[6] = { 56, 96, 90, 80, 60, 94 };
static int kb_cols(int r) { return r < 3 ? 10 : (r == 3 ? 9 : 6); }
static char kb_char(int r, int c)                      /* caractere da tecla (respeita maiuscula/minuscula) */
{
    char ch = KB_ROW[r][c];
    if (!inCaps && ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
    return ch;
}
/* tela de detalhes do save */
typedef struct { char name[36]; u32 size; } DFile;
static DFile dtF[96];
static int   dtN = 0, dtScroll = 0, dtOpen = 0;
#define TUT_PAGES 3
static int   tutOpen = 0, tutPage = 0;      /* tutorial (menu START) */
static char  dtTitle[48], dtL[3][80], dtNote[80];
static int   dtReg = 4;
static int   dtSel = 0, dtCan = 0;                   /* arquivo selecionado; 1 = Memory Card (pode exportar/importar/editar) */
/* editor hex */
static int   hexOpen = 0, hexEditing = 0, hexDirty = 0;
static u8   *hexBuf = NULL, *hexOrig = NULL;
static u32   hexN = 0, hexCur = 0, hexTop = 0;
static u8    hexSaved = 0;
static char  hexName[40], hexSave[40];
static void hex_edit(const char *save, const DFile *f);
static int  dt_export_file(const char *save, const DFile *f);
static int  dt_import_file(const char *save);

/* ---- modo de video: a interface e desenhada numa tela virtual (largura x altura do gsGlobal).
 * No modo "TV de tubo" ela e encolhida para a area segura (as TVs de tubo cortam as bordas);
 * retangulos que encostam na borda da tela virtual (fundo, barras) continuam indo ate a borda real. ---- */
enum { VID_AUTO = 0, VID_CRT = 1, VID_HDMI = 2 };
static const char *VID_NAME[3] = { "Automatico", "TV de tubo (CRT)", "TV moderna (HDMI)" };
static int   vidMode = VID_AUTO;
static float vsc = 1.0f, vox = 0.0f, voy = 0.0f;
static int   vidOrigMode, vidOrigW, vidOrigH, vidOrigInter, vidOrigField;     /* modo detectado na abertura */
#define VX(x) (vox + (x) * vsc)
#define VY(y) (voy + (y) * vsc)

static void vbox(float x, float y, float w, float h, float *X0, float *Y0, float *X1, float *Y1)
{
    float rw = (float)gsGlobal->Width, rh = (float)gsGlobal->Height;
    *X0 = (x <= 0)      ? 0  : VX(x);
    *Y0 = (y <= 0)      ? 0  : VY(y);
    *X1 = (x + w >= rw) ? rw : VX(x + w);
    *Y1 = (y + h >= rh) ? rh : VY(y + h);
}

static void rect(float x, float y, float w, float h, u64 c)
{
    float X0, Y0, X1, Y1;
    vbox(x, y, w, h, &X0, &Y0, &X1, &Y1);
    gsKit_prim_sprite(gsGlobal, X0, Y0, X1, Y1, 1, c);
}

static void vgrad(float x, float y, float w, float h, u64 c1, u64 c2)
{
    float X0, Y0, X1, Y1;
    vbox(x, y, w, h, &X0, &Y0, &X1, &Y1);
    gsKit_prim_quad_gouraud(gsGlobal, X0, Y0, X1, Y0, X0, Y1, X1, Y1, 1, c1, c1, c2, c2);
}

static void hgrad(float x, float y, float w, float h, u64 c1, u64 c2)
{
    float X0, Y0, X1, Y1;
    vbox(x, y, w, h, &X0, &Y0, &X1, &Y1);
    gsKit_prim_quad_gouraud(gsGlobal, X0, Y0, X1, Y0, X0, Y1, X1, Y1, 1, c1, c2, c1, c2);
}

/* retangulo com cantos arredondados (raio r) e degrade vertical (c1 em cima, c2 embaixo) */
static int rr_inset(int dy, int r)
{
    float d = (float)(r - dy) - 0.5f, q = (float)(r * r) - d * d;
    return r - (int)(q > 0 ? sqrtf(q) : 0.0f);
}

static void rpanel(float x, float y, float w, float h, int r, u64 c1, u64 c2)
{
    int dy;
    if (h < 2 * r + 1) r = (int)(h / 2);
    for (dy = 0; dy < r; dy++) {
        float ins = (float)rr_inset(dy, r);
        rect(x + ins, y + dy, w - 2 * ins, 1, c1);
        rect(x + ins, y + h - 1 - dy, w - 2 * ins, 1, c2);
    }
    vgrad(x, y + r, w, h - 2 * r, c1, c2);
}

/* igual, com degrade horizontal */
static void rpanel_h(float x, float y, float w, float h, int r, u64 c1, u64 c2)
{
    int dy;
    if (h < 2 * r + 1) r = (int)(h / 2);
    for (dy = 0; dy < r; dy++) {
        float ins = (float)rr_inset(dy, r);
        hgrad(x + ins, y + dy, w - 2 * ins, 1, c1, c2);
        hgrad(x + ins, y + h - 1 - dy, w - 2 * ins, 1, c1, c2);
    }
    hgrad(x, y + r, w, h - 2 * r, c1, c2);
}


static void text(float x, float y, float scale, u64 col, const char *s)
{
    gsKit_fontm_print_scaled(gsGlobal, font, VX(x), VY(y), 1, scale * vsc, col, s);
}

static float tw(const char *s, float scale) { return (float)strlen(s) * FW * scale; }

static void ctext(float cx, float y, float scale, u64 col, const char *s)
{
    text(cx - tw(s, scale) / 2.0f, y, scale, col, s);
}

static void trunc_copy(char *dst, const char *src, int max)
{
    int n = (int)strlen(src);
    if (n <= max) { strcpy(dst, src); return; }
    memcpy(dst, src, max - 2);
    dst[max - 2] = '.'; dst[max - 1] = '.'; dst[max] = 0;
}

/* corta o texto para caber em maxw pixels */
static void fit(char *dst, const char *src, float maxw, float scale)
{
    int max = (int)(maxw / (FW * scale));
    if (max < 4) max = 4;
    if (max > 38) max = 38;
    trunc_copy(dst, src, max);
}

/* ---- formas simples feitas de sprites ---- */
static void fill_circle(float cx, float cy, int r, u64 c)
{
    int dy, hw;
    for (dy = -r; dy <= r; dy++) {
        hw = 0;
        while ((hw + 1) * (hw + 1) + dy * dy <= r * r) hw++;
        rect(cx - hw, cy + dy, 2 * hw + 1, 1, c);
    }
}

static void fill_tri(float cx, float cy, int r, u64 c)
{
    int i;
    for (i = 0; i <= 2 * r; i++)
        rect(cx - i / 2, cy - r + i, 2 * (i / 2) + 1, 1, c);
}

/* ---- icones dos botoes como textura (btnicons.c). Se a VRAM nao der, cai no desenho por codigo ---- */
static GSTEXTURE btnTex[4];
static int       btnReady = 0;                   /* 0 = ainda nao enviada ao GS (reenvia apos limpar a VRAM) */

static void btn_tex_init(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        memset(&btnTex[i], 0, sizeof(GSTEXTURE));
        btnTex[i].Width  = BTN_SZ;
        btnTex[i].Height = BTN_SZ;
        btnTex[i].PSM    = GS_PSM_CT32;
        btnTex[i].Filter = GS_FILTER_LINEAR;
        btnTex[i].Mem    = (void *)btn_rgba[i];
        btnTex[i].Vram   = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(BTN_SZ, BTN_SZ, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    }
    btnReady = 0;
}

/* botao do controle (18x18): kind 0=X 1=quadrado 2=triangulo 3=circulo */
static void pad_btn(float x, float y, int kind, u64 col)
{
    int i;
    if (kind >= 0 && kind < 4 && btnTex[kind].Vram != GSKIT_ALLOC_ERROR && btnTex[kind].Mem) {
        if (!btnReady) {
            FlushCache(0);
            for (i = 0; i < 4; i++)
                if (btnTex[i].Vram != GSKIT_ALLOC_ERROR) gsKit_texture_upload(gsGlobal, &btnTex[i]);
            btnReady = 1;
        }
        gsKit_prim_sprite_texture(gsGlobal, &btnTex[kind],
                                  VX(x - 1), VY(y - 1), 0.0f, 0.0f,
                                  VX(x + 19), VY(y + 19), (float)BTN_SZ, (float)BTN_SZ, 1,
                                  GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
        return;
    }
    fill_circle(x + 9, y + 9, 9, col);
    switch (kind) {
    case 0:
        for (i = -4; i <= 4; i++) {
            rect(x + 8 + i, y + 8 + i, 2, 2, C_DARK);
            rect(x + 8 + i, y + 8 - i, 2, 2, C_DARK);
        }
        break;
    case 1:
        rect(x + 5, y + 5, 8, 2, C_DARK);  rect(x + 5, y + 11, 8, 2, C_DARK);
        rect(x + 5, y + 5, 2, 8, C_DARK);  rect(x + 11, y + 5, 2, 8, C_DARK);
        break;
    case 2:
        fill_tri(x + 9, y + 9, 5, C_DARK);
        break;
    default:
        fill_circle(x + 9, y + 9, 5, C_DARK);
        fill_circle(x + 9, y + 9, 3, col);
        break;
    }
}

static float key_btn(float x, float y, int kind, u64 col, const char *label)
{
    pad_btn(x, y, kind, col);
    text(x + 24, y + 3, 0.45f, C_TXT, label);
    return x + 24 + tw(label, 0.45f) + 18;
}

static float key_pill(float x, float y, const char *keys, const char *label)
{
    float w = tw(keys, 0.38f) + 12;
    rpanel(x, y, w, 18, 4, RGBA(40, 45, 92, 0x80), RGBA(32, 36, 78, 0x80));
    text(x + 6, y + 4, 0.38f, C_ACC, keys);
    text(x + w + 6, y + 3, 0.45f, C_TXT, label);
    return x + w + 6 + tw(label, 0.45f) + 14;
}

/* linha do tutorial: tecla em "pilula" na coluna da esquerda e a explicacao a partir de bx+120 */
static void tut_key(float bx, float y, const char *keys, const char *label)
{
    float w = tw(keys, 0.38f) + 12;
    rect(bx + 22, y, w, 18, C_CARD);
    text(bx + 28, y + 4, 0.38f, C_ACC, keys);
    text(bx + 120, y + 3, 0.44f, C_TXT, label);
}

/* ---- regiao do jogo a partir do nome da pasta ---- */
static int region_of(const char *n)
{
    if (n[0] == 'B') {
        if (n[1] == 'A') return 0;   /* BASLUS / BASCUS -> NTSC-U */
        if (n[1] == 'E') return 1;   /* BESLES / BESCES -> PAL    */
        if (n[1] == 'I') return 2;   /* BISLPS / BISCPS -> NTSC-J */
        if (n[1] == 'K') return 3;   /* BKSLKS          -> NTSC-K */
    }
    return 4;
}
#define REG_PS1 5
static const char *REG_TAG[] = { "US", "EU", "JP", "KR", "?", "PS1" };
/* regiao exibida: saves de PS1 aparecem como "PS1" */
static int ent_region(const Entry *e) { return e->ps1 ? REG_PS1 : region_of(e->name); }

static u64 reg_col(int r)
{
    switch (r) {
        case 0:  return RGBA(40, 110, 255, 0x80);
        case 1:  return RGBA(40, 190, 120, 0x80);
        case 2:  return RGBA(240, 70, 100, 0x80);
        case 3:  return RGBA(240, 170, 40, 0x80);
        case 5:  return RGBA(150, 90, 230, 0x80);
        default: return RGBA(120, 120, 160, 0x80);
    }
}

/* ============================================================
 *  Icones 3D dos saves (icon.sys + arquivo .ico) -> miniaturas
 * ============================================================ */
#define ICON_SZ     64
#define ICON_SLOTS  17            /* 0..15: cards da grade; 16: icone da tela de detalhes */
#define ICON_DET    16
#define ICON_ZSIGN  (-1.0f)   /* sentido do eixo Z do .ico em relacao a camera (use 1.0f se aparecer de costas) */
#define ICON_FLIPY  1.0f      /* use -1.0f se aparecer de cabeca para baixo */
#define ICON_CA0    0.8776f   /* cos(0.5 rad) - angulo inicial */
#define ICON_SA0    0.4794f   /* sin(0.5 rad) */

typedef struct {
    int    n;                 /* vertices (multiplo de 3) */
    float *pos, *nrm, *uv;    /* normalizados */
    u8    *col;               /* RGBA por vertice */
    u16   *tex;               /* 128x128 ABGR1555 (ou NULL) */
    u32   *thumb;             /* miniatura ICON_SZ x ICON_SZ RGBA8888 */
    u32    ver;               /* muda a cada render (para reenviar a textura) */
    u64    bg[4];             /* cores de fundo do icon.sys (4 cantos) */
} Icon;

static Icon     *icons[2][MAX_ENT];
static u8        iconFail[2][MAX_ENT];
static int       gridRows = 3;
static int       spinTab = -1, spinIdx = -1;
static const Icon *dtIcon = NULL;                     /* icone 3D do save na tela de detalhes (NULL = icone da regiao) */
static float     gCos = ICON_CA0, gSin = ICON_SA0;
static GSTEXTURE iconTex[ICON_SLOTS];
static const Icon *slotOwner[ICON_SLOTS];
static u32       slotVer[ICON_SLOTS];

static void icon_free_all(int t);
static void sync_back(Entry *full, int fn, const Entry *view, int vn);

static void icon_slots_init(void)
{
    int i;
    for (i = 0; i < ICON_SLOTS; i++) {
        memset(&iconTex[i], 0, sizeof(GSTEXTURE));
        iconTex[i].Width  = ICON_SZ;
        iconTex[i].Height = ICON_SZ;
        iconTex[i].PSM    = GS_PSM_CT32;
        iconTex[i].Filter = GS_FILTER_LINEAR;
        iconTex[i].Vram   = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(ICON_SZ, ICON_SZ, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    }
}

/* desenha a miniatura (envia a textura ao GS so quando mudou) sobre o fundo do icon.sys */
static void draw_icon(int slot, const Icon *ic, float x, float y, float sz)
{
    if (slot < 0 || slot >= ICON_SLOTS) return;
    if (iconTex[slot].Vram == GSKIT_ALLOC_ERROR) return;
    if (slotOwner[slot] != ic || slotVer[slot] != ic->ver) {
        iconTex[slot].Mem = ic->thumb;
        FlushCache(0);
        gsKit_texture_upload(gsGlobal, &iconTex[slot]);
        slotOwner[slot] = ic;
        slotVer[slot] = ic->ver;
    }
    {
        float X0 = VX(x), Y0 = VY(y), X1 = VX(x + sz), Y1 = VY(y + sz);
        gsKit_prim_quad_gouraud(gsGlobal, X0, Y0, X1, Y0, X0, Y1, X1, Y1, 1,
                                ic->bg[0], ic->bg[1], ic->bg[2], ic->bg[3]);
        gsKit_prim_sprite_texture(gsGlobal, &iconTex[slot], X0, Y0, 0.0f, 0.0f, X1, Y1,
                                  (float)ICON_SZ, (float)ICON_SZ, 1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
    }
}

/* ---- aplica o modo de video em execucao (mesmo caminho que o driver SDL do PS2 usa) ---- */
static void video_apply(int mode)
{
    int i;
    if (mode == VID_HDMI) {                          /* 480p progressivo */
        gsGlobal->Mode      = GS_MODE_DTV_480P;
        gsGlobal->Interlace = GS_NONINTERLACED;
        gsGlobal->Field     = GS_FRAME;
        gsGlobal->Width     = 640;
        gsGlobal->Height    = 480;
    } else {                                         /* automatico e TV de tubo: modo detectado na abertura */
        gsGlobal->Mode      = vidOrigMode;
        gsGlobal->Interlace = vidOrigInter;
        gsGlobal->Field     = vidOrigField;
        gsGlobal->Width     = vidOrigW;
        gsGlobal->Height    = vidOrigH;
    }
    gsKit_vram_clear(gsGlobal);
    gsKit_init_screen(gsGlobal);
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);
    gsKit_fontm_upload(gsGlobal, font);              /* a VRAM foi limpa: reenvia a fonte e os slots de icone */
    font->Spacing = 0.62f;                           /* o upload zera o espacamento: refaz, como na abertura */
    icon_slots_init();
    btn_tex_init();
    for (i = 0; i < ICON_SLOTS; i++) { slotOwner[i] = NULL; slotVer[i] = 0; }
    if (mode == VID_CRT) {
        vsc = 0.90f;                                 /* area segura: 90% da tela */
        vox = (float)gsGlobal->Width  * (1.0f - vsc) / 2.0f;
        voy = (float)gsGlobal->Height * (1.0f - vsc) / 2.0f;
    } else {
        vsc = 1.0f; vox = 0.0f; voy = 0.0f;
    }
    vidMode = mode;
    gsKit_clear(gsGlobal, RGBA(0, 0, 0, 0x80));
}

static void fmt_size(char *b, int n, u32 sz)
{
    if (sz >= 1024 * 1024) snprintf(b, n, "%u.%u MB", sz >> 20, ((sz & 0xFFFFF) * 10) >> 20);
    else                   snprintf(b, n, "%u KB", (sz + 1023) >> 10);
}

static void fmt_cnt(char *b, int shown, int total)
{
    if (shown != total) snprintf(b, 32, "%d/%d", shown, total);
    else                snprintf(b, 32, "%d", shown);
}

/* ---- acabamento visual: sombras suaves, texto com sombra, paineis com degrade e cantos cortados, janelas ---- */
static void soft_shadow(float x, float y, float w, float h)
{
    rect(x + 2, y + 3, w, h, RGBA(0, 0, 0, 0x2A));
    rect(x + 4, y + 6, w, h, RGBA(0, 0, 0, 0x16));
}

static void text_sh(float x, float y, float scale, u64 col, const char *s)
{
    text(x + 1, y + 1, scale, RGBA(0, 0, 0, 0x70), s);
    text(x, y, scale, col, s);
}

/* painel com degrade vertical e cantos "chanfrados" (cantos arredondados baratos para o GS) */
static void panel(float x, float y, float w, float h, u64 c1, u64 c2)
{
    rect(x + 3, y, w - 6, 3, c1);
    rect(x + 1, y + 1, 2, 2, c1);
    vgrad(x, y + 3, w, h - 6, c1, c2);
    rect(x + 3, y + h - 3, w - 6, 3, c2);
    rect(x + 1, y + h - 3, 2, 2, c2);
}

/* janela modal: escurece o fundo, sombra, corpo em degrade, borda fina e barra de acento com brilho */
static void window(float x, float y, float w, float h)
{
    u64 edge = RGBA(90, 100, 190, 0x60);
    rect(0, 0, (float)gsGlobal->Width, (float)gsGlobal->Height, C_DIM);
    soft_shadow(x, y, w, h);
    vgrad(x, y, w, h, RGBA(32, 36, 76, 0x80), RGBA(19, 22, 48, 0x80));
    rect(x, y + 4, 1, h - 4, edge);
    rect(x + w - 1, y + 4, 1, h - 4, edge);
    rect(x, y + h - 1, w, 1, edge);
    hgrad(x, y, w, 4, C_ACC, C_ACC2);
    vgrad(x, y + 4, w, 22, RGBA(0, 160, 255, 0x1E), RGBA(0, 160, 255, 0x00));
}

static void draw_card(float x, float y, float w, float h, const Entry *e, int isCur, int isUsb, const Icon *ic, int slot)
{
    char buf[48];
    int r = ent_region(e);

    soft_shadow(x, y, w, h);
    if (isCur) {
        int p = (int)(frameNo % 60), a = p < 30 ? p : 60 - p, k;
        for (k = 4; k >= 1; k--)                           /* brilho pulsante em volta do card selecionado */
            rect(x - 3 - k * 2, y - 3 - k * 2, w + 6 + k * 4, h + 6 + k * 4, RGBA(0, 170, 255, (u8)((5 + (5 - k) * 4) * (60 + a) / 90)));
        rect(x - 3, y - 3, w + 6, h + 6, RGBA(0, 140 + a * 4, 255, 0x80));
    }
    panel(x, y, w, h, e->sel ? RGBA(32, 96, 116, 0x80) : RGBA(41, 46, 88, 0x80),
                      e->sel ? RGBA(22, 66, 84, 0x80)  : RGBA(26, 29, 59, 0x80));
    rect(x + 2, y, w - 4, 3, isUsb ? C_ACC2 : reg_col(r));
    rect(x + 4, y + 3, w - 8, 1, RGBA(255, 255, 255, 0x18));        /* reflexo fino no topo do card */

    /* icone original do save (3D) ou, se nao houver, o icone da regiao */
    rect(x + 6, y + 9, 46, 46, RGBA(0, 0, 0, 0x3C));                  /* moldura do icone */
    if (ic) {
        draw_icon(slot, ic, x + 7, y + 10, 44);
    } else {
        vgrad(x + 8, y + 11, 42, 42, reg_col(r), RGBA(20, 22, 50, 0x80));
        fill_circle(x + 29, y + 32, 17, RGBA(255, 255, 255, 0x22));
        ctext(x + 29, y + 22, 0.8f, C_TXT, REG_TAG[r]);
    }

    /* tamanho / data / hora */
    if (!isUsb && !e->sizeDone) snprintf(buf, sizeof(buf), "..."); else fmt_size(buf, sizeof(buf), e->size);
    text_sh(x + 58, y + 10, 0.50f, C_ACC, buf);
    text(x + 58, y + 26, 0.45f, C_TXT, e->date);
    text(x + 58, y + 40, 0.45f, C_MUTED, e->time);
    if (isUsb && e->kind != FMT_DIR) {
        const char *tg = FMT_NAME[e->kind];
        text(x + w - 8 - tw(tg, 0.45f), y + 40, 0.45f, C_ACC2, tg);
    }

    /* nome */
    if (e->title[0]) {                        /* nome do jogo em cima, pasta embaixo */
        fit(buf, e->title, w - 16, 0.40f);
        text_sh(x + 8, y + 54, 0.40f, C_TXT, buf);
        fit(buf, e->name, w - 16, 0.32f);
        text(x + 8, y + 67, 0.32f, C_MUTED, buf);
    } else {
        fit(buf, e->name, w - 16, 0.50f);
        text_sh(x + 8, y + 58, 0.50f, C_TXT, buf);
    }

    if (e->sel) {
        rect(x + w - 22, y + 8, 16, 16, C_GREEN);
        ctext(x + w - 14, y + 11, 0.38f, C_DARK, "OK");
    }
}

static void row_geom(float x, float w, float *dateR, float *sizeR, float *fmtR, float *chipX)
{
    *dateR = x + w - 10;
    *sizeR = *dateR - 150;
    *fmtR  = *sizeR - 56;
    *chipX = *fmtR - 62;
}

/* uma linha da visao em lista: icone pequeno, nome do jogo / pasta, formato, tamanho e data */
static void draw_row(float x, float y, float w, float h, const Entry *e, int isCur, int isUsb, const Icon *ic, int slot)
{
    char buf[96], dt[48], sz[24];
    int r = ent_region(e);
    float rx = x + w - 10;

    if (isCur) {
        int p = (int)(frameNo % 60), a = p < 30 ? p : 60 - p, k;
        for (k = 3; k >= 1; k--)                                         /* brilho em volta da linha selecionada */
            rpanel(x - 2 - k * 2, y - 2 - k * 2, w + 4 + k * 4, h + 4 + k * 4, 8 + k, RGBA(0, 170, 255, (u8)(6 + (4 - k) * 4)), RGBA(0, 170, 255, (u8)(6 + (4 - k) * 4)));
        rpanel(x - 2, y - 2, w + 4, h + 4, 9, RGBA(0, 140 + a * 4, 255, 0x80), RGBA(0, 140 + a * 4, 255, 0x80));
    }
    rpanel(x, y, w, h, 7, e->sel ? RGBA(32, 96, 116, 0x80) : (isCur ? RGBA(42, 58, 120, 0x80) : RGBA(38, 43, 94, 0x80)),
                           e->sel ? RGBA(22, 66, 84, 0x80)  : (isCur ? RGBA(29, 42, 94, 0x80)  : RGBA(26, 29, 68, 0x80)));
    rect(x + 1, y + 6, 3, h - 12, isUsb ? C_ACC2 : reg_col(r));

    rpanel(x + 6, y + 2, h - 4, h - 4, 4, RGBA(0, 0, 0, 0x3C), RGBA(0, 0, 0, 0x3C));   /* icone */
    if (ic) {
        draw_icon(slot, ic, x + 7, y + 3, h - 6);
    } else {
        vgrad(x + 7, y + 3, h - 6, h - 6, reg_col(r), RGBA(20, 22, 50, 0x80));
        ctext(x + 7 + (h - 6) / 2, y + 7, 0.5f, C_TXT, REG_TAG[r]);
    }
    if (e->sel) {
        rect(x + h - 8, y + h - 14, 16, 12, C_GREEN);
        ctext(x + h, y + h - 13, 0.30f, C_DARK, "OK");
    }

    {
        float dateR, sizeR, fmtR, chipX;
        row_geom(x, w, &dateR, &sizeR, &fmtR, &chipX);
        snprintf(dt, sizeof(dt), "%s %s", e->date, e->time);
        if (!isUsb && !e->sizeDone) snprintf(sz, sizeof(sz), "..."); else fmt_size(sz, sizeof(sz), e->size);
        text(dateR - tw(dt, 0.42f), y + 8, 0.42f, C_MUTED, dt);
        text(sizeR - tw(sz, 0.46f), y + 8, 0.46f, C_ACC, sz);
        if (isUsb && e->kind != FMT_DIR) {
            const char *tg = FMT_NAME[e->kind];
            text(fmtR - tw(tg, 0.42f), y + 8, 0.42f, C_ACC2, tg);
        }
        rpanel(chipX, y + 7, 32, 16, 5, reg_col(r), reg_col(r));                            /* etiqueta da regiao */
        ctext(chipX + 16, y + 10, 0.34f, C_DARK, REG_TAG[r]);
        rx = chipX - 8;
    }
    {
        float nx = x + h + 8, mw = rx - nx;
        if (e->title[0]) {
            fit(buf, e->title, mw, 0.46f);
            text_sh(nx, y + 3, 0.46f, C_TXT, buf);
            fit(buf, e->name, mw, 0.30f);
            text(nx, y + 17, 0.30f, C_MUTED, buf);
        } else {
            fit(buf, e->name, mw, 0.48f);
            text_sh(nx, y + 8, 0.48f, C_TXT, buf);
        }
    }
}

/* ---- logo: Memory Card com uma coroa (desenhada com sprites, qualquer tamanho) ----
 * x,y = canto superior esquerdo; h = altura em pixels (a largura e 0.8 * h). */
static u64 logo_col(float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return RGBA((int)(150.0f * t), (int)(200.0f - 110.0f * t), 255, 0x80);
}

static void draw_logo(float x, float y, float h)
{
    const float u = h / 300.0f;
    int py, rows = (int)h, i, rd = (int)(9.0f * u + 0.5f);
    for (py = 0; py < rows; py++) {
        float yy = ((float)py + 0.5f) / u, xr, xi, w;
        u64 c = logo_col(yy / 300.0f);
        float ry = y + (float)py;
        xr = yy < 72 ? 168 + yy : 240;
        rect(x, ry, xr * u, 1, c);                                         /* borda (degrade) */
        if (yy > 9 && yy < 291) {
            xi = yy < 72 ? 168 + yy - 14 : 231;
            rect(x + 9 * u, ry, (xi - 9) * u, 1, RGBA(18, 22, 56, 0x80));  /* miolo escuro */
        }
        if (yy >= 98 && yy < 142) { w = (yy - 98) * 30.0f / 44.0f; rect(x + (120 - w) * u, ry, 2 * w * u, 1, c); }
        if (yy >= 112 && yy < 142) {
            w = (yy - 112) * 32.0f / 30.0f;
            rect(x + 58 * u, ry, w * u, 1, c);
            rect(x + (182 - w) * u, ry, w * u, 1, c);
        }
        if (yy >= 142 && yy < 176) rect(x + 58 * u, ry, 124 * u, 1, c);     /* corpo da coroa */
        if (yy >= 184 && yy < 200) rect(x + 58 * u, ry, 124 * u, 1, c);     /* faixa da base */
        if (yy >= 236 && yy < 274)
            for (i = 0; i < 5; i++) rect(x + (44 + 32 * i) * u, ry, 20 * u, 1, c);   /* contatos */
    }
    if (rd < 1) rd = 1;
    fill_circle(x + 58 * u, y + 106 * u, rd, RGBA(255, 255, 255, 0x80));
    fill_circle(x + 120 * u, y + 90 * u, rd + (h > 60 ? 1 : 0), RGBA(255, 255, 255, 0x80));
    fill_circle(x + 182 * u, y + 106 * u, rd, RGBA(255, 255, 255, 0x80));
}

static void render(void)
{
    const float W = (float)gsGlobal->Width, H = (float)gsGlobal->Height;
    const float M = 24.0f;
    char buf[96], buf2[48];
    Entry *L = (tab == TAB_MC) ? mcList : usbList;
    int N = (tab == TAB_MC) ? mcCount : usbCount;
    int i, marked = 0;
    float x;

    if (clkT-- <= 0) { get_now(&clk); clkT = 30; }

    gsKit_clear(gsGlobal, RGBA(7, 8, 20, 0x80));
    vgrad(0, 0, W, H, C_BG_T, C_BG_B);
    hgrad(W * 0.45f, 60, W * 0.55f, H - 120, RGBA(100, 60, 220, 0x00), RGBA(100, 60, 220, 0x18));   /* brilho roxo a direita */
    hgrad(0, 60, W * 0.55f, H - 120, RGBA(0, 130, 230, 0x14), RGBA(0, 130, 230, 0x00));            /* brilho azul a esquerda */

    /* ---- cabecalho ---- */
    vgrad(0, 0, W, 54, RGBA(36, 40, 84, 0x80), RGBA(20, 22, 48, 0x80));
    hgrad(0, 54, W, 3, C_ACC, C_ACC2);
    vgrad(0, 57, W, 12, RGBA(100, 80, 255, 0x26), RGBA(100, 80, 255, 0x00));
    draw_logo(M + 2, 9, 38);
    text_sh(M + 46, 7, 0.78f, C_TXT, "SAVE MANAGER");
    text_sh(M + 46 + tw("SAVE MANAGER ", 0.78f), 7, 0.78f, C_ACC, "REIS");
    text(M + 48, 33, 0.40f, C_MUTED, "Gerenciador de saves  " APP_VERSION "  -  por Diego Reis");
    if (clk.y) {
        snprintf(buf, sizeof(buf), "%02d:%02d", clk.h, clk.mi);
        {
            int az = tzMin < 0 ? -tzMin : tzMin;
            char zs[12];
            if (az % 60) snprintf(zs, sizeof(zs), "%c%d:%02d", tzMin < 0 ? '-' : '+', az / 60, az % 60);
            else         snprintf(zs, sizeof(zs), "%c%d", tzMin < 0 ? '-' : '+', az / 60);
            snprintf(buf2, sizeof(buf2), "%02d/%02d/%04d  GMT%s", clk.d, clk.mo, clk.y, zs);
        }
    } else {
        snprintf(buf, sizeof(buf), "--:--");
        snprintf(buf2, sizeof(buf2), "--/--/----");
    }
    text_sh(W - M - tw(buf, 0.85f), 5, 0.85f, C_ACC, buf);
    text(W - M - tw(buf2, 0.40f), 34, 0.40f, C_MUTED, buf2);

    /* ---- abas ---- */
    {
        static const char *names[3] = { "MEMORY CARD", "USB / HD", "CONVERTER" };
        x = M;
        for (i = 0; i < 3; i++) {
            float w = tw(names[i], 0.55f) + 26;
            if (i == tab) {
                rpanel(x - 3, 61, w + 6, 30, 9, RGBA(0, 200, 255, 0x14), RGBA(0, 200, 255, 0x14));     /* brilho */
                rpanel_h(x, 64, w, 24, 7, C_ACC, C_ACC2);
                text(x + 13, 69, 0.55f, C_DARK, names[i]);
            } else {
                rpanel(x, 64, w, 24, 7, RGBA(40, 45, 92, 0x80), RGBA(32, 36, 78, 0x80));
                text(x + 13, 69, 0.55f, C_MUTED, names[i]);
            }
            x += w + 8;
        }
    }
    if (tab == TAB_MC) {
        if (nCards <= 2) {
            float bx = W - M - 60;
            text(bx - 8 - tw("SLOT", 0.45f), 70, 0.45f, C_MUTED, "SLOT");
            for (i = 0; i < 2; i++) {
                float sx = bx + i * 32;
                if (i == mcSlot) { rpanel_h(sx, 64, 28, 24, 6, C_ACC, C_ACC2);                                  ctext(sx + 14, 69, 0.55f, C_DARK, i ? "2" : "1"); }
                else             { rpanel(sx, 64, 28, 24, 6, RGBA(40, 45, 92, 0x80), RGBA(32, 36, 78, 0x80));   ctext(sx + 14, 69, 0.55f, C_MUTED, i ? "2" : "1"); }
            }
        } else {                                   /* multitap: mostra "1B  (2/8)" */
            char nm[8], lb[32];
            float w, sx;
            card_name(mcSlot, nm, sizeof(nm));
            snprintf(lb, sizeof(lb), "%s  (%d/%d)", nm, mcSlot + 1, nCards);
            w = tw(lb, 0.55f) + 22;
            sx = W - M - w;
            text(sx - 8 - tw("SLOT", 0.45f), 70, 0.45f, C_MUTED, "SLOT");
            rpanel_h(sx, 64, w, 24, 6, C_ACC, C_ACC2);
            ctext(sx + w / 2, 69, 0.55f, C_DARK, lb);
        }
    } else if (tab == TAB_CONV) {
        char cs[40];
        snprintf(cs, sizeof(cs), "DESTINO: %s", FMT_NAME[convFmt]);
        text(W - M - tw(cs, 0.45f), 70, 0.45f, C_ACC2, cs);
    } else if (tab == TAB_USB) {
        const char *s = usbOk ? "USB CONECTADO" : "SEM USB";
        float w = tw(s, 0.45f);
        fill_circle(W - M - w - 12, 76, 4, usbOk ? C_GREEN : C_RED);
        text(W - M - w, 70, 0.45f, usbOk ? C_GREEN : C_RED, s);
    }

    for (i = 0; i < N; i++) marked += L[i].sel;

    /* ---- quadros de resumo ---- */
    {
        const float tgap = 10, tile = (W - 2 * M - 2 * tgap) / 3.0f;
        const char *lab[3];
        char val[3][32];
        if (tab == TAB_MC) {
            lab[0] = "SAVES"; lab[1] = "MARCADOS"; lab[2] = "LIVRE";
            fmt_cnt(val[0], mcCount, mcFullN);
            snprintf(val[1], 32, "%d", marked);
            if (mcReady) snprintf(val[2], 32, "%d.%d MB", mcFreeKB / 1024, (mcFreeKB % 1024) * 10 / 1024);
            else         snprintf(val[2], 32, "--");
        } else if (tab == TAB_CONV) {
            lab[0] = "BACKUPS"; lab[1] = "MARCADOS"; lab[2] = "DESTINO";
            fmt_cnt(val[0], usbCount, usbFullN);
            snprintf(val[1], 32, "%d", marked);
            snprintf(val[2], 32, "%s", FMT_NAME[convFmt]);
        } else {
            lab[0] = "BACKUPS"; lab[1] = "MARCADOS"; lab[2] = "PASTA";
            fmt_cnt(val[0], usbCount, usbFullN);
            snprintf(val[1], 32, "%d", marked);
            snprintf(val[2], 32, "SaveManagerReis");
        }
        for (i = 0; i < 3; i++) {
            float tx = M + i * (tile + tgap);
            rpanel(tx, 96, tile, 26, 7, RGBA(40, 45, 92, 0x80), RGBA(28, 32, 74, 0x80));
            rect(tx + 1, 101, 3, 16, i == 1 ? C_GREEN : C_ACC);
            text(tx + 11, 103, 0.40f, C_MUTED, lab[i]);
            text(tx + tile - 8 - tw(val[i], 0.50f), 102, 0.50f,
                 (tab == TAB_MC && i == 2 && mcReady && mcFreeKB < 256) ? C_RED :
                 (tab == TAB_MC && i == 2 && mcReady && mcFreeKB < 1024) ? RGBA(255, 200, 70, 0x80) : C_TXT, val[i]);
        }
    }

    /* ---- grade de cards ---- */
    if (N == 0) {
        soft_shadow(M, 134, W - 2 * M, 120);
        panel(M, 134, W - 2 * M, 120, RGBA(40, 45, 86, 0x80), RGBA(24, 27, 56, 0x80));
        hgrad(M, 134, W - 2 * M, 3, C_ACC, C_ACC2);
        rect(M + 18, 160, 44, 44, RGBA(0, 0, 0, 0x40));                      /* icone grande: cartao vazio */
        rect(M + 22, 164, 36, 36, RGBA(60, 66, 120, 0x80));
        ctext(M + 40, 168, 0.9f, C_MUTED, "?");
        if (tab == TAB_MC) {
            ctext(W / 2, 160, 0.75f, C_TXT, mcReady ? (mcFullN > 0 ? "Nenhum save corresponde ao filtro" : "Nenhum save neste Memory Card") : (mcNoFmt ? "Cartao nao formatado ou corrompido" : "Insira um Memory Card"));
            ctext(W / 2, 200, 0.48f, C_MUTED, mcFullN > 0 ? "START > Organizar abre o menu de filtros e busca" : "L2 troca o Memory Card");
        } else {
            ctext(W / 2, 160, 0.75f, C_TXT, usbOk ? (usbFullN > 0 ? "Nenhum backup corresponde ao filtro" : "Nenhum backup ainda") : "Conecte um pendrive / HD USB");
            ctext(W / 2, 200, 0.48f, C_MUTED, usbOk ? (usbFullN > 0 ? "START > Organizar abre o menu de filtros e busca" : "Exporte um save, coloque .psu .max .cbs .sps .xps em SaveManagerReis ou VMCs em mass:/VMC") : "Depois pressione O para atualizar");
        }
    } else {
        const float gap = 10, vgap = listMode ? 3 : 8, top = listMode ? 144 : 130, ch = listMode ? 30 : 78;
        const float cw = (W - 2 * M - 3 * gap) / 4.0f;
        int cols = COLS;
        int rows = (int)((H - 58 - top + vgap) / (ch + vgap));
        int cur = cursorPos[tab], curRow = cur / cols, first, last;
        if (rows < 1) rows = 1;
        if (rows > 16) rows = 16;
        gridRows = rows;
        if (curRow < topRow[tab]) topRow[tab] = curRow;
        if (curRow >= topRow[tab] + rows) topRow[tab] = curRow - rows + 1;
        first = topRow[tab] * cols;
        last  = (topRow[tab] + rows) * cols;
        if (last > N) last = N;
        if (listMode) {                                       /* cabecalho das colunas */
            float rw = W - 2 * M - 12, dR, sR, fR, cX;
            row_geom(M, rw, &dR, &sR, &fR, &cX);
            text(M + 8 + ch, top - 13, 0.30f, C_MUTED, "NOME DO JOGO / PASTA");
            ctext(cX + 16, top - 13, 0.30f, C_MUTED, "REGIAO");
            text(sR - tw("TAMANHO", 0.30f), top - 13, 0.30f, C_MUTED, "TAMANHO");
            text(dR - tw("DATA", 0.30f), top - 13, 0.30f, C_MUTED, "DATA");
        }
        for (i = first; i < last; i++) {
            int r = i / cols - topRow[tab], c = i % cols;
            if (listMode)
                draw_row(M, top + r * (ch + vgap), W - 2 * M - 12, ch, &L[i], i == cur, tab != TAB_MC, tab < 2 ? icons[tab][i] : NULL, i - first);
            else
                draw_card(M + c * (cw + gap), top + r * (ch + vgap), cw, ch, &L[i], i == cur, tab != TAB_MC, tab < 2 ? icons[tab][i] : NULL, i - first);
        }
        {
            int totalRows = (N + cols - 1) / cols;
            if (totalRows > rows) {
                float th = rows * (ch + vgap) - vgap, bh = th * rows / totalRows;
                rect(W - 14, top, 4, th, C_CARD);
                rect(W - 14, top + (th - bh) * topRow[tab] / (totalRows - rows), 4, bh, C_ACC);
            }
        }
    }

    /* ---- rodape ---- */
    vgrad(0, H - 56, W, 56, RGBA(22, 25, 54, 0x80), RGBA(13, 15, 34, 0x80));
    hgrad(0, H - 56, W, 2, C_ACC2, C_ACC);
    {
    x = M;
    x = key_btn(x, H - 50, 0, C_BLUE, "Marcar");
    x = key_btn(x, H - 50, 1, C_PINK, "Todos");
    {
        char tl[48];
        if (tab == TAB_MC)        snprintf(tl, sizeof(tl), "Exportar p/ USB");
        else if (tab == TAB_USB)  snprintf(tl, sizeof(tl), "Importar p/ Memory Card");
        else                      snprintf(tl, sizeof(tl), "Converter p/ %s", FMT_NAME[convFmt]);
        x = key_btn(x, H - 50, 2, C_GREEN, tl);
    }
    x = key_btn(x, H - 50, 3, C_RED, "Atualizar");
    if (x + 90 < W) { rect(x - 8, H - 50, 1, 18, C_CARD); key_pill(x + 4, H - 50, "R3", "Detalhes"); }
    x = M;
    x = key_pill(x, H - 30, "L1/R1", "Abas");
    if (tab == TAB_MC) {
        char fl[32];
        snprintf(fl, sizeof(fl), "Fmt: %s", FMT_NAME[expFmt]);
        x = key_pill(x, H - 30, "R2", fl);
    }
    x = key_pill(x, H - 30, "L3", listMode ? "Grade" : "Lista");
    x = key_pill(x, H - 30, "L2", "Slot");
    x = key_pill(x, H - 30, "SELECT", "Reler");
    if (tab == TAB_CONV) {
        char fl[32];
        snprintf(fl, sizeof(fl), "Destino: %s", FMT_NAME[convFmt]);
        x = key_pill(x, H - 30, "R2", fl);
    }
    key_pill(x, H - 30, "START", "Menu");
    }

    /* ---- aviso (toast) ---- */
    if (ui.toastT > 0) {
        float bw = tw(ui.toast, 0.50f) + 30;
        soft_shadow((W - bw) / 2, H - 96, bw, 26);
        panel((W - bw) / 2, H - 96, bw, 26, ui.toastErr ? RGBA(255, 110, 120, 0x80) : RGBA(90, 240, 150, 0x80),
                                           ui.toastErr ? RGBA(220, 60, 80, 0x80)   : RGBA(40, 190, 110, 0x80));
        ctext(W / 2, H - 90, 0.50f, C_DARK, ui.toast);
    }

    /* ---- tela de abertura: logo grande por cima de tudo ---- */
    if (splash) {
        float lh = 100, lx = W / 2 - 215, ly = 44;
        vgrad(0, 0, W, H, RGBA(16, 18, 42, 0x80), RGBA(7, 8, 20, 0x80));
        hgrad(0, 0, W, 6, C_ACC, C_ACC2);
        draw_logo(lx, ly, lh);
        text_sh(lx + 100, ly + 2, 0.80f, C_TXT, "SAVE MANAGER");
        text_sh(lx + 98, ly + 22, 1.75f, C_ACC, "REIS");
        text(lx + 100, ly + 82, 0.45f, C_MUTED, "HOMEBREW PS2  " APP_VERSION);
    }

    /* ---- janela de progresso / confirmacao ---- */
    if (ui.menu) {
        float rh = ui.mn > 14 ? 21.0f : (ui.mn > 11 ? 23.0f : 26.0f);
        float mw = 500, mh = 66 + ui.mn * rh, mx = (W - mw) / 2, my = (H - mh) / 2 - 10;
        int k;
        window(mx, my, mw, mh);
        ctext(W / 2, my + 14, 0.65f, C_TXT, ui.title);
        for (k = 0; k < ui.mn; k++) {
            float iy = my + 44 + k * rh;
            if (k == ui.msel) { hgrad(mx + 14, iy, mw - 28, rh - 4, C_ACC, C_ACC2); rect(mx + 14, iy, 4, rh - 4, RGBA(255, 255, 255, 0x80)); text(mx + 26, iy + (rh - 4 - 14) / 2, 0.48f, C_DARK, ui.mitem[k]); }
            else              { panel(mx + 14, iy, mw - 28, rh - 4, RGBA(38, 43, 84, 0x80), RGBA(28, 31, 62, 0x80)); text(mx + 26, iy + (rh - 4 - 14) / 2, 0.48f, C_TXT, ui.mitem[k]); }
        }
        text(mx + 14, my + mh - 18, 0.38f, C_MUTED, "X escolher    O voltar");
    } else if (inEditing) {
        float bw = 540, bh = 332, bx = (W - bw) / 2, by = (H - bh) / 2, gx, ax;
        int r, c;
        char kc[2] = { 0, 0 };
        window(bx, by, bw, bh);
        ctext(W / 2, by + 12, 0.60f, C_MUTED, inTitle);
        panel(bx + 20, by + 40, bw - 40, 36, RGBA(9, 11, 26, 0x80), RGBA(18, 20, 44, 0x80));
        rect(bx + 20, by + 75, bw - 40, 1, RGBA(0, 200, 255, 0x80));
        text(bx + 30, by + 48, 0.70f, C_TXT, inBuf);
        if ((frameNo >> 4) & 1) rect(bx + 32 + tw(inBuf, 0.70f), by + 46, 3, 22, C_ACC);      /* cursor piscando */
        gx = bx + (bw - (10 * 44 + 9 * 4)) / 2;
        for (r = 0; r < 4; r++) {
            for (c = 0; c < kb_cols(r); c++) {
                float kx = gx + c * 48, ky = by + 90 + r * 38;
                kc[0] = kb_char(r, c);
                if (r == inKr && c == inKc) { hgrad(kx, ky, 44, 32, C_ACC, C_ACC2); ctext(kx + 22, ky + 8, 0.62f, C_DARK, kc); }
                else                        { panel(kx, ky, 44, 32, RGBA(46, 51, 96, 0x80), RGBA(30, 34, 68, 0x80)); ctext(kx + 22, ky + 8, 0.62f, C_TXT, kc); }
            }
        }
        ax = bx + (bw - 496) / 2;
        for (c = 0; c < 6; c++) {
            const char *lb = (c == 0) ? (inCaps ? "ABC" : "abc") : KB_ACT[c];
            if (inKr == 4 && c == inKc) { hgrad(ax, by + 246, KB_ACTW[c], 32, C_ACC, C_ACC2); ctext(ax + KB_ACTW[c] / 2, by + 255, 0.48f, C_DARK, lb); }
            else                        { panel(ax, by + 246, KB_ACTW[c], 32, RGBA(46, 51, 96, 0x80), RGBA(30, 34, 68, 0x80)); ctext(ax + KB_ACTW[c] / 2, by + 255, 0.48f, C_TXT, lb); }
            ax += KB_ACTW[c] + 4;
        }
        ctext(W / 2, by + 292, 0.38f, C_MUTED, "X digita   Quadrado apaga   Triangulo espaco   L1/R1 maiuscula");
        ctext(W / 2, by + 308, 0.38f, C_ACC2, "START confirma    O cancela");
    } else if (hexOpen) {
        float bw = 580, bh = 372, bx = (W - bw) / 2, by = (H - bh) / 2, cw = FW * 0.40f;
        float xa = bx + 18, xh = xa + 10 * cw, xc = xh + 49 * cw;
        char ln[100], hx[64], as[20];
        u32 r, c, base;
        window(bx, by, bw, bh);
        ctext(W / 2, by + 12, 0.60f, C_MUTED, "EDITOR HEX");
        snprintf(ln, sizeof(ln), "%s / %s  -  %lu bytes%s", hexSave, hexName, (unsigned long)hexN, hexDirty ? "   [ALTERADO]" : "");
        text(bx + 18, by + 34, 0.42f, hexDirty ? C_RED : C_ACC, ln);
        rect(bx + 18, by + 54, bw - 36, 2, C_CARD);
        for (r = 0; r < 16; r++) {
            u32 row = hexTop + r;
            float ry = by + 62 + r * 15;
            base = row * 16;
            if (base >= hexN) break;
            if ((hexCur >> 4) == row) {
                c = hexCur & 15;
                rect(xh + c * 3 * cw - 2, ry - 1, 2 * cw + 4, 14, hexEditing ? C_GREEN : C_ACC);
                rect(xc + c * cw - 1, ry - 1, cw + 2, 14, hexEditing ? C_GREEN : C_ACC);
            }
            snprintf(ln, sizeof(ln), "%08lX", (unsigned long)base);
            text(xa, ry, 0.40f, C_MUTED, ln);
            hx[0] = 0;
            for (c = 0; c < 16; c++) {
                char t[4];
                if (base + c < hexN) snprintf(t, sizeof(t), "%02X ", hexBuf[base + c]); else snprintf(t, sizeof(t), "   ");
                strcat(hx, t);
                as[c] = (base + c < hexN && hexBuf[base + c] >= 32 && hexBuf[base + c] < 127) ? (char)hexBuf[base + c] : '.';
            }
            as[16] = 0;
            text(xh, ry, 0.40f, C_TXT, hx);
            text(xc, ry, 0.40f, C_TXT, as);
            if ((hexCur >> 4) == row) {
                char t[4];
                c = hexCur & 15;
                snprintf(t, sizeof(t), "%02X", hexBuf[hexCur]);
                text(xh + c * 3 * cw, ry, 0.40f, C_DARK, t);
                t[0] = as[c]; t[1] = 0;
                text(xc + c * cw, ry, 0.40f, C_DARK, t);
            }
        }
        if (hexN > 256) {
            float th = 16 * 15, bhh = th * 16.0f / (float)((hexN + 15) / 16);
            if (bhh < 6) bhh = 6;
            rect(bx + bw - 14, by + 62, 4, th, C_CARD);
            rect(bx + bw - 14, by + 62 + (th - bhh) * (float)hexTop / (float)(((hexN + 15) / 16) - 16), 4, bhh, C_ACC);
        }
        if (hexEditing) {
            snprintf(ln, sizeof(ln), "Editando o byte %08lX: Cima/Baixo +-1   Esq/Dir +-16   X ok   O desfaz", (unsigned long)hexCur);
            ctext(W / 2, by + bh - 44, 0.40f, C_GREEN, ln);
        } else {
            ctext(W / 2, by + bh - 44, 0.40f, C_MUTED, "Direcional move   L2/R2 pagina   X edita o byte");
        }
        ctext(W / 2, by + bh - 24, 0.42f, C_ACC2, "START gravar no Memory Card    O sair");
    } else if (tutOpen) {
        static const char *TT[TUT_PAGES] = { "BOTOES DO CONTROLE", "ATALHOS", "ABAS, FORMATOS E MENU START" };
        float bw = 580, bh = 392, bx = (W - bw) / 2, by = (H - bh) / 2, ly = by + 74;
        char pg[16];
        int k;
        window(bx, by, bw, bh);
        ctext(W / 2, by + 12, 0.60f, C_MUTED, "TUTORIAL");
        text(bx + 20, by + 38, 0.60f, C_ACC, TT[tutPage]);
        snprintf(pg, sizeof(pg), "%d/%d", tutPage + 1, TUT_PAGES);
        text(bx + bw - 20 - tw(pg, 0.44f), by + 40, 0.44f, C_MUTED, pg);
        if (tutPage == 0) {
            pad_btn(bx + 22, ly, 0, C_BLUE);   text(bx + 54, ly + 3, 0.44f, C_TXT, "Marca / desmarca o save selecionado");                     ly += 34;
            pad_btn(bx + 22, ly, 1, C_PINK);   text(bx + 54, ly + 3, 0.44f, C_TXT, "Marca todos (se todos ja estao marcados, desmarca)");      ly += 34;
            pad_btn(bx + 22, ly, 2, C_GREEN);  text(bx + 54, ly + 3, 0.44f, C_TXT, "Exporta p/ USB (aba Memory Card), importa p/ cartao (aba USB)"); ly += 18;
            text(bx + 54, ly + 3, 0.44f, C_TXT, "ou converte (aba Converter)");                                                                    ly += 34;
            pad_btn(bx + 22, ly, 3, C_RED);    text(bx + 54, ly + 3, 0.44f, C_TXT, "Atualiza a lista de saves");                               ly += 40;
            text(bx + 22, ly, 0.44f, C_ACC, "Direcional");
            text(bx + 120, ly, 0.44f, C_TXT, "Move o cursor entre os saves");                                                                   ly += 30;
            text(bx + 22, ly, 0.40f, C_MUTED, "Segure O durante uma copia ou conversao para cancelar.");
        } else if (tutPage == 1) {
            tut_key(bx, ly, "L1/R1",  "Troca de aba (Memory Card / USB / Converter)");   ly += 30;
            tut_key(bx, ly, "L2",     "Troca o slot do Memory Card (inclusive multitap)"); ly += 30;
            tut_key(bx, ly, "R2",     "Muda o formato de exportacao (Converter: destino)"); ly += 30;
            tut_key(bx, ly, "SELECT", "Atualiza as listas do cartao e do USB");            ly += 30;
            tut_key(bx, ly, "START",  "Abre o menu de ferramentas");                       ly += 30;
            tut_key(bx, ly, "L3",     "Alterna a visao: grade de cards / lista");       ly += 30;
            tut_key(bx, ly, "R3",     "Detalhes: exporta/importa arquivo e editor hex");
        } else {
            static const char *NM[13] = { "Verificar", "Apagar", "Copiar", "Icone", "Backup de tudo", "Recuperar cartao", "Formatar",
                                          "Organizar", "Detalhes", "Renomear", "Recuperar save", "Criar VMC", "Ajustes" };
            static const char *DS[13] = { "acha saves corrompidos", "apaga os saves marcados", "copia p/ o outro Memory Card", "troca o icone (BMP 128x128)",
                                          "todos os saves p/ o USB", "cartao ilegivel / corrompido", "apaga o cartao inteiro",
                                          "ordem, filtro e busca", "arquivos e tamanho do save", "nome do jogo, pasta ou backup",
                                          "tenta salvar o save selecionado", "cartao virtual novo (OPL)", "som, musica, video, hora" };
            text(bx + 20, ly - 8, 0.40f, C_MUTED, "Formatos: Pasta, PSU, MAX (compactado). Le tambem CBS, SPS, XPS e VMC.");
            text(bx + 20, ly + 8, 0.40f, C_MUTED, "Abas: Memory Card (cartao), USB (backups no pendrive), Converter.");
            for (k = 0; k < 13; k++) {
                float ry = ly + 32 + k * 17;
                text(bx + 20, ry, 0.40f, C_ACC, NM[k]);
                text(bx + 170, ry, 0.40f, C_TXT, DS[k]);
            }
        }
        ctext(W / 2, by + bh - 24, 0.42f, C_ACC2, "Esq/Dir muda de pagina    O fechar");
    } else if (dtOpen) {
        float bw = 560, bh = 392, bx = (W - bw) / 2, by = (H - bh) / 2;
        int k, vis = 10;
        char sz[24], ln[64];
        window(bx, by, bw, bh);
        ctext(W / 2, by + 12, 0.60f, C_MUTED, "DETALHES DO SAVE");
        {
            char fb[96];
            float tx = bx + 100, mw = bw - 100 - 20;
            rect(bx + 20, by + 40, 72, 72, RGBA(0, 0, 0, 0x3C));          /* moldura do icone */
            if (dtIcon) {
                draw_icon(ICON_DET, dtIcon, bx + 24, by + 44, 64);
            } else {
                vgrad(bx + 24, by + 44, 64, 64, reg_col(dtReg), RGBA(20, 22, 50, 0x80));
                fill_circle(bx + 56, by + 76, 26, RGBA(255, 255, 255, 0x22));
                ctext(bx + 56, by + 62, 1.0f, C_TXT, REG_TAG[dtReg]);
            }
            fit(fb, dtTitle, mw, 0.60f);
            text(tx, by + 38, 0.60f, C_ACC, fb);
            for (k = 0; k < 3; k++) { fit(fb, dtL[k], mw, 0.42f); text(tx, by + 66 + k * 16, 0.42f, C_TXT, fb); }
        }
        rect(bx + 20, by + 118, bw - 40, 2, C_CARD);
        if (dtN == 0) ctext(W / 2, by + 170, 0.45f, C_MUTED, dtNote);
        for (k = 0; k < vis && dtScroll + k < dtN; k++) {
            const DFile *f = &dtF[dtScroll + k];
            float ry = by + 128 + k * 20;
            if (dtCan && dtScroll + k == dtSel) hgrad(bx + 20, ry - 2, bw - 40, 20, RGBA(0, 200, 255, 0x50), RGBA(150, 90, 255, 0x50));
            else if (k & 1) rect(bx + 20, ry - 2, bw - 40, 20, RGBA(255, 255, 255, 0x0E));
            text(bx + 28, ry, 0.42f, C_TXT, f->name);
            fmt_size(sz, sizeof(sz), f->size);
            text(bx + bw - 28 - tw(sz, 0.42f), ry, 0.42f, C_MUTED, sz);
        }
        if (dtN > vis) {
            snprintf(ln, sizeof(ln), "%d-%d de %d arquivos", dtScroll + 1, dtScroll + vis < dtN ? dtScroll + vis : dtN, dtN);
            ctext(W / 2, by + bh - 44, 0.40f, C_MUTED, ln);
        }
        ctext(W / 2, by + bh - 24, 0.40f, C_ACC2, dtCan ? "X hex  Triangulo exportar  Quadrado importar  O fechar" : "Cima/Baixo rolar    O fechar");
    } else if (volEditing) {
        float bw = 500, bh = 236, bx = (W - bw) / 2, by = (H - bh) / 2;
        float sw = 38, gap = 4, sx = bx + (bw - (10 * sw + 9 * gap)) / 2, sy = by + 104;
        char pc[16];
        int k;
        window(bx, by, bw, bh);
        ctext(W / 2, by + 14, 0.65f, C_TXT, "VOLUME DA MUSICA");
        snprintf(pc, sizeof(pc), "%d%%", volVal * 10);
        ctext(W / 2, by + 46, 1.0f, volVal ? C_ACC : C_RED, pc);
        for (k = 0; k < 10; k++) {
            float h = 22 + k * 3, x = sx + k * (sw + gap), y = sy + 36 - h;   /* barras crescentes, como um indicador de volume */
            if (k < volVal) { vgrad(x, y, sw, h, C_ACC, C_ACC2); rect(x, y, sw, 2, RGBA(255, 255, 255, 0x40)); }
            else              rect(x, y, sw, h, C_CARD);
        }
        ctext(W / 2, sy + 44, 0.38f, C_MUTED, volVal == 0 ? "MUDO" : (volVal == 10 ? "MAXIMO" : " "));
        if (!snd_music_present())      ctext(W / 2, by + 168, 0.40f, C_RED, "Sem musica carregada (musica.wav)");
        else if (!snd_music_enabled()) ctext(W / 2, by + 168, 0.40f, C_RED, "Musica desligada: ligue em Ajustes para ouvir");
        else                           ctext(W / 2, by + 168, 0.40f, C_GREEN, "Tocando: o volume muda na hora");
        ctext(W / 2, by + 190, 0.38f, C_MUTED, "Esq/Dir ajusta   Cima maximo   Baixo mudo");
        ctext(W / 2, by + 206, 0.38f, C_MUTED, "Quadrado padrao (60%)   X confirmar   O cancelar");
    } else if (clkEditing) {
        static const char *lab[5] = { "DIA", "MES", "ANO", "HORA", "MIN" };
        static const float fw[5] = { 64, 64, 96, 64, 64 };
        float bw = 470, bh = 190, bx = (W - bw) / 2, by = (H - bh) / 2, fx, tot = 14 * 4;
        char v[8], zs[40];
        int k;
        for (k = 0; k < 5; k++) tot += fw[k];
        window(bx, by, bw, bh);
        ctext(W / 2, by + 14, 0.65f, C_TXT, "ACERTAR DATA E HORA");
        fx = bx + (bw - tot) / 2;
        for (k = 0; k < 5; k++) {
            if (k == 2) snprintf(v, sizeof(v), "%04d", clkEd[k]);
            else        snprintf(v, sizeof(v), "%02d", clkEd[k]);
            ctext(fx + fw[k] / 2, by + 50, 0.40f, C_MUTED, lab[k]);
            if (k == clkSel) { hgrad(fx, by + 68, fw[k], 46, C_ACC, C_ACC2); ctext(fx + fw[k] / 2, by + 80, 0.85f, C_DARK, v); }
            else             { rect(fx, by + 68, fw[k], 46, C_CARD);         ctext(fx + fw[k] / 2, by + 80, 0.85f, C_TXT, v); }
            fx += fw[k] + 14;
        }
        snprintf(zs, sizeof(zs), "Hora local (GMT%c%d%s)", tzMin < 0 ? '-' : '+', (tzMin < 0 ? -tzMin : tzMin) / 60,
                 (tzMin % 60) ? ":30" : "");
        ctext(W / 2, by + 128, 0.42f, C_MUTED, zs);
        ctext(W / 2, by + 150, 0.38f, C_MUTED, "Cima/Baixo altera   Esq/Dir muda o campo");
        ctext(W / 2, by + 166, 0.38f, C_MUTED, "X gravar no relogio do console    O cancelar");
    } else if (ui.busy || ui.modal) {
        int nx = ui.modal ? ui.extraN : 0, k;
        float bh = 130 + nx * 18, bx = (W - 440) / 2, by = (H - bh) / 2, yb = by + 92 + nx * 18;
        window(bx, by, 440, bh);
        ctext(W / 2, by + 16, 0.7f, C_TXT, ui.title);
        ctext(W / 2, by + 48, 0.48f, C_MUTED, ui.msg);
        for (k = 0; k < nx; k++) ctext(W / 2, by + 70 + k * 18, 0.44f, ui.extraCol[k], ui.extra[k]);
        if (ui.busy) {
            float pct = ui.total ? (float)ui.done / (float)ui.total : 0.0f;
            if (pct > 1.0f) pct = 1.0f;
            panel(bx + 30, by + 92, 380, 16, RGBA(14, 16, 36, 0x80), RGBA(30, 34, 68, 0x80));
            hgrad(bx + 30, by + 92, 380 * pct, 16, C_ACC, C_ACC2);
            rect(bx + 30, by + 92, 380 * pct, 2, RGBA(255, 255, 255, 0x40));
            if (380 * pct > 40) {                                   /* faixa de brilho que corre pela barra */
                float fw = 380 * pct, sx = (float)((frameNo * 3) % (int)fw), sw = 22;
                if (sx + sw > fw) sw = fw - sx;
                if (sw > 0) rect(bx + 30 + sx, by + 92, sw, 16, RGBA(255, 255, 255, 0x26));
            }
            if (g_multi) ctext(W / 2, by + 112, 0.38f, C_MUTED, "Segure O para cancelar");
        } else if (ui.modal == 2) {
            key_btn(W / 2 - 30, yb, 0, C_BLUE, "OK");
        } else {
            key_btn(W / 2 - 100, yb, 0, C_BLUE, "Sim");
            key_btn(W / 2 + 20, yb, 3, C_RED, "Nao");
        }
    }
}

static void present(void)
{
    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);
    snd_update();
    frameNo++;
}

static void progress(const char *msg, u32 done, u32 total)
{
    cancel_poll();
    ui.done = done; ui.total = total;
    snprintf(ui.msg, sizeof(ui.msg), "%s", msg);
    render();
    present();
}

static void toast(const char *fmt, int n)
{
    snprintf(ui.toast, sizeof(ui.toast), fmt, n);
    ui.toastErr = (strstr(ui.toast, "alha") || strstr(ui.toast, "nao ") || strstr(ui.toast, "Conecte") || strstr(ui.toast, "Sem ")) ? 1 : 0;
    ui.toastT = 240;
    snd_play(ui.toastErr ? SND_ERR : SND_DONE);
}

/* aviso com texto pronto (err = 1 para erro) */
static void toast_s(const char *msg, int err)
{
    snprintf(ui.toast, sizeof(ui.toast), "%s", msg);
    ui.toastErr = err;
    ui.toastT = 240;
    snd_play(err ? SND_ERR : SND_DONE);
}

/* ============================================================
 *  Controle
 * ============================================================ */
static void read_pad(void)
{
    struct padButtonStatus pb;
    int st = padGetState(0, 0);
    pad_new = 0;
    if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) {
        u32 now;
        if (padRead(0, 0, &pb) == 0) return;
        now = 0xFFFF ^ pb.btns;
        pad_new = now & ~pad_old;
        if (now & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) {
            pad_hold++;
            if (pad_hold > 18 && (pad_hold % 4) == 0)
                pad_new |= now & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
        } else pad_hold = 0;
        pad_old = now;
    }
}

/* le o controle e marca o cancelamento se o O estiver apertado */
static int cancel_poll(void)
{
    if (g_cancel) return 1;
    read_pad();
    if (pad_old & PAD_CIRCLE) g_cancel = 1;
    return g_cancel;
}

static int confirm(const char *title, const char *msg)
{
    int res = -1;
    snprintf(ui.title, sizeof(ui.title), "%s", title);
    snprintf(ui.msg, sizeof(ui.msg), "%s", msg);
    ui.modal = 1;
    while (res < 0) {
        read_pad();
        if (pad_new & PAD_CROSS)  { res = 1; snd_play(SND_OK); }
        if (pad_new & PAD_CIRCLE) { res = 0; snd_play(SND_BACK); }
        render();
        present();
    }
    ui.modal = 0;
    ui.extraN = 0;
    return res;
}

static void info(const char *title, const char *msg)
{
    int done = 0;
    snprintf(ui.title, sizeof(ui.title), "%s", title);
    snprintf(ui.msg, sizeof(ui.msg), "%s", msg);
    ui.modal = 2;
    while (!done) {
        read_pad();
        if (pad_new & (PAD_CROSS | PAD_CIRCLE)) { done = 1; snd_play(SND_OK); }
        render();
        present();
    }
    ui.modal = 0;
}

/* menu de opcoes: retorna o indice escolhido ou -1 (cancelado) */
static int menu_run(const char *title, const char **items, int n)
{
    int res = -2, k;
    snprintf(ui.title, sizeof(ui.title), "%s", title);
    for (k = 0; k < n && k < 20; k++) ui.mitem[k] = items[k];
    ui.mn = n; ui.msel = 0; ui.menu = 1;
    while (res == -2) {
        read_pad();
        if (pad_new & PAD_UP)    { ui.msel = (ui.msel + n - 1) % n; snd_play(SND_MOVE); }
        if (pad_new & PAD_DOWN)  { ui.msel = (ui.msel + 1) % n; snd_play(SND_MOVE); }
        if (pad_new & PAD_CROSS) { res = ui.msel; snd_play(SND_OK); }
        if (pad_new & PAD_CIRCLE) { res = -1; snd_play(SND_BACK); }
        render();
        present();
    }
    ui.menu = 0;
    return res;
}

/* ============================================================
 *  Memory Card
 * ============================================================ */
static int mc_list(int slot, const char *pat, sceMcTblGetDir *tbl, int max)
{
    int n = 0;
    mcGetDir(MP(slot), MS(slot), (char *)pat, 0, max, tbl);
    mcSync(0, NULL, &n);
    return n < 0 ? 0 : n;
}

static void mc_check(void)
{
    int type, freec, fmt, ret = -10;
    mcGetInfo(MP(mcSlot), MS(mcSlot), &type, &freec, &fmt);
    mcSync(0, NULL, &ret);
    mcGetInfo(MP(mcSlot), MS(mcSlot), &type, &freec, &fmt);   /* 2a chamada: estabiliza apos troca de cartao */
    mcSync(0, NULL, &ret);
    mcReady = (ret >= -1) && (ret != -2) ? 1 : 0;
    if (ret == -1) mcReady = (fmt != 0);
    mcNoFmt = (ret == -2) || (ret == -1 && fmt == 0);
    mcFreeKB = mcReady ? freec : 0;
}


/* ============================================================
 *  Organizar / filtrar / buscar
 *  mcFull / usbFull guardam TODOS os saves; mcList / usbList sao a vista (filtrada e ordenada).
 * ============================================================ */

/* "dd/mm/aa" + "hh:mm" -> minutos (ou -1) */
static long date_key(const Entry *e)
{
    int d, mo, y, h, mi;
    if (sscanf(e->date, "%d/%d/%d", &d, &mo, &y) != 3) return -1;
    if (sscanf(e->time, "%d:%d", &h, &mi) != 2) return -1;
    return ((((long)y * 12 + mo) * 31 + d) * 24 + h) * 60 + mi;
}

static int has_text(const char *hay, const char *needle)       /* busca sem diferenciar maiusculas */
{
    int n = (int)strlen(needle), i, j;
    if (n == 0) return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; j < n && hay[i + j]; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

static int view_match(const Entry *e)
{
    if (filtReg >= 0 && ent_region(e) != filtReg) return 0;
    if (searchStr[0] && !has_text(e->name, searchStr) && !has_text(e->title, searchStr)) return 0;
    return 1;
}

static int cmp_view(const void *pa, const void *pb)
{
    const Entry *a = (const Entry *)pa, *b = (const Entry *)pb;
    int c = 0;
    switch (sortMode) {
    case 1: { long ka = date_key(a), kb = date_key(b); c = (ka < kb) ? 1 : (ka > kb) ? -1 : 0; break; }   /* mais recente primeiro */
    case 2: c = (a->size < b->size) ? 1 : (a->size > b->size) ? -1 : 0; break;                           /* maior primeiro */
    case 3: c = ent_region(a) - ent_region(b); break;
    default: break;
    }
    if (c) return c;
    c = strcmp(a->name, b->name);
    if (c) return c;
    return strcmp(b->stamp, a->stamp);                       /* mesmo save: backup mais novo primeiro */
}

static void view_build(const Entry *full, int fn, Entry *out, int *on)
{
    int i, n = 0;
    for (i = 0; i < fn; i++) if (view_match(&full[i])) out[n++] = full[i];
    if (n > 1) qsort(out, n, sizeof(Entry), cmp_view);
    *on = n;
}

/* ---- tamanho dos saves do Memory Card: calculado 1 por vez, fora do caminho da troca de slot ---- */
static int mcsize_one(int fi)
{
    char pat[64];
    int j, m;
    u32 sz = 0;
    Entry *f = &mcFull[fi];
    snprintf(pat, sizeof(pat), "/%s/*", f->name);
    m = mc_list(mcSlot, pat, subtbl, MAX_ENT);
    for (j = 0; j < m; j++)
        if (!(subtbl[j].AttrFile & MC_ATTR_DIR)) sz += subtbl[j].FileSizeByte;
    f->size = sz; f->sizeDone = 1;
    for (j = 0; j < mcCount; j++)                          /* o mesmo save na vista atual (filtrada / ordenada) */
        if (!strcmp(mcList[j].name, f->name)) { mcList[j].size = sz; mcList[j].sizeDone = 1; break; }
    return 1;
}

static void mcsize_resort(void)                            /* ordenando por tamanho: reordena quando tudo estiver pronto */
{
    char cur[40];
    int i;
    if (sortMode != 2 || mcCount == 0) return;
    snprintf(cur, sizeof(cur), "%s", mcList[cursorPos[TAB_MC] < mcCount ? cursorPos[TAB_MC] : 0].name);
    sync_back(mcFull, mcFullN, mcList, mcCount);
    view_build(mcFull, mcFullN, mcList, &mcCount);
    for (i = 0; i < mcCount; i++) if (!strcmp(mcList[i].name, cur)) { cursorPos[TAB_MC] = i; break; }
    icon_free_all(TAB_MC);
}

static void mcsize_pump(void)                              /* 1 save por chamada */
{
    int i, left = 0, did = 0;
    for (i = 0; i < mcFullN; i++) {
        if (mcFull[i].sizeDone) continue;
        if (!did) { mcsize_one(i); did = 1; }
        else left = 1;
    }
    if (did && !left) mcsize_resort();
}

static void mcsize_all(void)                               /* antes de exportar, copiar, ver detalhes...: precisa de todos */
{
    int i, any = 0;
    for (i = 0; i < mcFullN; i++) if (!mcFull[i].sizeDone) { mcsize_one(i); any = 1; }
    if (any) mcsize_resort();
}


static void refresh_mc(void)
{
    int i, n;
    mcCount = 0;
    mcFullN = 0;
    cursorPos[TAB_MC] = 0; topRow[TAB_MC] = 0;
    icon_free_all(TAB_MC);
    mc_check();
    if (!mcReady) return;
    n = mc_list(mcSlot, "/*", mctbl, MAX_ENT);
    mcRawN = n;
    for (i = 0; i < n && mcCount < MAX_ENT; i++) {
        sceMcTblGetDir *t = &mctbl[i];
        Entry *e;
        if (!(t->AttrFile & MC_ATTR_DIR)) continue;
        if (t->EntryName[0] == '.') continue;
        e = &mcList[mcCount++];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", (char *)t->EntryName);
        e->ps1 = (t->AttrFile & 0x1000) ? 1 : 0;           /* flag "PS1" do sistema de arquivos do cartao */
        {
            Now mt;
            mt.y = t->_Modify.Year; mt.mo = t->_Modify.Month; mt.d = t->_Modify.Day;
            mt.h = t->_Modify.Hour; mt.mi = t->_Modify.Min;   mt.s = t->_Modify.Sec;
            if (mt.y >= 2000) jst_to_local(&mt);
            snprintf(e->date, sizeof(e->date), "%02d/%02d/%02d", mt.d, mt.mo, mt.y % 100);
            snprintf(e->time, sizeof(e->time), "%02d:%02d", mt.h, mt.mi);
        }
        e->sizeDone = 0;                                   /* o tamanho e calculado aos poucos, depois (mcsize_pump) */
    }
    memcpy(mcFull, mcList, (size_t)mcCount * sizeof(Entry));
    mcFullN = mcCount;
    view_build(mcFull, mcFullN, mcList, &mcCount);
}

/* ============================================================
 *  USB / HD
 * ============================================================ */
static int usb_ready(void)
{
    DIR *d = opendir("mass:/");
    if (d) { closedir(d); return 1; }
    return 0;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static u32 dir_size(const char *path)
{
    struct dirent *de;
    struct stat st;
    char full[320];
    u32 total = 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    while ((de = readdir(d)) != NULL) {
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        if (stat(full, &st) == 0 && !S_ISDIR(st.st_mode)) total += (u32)st.st_size;
    }
    closedir(d);
    return total;
}

__attribute__((unused)) static int cmp_usb(const void *a, const void *b)
{
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    int c = strcmp(y->stamp, x->stamp);        /* mais recentes primeiro */
    return c ? c : strcmp(x->name, y->name);
}

/* ---- leitura little-endian (usada tambem nos formatos .psu/.max) ---- */
static u32 ld16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 ld32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static void st16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void st32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* nome valido para arquivo/pasta dentro do Memory Card */
static int safe_name(const char *n)
{
    size_t l = strlen(n);
    if (l == 0 || l > 31) return 0;
    if (!strcmp(n, ".") || !strcmp(n, "..")) return 0;
    if (strchr(n, '/') || strchr(n, '\\') || strchr(n, ':')) return 0;
    return 1;
}

/* .psu / .max / .cbs / .sps / .xps (sem diferenciar maiusculas) */
static int ext_kind(const char *n)
{
    size_t l = strlen(n);
    if (l > 4 && n[l - 4] == '.') {
        char a = n[l - 3] | 0x20, b = n[l - 2] | 0x20, c = n[l - 1] | 0x20;
        if (a == 'p' && b == 's' && c == 'u') return FMT_PSU;
        if (a == 'm' && b == 'a' && c == 'x') return FMT_MAX;
        if (a == 'c' && b == 'b' && c == 's') return FMT_CBS;
        if (a == 's' && b == 'p' && c == 's') return FMT_SPS;
        if (a == 'x' && b == 'p' && c == 's') return FMT_XPS;
    }
    return FMT_DIR;
}

/* le o cabecalho de um .psu/.max: nome da pasta do save e tamanho. 0 = invalido */
static int g_peekPs1 = 0;                       /* peek_pack: o backup e de um save de PS1 */
#define PS1_MARK "SMR_PS1.FLG"                  /* arquivo marcador dentro de backups em Pasta */

static int peek_pack(const char *path, int kind, char *name, int nmax, u32 *size)
{
    static u8 h[4096];
    struct stat st;
    int fd, r;
    g_peekPs1 = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    r = read(fd, h, sizeof(h));
    close(fd);
    if (stat(path, &st) != 0) return 0;
    if (kind == FMT_PSU) {
        if (r < 512 || !(ld16(h) & MC_ATTR_DIR)) return 0;
        snprintf(name, nmax, "%.31s", (char *)h + 64);
        *size = (u32)st.st_size;
        g_peekPs1 = (ld16(h) & 0x1000) ? 1 : 0;
    } else if (kind == FMT_CBS) {
        char dn[64];
        if (r < 52 || sv_peek_cbs(h, (u32)r, dn, sizeof(dn))) return 0;
        snprintf(name, nmax, "%.31s", dn);
        *size = (u32)st.st_size;
    } else if (kind == FMT_SPS || kind == FMT_XPS) {
        char dn[64];
        if (r < 120 || sv_peek_sps(h, (u32)r, dn, sizeof(dn))) return 0;
        snprintf(name, nmax, "%.31s", dn);
        *size = (u32)st.st_size;
    } else {
        if (r < 0x5C || memcmp(h, "Ps2PowerSave", 12) != 0) return 0;
        snprintf(name, nmax, "%.31s", (char *)h + 0x10);
        *size = ld32(h + 0x58);                     /* tamanho descomprimido */
    }
    return safe_name(name) && name[0] != '.';
}

static void add_usb(const char *name, const char *stamp, const char *path, int kind, u32 size)
{
    Entry *e;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sc = 0;
    if (usbCount >= MAX_ENT) return;
    e = &usbList[usbCount++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->stamp, sizeof(e->stamp), "%s", stamp);
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->kind = (u8)kind;
    e->size = size;
    if (sscanf(stamp, "%d-%d-%d_%d-%d-%d", &y, &mo, &d, &h, &mi, &sc) >= 5) {
        snprintf(e->date, sizeof(e->date), "%02d/%02d/%02d", d, mo, y % 100);
        snprintf(e->time, sizeof(e->time), "%02d:%02d", h, mi);
    } else {
        snprintf(e->date, sizeof(e->date), "--/--/--");
        snprintf(e->time, sizeof(e->time), "--:--");
    }
}

/* adiciona um .psu/.max (se o cabecalho for valido) */
static void add_pack_file(const char *path, const char *fname, int kind)
{
    char name[40], stamp[24];
    u32 size = 0;
    size_t l = strlen(fname) - 4;
    if (strlen(path) >= sizeof(((Entry *)0)->path)) return;
    if (!peek_pack(path, kind, name, sizeof(name), &size)) return;
    if (l > sizeof(stamp) - 1) l = sizeof(stamp) - 1;
    memcpy(stamp, fname, l);
    stamp[l] = 0;
    {
        int before = usbCount;
        add_usb(name, stamp, path, kind, size);
        if (usbCount > before) usbList[usbCount - 1].ps1 = (u8)g_peekPs1;
    }
}

/* ---------------- cartoes virtuais (VMC do OPL, pasta mass:/VMC) ---------------- */
#define VMC_DIR "mass:/VMC"

typedef struct { Vmc *v; char stamp[24]; char path[128]; } VScan;

static int vmc_sz_cb(void *ctx, const VmcEnt *e)
{
    if (e->mode & VMC_DF_FILE) *(u32 *)ctx += e->length;
    return 0;
}

static int vmc_root_cb(void *ctx, const VmcEnt *e)
{
    VScan *sc = (VScan *)ctx;
    u32 sz = 0;
    int before;
    if (!(e->mode & VMC_DF_DIR) || e->name[0] == '.' || !safe_name(e->name)) return 0;
    vmc_list_dir(sc->v, e, vmc_sz_cb, &sz);
    before = usbCount;
    add_usb(e->name, sc->stamp, sc->path, FMT_VMC, sz);
    if (usbCount > before) {
        Entry *u = &usbList[usbCount - 1];
        Now mt;
        u->ps1 = (e->mode & VMC_DF_PSX) ? 1 : 0;
        mt.y = e->mo[6] | (e->mo[7] << 8); mt.mo = e->mo[5]; mt.d = e->mo[4];
        mt.h = e->mo[3]; mt.mi = e->mo[2]; mt.s = e->mo[1];
        if (mt.y >= 2000 && mt.mo >= 1 && mt.mo <= 12 && mt.d >= 1 && mt.d <= 31 && mt.h < 24 && mt.mi < 60) {
            jst_to_local(&mt);
            snprintf(u->date, sizeof(u->date), "%02d/%02d/%02d", mt.d, mt.mo, mt.y % 100);
            snprintf(u->time, sizeof(u->time), "%02d:%02d", mt.h, mt.mi);
        }
    }
    return usbCount >= MAX_ENT;
}

static int is_vmc_name(const char *n)               /* .bin / .ps2 / .vmc */
{
    int l = (int)strlen(n);
    char a, b, c;
    if (l < 5 || n[l - 4] != '.') return 0;
    a = (char)(n[l - 3] | 32); b = (char)(n[l - 2] | 32); c = (char)(n[l - 1] | 32);
    return (a == 'b' && b == 'i' && c == 'n') || (a == 'p' && b == 's' && c == '2') || (a == 'v' && b == 'm' && c == 'c');
}

static void scan_vmc(void)
{
    static Vmc v;
    struct dirent *de;
    DIR *d = opendir(VMC_DIR);
    if (!d) return;
    while ((de = readdir(d)) != NULL && usbCount < MAX_ENT) {
        VScan sc;
        struct stat st;
        char path[160];
        int l;
        if (de->d_name[0] == '.' || !is_vmc_name(de->d_name)) continue;
        snprintf(path, sizeof(path), VMC_DIR "/%s", de->d_name);
        if (strlen(path) >= sizeof(sc.path)) continue;
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode) || st.st_size < 1024 * 1024) continue;
        if (vmc_open(&v, path) != 0) continue;
        sc.v = &v;
        snprintf(sc.path, sizeof(sc.path), "%s", path);
        snprintf(sc.stamp, sizeof(sc.stamp), "%s", de->d_name);
        l = (int)strlen(sc.stamp);
        if (l > 4) sc.stamp[l - 4] = 0;               /* sem a extensao */
        vmc_list_root(&v, vmc_root_cb, &sc);
        vmc_close(&v);
    }
    closedir(d);
}

static void refresh_usb(void)
{
    struct dirent *ge, *se;
    char path[160], gname[64];
    DIR *g, *s;
    usbCount = 0;
    usbFullN = 0;
    cursorPos[TAB_USB] = 0; topRow[TAB_USB] = 0;
    cursorPos[TAB_CONV] = 0; topRow[TAB_CONV] = 0;
    icon_free_all(TAB_USB);
    usbOk = usb_ready();
    if (!usbOk) return;
    g = opendir(ROOT_DIR);
    while (g && (ge = readdir(g)) != NULL && usbCount < MAX_ENT) {
        if (ge->d_name[0] == '.') continue;
        snprintf(gname, sizeof(gname), "%s", ge->d_name);
        snprintf(path, sizeof(path), ROOT_DIR "/%s", gname);
        if (!is_dir(path)) {                       /* .psu / .max solto dentro de SaveManagerReis */
            int k = ext_kind(gname);
            if (k != FMT_DIR) add_pack_file(path, gname, k);
            continue;
        }
        s = opendir(path);
        if (!s) continue;
        while ((se = readdir(s)) != NULL && usbCount < MAX_ENT) {
            char sub[256];
            int k;
            if (se->d_name[0] == '.') continue;
            snprintf(sub, sizeof(sub), "%s/%s", path, se->d_name);
            if (is_dir(sub)) {
                if (strlen(sub) < sizeof(((Entry *)0)->path)) {
                    char mk[300];
                    struct stat mst;
                    int before = usbCount;
                    add_usb(gname, se->d_name, sub, FMT_DIR, dir_size(sub));
                    snprintf(mk, sizeof(mk), "%s/" PS1_MARK, sub);
                    if (usbCount > before && stat(mk, &mst) == 0) usbList[usbCount - 1].ps1 = 1;
                }
            } else if ((k = ext_kind(se->d_name)) != FMT_DIR) {
                add_pack_file(sub, se->d_name, k);
            }
        }
        closedir(s);
    }
    if (g) closedir(g);
    scan_vmc();                                      /* cartoes virtuais do OPL */
    memcpy(usbFull, usbList, (size_t)usbCount * sizeof(Entry));
    usbFullN = usbCount;
    view_build(usbFull, usbFullN, usbList, &usbCount);
}

/* ============================================================
 *  Copia de arquivos
 * ============================================================ */
/* ============================================================
 *  Leitura e renderizacao dos icones (.ico do PS2)
 * ============================================================ */
static u32 rd32(const u8 *p)  { u32 v; memcpy(&v, p, 4); return v; }
static u32 rd16(const u8 *p)  { u16 v; memcpy(&v, p, 2); return v; }
static int rd16s(const u8 *p) { short v; memcpy(&v, p, 2); return v; }

static float fsqrt(float x)
{
    float g;
    int i;
    if (x <= 0.0f) return 0.0f;
    g = x > 1.0f ? x * 0.5f : 1.0f;
    for (i = 0; i < 24; i++) g = 0.5f * (g + x / g);
    return g;
}

static void icon_free(Icon *ic)
{
    if (!ic) return;
    free(ic->pos); free(ic->nrm); free(ic->uv); free(ic->col);
    free(ic->tex); free(ic->thumb);
    free(ic);
}

static void icon_free_all(int t)
{
    int i;
    for (i = 0; i < MAX_ENT; i++) { icon_free(icons[t][i]); icons[t][i] = NULL; iconFail[t][i] = 0; }
    for (i = 0; i < ICON_SLOTS; i++) slotOwner[i] = NULL;
    spinTab = -1; spinIdx = -1;
}

/* le um arquivo inteiro (Memory Card ou USB) para a memoria */
static u8 *read_all(int isUsb, const char *path, int *outLen)
{
    int cap = 65536, len = 0, r = 0, fd = -1;
    u8 *buf = (u8 *)malloc(cap);
    if (!buf) return NULL;
    if (isUsb) fd = open(path, O_RDONLY);
    else { mcOpen(MP(mcSlot), MS(mcSlot), path, MC_RDONLY); mcSync(0, NULL, &fd); }
    if (fd < 0) { free(buf); return NULL; }
    for (;;) {
        if (isUsb) r = read(fd, iobuf, CHUNK);
        else { mcRead(fd, iobuf, CHUNK); mcSync(0, NULL, &r); }
        if (r <= 0) break;
        if (len + r > cap) {
            u8 *nb;
            if (cap >= (1 << 20)) break;
            nb = (u8 *)realloc(buf, cap * 2);
            if (!nb) break;
            buf = nb; cap *= 2;
        }
        memcpy(buf + len, iobuf, r);
        len += r;
    }
    if (isUsb) close(fd);
    else { mcClose(fd); mcSync(0, NULL, &r); }
    if (len <= 0) { free(buf); return NULL; }
    *outLen = len;
    return buf;
}

/* textura RLE: codigo < 0xFF00 = repete 1 pixel; senao copia (0x10000-codigo) pixels */
static int rle_decode(const u8 *d, int len, int p, u16 *out)
{
    int n = 0;
    while (p + 2 <= len && n < 16384) {
        u32 code = rd16(d + p);
        p += 2;
        if (code < 0xFF00) {
            u16 px;
            if (p + 2 > len) return 0;
            px = (u16)rd16(d + p); p += 2;
            while (code > 0 && n < 16384) { out[n++] = px; code--; }
        } else {
            int cnt = 0x10000 - (int)code;
            while (cnt-- > 0 && n < 16384) {
                if (p + 2 > len) return 0;
                out[n++] = (u16)rd16(d + p); p += 2;
            }
        }
    }
    return n == 16384;
}

static Icon *icon_parse(const u8 *d, int len)
{
    u32 shapes, attr, nv;
    int vsz, v;
    float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f }, c[3], r2 = 0.0f, rad;
    Icon *ic;

    if (len < 40 || rd32(d) != 0x010000) return NULL;
    shapes = rd32(d + 4); attr = rd32(d + 8); nv = rd32(d + 16);
    if (shapes < 1 || shapes > 64 || nv < 3 || nv > 8000 || (nv % 3)) return NULL;
    vsz = (int)shapes * 8 + 16;                    /* posicoes de cada shape + normal + uv + cor */
    if (20 + (int)nv * vsz > len) return NULL;

    ic = (Icon *)calloc(1, sizeof(Icon));
    if (!ic) return NULL;
    ic->n = (int)nv;
    ic->pos   = (float *)malloc(nv * 3 * sizeof(float));
    ic->nrm   = (float *)malloc(nv * 3 * sizeof(float));
    ic->uv    = (float *)malloc(nv * 2 * sizeof(float));
    ic->col   = (u8 *)malloc(nv * 4);
    ic->thumb = (u32 *)memalign(64, ICON_SZ * ICON_SZ * 4);
    if (!ic->pos || !ic->nrm || !ic->uv || !ic->col || !ic->thumb) { icon_free(ic); return NULL; }

    for (v = 0; v < (int)nv; v++) {
        const u8 *b = d + 20 + v * vsz;
        const u8 *nb = b + shapes * 8;
        int k;
        for (k = 0; k < 3; k++) {
            float f = (float)rd16s(b + k * 2);
            ic->pos[v * 3 + k] = f;
            ic->nrm[v * 3 + k] = (float)rd16s(nb + k * 2) / 4096.0f;
            if (f < mn[k]) mn[k] = f;
            if (f > mx[k]) mx[k] = f;
        }
        ic->uv[v * 2]     = (float)rd16s(nb + 8) / 4096.0f;
        ic->uv[v * 2 + 1] = (float)rd16s(nb + 10) / 4096.0f;
        memcpy(ic->col + v * 4, nb + 12, 4);
    }
    for (v = 0; v < 3; v++) c[v] = (mn[v] + mx[v]) * 0.5f;
    for (v = 0; v < (int)nv; v++) {
        float dx = ic->pos[v * 3] - c[0], dy = ic->pos[v * 3 + 1] - c[1], dz = ic->pos[v * 3 + 2] - c[2];
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > r2) r2 = d2;
    }
    rad = fsqrt(r2);
    if (rad < 1.0f) rad = 1.0f;
    for (v = 0; v < (int)nv; v++) {
        ic->pos[v * 3]     = (ic->pos[v * 3]     - c[0]) / rad;
        ic->pos[v * 3 + 1] = (ic->pos[v * 3 + 1] - c[1]) / rad;
        ic->pos[v * 3 + 2] = (ic->pos[v * 3 + 2] - c[2]) / rad;
    }

    /* textura 128x128 (ultimo segmento do arquivo) */
    if (attr & 4) {
        u16 *tex = (u16 *)malloc(16384 * sizeof(u16));
        if (tex) {
            int ok = 0;
            if (attr & 8) {                                  /* comprimida (RLE) */
                int ap = 20 + (int)nv * vsz;
                if (ap + 20 <= len) {
                    u32 nf = rd32(d + ap + 16);
                    int hsz;
                    for (hsz = 16; hsz >= 8 && !ok; hsz -= 8) {   /* cabecalho de frame real = 16 bytes (id, chaves, 2 desconhecidos); 8 so como reserva */
                        int p = ap + 20;
                        u32 f;
                        for (f = 0; f < nf && f < 4096; f++) {
                            u32 keys;
                            if (p + 8 > len) { p = -1; break; }
                            keys = rd32(d + p + 4);
                            if (keys > 100000) { p = -1; break; }
                            p += hsz + (int)keys * 8;
                        }
                        /* o u32 antes dos dados e o tamanho comprimido: tem de caber no arquivo (evita decodificar lixo) */
                        if (p > 0 && p + 4 < len && rd32(d + p) >= 2 && rd32(d + p) <= (u32)(len - p - 4) &&
                            rle_decode(d, len, p + 4, tex)) ok = 1;
                    }
                }
            } else if (len >= 32768 + 20) {                  /* sem compressao */
                memcpy(tex, d + len - 32768, 32768);
                ok = 1;
            }
            if (ok) ic->tex = tex; else free(tex);
        }
    }
    return ic;
}

/* rasteriza o modelo (projecao ortografica, z-buffer, sombreamento simples) na miniatura */
static void icon_render(Icon *ic, float ca, float sa)
{
    static float zb[ICON_SZ * ICON_SZ];
    const float S = ICON_SZ * 0.46f, C0 = ICON_SZ * 0.5f;
    const float LX = -0.35f, LY = -0.45f, LZ = 0.82f;
    u32 *out = ic->thumb;
    int i, t, k;

    for (i = 0; i < ICON_SZ * ICON_SZ; i++) { out[i] = 0; zb[i] = -1e9f; }

    for (t = 0; t + 2 < ic->n; t += 3) {
        float sx[3], sy[3], sd[3], it[3], area;
        float minx, maxx, miny, maxy;
        int x0, x1, y0, y1, px, py;

        for (k = 0; k < 3; k++) {
            int v = t + k;
            const float *p = ic->pos + v * 3, *n = ic->nrm + v * 3;
            float xr = p[0] * ca + p[2] * sa;
            float zr = -p[0] * sa + p[2] * ca;
            float nx = n[0] * ca + n[2] * sa;
            float nz = -n[0] * sa + n[2] * ca;
            float ny = n[1];
            float dt = nx * LX + ny * LY + nz * ICON_ZSIGN * LZ;
            sx[k] = C0 + xr * S;
            sy[k] = C0 + p[1] * S * ICON_FLIPY;
            sd[k] = zr * ICON_ZSIGN;                      /* maior = mais perto */
            it[k] = (nx * nx + ny * ny + nz * nz < 0.01f) ? 1.0f : 0.55f + 0.75f * (dt > 0.0f ? dt : 0.0f);
        }
        area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (area < 1e-4f && area > -1e-4f) continue;

        minx = maxx = sx[0]; miny = maxy = sy[0];
        for (k = 1; k < 3; k++) {
            if (sx[k] < minx) minx = sx[k];
            if (sx[k] > maxx) maxx = sx[k];
            if (sy[k] < miny) miny = sy[k];
            if (sy[k] > maxy) maxy = sy[k];
        }
        x0 = (int)minx - 1; if (x0 < 0) x0 = 0;
        y0 = (int)miny - 1; if (y0 < 0) y0 = 0;
        x1 = (int)maxx + 1; if (x1 > ICON_SZ - 1) x1 = ICON_SZ - 1;
        y1 = (int)maxy + 1; if (y1 > ICON_SZ - 1) y1 = ICON_SZ - 1;

        for (py = y0; py <= y1; py++) {
            for (px = x0; px <= x1; px++) {
                float fx = px + 0.5f, fy = py + 0.5f, w0, w1, w2, d, inten;
                int idx = py * ICON_SZ + px, r, g, b;

                w0 = ((sx[1] - fx) * (sy[2] - fy) - (sx[2] - fx) * (sy[1] - fy)) / area;
                w1 = ((sx[2] - fx) * (sy[0] - fy) - (sx[0] - fx) * (sy[2] - fy)) / area;
                w2 = 1.0f - w0 - w1;
                if (w0 < -0.01f || w1 < -0.01f || w2 < -0.01f) continue;
                d = w0 * sd[0] + w1 * sd[1] + w2 * sd[2];
                if (d <= zb[idx]) continue;
                zb[idx] = d;
                inten = w0 * it[0] + w1 * it[1] + w2 * it[2];

                if (ic->tex) {
                    float uu = w0 * ic->uv[(t) * 2]     + w1 * ic->uv[(t + 1) * 2]     + w2 * ic->uv[(t + 2) * 2];
                    float vv = w0 * ic->uv[(t) * 2 + 1] + w1 * ic->uv[(t + 1) * 2 + 1] + w2 * ic->uv[(t + 2) * 2 + 1];
                    int tx = ((int)(uu * 128.0f)) & 127, ty = ((int)(vv * 128.0f)) & 127;
                    u16 c = ic->tex[ty * 128 + tx];
                    r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3;
                } else {
                    r = (int)(w0 * ic->col[t * 4]     + w1 * ic->col[(t + 1) * 4]     + w2 * ic->col[(t + 2) * 4]);
                    g = (int)(w0 * ic->col[t * 4 + 1] + w1 * ic->col[(t + 1) * 4 + 1] + w2 * ic->col[(t + 2) * 4 + 1]);
                    b = (int)(w0 * ic->col[t * 4 + 2] + w1 * ic->col[(t + 1) * 4 + 2] + w2 * ic->col[(t + 2) * 4 + 2]);
                }
                r = (int)(r * inten); if (r > 255) r = 255;
                g = (int)(g * inten); if (g > 255) g = 255;
                b = (int)(b * inten); if (b > 255) b = 255;
                out[idx] = (u32)r | ((u32)g << 8) | ((u32)b << 16) | (0x80u << 24);
            }
        }
    }
    ic->ver++;
}

static void icon_spin(void)
{
    const float c0 = 0.99920f, s0 = 0.03999f;          /* ~0.04 rad por quadro */
    float c = gCos * c0 - gSin * s0, s = gSin * c0 + gCos * s0;
    float n = c * c + s * s;
    gCos = c * (1.5f - 0.5f * n);
    gSin = s * (1.5f - 0.5f * n);
}

static Icon *icon_load(int isUsb, const Entry *e)
{
    char base[200], path[300], iname[65];
    u8 *sys, *ico;
    int sl = 0, il = 0, k;
    Icon *ic;

    if (isUsb && e->kind != FMT_DIR) return NULL;     /* .psu/.max: usa o icone da regiao */
    if (isUsb) snprintf(base, sizeof(base), ROOT_DIR "/%s/%s", e->name, e->stamp);
    else       snprintf(base, sizeof(base), "/%s", e->name);

    snprintf(path, sizeof(path), "%s/icon.sys", base);
    sys = read_all(isUsb, path, &sl);
    if (!sys) return NULL;
    if (sl < 0x144 || memcmp(sys, "PS2D", 4) != 0) { free(sys); return NULL; }
    memcpy(iname, sys + 0x104, 64);
    iname[64] = 0;

    snprintf(path, sizeof(path), "%s/%s", base, iname);
    ico = read_all(isUsb, path, &il);
    if (!ico) { free(sys); return NULL; }
    ic = icon_parse(ico, il);
    free(ico);
    if (ic) {
        for (k = 0; k < 4; k++) {                      /* cores de fundo dos 4 cantos */
            u32 r = rd32(sys + 0x10 + k * 16), g = rd32(sys + 0x14 + k * 16), b = rd32(sys + 0x18 + k * 16);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            ic->bg[k] = RGBA(r, g, b, 0x80);
        }
        icon_render(ic, ICON_CA0, ICON_SA0);
    }
    free(sys);
    return ic;
}

/* carrega 1 icone por quadro (o do cursor primeiro, depois os visiveis) */
static void icon_pump(int t, Entry *L, int N)
{
    int first = topRow[t] * COLS, last = first + gridRows * COLS, i, c = cursorPos[t];
    if (last > N) last = N;
    if (c >= first && c < last && !icons[t][c] && !iconFail[t][c]) i = c;
    else {
        for (i = first; i < last; i++) if (!icons[t][i] && !iconFail[t][i]) break;
        if (i >= last) return;
    }
    icons[t][i] = icon_load(t == TAB_USB, &L[i]);
    if (!icons[t][i]) iconFail[t][i] = 1;
}


/* ---- nome do jogo: titulo do icon.sys (Shift-JIS, offset 0xC0, 68 bytes) ----
 * Converte o que for ASCII / letras e numeros de largura total para texto simples.
 * Kanji/kana sao ignorados (a fonte do app so tem ASCII); se nao sobrar texto, usa o nome da pasta. */
/* decodifica os bytes [from, to) do titulo (Shift-JIS) para texto simples; kanji/kana sao ignorados */
static void sjis_range(const u8 *t, int from, int to, char *out, int max)
{
    int i = from, o = 0;
    while (i < to && t[i] && o < max - 2) {
        u8 c = t[i], d;
        char ch = 0;
        int adv = 1;
        if (c >= 0x20 && c < 0x7F) {
            ch = (char)c;
        } else if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
            d = t[i + 1];
            adv = d ? 2 : 1;
            if (c == 0x82) {
                if (d >= 0x4F && d <= 0x58)      ch = (char)('0' + (d - 0x4F));
                else if (d >= 0x60 && d <= 0x79) ch = (char)('A' + (d - 0x60));
                else if (d >= 0x81 && d <= 0x9A) ch = (char)('a' + (d - 0x81));
            } else if (c == 0x81) {
                switch (d) {
                case 0x40: ch = ' ';  break;
                case 0x41: ch = ',';  break;
                case 0x42: case 0x44: ch = '.'; break;
                case 0x46: ch = ':';  break;
                case 0x47: ch = ';';  break;
                case 0x48: ch = '?';  break;
                case 0x49: ch = '!';  break;
                case 0x51: ch = '_';  break;
                case 0x5E: ch = '/';  break;
                case 0x60: ch = '~';  break;
                case 0x65: case 0x66: ch = '\''; break;
                case 0x67: case 0x68: ch = '"';  break;
                case 0x69: ch = '(';  break;
                case 0x6A: ch = ')';  break;
                case 0x7B: ch = '+';  break;
                case 0x7C: ch = '-';  break;
                case 0x93: ch = '%';  break;
                case 0x94: ch = '#';  break;
                case 0x95: ch = '&';  break;
                case 0x96: ch = '*';  break;
                case 0x97: ch = '@';  break;
                default: break;
                }
            }
        }
        i += adv;
        if (!ch) continue;
        if (ch == ' ' && (o == 0 || out[o - 1] == ' ')) continue;
        out[o++] = ch;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

/* titulo de duas linhas (icon.sys, offset 0xC0, 68 bytes; nl = onde comeca a 2a linha) -> uma linha de texto */
static void sjis_title(const u8 *t, int nl, char *out, int max)
{
    char a[40], b[40];
    int i, alnum = 0;
    if (nl < 0 || nl > 68) nl = 68;
    sjis_range(t, 0, nl, a, sizeof(a));
    sjis_range(t, nl, 68, b, sizeof(b));
    snprintf(out, (size_t)max, "%s%s%s", a, (a[0] && b[0]) ? " " : "", b);
    for (i = 0; out[i]; i++)
        if ((out[i] >= '0' && out[i] <= '9') || (out[i] >= 'A' && out[i] <= 'Z') || (out[i] >= 'a' && out[i] <= 'z')) alnum++;
    if (alnum < 2) out[0] = 0;
}

/* texto -> Shift-JIS de largura total (como os saves oficiais). Retorna os bytes gerados; ignora o que nao tem equivalente */
static int sjis_encode(const char *str, u8 *dst, int cap)
{
    int o = 0;
    for (; *str; str++) {
        unsigned char ch = (unsigned char)*str;
        u8 hi = 0x81, lo = 0;
        if (ch >= '0' && ch <= '9')      { hi = 0x82; lo = (u8)(0x4F + (ch - '0')); }
        else if (ch >= 'A' && ch <= 'Z') { hi = 0x82; lo = (u8)(0x60 + (ch - 'A')); }
        else if (ch >= 'a' && ch <= 'z') { hi = 0x82; lo = (u8)(0x81 + (ch - 'a')); }
        else switch (ch) {
            case ' ': lo = 0x40; break;   case ',': lo = 0x41; break;   case '.': lo = 0x44; break;
            case ':': lo = 0x46; break;   case ';': lo = 0x47; break;   case '?': lo = 0x48; break;
            case '!': lo = 0x49; break;   case '_': lo = 0x51; break;   case '/': lo = 0x5E; break;
            case '~': lo = 0x60; break;   case '\'': lo = 0x66; break;  case '"': lo = 0x68; break;
            case '(': lo = 0x69; break;   case ')': lo = 0x6A; break;   case '+': lo = 0x7B; break;
            case '-': lo = 0x7C; break;   case '%': lo = 0x93; break;   case '#': lo = 0x94; break;
            case '&': lo = 0x95; break;   case '*': lo = 0x96; break;   case '@': lo = 0x97; break;
            default: continue;
        }
        if (o + 2 > cap) break;
        dst[o++] = hi; dst[o++] = lo;
    }
    return o;
}

/* le o icon.sys de dentro de um .psu (entradas de 512 bytes + dados alinhados em 1024) */
/* le um arquivo de um save dentro de um VMC (para o titulo e detalhes) */
typedef struct { const char *name; Vmc *v; u8 *dst; int cap; long got; } VNamed;

static int vmc_named_cb(void *ctx, const VmcEnt *e)
{
    VNamed *n = (VNamed *)ctx;
    if ((e->mode & VMC_DF_FILE) && !strcmp(e->name, n->name)) {
        n->got = vmc_read_file(n->v, e, n->dst, (u32)n->cap);
        return 1;
    }
    return 0;
}

static int vmc_read_named(const char *path, const char *save, const char *file, u8 *dst, int cap)
{
    static Vmc v;
    VmcEnt dir;
    VNamed n;
    if (vmc_open(&v, path) != 0) return 0;
    if (vmc_find_root(&v, save, &dir) != 0) { vmc_close(&v); return 0; }
    n.name = file; n.v = &v; n.dst = dst; n.cap = cap; n.got = 0;
    vmc_list_dir(&v, &dir, vmc_named_cb, &n);
    vmc_close(&v);
    return n.got > 0 ? (int)n.got : 0;
}

static int psu_read_sys(const char *path, u8 *dst, int cap)
{
    u8 h[512];
    int fd, k, got = 0;
    u32 total;
    fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, h, 512) != 512 || !(ld16(h) & MC_ATTR_DIR)) { close(fd); return 0; }
    total = ld32(h + 4);                                  /* n de entradas (inclui . e ..) */
    if (total > 512) total = 512;
    for (k = 0; k < (int)total; k++) {
        u32 sz, pad;
        if (read(fd, h, 512) != 512) break;
        sz = ld32(h + 4);
        pad = (1024 - (sz & 1023)) & 1023;
        if (!(ld16(h) & MC_ATTR_DIR) && !strncmp((const char *)h + 64, "icon.sys", 9)) {
            int n = (int)(sz < (u32)cap ? sz : (u32)cap);
            got = read(fd, dst, n);
            break;
        }
        if (sz || pad) lseek(fd, (off_t)sz + (off_t)pad, SEEK_CUR);
    }
    close(fd);
    return got > 0 ? got : 0;
}

static void title_load(int isUsb, Entry *e)
{
    char path[200];
    u8 *sys = NULL, tmp[0x144];
    int sl = 0;

    e->title[0] = 0;
    if (!isUsb) {
        snprintf(path, sizeof(path), "/%s/icon.sys", e->name);
        sys = read_all(0, path, &sl);
    } else if (e->kind == FMT_DIR) {
        snprintf(path, sizeof(path), "%s/icon.sys", e->path);
        sys = read_all(1, path, &sl);
    } else if (e->kind == FMT_PSU) {
        sl = psu_read_sys(e->path, tmp, sizeof(tmp));
        if (sl >= 0x104 && !memcmp(tmp, "PS2D", 4)) sjis_title(tmp + 0xC0, (int)ld16(tmp + 6), e->title, sizeof(e->title));
        return;
    } else if (e->kind == FMT_VMC) {
        sl = vmc_read_named(e->path, e->name, "icon.sys", tmp, sizeof(tmp));
        if (sl >= 0x104 && !memcmp(tmp, "PS2D", 4)) sjis_title(tmp + 0xC0, (int)ld16(tmp + 6), e->title, sizeof(e->title));
        return;
    } else {
        return;                                       /* .max e comprimido: usa o nome da pasta */
    }
    if (!sys) return;
    if (sl >= 0x104 && !memcmp(sys, "PS2D", 4))
        sjis_title(sys + 0xC0, (int)ld16(sys + 6), e->title, sizeof(e->title));
    free(sys);
}

/* le 1 titulo por quadro (o do cursor primeiro, depois os visiveis) */
static void title_pump(int t, Entry *L, int N)
{
    int first = topRow[t] * COLS, last = first + gridRows * COLS, i, c = cursorPos[t];
    if (last > N) last = N;
    if (c >= first && c < last && !L[c].titleDone) i = c;
    else {
        for (i = first; i < last; i++) if (!L[i].titleDone) break;
        if (i >= last) return;
    }
    L[i].titleDone = 1;
    title_load(t != TAB_MC, &L[i]);
}

/* ---- aplica ordem / filtro / busca nas duas listas ---- */
static void sync_back(Entry *full, int fn, const Entry *view, int vn)
{
    int i, j;
    for (i = 0; i < vn; i++)
        for (j = 0; j < fn; j++)
            if (!strcmp(full[j].name, view[i].name) && !strcmp(full[j].stamp, view[i].stamp) && !strcmp(full[j].path, view[i].path)) {
                full[j] = view[i];                   /* guarda marcacoes e titulos ja lidos */
                break;
            }
}

/* a busca tambem procura no nome do jogo: le os titulos que faltam */
static void load_all_titles(void)
{
    int i, d = 0, tot = mcFullN + usbFullN;
    g_cancel = 0; g_multi = 1;
    ui.busy = 1;
    snprintf(ui.title, sizeof(ui.title), "LENDO NOMES DOS JOGOS");
    for (i = 0; i < mcFullN; i++) {
        if (cancel_poll()) break;
        if (!mcFull[i].titleDone) { mcFull[i].titleDone = 1; title_load(0, &mcFull[i]); }
        if ((++d & 3) == 0) progress("Memory Card", (u32)d, (u32)(tot ? tot : 1));
    }
    for (i = 0; i < usbFullN; i++) {
        if (cancel_poll()) break;
        if (!usbFull[i].titleDone) { usbFull[i].titleDone = 1; title_load(1, &usbFull[i]); }
        if ((++d & 3) == 0) progress("USB", (u32)d, (u32)(tot ? tot : 1));
    }
    ui.busy = 0; g_multi = 0;
}

static void view_apply_all(void)
{
    int t;
    sync_back(mcFull, mcFullN, mcList, mcCount);
    sync_back(usbFull, usbFullN, usbList, usbCount);
    if (searchStr[0]) load_all_titles();
    view_build(mcFull, mcFullN, mcList, &mcCount);
    view_build(usbFull, usbFullN, usbList, &usbCount);
    for (t = 0; t < 3; t++) { cursorPos[t] = 0; topRow[t] = 0; }
    icon_free_all(TAB_MC);
    icon_free_all(TAB_USB);
}

/* ---- teclado na tela: direcional move, X digita, Quadrado apaga, Triangulo espaco, L1/R1 maiuscula, START confirma ---- */
static int text_input(const char *title, char *buf, int max)
{
    int done = 0, ok = 0, lim = max - 1 < (int)sizeof(inBuf) - 1 ? max - 1 : (int)sizeof(inBuf) - 1;
    snprintf(inBuf, sizeof(inBuf), "%.*s", lim, buf);
    inLen = (int)strlen(inBuf);
    inKr = 0; inKc = 0; inCaps = 1;
    inTitle = title;
    inEditing = 1;
    while (!done) {
        int dr = 0, dc = 0, press = -1;                     /* press: 0 tecla, 1 espaco, 2 apagar, 3 limpar, 4 ok, 5 cancelar, 6 maiuscula */
        read_pad();
        if (pad_new & PAD_UP)    dr = -1;
        if (pad_new & PAD_DOWN)  dr = 1;
        if (pad_new & PAD_LEFT)  dc = -1;
        if (pad_new & PAD_RIGHT) dc = 1;
        if (dr) {
            int nr = (inKr + dr + 5) % 5, nc;
            if (inKr < 4 && nr == 4)      nc = inKc * 6 / 10;                       /* grade -> botoes */
            else if (inKr == 4 && nr < 4) nc = inKc * 10 / 6;                       /* botoes -> grade */
            else                          nc = inKc;
            if (nc >= kb_cols(nr)) nc = kb_cols(nr) - 1;
            inKr = nr; inKc = nc;
            snd_play(SND_MOVE);
        }
        if (dc) {
            inKc = (inKc + dc + kb_cols(inKr)) % kb_cols(inKr);
            snd_play(SND_MOVE);
        }
        if (pad_new & PAD_CROSS) {
            if (inKr < 4) press = 0;
            else          press = (inKc == 0) ? 6 : inKc;                           /* Aa, ESPACO, APAGAR, LIMPAR, OK, CANCELAR */
        }
        if (pad_new & PAD_SQUARE)   press = 2;
        if (pad_new & PAD_TRIANGLE) press = 1;
        if (pad_new & (PAD_L1 | PAD_R1)) press = 6;
        if (pad_new & PAD_START)    press = 4;
        if (pad_new & PAD_CIRCLE)   press = 5;
        switch (press) {
        case 0: if (inLen < lim) { inBuf[inLen++] = kb_char(inKr, inKc); inBuf[inLen] = 0; snd_play(SND_SELECT); } break;
        case 1: if (inLen < lim && inLen > 0 && inBuf[inLen - 1] != ' ') { inBuf[inLen++] = ' '; inBuf[inLen] = 0; snd_play(SND_SELECT); } break;
        case 2: if (inLen > 0) { inBuf[--inLen] = 0; snd_play(SND_BACK); } break;
        case 3: inLen = 0; inBuf[0] = 0; snd_play(SND_BACK); break;
        case 4: ok = 1; done = 1; snd_play(SND_OK); break;
        case 5: done = 1; snd_play(SND_BACK); break;
        case 6: inCaps = !inCaps; snd_play(SND_TAB); break;
        default: break;
        }
        render();
        present();
    }
    inEditing = 0;
    while (inLen > 0 && inBuf[inLen - 1] == ' ') inBuf[--inLen] = 0;                /* sem espaco sobrando no fim */
    if (ok) snprintf(buf, max, "%s", inBuf);
    return ok;
}

/* ---- menu: ordenar / filtrar por regiao / buscar ---- */
static void clear_filters(void)
{
    filtReg = -1;
    searchStr[0] = 0;
}

static void tool_view(void)
{
    for (;;) {
        char l0[40], l1[40], l2[48];
        const char *it[5];
        int c;
        snprintf(l0, sizeof(l0), "Ordenar por: %s", SORT_NAME[sortMode]);
        snprintf(l1, sizeof(l1), "Filtrar regiao: %s", filtReg < 0 ? "Todas" : FILT_NAME[filtReg]);
        snprintf(l2, sizeof(l2), "Buscar: %s", searchStr[0] ? searchStr : "(nenhuma)");
        it[0] = l0; it[1] = l1; it[2] = l2; it[3] = "Limpar filtro e busca"; it[4] = "Voltar";
        c = menu_run("ORGANIZAR / FILTRAR / BUSCAR", it, 5);
        if (c < 0 || c == 4) break;
        if (c == 0) sortMode = (sortMode + 1) % 4;
        else if (c == 1) { filtReg++; if (filtReg > 5) filtReg = -1; }
        else if (c == 2) {
            char t[24];
            snprintf(t, sizeof(t), "%s", searchStr);
            if (text_input("BUSCAR POR NOME OU JOGO", t, sizeof(t))) snprintf(searchStr, sizeof(searchStr), "%s", t);
        }
        else if (c == 3) { sortMode = 1; clear_filters(); }
        view_apply_all();
    }
    {
        char m[96];
        snprintf(m, sizeof(m), "Ordem: %s | Regiao: %s%s%s", SORT_NAME[sortMode],
                 filtReg < 0 ? "todas" : FILT_NAME[filtReg],
                 searchStr[0] ? " | Busca: " : "", searchStr[0] ? searchStr : "");
        toast_s(m, 0);
    }
}

/* ---- detalhes do save selecionado ---- */
static void dt_add(const char *name, u32 size)
{
    if (dtN >= (int)(sizeof(dtF) / sizeof(dtF[0]))) return;
    snprintf(dtF[dtN].name, sizeof(dtF[dtN].name), "%s", name);
    dtF[dtN].size = size;
    dtN++;
}

static void dt_psu(const char *path)
{
    u8 h[512];
    int fd, k;
    u32 total;
    fd = open(path, O_RDONLY);
    if (fd < 0) return;
    if (read(fd, h, 512) != 512 || !(ld16(h) & MC_ATTR_DIR)) { close(fd); return; }
    total = ld32(h + 4);
    if (total > 512) total = 512;
    for (k = 0; k < (int)total; k++) {
        u32 sz, pad;
        if (read(fd, h, 512) != 512) break;
        sz = ld32(h + 4);
        pad = (1024 - (sz & 1023)) & 1023;
        if (!(ld16(h) & MC_ATTR_DIR)) dt_add((const char *)h + 64, sz);
        if (sz || pad) lseek(fd, (off_t)sz + (off_t)pad, SEEK_CUR);
    }
    close(fd);
}

static int dt_vmc_cb(void *ctx, const VmcEnt *e)
{
    (void)ctx;
    if (e->mode & VMC_DF_FILE) dt_add(e->name, e->length);
    return 0;
}

static void dt_vmc(const char *path, const char *save)
{
    static Vmc v;
    VmcEnt dir;
    if (vmc_open(&v, path) != 0) return;
    if (vmc_find_root(&v, save, &dir) == 0) vmc_list_dir(&v, &dir, dt_vmc_cb, NULL);
    vmc_close(&v);
}

static void show_details(int t)
{
    static const char *RNAME[6] = { "NTSC-U (Americas)", "PAL (Europa)", "NTSC-J (Japao)", "NTSC-K (Coreia)", "Regiao desconhecida", "PlayStation 1" };
    Entry *L = (t == TAB_MC) ? mcList : usbList;
    int N = (t == TAB_MC) ? mcCount : usbCount, isUsb = (t != TAB_MC), done = 0, i, n;
    Entry *e;
    char sz[24], pat[64], sname[40];
    int changed = 0;

    if (N <= 0 || cursorPos[t] < 0 || cursorPos[t] >= N) return;
    if (t == TAB_MC) mcsize_all();
    e = &L[cursorPos[t]];
    if (!e->titleDone) { e->titleDone = 1; title_load(isUsb, e); }
    snprintf(sname, sizeof(sname), "%s", e->name);

    dtN = 0; dtScroll = 0; dtSel = 0; dtNote[0] = 0;
    dtCan = !isUsb;
    snprintf(dtTitle, sizeof(dtTitle), "%.46s", e->title[0] ? e->title : e->name);
    fmt_size(sz, sizeof(sz), e->size);
    snprintf(dtL[0], sizeof(dtL[0]), "Pasta: %s    %s: %s", e->name, e->kind == FMT_VMC ? "Cartao virtual" : "Formato", isUsb ? (e->kind == FMT_VMC ? e->stamp : FMT_NAME[e->kind]) : "Memory Card");
    snprintf(dtL[1], sizeof(dtL[1]), "Data: %s %s    Tamanho: %s", e->date, e->time, sz);
    snprintf(dtL[2], sizeof(dtL[2]), "Regiao: %s", RNAME[ent_region(e)]);

    if (!isUsb) {
        snprintf(pat, sizeof(pat), "/%s/*", e->name);
        n = mc_list(mcSlot, pat, subtbl, MAX_ENT);
        for (i = 0; i < n; i++) {
            if (subtbl[i].AttrFile & MC_ATTR_DIR) continue;
            dt_add((const char *)subtbl[i].EntryName, subtbl[i].FileSizeByte);
        }
    } else if (e->kind == FMT_DIR) {
        DIR *d = opendir(e->path);
        struct dirent *de;
        if (d) {
            while ((de = readdir(d)) != NULL) {
                char fp[300];
                struct stat st;
                if (de->d_name[0] == '.' || !strcmp(de->d_name, PS1_MARK)) continue;
                snprintf(fp, sizeof(fp), "%s/%s", e->path, de->d_name);
                if (stat(fp, &st) != 0 || S_ISDIR(st.st_mode)) continue;
                dt_add(de->d_name, (u32)st.st_size);
            }
            closedir(d);
        }
    } else if (e->kind == FMT_PSU) {
        dt_psu(e->path);
    } else if (e->kind == FMT_VMC) {
        dt_vmc(e->path, e->name);
    } else {
        snprintf(dtNote, sizeof(dtNote), "Lista de arquivos indisponivel neste formato (compactado)");
    }
    if (dtN == 0 && !dtNote[0]) snprintf(dtNote, sizeof(dtNote), "Nenhum arquivo encontrado");

    dtReg = ent_region(e);
    dtIcon = NULL;
    if (t < 2) {                                           /* icone 3D do save (gira); sem icone, usa o da regiao */
        int ci = cursorPos[t];
        if (!icons[t][ci] && !iconFail[t][ci]) {
            icons[t][ci] = icon_load(isUsb, e);
            if (!icons[t][ci]) iconFail[t][ci] = 1;
        }
        dtIcon = icons[t][ci];
        if (dtIcon) { gCos = ICON_CA0; gSin = ICON_SA0; spinTab = t; spinIdx = ci; }
    }

    dtOpen = 1;
    snd_play(SND_TAB);
    while (!done) {
        read_pad();
        if (dtIcon) { icon_spin(); icon_render((Icon *)dtIcon, gCos, gSin); }
        if (dtCan && dtN > 0) {
            if ((pad_new & PAD_DOWN) && dtSel < dtN - 1) { dtSel++; snd_play(SND_MOVE); }
            if ((pad_new & PAD_UP) && dtSel > 0)         { dtSel--; snd_play(SND_MOVE); }
            if (dtSel < dtScroll) dtScroll = dtSel;
            if (dtSel >= dtScroll + 10) dtScroll = dtSel - 9;
        } else {
            if ((pad_new & PAD_DOWN) && dtScroll + 10 < dtN) { dtScroll++; snd_play(SND_MOVE); }
            if ((pad_new & PAD_UP) && dtScroll > 0)          { dtScroll--; snd_play(SND_MOVE); }
        }
        if (dtCan) {
            int reload = 0;
            if ((pad_new & PAD_CROSS) && dtN > 0) {
                DFile cf = dtF[dtSel];
                hex_edit(sname, &cf);
                reload = 1;
            }
            if ((pad_new & PAD_TRIANGLE) && dtN > 0) dt_export_file(sname, &dtF[dtSel]);
            if (pad_new & PAD_SQUARE) { if (dt_import_file(sname)) reload = 1; }
            if (reload) {                                  /* recarrega a lista de arquivos do save */
                int i2, n2;
                snprintf(pat, sizeof(pat), "/%s/*", sname);
                n2 = mc_list(mcSlot, pat, subtbl, MAX_ENT);
                dtN = 0;
                for (i2 = 0; i2 < n2; i2++) {
                    if (subtbl[i2].AttrFile & MC_ATTR_DIR) continue;
                    dt_add((const char *)subtbl[i2].EntryName, subtbl[i2].FileSizeByte);
                }
                if (dtSel >= dtN) dtSel = dtN ? dtN - 1 : 0;
                changed = 1;
            }
            if (pad_new & PAD_CIRCLE) { done = 1; snd_play(SND_BACK); }
        } else if (pad_new & (PAD_CIRCLE | PAD_CROSS)) { done = 1; snd_play(SND_BACK); }
        render();
        present();
    }
    if (changed) refresh_mc();
    dtOpen = 0;
    dtIcon = NULL;
}

static int copy_mc_to_usb(const char *src, const char *dst, const char *label)
{
    int fd = -1, r = 0, out, err = 0;
    mcOpen(MP(mcSlot), MS(mcSlot), src, MC_RDONLY);
    mcSync(0, NULL, &fd);
    if (fd < 0) return -1;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) { mcClose(fd); mcSync(0, NULL, &r); return -2; }
    for (;;) {
        mcRead(fd, iobuf, CHUNK);
        mcSync(0, NULL, &r);
        if (r <= 0) { if (r < 0) err = -3; break; }
        if (write(out, iobuf, r) != r) { err = -4; break; }
        g_done += r;
        progress(label, g_done, g_total);
    }
    close(out);
    mcClose(fd); mcSync(0, NULL, &r);
    return err;
}

static int copy_usb_to_mc(const char *src, const char *dst, const char *label)
{
    int in, fd = -1, r = 0, w = 0, err = 0;
    in = open(src, O_RDONLY);
    if (in < 0) return -1;
    mcOpen(MP(mcSlot), MS(mcSlot), dst, MC_WRONLY | MC_CREAT | MC_TRUNC);
    mcSync(0, NULL, &fd);
    if (fd < 0) { close(in); return -2; }
    for (;;) {
        r = read(in, iobuf, CHUNK);
        if (r <= 0) { if (r < 0) err = -3; break; }
        mcWrite(fd, iobuf, r);
        mcSync(0, NULL, &w);
        if (w != r) { err = -4; break; }
        g_done += r;
        progress(label, g_done, g_total);
    }
    close(in);
    mcClose(fd); mcSync(0, NULL, &r);
    return err;
}

/* exporta 1 save do Memory Card -> USB. retorna n de falhas */
static int export_folder(const Entry *e, const char *stamp)
{
    char d1[128], d2[192], src[96], dst[300], lbl[96];
    int i, n, fails = 0;

    snprintf(d1, sizeof(d1), ROOT_DIR "/%s", e->name);
    snprintf(d2, sizeof(d2), "%s/%s", d1, stamp);
    mkdir(ROOT_DIR, 0777);
    mkdir(d1, 0777);
    mkdir(d2, 0777);

    if (e->ps1) {                                   /* marca o backup como save de PS1 */
        int mf;
        snprintf(dst, sizeof(dst), "%s/" PS1_MARK, d2);
        mf = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (mf >= 0) close(mf);
    }
    snprintf(src, sizeof(src), "/%s/*", e->name);
    n = mc_list(mcSlot, src, subtbl, MAX_ENT);
    for (i = 0; i < n; i++) {
        if (subtbl[i].AttrFile & MC_ATTR_DIR) continue;
        snprintf(src, sizeof(src), "/%s/%s", e->name, (char *)subtbl[i].EntryName);
        snprintf(dst, sizeof(dst), "%s/%s", d2, (char *)subtbl[i].EntryName);
        snprintf(lbl, sizeof(lbl), "%s", e->name);
        if (copy_mc_to_usb(src, dst, lbl) < 0) fails++;
    }
    return fails;
}

/* importa 1 backup USB -> Memory Card. retorna n de falhas */
static int import_folder(const Entry *e)
{
    char dir[200], src[300], dst[96], lbl[96];
    struct dirent *de;
    DIR *d;
    int r = 0, fails = 0;

    snprintf(dst, sizeof(dst), "/%s", e->name);
    mcMkDir(MP(mcSlot), MS(mcSlot), dst);
    mcSync(0, NULL, &r);                    /* ja existir nao e erro: sobrescreve arquivos */

    snprintf(dir, sizeof(dir), ROOT_DIR "/%s/%s", e->name, e->stamp);
    d = opendir(dir);
    if (!d) return 1;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!strcmp(de->d_name, PS1_MARK)) continue;
        snprintf(src, sizeof(src), "%s/%s", dir, de->d_name);
        if (is_dir(src)) continue;
        snprintf(dst, sizeof(dst), "/%s/%s", e->name, de->d_name);
        snprintf(lbl, sizeof(lbl), "%s", e->name);
        if (copy_usb_to_mc(src, dst, lbl) < 0) fails++;
    }
    closedir(d);
    return fails;
}

/* ============================================================
 *  Formatos .PSU (EMS / uLaunchELF / mymc) e .MAX (Action Replay MAX)
 * ============================================================ */
static int wr_all(int fd, const void *buf, u32 n)
{
    const u8 *p = (const u8 *)buf;
    while (n) {
        int w = write(fd, p, n > CHUNK ? CHUNK : n);
        if (w <= 0) return -1;
        p += w; n -= (u32)w;
    }
    return 0;
}

/* Memory Card -> descritor (copia o arquivo inteiro). retorna bytes ou -1 */
static int mc_file_to_fd(const char *src, int out, const char *label)
{
    int fd = -1, r = 0, total = 0;
    mcOpen(MP(mcSlot), MS(mcSlot), src, MC_RDONLY);
    mcSync(0, NULL, &fd);
    if (fd < 0) return -1;
    for (;;) {
        mcRead(fd, iobuf, CHUNK);
        mcSync(0, NULL, &r);
        if (r < 0) { total = -1; break; }
        if (r == 0) break;
        if (wr_all(out, iobuf, (u32)r) != 0) { total = -1; break; }
        total += r;
        g_done += (u32)r;
        progress(label, g_done, g_total);
    }
    mcClose(fd); mcSync(0, NULL, &r);
    return total;
}

/* Memory Card -> memoria (exatamente 'size' bytes). 0 = ok */
static int mc_file_to_mem(const char *src, u8 *dst, u32 size, const char *label)
{
    int fd = -1, r = 0;
    u32 got = 0;
    mcOpen(MP(mcSlot), MS(mcSlot), src, MC_RDONLY);
    mcSync(0, NULL, &fd);
    if (fd < 0) return -1;
    while (got < size) {
        u32 want = size - got;
        if (want > CHUNK) want = CHUNK;
        mcRead(fd, iobuf, (int)want);
        mcSync(0, NULL, &r);
        if (r <= 0) break;
        memcpy(dst + got, iobuf, (u32)r);
        got += (u32)r;
        g_done += (u32)r;
        progress(label, g_done, g_total);
    }
    mcClose(fd); mcSync(0, NULL, &r);
    return got == size ? 0 : -1;
}

/* descritor -> Memory Card ('size' bytes). -2 = nao abriu (nada foi lido) */
static int fd_to_mc(int in, u32 size, const char *dst, const char *label)
{
    int fd = -1, r, w = 0, err = 0;
    u32 left = size;
    mcOpen(MP(mcSlot), MS(mcSlot), dst, MC_WRONLY | MC_CREAT | MC_TRUNC);
    mcSync(0, NULL, &fd);
    if (fd < 0) return -2;
    while (left) {
        u32 want = left > CHUNK ? CHUNK : left;
        r = read(in, iobuf, want);
        if (r <= 0) { err = -3; break; }
        mcWrite(fd, iobuf, r);
        mcSync(0, NULL, &w);
        if (w != r) { err = -4; break; }
        left -= (u32)r;
        g_done += (u32)r;
        progress(label, g_done, g_total);
    }
    mcClose(fd); mcSync(0, NULL, &r);
    return err;
}

/* memoria -> Memory Card. -2 = nao abriu */
/* marca a pasta como "save de PS1" no cartao (atributo 0x9027), como faz o uLaunchELF ao restaurar.
 * Mantem as datas e o resto da entrada: le a entrada atual e so troca o atributo. 0 = ok */
static int ps1_flag(int card, const char *name)
{
    static sceMcTblGetDir tbl __attribute__((aligned(64)));
    char path[64];
    int r = 0, n;
    snprintf(path, sizeof(path), "/%s", name);
    memset(&tbl, 0, sizeof(tbl));
    n = mc_list(card, path, &tbl, 1);
    if (n < 1) return 1;
    tbl.AttrFile = 0x9027;
    mcSetFileInfo(MP(card), MS(card), path, &tbl, 0xFEED);       /* 0xFEED: valor usado pelo uLaunchELF */
    mcSync(0, NULL, &r);
    return r < 0 ? 1 : 0;
}

static int mem_to_mc(const u8 *src, u32 size, const char *dst, const char *label)
{
    int fd = -1, w = 0, r, err = 0;
    u32 off = 0;
    mcOpen(MP(mcSlot), MS(mcSlot), dst, MC_WRONLY | MC_CREAT | MC_TRUNC);
    mcSync(0, NULL, &fd);
    if (fd < 0) return -2;
    while (off < size) {
        u32 n = size - off;
        if (n > CHUNK) n = CHUNK;
        memcpy(iobuf, src + off, n);
        mcWrite(fd, iobuf, (int)n);
        mcSync(0, NULL, &w);
        if (w != (int)n) { err = -4; break; }
        off += n;
        g_done += n;
        progress(label, g_done, g_total);
    }
    mcClose(fd); mcSync(0, NULL, &r);
    return err;
}

/* ---------------- arquivos soltos e editor hex (tela de detalhes, Memory Card) ---------------- */
#define HEX_MAXBYTES (1024u * 1024)

static int dt_export_file(const char *save, const DFile *f)
{
    char src[96], dst[200], lbl[64];
    u8 *buf;
    int fd, ok;
    if (!usb_ready()) { usbOk = 0; toast_s("Conecte um pendrive/HD USB", 1); return 0; }
    if (f->size > 8u * 1024 * 1024) { toast_s("Arquivo grande demais para exportar", 1); return 0; }
    buf = (u8 *)malloc(f->size ? f->size : 1);
    if (!buf) { toast_s("Sem memoria", 1); return 0; }
    snprintf(src, sizeof(src), "/%s/%s", save, f->name);
    snprintf(lbl, sizeof(lbl), "%s", f->name);
    g_done = 0; g_total = f->size ? f->size : 1;
    if (f->size && mc_file_to_mem(src, buf, f->size, lbl) != 0) { free(buf); toast_s("Falha ao ler o arquivo", 1); return 0; }
    mkdir(ROOT_DIR, 0777);
    mkdir(ROOT_DIR "/arquivos", 0777);
    snprintf(dst, sizeof(dst), ROOT_DIR "/arquivos/%s__%s", save, f->name);
    fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    ok = (fd >= 0) && (f->size == 0 || wr_all(fd, buf, f->size) == 0);
    if (fd >= 0) close(fd);
    free(buf);
    if (!ok) { unlink(dst); toast_s("Falha ao gravar no USB", 1); return 0; }
    toast_s("Arquivo exportado: SaveManagerReis/arquivos", 0);
    return 1;
}

static int dt_import_file(const char *save)
{
    static char names[19][40];
    const char *it[20], *name;
    char path[200], dst[96], m[100], pre[40];
    DIR *d;
    struct dirent *de;
    struct stat st;
    int n = 0, c, fd, rc, ok;

    if (!usb_ready()) { usbOk = 0; toast_s("Conecte um pendrive/HD USB", 1); return 0; }
    d = opendir(ROOT_DIR "/arquivos");
    if (!d) { toast_s("Coloque o arquivo em SaveManagerReis/arquivos", 1); return 0; }
    while ((de = readdir(d)) != NULL && n < 19) {
        if (de->d_name[0] == '.' || strlen(de->d_name) > 38) continue;
        snprintf(path, sizeof(path), ROOT_DIR "/arquivos/%s", de->d_name);
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        snprintf(names[n], sizeof(names[n]), "%s", de->d_name);
        it[n] = names[n];
        n++;
    }
    closedir(d);
    if (n == 0) { toast_s("Nenhum arquivo em SaveManagerReis/arquivos", 1); return 0; }
    it[n] = "Cancelar";
    c = menu_run("IMPORTAR ARQUIVO PARA O SAVE", it, n + 1);
    if (c < 0 || c >= n) return 0;

    name = names[c];
    snprintf(pre, sizeof(pre), "%s__", save);              /* tira o prefixo "<SAVE>__" dos arquivos exportados */
    if (!strncmp(name, pre, strlen(pre))) name += strlen(pre);
    if (!safe_name(name)) { toast_s("Nome invalido (maximo 31 caracteres)", 1); return 0; }

    snprintf(m, sizeof(m), "Gravar %s no save? Substitui se ja existir", name);
    dtOpen = 0;
    ok = confirm("IMPORTAR ARQUIVO", m);
    dtOpen = 1;
    if (!ok) return 0;

    snprintf(path, sizeof(path), ROOT_DIR "/arquivos/%s", names[c]);
    fd = open(path, O_RDONLY);
    if (fd < 0 || stat(path, &st) != 0) { if (fd >= 0) close(fd); toast_s("Falha ao abrir o arquivo", 1); return 0; }
    snprintf(dst, sizeof(dst), "/%s/%s", save, name);
    g_done = 0; g_total = (u32)st.st_size ? (u32)st.st_size : 1;
    rc = fd_to_mc(fd, (u32)st.st_size, dst, name);
    close(fd);
    if (rc != 0) { toast_s("Falha ao gravar (sem espaco ou erro de leitura)", 1); return 1; }
    toast_s("Arquivo importado para o save", 0);
    return 1;
}

static void hex_close(void)
{
    free(hexBuf); free(hexOrig);
    hexBuf = hexOrig = NULL;
    hexOpen = 0; hexEditing = 0; hexDirty = 0;
}

/* grava as alteracoes: copia do original no USB (so na 1a vez), grava e, se falhar, restaura */
static int hex_save(void)
{
    char dst[96], bak[200], lbl[64];
    struct stat st;
    int ok;
    hexOpen = 0;
    ok = confirm("GRAVAR ALTERACOES?", usb_ready() ? "Uma copia do original fica no USB (arquivos)" : "ATENCAO: sem USB, nao havera copia do original");
    hexOpen = 1;
    if (!ok) return 0;
    if (usb_ready()) {
        snprintf(bak, sizeof(bak), ROOT_DIR "/arquivos/%s__%s.orig", hexSave, hexName);
        if (stat(bak, &st) != 0) {
            int fd;
            mkdir(ROOT_DIR, 0777);
            mkdir(ROOT_DIR "/arquivos", 0777);
            fd = open(bak, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0 || wr_all(fd, hexOrig, hexN) != 0) {
                if (fd >= 0) close(fd);
                unlink(bak);
                toast_s("Falha ao salvar a copia no USB: nada foi gravado", 1);
                return 0;
            }
            close(fd);
        }
    }
    snprintf(dst, sizeof(dst), "/%s/%s", hexSave, hexName);
    snprintf(lbl, sizeof(lbl), "%s", hexName);
    g_done = 0; g_total = hexN ? hexN : 1;
    if (mem_to_mc(hexBuf, hexN, dst, lbl) != 0) {
        g_done = 0;
        mem_to_mc(hexOrig, hexN, dst, lbl);                 /* tenta restaurar o original */
        toast_s("Falha ao gravar: original restaurado", 1);
        return 0;
    }
    memcpy(hexOrig, hexBuf, hexN);
    hexDirty = 0;
    toast_s("Arquivo gravado no Memory Card", 0);
    return 1;
}

static void hex_edit(const char *save, const DFile *f)
{
    char src[96], lbl[64];
    int done = 0, wasDt;
    if (f->size == 0) { toast_s("Arquivo vazio", 1); return; }
    if (f->size > HEX_MAXBYTES) { toast_s("Arquivo grande demais para o editor (max 1 MB)", 1); return; }
    hexBuf = (u8 *)malloc(f->size);
    hexOrig = (u8 *)malloc(f->size);
    if (!hexBuf || !hexOrig) { hex_close(); toast_s("Sem memoria", 1); return; }
    snprintf(src, sizeof(src), "/%s/%s", save, f->name);
    snprintf(lbl, sizeof(lbl), "%s", f->name);
    g_done = 0; g_total = f->size;
    if (mc_file_to_mem(src, hexBuf, f->size, lbl) != 0) { hex_close(); toast_s("Falha ao ler o arquivo", 1); return; }
    memcpy(hexOrig, hexBuf, f->size);
    hexN = f->size; hexCur = 0; hexTop = 0; hexEditing = 0; hexDirty = 0;
    snprintf(hexName, sizeof(hexName), "%s", f->name);
    snprintf(hexSave, sizeof(hexSave), "%s", save);
    wasDt = dtOpen; dtOpen = 0;                              /* a tela de detalhes fica por baixo */
    hexOpen = 1;
    snd_play(SND_TAB);

    while (!done) {
        u32 row;
        read_pad();
        if (hexEditing) {
            u8 *b = &hexBuf[hexCur];
            if (pad_new & PAD_UP)    { (*b)++;      hexDirty = 1; }
            if (pad_new & PAD_DOWN)  { (*b)--;      hexDirty = 1; }
            if (pad_new & PAD_RIGHT) { *b += 16;    hexDirty = 1; }
            if (pad_new & PAD_LEFT)  { *b -= 16;    hexDirty = 1; }
            if (pad_new & PAD_CROSS)  { hexEditing = 0; hexDirty = memcmp(hexBuf, hexOrig, hexN) != 0; snd_play(SND_OK); }
            if (pad_new & PAD_CIRCLE) { *b = hexSaved; hexEditing = 0; hexDirty = memcmp(hexBuf, hexOrig, hexN) != 0; snd_play(SND_BACK); }
        } else {
            if ((pad_new & PAD_RIGHT) && hexCur + 1 < hexN) { hexCur++; snd_play(SND_MOVE); }
            if ((pad_new & PAD_LEFT) && hexCur > 0)         { hexCur--; snd_play(SND_MOVE); }
            if ((pad_new & PAD_DOWN) && hexCur + 16 < hexN) { hexCur += 16; snd_play(SND_MOVE); }
            if ((pad_new & PAD_UP) && hexCur >= 16)         { hexCur -= 16; snd_play(SND_MOVE); }
            if (pad_new & PAD_R2) { hexCur = (hexCur + 256 < hexN) ? hexCur + 256 : hexN - 1; snd_play(SND_MOVE); }
            if (pad_new & PAD_L2) { hexCur = (hexCur >= 256) ? hexCur - 256 : 0; snd_play(SND_MOVE); }
            if (pad_new & PAD_CROSS) { hexSaved = hexBuf[hexCur]; hexEditing = 1; snd_play(SND_SELECT); }
            if ((pad_new & PAD_START) && hexDirty) hex_save();
            if (pad_new & PAD_CIRCLE) {
                int ok = 1;
                if (hexDirty) {
                    hexOpen = 0;
                    ok = confirm("SAIR SEM GRAVAR?", "As alteracoes feitas serao perdidas");
                    hexOpen = 1;
                }
                if (ok) { done = 1; snd_play(SND_BACK); }
            }
        }
        row = hexCur >> 4;
        if (row < hexTop) hexTop = row;
        if (row >= hexTop + 16) hexTop = row - 15;
        render();
        present();
    }
    hex_close();
    dtOpen = wasDt;
}

static const sceMcTblGetDir *mc_raw(const char *name)
{
    int i;
    for (i = 0; i < mcRawN; i++)
        if (!strcmp((const char *)mctbl[i].EntryName, name)) return &mctbl[i];
    return NULL;
}

/* ---------------- PSU ---------------- */
/* entrada de 512 bytes: mode(2) ?(2) tamanho(4) criado(8) setor(4) dirent(4) modificado(8) 0(32) nome(448) */
static void psu_hdr(u8 *h, u32 mode, u32 size, const u8 *cr, const u8 *mo, const char *name)
{
    memset(h, 0, 512);
    st16(h, mode);
    st32(h + 4, size);
    if (cr) memcpy(h + 8, cr, 8);
    if (mo) memcpy(h + 24, mo, 8);
    snprintf((char *)h + 64, 448, "%s", name);
}

static int export_psu(const Entry *e, const char *stamp)
{
    static const u8 zeros[1024];
    char d1[128], outp[200], src[96], lbl[96];
    u8 hdr[512];
    const sceMcTblGetDir *raw = mc_raw(e->name);
    const u8 *cr = raw ? (const u8 *)&raw->_Create : NULL;
    const u8 *mo = raw ? (const u8 *)&raw->_Modify : NULL;
    int i, n, nf = 0, fd, ok = 1;

    snprintf(src, sizeof(src), "/%s/*", e->name);
    n = mc_list(mcSlot, src, subtbl, MAX_ENT);
    for (i = 0; i < n; i++) if (!(subtbl[i].AttrFile & MC_ATTR_DIR)) nf++;
    if (nf == 0) return 1;

    snprintf(d1, sizeof(d1), ROOT_DIR "/%s", e->name);
    snprintf(outp, sizeof(outp), "%s/%s.psu", d1, stamp);
    mkdir(ROOT_DIR, 0777);
    mkdir(d1, 0777);
    fd = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return 1;

    psu_hdr(hdr, e->ps1 ? 0x9027 : 0x8427, (u32)nf + 2, cr, mo, e->name);  if (wr_all(fd, hdr, 512)) ok = 0;
    psu_hdr(hdr, 0x8427, 0, cr, mo, ".");                if (wr_all(fd, hdr, 512)) ok = 0;
    psu_hdr(hdr, 0x8427, 0, cr, mo, "..");               if (wr_all(fd, hdr, 512)) ok = 0;

    snprintf(lbl, sizeof(lbl), "%s", e->name);
    for (i = 0; i < n && ok; i++) {
        const sceMcTblGetDir *t = &subtbl[i];
        u32 sz, pad;
        if (t->AttrFile & MC_ATTR_DIR) continue;
        sz = t->FileSizeByte;
        snprintf(src, sizeof(src), "/%s/%s", e->name, (const char *)t->EntryName);
        psu_hdr(hdr, 0x8497, sz, (const u8 *)&t->_Create, (const u8 *)&t->_Modify, (const char *)t->EntryName);
        if (wr_all(fd, hdr, 512)) { ok = 0; break; }
        if (mc_file_to_fd(src, fd, lbl) != (int)sz) { ok = 0; break; }
        pad = (1024 - (sz & 1023)) & 1023;
        if (pad && wr_all(fd, zeros, pad)) ok = 0;
    }
    close(fd);
    if (!ok) { unlink(outp); return 1; }
    return 0;
}

static int import_psu(const Entry *e)
{
    u8 h[512];
    char dst[96], lbl[96], fname[448];
    int fd, r = 0, fails = 0;

    fd = open(e->path, O_RDONLY);
    if (fd < 0) return 1;
    if (read(fd, h, 512) != 512 || !(ld16(h) & MC_ATTR_DIR)) { close(fd); return 1; }

    snprintf(dst, sizeof(dst), "/%s", e->name);
    mcMkDir(MP(mcSlot), MS(mcSlot), dst);
    mcSync(0, NULL, &r);                         /* ja existir nao e erro */
    snprintf(lbl, sizeof(lbl), "%s", e->name);

    while (read(fd, h, 512) == 512) {
        u32 mode = ld16(h), sz = ld32(h + 4), pad;
        int rc;
        if (mode & MC_ATTR_DIR) continue;        /* "." e ".." */
        memcpy(fname, h + 64, 447);
        fname[447] = 0;
        pad = (1024 - (sz & 1023)) & 1023;
        if (!safe_name(fname)) {
            fails++;
            lseek(fd, (off_t)sz + (off_t)pad, SEEK_CUR);
            continue;
        }
        snprintf(dst, sizeof(dst), "/%s/%s", e->name, fname);
        rc = fd_to_mc(fd, sz, dst, lbl);
        if (rc == -2) { fails++; lseek(fd, (off_t)sz, SEEK_CUR); }
        else if (rc < 0) { fails++; break; }     /* fluxo quebrado no meio do arquivo */
        lseek(fd, (off_t)pad, SEEK_CUR);
    }
    close(fd);
    return fails;
}

/* ---------------- MAX ---------------- */
/* cabecalho (0x58 bytes): "Ps2PowerSave"(12) crc32(4) pasta(32) nome(32) tam_comprimido(4) n_arquivos(4)
 * seguido do fluxo LZARI ([u32 tam_descomprimido][dados]); arquivos: [u32 tam][nome 32][dados][pad p/ 16] */
static int max_put(void *ctx, const uint8_t *p, uint32_t n) { return wr_all(*(int *)ctx, p, (u32)n); }

typedef struct { const char *label; u32 base; } MaxTick;
static void max_tick(void *ctx, uint32_t pos)
{
    MaxTick *t = (MaxTick *)ctx;
    progress(t->label, t->base + (u32)pos, g_total);
}

static int export_max(const Entry *e, const char *stamp)
{
    char d1[128], outp[200], src[96], lbl[96];
    u8 h[0x58];
    u8 *arch;
    u32 archSz = 0, rawSum = 0, p = 0, base, crc = 0, off = 0;
    int i, n, nf = 0, fd, ok = 1, r;
    long comp;
    MaxTick mt;

    snprintf(src, sizeof(src), "/%s/*", e->name);
    n = mc_list(mcSlot, src, subtbl, MAX_ENT);
    for (i = 0; i < n; i++) {
        u32 sz;
        if (subtbl[i].AttrFile & MC_ATTR_DIR) continue;
        sz = subtbl[i].FileSizeByte;
        archSz += 36 + sz;
        archSz += (16 - ((archSz + 8) & 15)) & 15;
        rawSum += sz;
        nf++;
    }
    if (nf == 0 || archSz > MAX_ARCH) return 1;
    arch = (u8 *)malloc(archSz);
    if (!arch) return 1;
    memset(arch, 0, archSz);

    snprintf(lbl, sizeof(lbl), "%s", e->name);
    base = g_done;
    for (i = 0; i < n && ok; i++) {
        const sceMcTblGetDir *t = &subtbl[i];
        u32 sz;
        if (t->AttrFile & MC_ATTR_DIR) continue;
        sz = t->FileSizeByte;
        st32(arch + p, sz);
        snprintf((char *)arch + p + 4, 32, "%s", (const char *)t->EntryName);
        snprintf(src, sizeof(src), "/%s/%s", e->name, (const char *)t->EntryName);
        if (mc_file_to_mem(src, arch + p + 36, sz, lbl) != 0) { ok = 0; break; }
        p += 36 + sz;
        p += (16 - ((p + 8) & 15)) & 15;
    }
    if (!ok) { free(arch); return 1; }

    snprintf(d1, sizeof(d1), ROOT_DIR "/%s", e->name);
    snprintf(outp, sizeof(outp), "%s/%s.max", d1, stamp);
    mkdir(ROOT_DIR, 0777);
    mkdir(d1, 0777);
    fd = open(outp, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { free(arch); return 1; }

    memset(h, 0, sizeof(h));
    memcpy(h, "Ps2PowerSave", 12);
    snprintf((char *)h + 0x10, 32, "%s", e->name);
    snprintf((char *)h + 0x30, 32, "%s", e->name);
    st32(h + 0x54, (u32)nf);
    if (wr_all(fd, h, sizeof(h))) ok = 0;

    mt.label = lbl;
    mt.base = base + rawSum;                     /* a compressao ocupa a 2a metade da barra */
    if (ok) {
        comp = lzari_encode(arch, archSz, max_put, &fd, max_tick, &mt);
        if (comp < 0) ok = 0;
        else {
            st32(h, (u32)comp);                  /* tamanho comprimido (inclui o u32 de tamanho) */
            if (lseek(fd, 0x50, SEEK_SET) != 0x50 || wr_all(fd, h, 4)) ok = 0;
        }
    }
    free(arch);

    if (ok) {                                    /* CRC32 do arquivo inteiro, com o campo do CRC = 0 */
        lseek(fd, 0, SEEK_SET);
        for (;;) {
            r = read(fd, iobuf, CHUNK);
            if (r <= 0) break;
            if (off == 0 && r >= 16) memset(iobuf + 12, 0, 4);
            crc = crc32_update(crc, iobuf, (u32)r);
            off += (u32)r;
        }
        st32(h, crc);
        if (lseek(fd, 12, SEEK_SET) != 12 || wr_all(fd, h, 4)) ok = 0;
    }
    close(fd);
    g_done = base + 2 * rawSum;
    if (!ok) { unlink(outp); return 1; }
    return 0;
}

static struct { int fd, n, i; u8 buf[CHUNK]; } mrd;
static int max_get(void *ctx)
{
    (void)ctx;
    if (mrd.i >= mrd.n) {
        mrd.n = read(mrd.fd, mrd.buf, CHUNK);
        mrd.i = 0;
        if (mrd.n <= 0) { mrd.n = 0; return -1; }
    }
    return mrd.buf[mrd.i++];
}
static void max_dtick(void *ctx, uint32_t pos)
{
    (void)pos;
    progress((const char *)ctx, g_done, g_total);   /* mantem a tela viva durante a descompressao */
}

static int import_max(const Entry *e)
{
    u8 h[0x5C], *arch;
    char dst[96], lbl[96], fname[33];
    u32 nfiles, decSz, p = 0;
    int fd, r = 0, fails = 0, k;
    long got;

    fd = open(e->path, O_RDONLY);
    if (fd < 0) return 1;
    if (read(fd, h, sizeof(h)) != (int)sizeof(h) || memcmp(h, "Ps2PowerSave", 12) != 0) { close(fd); return 1; }
    nfiles = ld32(h + 0x54);
    decSz  = ld32(h + 0x58);
    if (nfiles == 0 || decSz == 0 || decSz > MAX_ARCH) { close(fd); return 1; }
    arch = (u8 *)malloc(decSz);
    if (!arch) { close(fd); return 1; }

    snprintf(lbl, sizeof(lbl), "%s", e->name);
    lseek(fd, 0x58, SEEK_SET);
    mrd.fd = fd; mrd.n = 0; mrd.i = 0;
    got = lzari_decode(max_get, NULL, arch, decSz, max_dtick, lbl);
    close(fd);
    if (got != (long)decSz) { free(arch); return 1; }

    snprintf(dst, sizeof(dst), "/%s", e->name);
    mcMkDir(MP(mcSlot), MS(mcSlot), dst);
    mcSync(0, NULL, &r);

    for (k = 0; k < (int)nfiles; k++) {
        u32 sz;
        if (p + 36 > decSz) { fails++; break; }
        sz = ld32(arch + p);
        memcpy(fname, arch + p + 4, 32);
        fname[32] = 0;
        if (sz > decSz - p - 36) { fails++; break; }
        if (!safe_name(fname)) fails++;
        else {
            snprintf(dst, sizeof(dst), "/%s/%s", e->name, fname);
            if (mem_to_mc(arch + p + 36, sz, dst, lbl) < 0) fails++;
        }
        p += 36 + sz;
        p += (16 - ((p + 8) & 15)) & 15;
    }
    free(arch);
    return fails;
}

/* ---------------- Conversao .psu/.max -> pasta (so no USB) ---------------- */
static int fd_to_file(int in, u32 size, const char *dst, const char *label)
{
    int out, r, err = 0;
    u32 left = size;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) return -2;
    while (left) {
        r = read(in, iobuf, left > CHUNK ? CHUNK : left);
        if (r <= 0) { err = -3; break; }
        if (wr_all(out, iobuf, (u32)r) != 0) { err = -4; break; }
        left -= (u32)r;
        g_done += (u32)r;
        progress(label, g_done, g_total);
    }
    close(out);
    return err;
}

static int mem_to_file(const u8 *src, u32 size, const char *dst, const char *label)
{
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) return -2;
    if (wr_all(out, src, size) != 0) { close(out); return -4; }
    close(out);
    g_done += size;
    progress(label, g_done, g_total);
    return 0;
}

static void conv_dir(const Entry *e, char *dir, int n)
{
    char d1[128];
    snprintf(d1, sizeof(d1), ROOT_DIR "/%s", e->name);
    snprintf(dir, n, "%s/%s", d1, e->stamp);
    mkdir(ROOT_DIR, 0777);
    mkdir(d1, 0777);
    mkdir(dir, 0777);
}

__attribute__((unused)) static int convert_psu(const Entry *e)
{
    u8 h[512];
    char dir[200], dst[300], lbl[96], fname[448];
    int fd, fails = 0;
    fd = open(e->path, O_RDONLY);
    if (fd < 0) return 1;
    if (read(fd, h, 512) != 512 || !(ld16(h) & MC_ATTR_DIR)) { close(fd); return 1; }
    conv_dir(e, dir, sizeof(dir));
    snprintf(lbl, sizeof(lbl), "%s", e->name);
    while (read(fd, h, 512) == 512) {
        u32 mode = ld16(h), sz = ld32(h + 4), pad;
        int rc;
        if (mode & MC_ATTR_DIR) continue;
        memcpy(fname, h + 64, 447);
        fname[447] = 0;
        pad = (1024 - (sz & 1023)) & 1023;
        if (!safe_name(fname)) { fails++; lseek(fd, (off_t)sz + (off_t)pad, SEEK_CUR); continue; }
        snprintf(dst, sizeof(dst), "%s/%s", dir, fname);
        rc = fd_to_file(fd, sz, dst, lbl);
        if (rc == -2) { fails++; lseek(fd, (off_t)sz, SEEK_CUR); }
        else if (rc < 0) { fails++; break; }
        lseek(fd, (off_t)pad, SEEK_CUR);
    }
    close(fd);
    return fails;
}

__attribute__((unused)) static int convert_max(const Entry *e)
{
    u8 h[0x5C], *arch;
    char dir[200], dst[300], lbl[96], fname[33];
    u32 nfiles, decSz, p = 0;
    int fd, fails = 0, k;
    long got;
    fd = open(e->path, O_RDONLY);
    if (fd < 0) return 1;
    if (read(fd, h, sizeof(h)) != (int)sizeof(h) || memcmp(h, "Ps2PowerSave", 12) != 0) { close(fd); return 1; }
    nfiles = ld32(h + 0x54);
    decSz  = ld32(h + 0x58);
    if (nfiles == 0 || decSz == 0 || decSz > MAX_ARCH) { close(fd); return 1; }
    arch = (u8 *)malloc(decSz);
    if (!arch) { close(fd); return 1; }
    snprintf(lbl, sizeof(lbl), "%s", e->name);
    lseek(fd, 0x58, SEEK_SET);
    mrd.fd = fd; mrd.n = 0; mrd.i = 0;
    got = lzari_decode(max_get, NULL, arch, decSz, max_dtick, lbl);
    close(fd);
    if (got != (long)decSz) { free(arch); return 1; }
    conv_dir(e, dir, sizeof(dir));
    for (k = 0; k < (int)nfiles; k++) {
        u32 sz;
        if (p + 36 > decSz) { fails++; break; }
        sz = ld32(arch + p);
        memcpy(fname, arch + p + 4, 32);
        fname[32] = 0;
        if (sz > decSz - p - 36) { fails++; break; }
        if (!safe_name(fname)) fails++;
        else {
            snprintf(dst, sizeof(dst), "%s/%s", dir, fname);
            if (mem_to_file(arch + p + 36, sz, dst, lbl) < 0) fails++;
        }
        p += 36 + sz;
        p += (16 - ((p + 8) & 15)) & 15;
    }
    free(arch);
    return fails;
}

/* ---------------- Aba CONVERTER: Pasta <-> PSU <-> MAX (tudo no USB) ---------------- */
#define PK_MAXF 128
typedef struct { char name[32]; u32 size; u8 *data; u8 cr[8], mo[8]; } PkFile;
typedef struct { int n; u32 total; int ps1; PkFile f[PK_MAXF]; } Pack;
static Pack pk;

static void pk_free(void)
{
    int i;
    for (i = 0; i < pk.n; i++) { free(pk.f[i].data); pk.f[i].data = NULL; }
    pk.n = 0; pk.total = 0; pk.ps1 = 0;
}

static PkFile *pk_add(const char *name, u32 size, const u8 *cr, const u8 *mo)
{
    PkFile *f;
    if (pk.n >= PK_MAXF || pk.total + size > MAX_ARCH) return NULL;
    f = &pk.f[pk.n];
    memset(f, 0, sizeof(*f));
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->size = size;
    if (cr) memcpy(f->cr, cr, 8);
    if (mo) memcpy(f->mo, mo, 8);
    f->data = (u8 *)malloc(size ? size : 1);
    if (!f->data) return NULL;
    pk.n++;
    pk.total += size;
    return f;
}

static int rd_exact(int fd, u8 *dst, u32 size, const char *label)
{
    u32 got = 0;
    while (got < size) {
        u32 want = size - got;
        int r = read(fd, dst + got, want > CHUNK ? CHUNK : want);
        if (r <= 0) return -1;
        got += (u32)r;
        g_done += (u32)r;
        progress(label, g_done, g_total);
    }
    return 0;
}

static int load_folder(const Entry *e)
{
    DIR *d = opendir(e->path);
    struct dirent *de;
    if (!d) return 1;
    while ((de = readdir(d)) != NULL) {
        char fp[300];
        struct stat st;
        PkFile *f;
        int fd, rc;
        if (de->d_name[0] == '.' || !safe_name(de->d_name)) continue;
        if (!strcmp(de->d_name, PS1_MARK)) { pk.ps1 = 1; continue; }
        snprintf(fp, sizeof(fp), "%s/%s", e->path, de->d_name);
        if (stat(fp, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        f = pk_add(de->d_name, (u32)st.st_size, NULL, NULL);
        if (!f) { closedir(d); return 1; }
        fd = open(fp, O_RDONLY);
        if (fd < 0) { closedir(d); return 1; }
        rc = rd_exact(fd, f->data, f->size, e->name);
        close(fd);
        if (rc) { closedir(d); return 1; }
    }
    closedir(d);
    return pk.n ? 0 : 1;
}

static int load_psu(const Entry *e)
{
    u8 h[512];
    char fname[448];
    int fd;
    fd = open(e->path, O_RDONLY);
    if (fd < 0) return 1;
    if (read(fd, h, 512) != 512 || !(ld16(h) & MC_ATTR_DIR)) { close(fd); return 1; }
    pk.ps1 = (ld16(h) & 0x1000) ? 1 : 0;
    while (read(fd, h, 512) == 512) {
        u32 mode = ld16(h), sz = ld32(h + 4), pad;
        PkFile *f;
        if (mode & MC_ATTR_DIR) continue;
        memcpy(fname, h + 64, 447);
        fname[447] = 0;
        pad = (1024 - (sz & 1023)) & 1023;
        if (!safe_name(fname)) { lseek(fd, (off_t)sz + (off_t)pad, SEEK_CUR); continue; }
        f = pk_add(fname, sz, h + 8, h + 24);
        if (!f || rd_exact(fd, f->data, sz, e->name)) { close(fd); return 1; }
        lseek(fd, (off_t)pad, SEEK_CUR);
    }
    close(fd);
    return pk.n ? 0 : 1;
}

static int load_max(const Entry *e)
{
    u8 h[0x5C], *arch;
    char fname[33];
    u32 nfiles, decSz, p = 0;
    int fd, k, bad = 0;
    long got;
    fd = open(e->path, O_RDONLY);
    if (fd < 0) return 1;
    if (read(fd, h, sizeof(h)) != (int)sizeof(h) || memcmp(h, "Ps2PowerSave", 12) != 0) { close(fd); return 1; }
    nfiles = ld32(h + 0x54);
    decSz  = ld32(h + 0x58);
    if (nfiles == 0 || decSz == 0 || decSz > MAX_ARCH) { close(fd); return 1; }
    arch = (u8 *)malloc(decSz);
    if (!arch) { close(fd); return 1; }
    lseek(fd, 0x58, SEEK_SET);
    mrd.fd = fd; mrd.n = 0; mrd.i = 0;
    got = lzari_decode(max_get, NULL, arch, decSz, max_dtick, (void *)e->name);
    close(fd);
    if (got != (long)decSz) { free(arch); return 1; }
    for (k = 0; k < (int)nfiles; k++) {
        u32 sz;
        PkFile *f;
        if (p + 36 > decSz) { bad = 1; break; }
        sz = ld32(arch + p);
        memcpy(fname, arch + p + 4, 32);
        fname[32] = 0;
        if (sz > decSz - p - 36) { bad = 1; break; }
        if (safe_name(fname)) {
            f = pk_add(fname, sz, NULL, NULL);
            if (!f) { bad = 1; break; }
            memcpy(f->data, arch + p + 36, sz);
        }
        p += 36 + sz;
        p += (16 - ((p + 8) & 15)) & 15;
    }
    free(arch);
    g_done += decSz;
    return (bad || pk.n == 0) ? 1 : 0;
}

/* ---------------- CodeBreaker (.cbs) / SharkPort (.sps) / X-Port (.xps) ---------------- */
#define SV_MAX (6u * 1024 * 1024)               /* limite (a RAM do PS2 e pequena) */

static int sv_add(void *ctx, const char *name, const uint8_t *data, uint32_t size, const uint8_t *cr, const uint8_t *mo)
{
    PkFile *f;
    (void)ctx;
    if (!safe_name(name)) return 0;              /* ignora nome invalido */
    f = pk_add(name, size, cr, mo);
    if (!f) return -1;
    if (size) memcpy(f->data, data, size);
    return 0;
}

static int load_sv(const Entry *e)
{
    struct stat st;
    u8 *buf;
    char dn[64];
    int fd, rc;
    u32 n;
    if (stat(e->path, &st) != 0 || st.st_size <= 0 || (u32)st.st_size > SV_MAX) return 1;
    n = (u32)st.st_size;
    buf = (u8 *)malloc(n);
    if (!buf) return 1;
    fd = open(e->path, O_RDONLY);
    if (fd < 0) { free(buf); return 1; }
    rc = rd_exact(fd, buf, n, e->name);
    close(fd);
    if (rc) { free(buf); return 1; }
    pk_free();
    dn[0] = 0;
    if (e->kind == FMT_CBS) rc = sv_parse_cbs(buf, n, SV_MAX, dn, sv_add, NULL);
    else                    rc = sv_parse_sps(buf, n, dn, sv_add, NULL);
    free(buf);
    if (rc != 0 || pk.n == 0) { pk_free(); return 1; }
    g_done += n;
    return 0;
}

/* ---------------- cartao virtual (VMC): carrega um save para o pacote em memoria ---------------- */
typedef struct { Vmc *v; int fails; } VLoad;

static int vmc_file_cb(void *ctx, const VmcEnt *e)
{
    VLoad *l = (VLoad *)ctx;
    PkFile *f;
    if (!(e->mode & VMC_DF_FILE) || !safe_name(e->name)) return 0;
    f = pk_add(e->name, e->length, e->cr, e->mo);
    if (!f) { l->fails = 1; return 1; }
    if (e->length && vmc_read_file(l->v, e, f->data, e->length) != (long)e->length) { l->fails = 1; return 1; }
    return 0;
}

static int load_vmc(const Entry *e)
{
    static Vmc v;
    VmcEnt dir;
    VLoad l;
    pk_free();
    if (vmc_open(&v, e->path) != 0) return 1;
    if (vmc_find_root(&v, e->name, &dir) != 0) { vmc_close(&v); return 1; }
    l.v = &v; l.fails = 0;
    pk.ps1 = (dir.mode & VMC_DF_PSX) ? 1 : 0;
    vmc_list_dir(&v, &dir, vmc_file_cb, &l);
    vmc_close(&v);
    if (l.fails || pk.n == 0) { pk_free(); return 1; }
    g_done += pk.total;
    return 0;
}

/* grava o pacote em memoria no Memory Card (pasta /NOME) */
static int pk_to_mc(const Entry *e)
{
    char dst[96];
    int i, r = 0, fails = 0;
    snprintf(dst, sizeof(dst), "/%s", e->name);
    mcMkDir(MP(mcSlot), MS(mcSlot), dst);
    mcSync(0, NULL, &r);                         /* ja existir nao e erro */
    for (i = 0; i < pk.n; i++) {
        snprintf(dst, sizeof(dst), "/%s/%s", e->name, pk.f[i].name);
        if (mem_to_mc(pk.f[i].data, pk.f[i].size, dst, e->name) < 0) fails++;
    }
    pk_free();
    return fails;
}

static int import_sv(const Entry *e)
{
    if (load_sv(e)) return 1;
    return pk_to_mc(e);
}

static int import_vmc(const Entry *e)
{
    if (load_vmc(e)) return 1;
    return pk_to_mc(e);
}

static int write_folder(const Entry *e, const char *d1)
{
    char dir[200], dst[300];
    int i, fails = 0;
    snprintf(dir, sizeof(dir), "%s/%s", d1, e->stamp);
    mkdir(dir, 0777);
    if (pk.ps1) {
        int mf;
        snprintf(dst, sizeof(dst), "%s/" PS1_MARK, dir);
        mf = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (mf >= 0) close(mf);
    }
    for (i = 0; i < pk.n; i++) {
        snprintf(dst, sizeof(dst), "%s/%s", dir, pk.f[i].name);
        if (mem_to_file(pk.f[i].data, pk.f[i].size, dst, e->name) < 0) fails++;
    }
    return fails;
}

static int write_psu(const Entry *e, const char *d1)
{
    static const u8 zeros[1024];
    char outp[300];
    u8 hdr[512];
    int i, fd, ok = 1;
    snprintf(outp, sizeof(outp), "%s/%s.psu", d1, e->stamp);
    fd = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return 1;
    psu_hdr(hdr, pk.ps1 ? 0x9027 : 0x8427, (u32)pk.n + 2, pk.f[0].cr, pk.f[0].mo, e->name); if (wr_all(fd, hdr, 512)) ok = 0;
    psu_hdr(hdr, 0x8427, 0, pk.f[0].cr, pk.f[0].mo, ".");                 if (wr_all(fd, hdr, 512)) ok = 0;
    psu_hdr(hdr, 0x8427, 0, pk.f[0].cr, pk.f[0].mo, "..");                if (wr_all(fd, hdr, 512)) ok = 0;
    for (i = 0; i < pk.n && ok; i++) {
        u32 sz = pk.f[i].size, pad = (1024 - (sz & 1023)) & 1023;
        psu_hdr(hdr, 0x8497, sz, pk.f[i].cr, pk.f[i].mo, pk.f[i].name);
        if (wr_all(fd, hdr, 512) || wr_all(fd, pk.f[i].data, sz) || (pad && wr_all(fd, zeros, pad))) ok = 0;
        g_done += sz;
        progress(e->name, g_done, g_total);
    }
    close(fd);
    if (!ok) { unlink(outp); return 1; }
    return 0;
}

static int write_max(const Entry *e, const char *d1)
{
    char outp[300];
    u8 h[0x58], *arch;
    u32 archSz = 0, p = 0, crc = 0, off = 0, base = g_done;
    int i, fd, ok = 1, r;
    long comp;
    MaxTick mt;
    for (i = 0; i < pk.n; i++) {
        archSz += 36 + pk.f[i].size;
        archSz += (16 - ((archSz + 8) & 15)) & 15;
    }
    arch = (u8 *)malloc(archSz);
    if (!arch) return 1;
    memset(arch, 0, archSz);
    for (i = 0; i < pk.n; i++) {
        st32(arch + p, pk.f[i].size);
        snprintf((char *)arch + p + 4, 32, "%s", pk.f[i].name);
        memcpy(arch + p + 36, pk.f[i].data, pk.f[i].size);
        p += 36 + pk.f[i].size;
        p += (16 - ((p + 8) & 15)) & 15;
    }
    snprintf(outp, sizeof(outp), "%s/%s.max", d1, e->stamp);
    fd = open(outp, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { free(arch); return 1; }
    memset(h, 0, sizeof(h));
    memcpy(h, "Ps2PowerSave", 12);
    snprintf((char *)h + 0x10, 32, "%s", e->name);
    snprintf((char *)h + 0x30, 32, "%s", e->name);
    st32(h + 0x54, (u32)pk.n);
    if (wr_all(fd, h, sizeof(h))) ok = 0;
    mt.label = e->name; mt.base = base;
    if (ok) {
        comp = lzari_encode(arch, archSz, max_put, &fd, max_tick, &mt);
        if (comp < 0) ok = 0;
        else { st32(h, (u32)comp); if (lseek(fd, 0x50, SEEK_SET) != 0x50 || wr_all(fd, h, 4)) ok = 0; }
    }
    free(arch);
    if (ok) {
        lseek(fd, 0, SEEK_SET);
        for (;;) {
            r = read(fd, iobuf, CHUNK);
            if (r <= 0) break;
            if (off == 0 && r >= 16) memset(iobuf + 12, 0, 4);
            crc = crc32_update(crc, iobuf, (u32)r);
            off += (u32)r;
        }
        st32(h, crc);
        if (lseek(fd, 12, SEEK_SET) != 12 || wr_all(fd, h, 4)) ok = 0;
    }
    close(fd);
    g_done = base + pk.total;
    if (!ok) { unlink(outp); return 1; }
    return 0;
}

/* converte um backup (qualquer formato) para 'target'. 0 = ok */
static int convert_entry(const Entry *e, int target)
{
    char d1[128];
    int rc;
    pk_free();
    if (e->kind == FMT_PSU)      rc = load_psu(e);
    else if (e->kind == FMT_MAX) rc = load_max(e);
    else if (e->kind == FMT_VMC) rc = load_vmc(e);
    else if (e->kind >= FMT_CBS) rc = load_sv(e);
    else                         rc = load_folder(e);
    if (rc == 0) {
        snprintf(d1, sizeof(d1), ROOT_DIR "/%s", e->name);
        mkdir(ROOT_DIR, 0777);
        mkdir(d1, 0777);
        if (target == FMT_PSU)      rc = write_psu(e, d1);
        else if (target == FMT_MAX) rc = write_max(e, d1);
        else                        rc = write_folder(e, d1);
    }
    pk_free();
    return rc;
}

/* ---------------- Ferramentas do Memory Card (baixo nivel) ---------------- */
static char badName[MAX_ENT][40];
static int  badN;

/* le o arquivo inteiro e confere o tamanho. 1 = ok */
static int mc_read_check(const char *path, u32 expect)
{
    int fd = -1, r = 0;
    u32 total = 0;
    mcOpen(MP(mcSlot), MS(mcSlot), path, MC_RDONLY);
    mcSync(0, NULL, &fd);
    if (fd < 0) return 0;
    for (;;) {
        mcRead(fd, iobuf, CHUNK);
        mcSync(0, NULL, &r);
        if (r < 0) { total = 0xFFFFFFFFu; break; }
        if (r == 0) break;
        total += (u32)r;
    }
    mcClose(fd); mcSync(0, NULL, &r);
    return total == expect;
}

/* verifica todos os saves de mcList: precisa de icon.sys e de todos os arquivos legiveis.
 * preenche badName/badN e devolve quantos tem problema */
static int mc_verify(void)
{
    int i, j, n;
    char pat[64], path[96];
    badN = 0;
    for (i = 0; i < mcFullN; i++) {
        int hasIcon = 0, nf = 0, ok = 1;
        if (cancel_poll()) break;
        if (mcFull[i].ps1) continue;                 /* saves de PS1 nao tem icon.sys */
        progress(mcFull[i].name, (u32)i, (u32)(mcFullN ? mcFullN : 1));
        snprintf(pat, sizeof(pat), "/%s/*", mcFull[i].name);
        n = mc_list(mcSlot, pat, subtbl, MAX_ENT);
        for (j = 0; j < n; j++) {
            if (subtbl[j].AttrFile & MC_ATTR_DIR) continue;
            nf++;
            if (!strcmp((const char *)subtbl[j].EntryName, "icon.sys")) hasIcon = 1;
            snprintf(path, sizeof(path), "/%s/%s", mcFull[i].name, (const char *)subtbl[j].EntryName);
            if (!mc_read_check(path, subtbl[j].FileSizeByte)) ok = 0;
        }
        if (!hasIcon || nf == 0) ok = 0;
        if (!ok && badN < MAX_ENT) snprintf(badName[badN++], sizeof(badName[0]), "%s", mcFull[i].name);
    }
    return badN;
}

/* apaga uma pasta de save (arquivos e depois a pasta). retorna numero de falhas */
static u32 delBase = 0, delSpan = 0;                     /* progresso do apagar (0 = sem barra) */
static int mc_delete_save(const char *name)
{
    char pat[64], path[96];
    int n, j, r = 0, fails = 0;
    snprintf(pat, sizeof(pat), "/%s/*", name);
    n = mc_list(mcSlot, pat, subtbl, MAX_ENT);
    for (j = 0; j < n; j++) {
        if (subtbl[j].AttrFile & MC_ATTR_DIR) continue;
        snprintf(path, sizeof(path), "/%s/%s", name, (const char *)subtbl[j].EntryName);
        mcDelete(MP(mcSlot), MS(mcSlot), path);
        mcSync(0, NULL, &r);
        if (r < 0) fails++;
        if (delSpan) { g_done = delBase + delSpan * (u32)(j + 1) / (u32)(n + 1); progress(name, g_done, g_total); }
    }
    snprintf(path, sizeof(path), "/%s", name);
    mcDelete(MP(mcSlot), MS(mcSlot), path);
    mcSync(0, NULL, &r);
    if (r < 0) fails++;
    return fails;
}

static int mc_format_card(void)
{
    int r = 0;
    mcFormat(MP(mcSlot), MS(mcSlot));
    mcSync(0, NULL, &r);
    return r;
}

/* copia uma pasta de save entre as duas portas de Memory Card. retorna numero de falhas */
static int mc_copy_save(const char *name, int srcPort, int dstPort, const char *label)
{
    char pat[64], sp[96];
    int n, j, r = 0, fails = 0;
    snprintf(pat, sizeof(pat), "/%s/*", name);
    n = mc_list(srcPort, pat, subtbl, MAX_ENT);
    snprintf(sp, sizeof(sp), "/%s", name);
    mcMkDir(MP(dstPort), MS(dstPort), sp);
    mcSync(0, NULL, &r);                              /* ja existir nao e erro */
    for (j = 0; j < n; j++) {
        int in = -1, out = -1, w = 0, err = 0;
        char f[96];
        if (subtbl[j].AttrFile & MC_ATTR_DIR) continue;
        snprintf(f, sizeof(f), "/%s/%s", name, (const char *)subtbl[j].EntryName);
        mcOpen(MP(srcPort), MS(srcPort), f, MC_RDONLY);
        mcSync(0, NULL, &in);
        if (in < 0) { fails++; continue; }
        mcOpen(MP(dstPort), MS(dstPort), f, MC_WRONLY | MC_CREAT | MC_TRUNC);
        mcSync(0, NULL, &out);
        if (out < 0) { mcClose(in); mcSync(0, NULL, &r); fails++; continue; }
        for (;;) {
            mcRead(in, iobuf, CHUNK);
            mcSync(0, NULL, &r);
            if (r < 0) { err = 1; break; }
            if (r == 0) break;
            mcWrite(out, iobuf, r);
            mcSync(0, NULL, &w);
            if (w != r) { err = 1; break; }
            g_done += (u32)r;
            progress(label, g_done, g_total);
        }
        mcClose(in);  mcSync(0, NULL, &r);
        mcClose(out); mcSync(0, NULL, &r);
        if (err) fails++;
    }
    return fails;
}

/* o Memory Card da porta indicada esta legivel? devolve KB livres em *freeKB */
static int mc_port_ready(int port, int *freeKB)
{
    int type, freec = 0, fmt = 0, ret = -10, ok;
    mcGetInfo(MP(port), MS(port), &type, &freec, &fmt);
    mcSync(0, NULL, &ret);
    mcGetInfo(MP(port), MS(port), &type, &freec, &fmt);
    mcSync(0, NULL, &ret);
    ok = (ret >= -1) && (ret != -2);
    if (ret == -1) ok = (fmt != 0);
    *freeKB = ok ? freec : 0;
    return ok;
}

/* ---------------- Icone personalizado (BMP 128x128 -> textura do .ico) ---------------- */
/* BMP 24/32 bits sem compressao, 128x128 -> 16384 pixels RGB555 (bit 15 ligado) */
static int bmp_to_tex(const u8 *b, int len, u16 *out, const char **err)
{
    u32 off, hsz, comp;
    int w, h, bpp, stride, top, x, y;
    if (len < 54 || b[0] != 'B' || b[1] != 'M') { *err = "Nao e um arquivo BMP"; return 0; }
    off = ld32(b + 10); hsz = ld32(b + 14);
    if (hsz < 40) { *err = "BMP antigo nao suportado"; return 0; }
    w = (int)ld32(b + 18); h = (int)ld32(b + 22);
    bpp = (int)ld16(b + 28); comp = ld32(b + 30);
    top = h < 0;
    if (top) h = -h;
    if (w != 128 || h != 128) { *err = "A imagem precisa ter 128x128 pixels"; return 0; }
    if ((bpp != 24 && bpp != 32) || (comp != 0 && !(bpp == 32 && comp == 3))) { *err = "Salve como BMP de 24 bits, sem compressao"; return 0; }
    stride = ((w * bpp + 31) / 32) * 4;
    if ((u32)len < off + (u32)stride * 128u) { *err = "Arquivo BMP incompleto"; return 0; }
    for (y = 0; y < 128; y++) {
        const u8 *p = b + off + (u32)(top ? y : 127 - y) * (u32)stride;
        for (x = 0; x < 128; x++, p += bpp / 8)
            out[y * 128 + x] = (u16)((p[2] >> 3) | ((p[1] >> 3) << 5) | ((p[0] >> 3) << 10) | 0x8000);
    }
    return 1;
}

/* posicao onde comeca a textura (ate o fim do arquivo) ou -1 */
static int ico_tex_start(const u8 *d, int len)
{
    u32 shapes, attr, nv, nf;
    int vsz, ap, hsz, found = -1;
    u16 *tmp;
    if (len < 40 || rd32(d) != 0x010000) return -1;
    shapes = rd32(d + 4); attr = rd32(d + 8); nv = rd32(d + 16);
    if (shapes < 1 || shapes > 64 || nv < 3 || nv > 8000 || (nv % 3)) return -1;
    vsz = (int)shapes * 8 + 16;
    ap = 20 + (int)nv * vsz;
    if (ap > len || !(attr & 4)) return -1;
    if (!(attr & 8)) return len >= 32768 + 20 ? len - 32768 : -1;
    if (ap + 20 > len) return -1;
    nf = rd32(d + ap + 16);
    tmp = (u16 *)malloc(16384 * sizeof(u16));
    if (!tmp) return -1;
    for (hsz = 16; hsz >= 8 && found < 0; hsz -= 8) {
        int p = ap + 20;
        u32 f;
        for (f = 0; f < nf && f < 4096; f++) {
            u32 keys;
            if (p + 8 > len) { p = -1; break; }
            keys = rd32(d + p + 4);
            if (keys > 100000) { p = -1; break; }
            p += hsz + (int)keys * 8;
        }
        if (p > 0 && p + 4 < len && rd32(d + p) >= 2 && rd32(d + p) <= (u32)(len - p - 4) &&
            rle_decode(d, len, p + 4, tmp)) found = p;
    }
    free(tmp);
    return found;
}

/* devolve um novo .ico igual ao original, com a textura trocada (sem compressao). NULL = formato nao reconhecido */
static u8 *ico_replace_tex(const u8 *d, int len, const u16 *tex, int *outLen)
{
    int ts = ico_tex_start(d, len), i;
    u8 *o;
    if (ts < 0) return NULL;
    o = (u8 *)malloc((size_t)ts + 32768);
    if (!o) return NULL;
    memcpy(o, d, (size_t)ts);
    st32(o + 8, (rd32(d + 8) | 4u) & ~8u);
    for (i = 0; i < 16384; i++) st16(o + ts + i * 2, tex[i]);
    *outLen = ts + 32768;
    return o;
}

/* ---------------- CBS (CodeBreaker): exportar ---------------- */
#define CBS_MAXBYTES (4u * 1024 * 1024)
static int export_cbs(const Entry *e, const char *stamp)
{
    char d1[128], outp[200], src[96], lbl[96];
    const sceMcTblGetDir *raw = mc_raw(e->name);
    SvFile *sf;
    u8 *out;
    u32 total = 0;
    uint32_t olen = 0;                              /* uint32_t: no EE e unsigned long (diferente de u32) */
    int i, n, nf = 0, k = 0, fd, ok = 1;

    snprintf(src, sizeof(src), "/%s/*", e->name);
    n = mc_list(mcSlot, src, subtbl, MAX_ENT);
    for (i = 0; i < n; i++) {
        if (subtbl[i].AttrFile & MC_ATTR_DIR) continue;
        total += subtbl[i].FileSizeByte;
        nf++;
    }
    if (nf == 0 || total > CBS_MAXBYTES) return 1;
    sf = (SvFile *)calloc((size_t)nf, sizeof(SvFile));
    if (!sf) return 1;

    snprintf(lbl, sizeof(lbl), "%s", e->name);
    for (i = 0; i < n && ok; i++) {
        const sceMcTblGetDir *t = &subtbl[i];
        u32 sz;
        u8 *buf;
        if (t->AttrFile & MC_ATTR_DIR) continue;
        sz = t->FileSizeByte;
        buf = (u8 *)malloc(sz ? sz : 1);
        if (!buf) { ok = 0; break; }
        snprintf(src, sizeof(src), "/%s/%s", e->name, (const char *)t->EntryName);
        if (sz && mc_file_to_mem(src, buf, sz, lbl) != 0) { free(buf); ok = 0; break; }
        sf[k].name = (const char *)t->EntryName;
        sf[k].data = buf;
        sf[k].size = sz;
        sf[k].cr = (const u8 *)&t->_Create;
        sf[k].mo = (const u8 *)&t->_Modify;
        k++;
    }
    out = NULL;
    if (ok) {
        out = sv_build_cbs(e->name, sf, k, raw ? (const u8 *)&raw->_Create : NULL, raw ? (const u8 *)&raw->_Modify : NULL, &olen);
        if (!out) ok = 0;
    }
    for (i = 0; i < k; i++) free((void *)sf[i].data);
    free(sf);
    if (!ok) return 1;

    snprintf(d1, sizeof(d1), ROOT_DIR "/%s", e->name);
    snprintf(outp, sizeof(outp), "%s/%s.cbs", d1, stamp);
    mkdir(ROOT_DIR, 0777);
    mkdir(d1, 0777);
    fd = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { free(out); return 1; }
    if (wr_all(fd, out, olen)) ok = 0;
    close(fd);
    free(out);
    if (!ok) { unlink(outp); return 1; }
    return 0;
}

/* ---------------- ZIP (so exportar) ----------------
 * ZIP padrao, sem compressao (metodo 0): abre em qualquer computador/celular. Cada arquivo do save
 * fica dentro de uma pasta com o nome do save: <PASTA>/<arquivo>. */
static void zp16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void zp32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static int export_zip(const Entry *e, const char *stamp)
{
    char d1[128], outp[200], src[96], lbl[96], nm[96];
    u8 lh[30 + 96], *cd = NULL, *buf;
    u32 cdCap, cdLen = 0, off = 0, nf = 0;
    int i, n, fd, ok = 1;

    snprintf(src, sizeof(src), "/%s/*", e->name);
    n = mc_list(mcSlot, src, subtbl, MAX_ENT);
    for (i = 0; i < n; i++) if (!(subtbl[i].AttrFile & MC_ATTR_DIR)) nf++;
    if (nf == 0) return 1;
    cdCap = (nf + 1) * (46 + 96) + 22;
    cd = (u8 *)malloc(cdCap);
    if (!cd) return 1;

    snprintf(d1, sizeof(d1), ROOT_DIR "/%s", e->name);
    snprintf(outp, sizeof(outp), "%s/%s.zip", d1, stamp);
    mkdir(ROOT_DIR, 0777);
    mkdir(d1, 0777);
    fd = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { free(cd); return 1; }

    snprintf(lbl, sizeof(lbl), "%s", e->name);
    for (i = 0; i < n && ok; i++) {
        const sceMcTblGetDir *t = &subtbl[i];
        u32 sz, crc, nl, dt, dd;
        if (t->AttrFile & MC_ATTR_DIR) continue;
        sz = t->FileSizeByte;
        buf = (u8 *)malloc(sz ? sz : 1);
        if (!buf) { ok = 0; break; }
        snprintf(src, sizeof(src), "/%s/%s", e->name, (const char *)t->EntryName);
        if (sz && mc_file_to_mem(src, buf, sz, lbl) != 0) { free(buf); ok = 0; break; }
        crc = crc32_update(0, buf, sz);
        snprintf(nm, sizeof(nm), "%s/%s", e->name, (const char *)t->EntryName);
        nl = (u32)strlen(nm);
        if (nl > 95) nl = 95;
        {   /* data e hora do arquivo (DOS); fora do intervalo vira 01/01/1980 */
            int yr = t->_Modify.Year, mo = t->_Modify.Month, dy = t->_Modify.Day;
            if (yr < 1980 || yr > 2107 || mo < 1 || mo > 12 || dy < 1 || dy > 31) { yr = 1980; mo = 1; dy = 1; }
            dd = ((u32)(yr - 1980) << 9) | ((u32)mo << 5) | (u32)dy;
            dt = ((u32)(t->_Modify.Hour & 31) << 11) | ((u32)(t->_Modify.Min & 63) << 5) | (u32)((t->_Modify.Sec & 63) / 2);
        }
        memset(lh, 0, 30);
        zp32(lh, 0x04034B50u); zp16(lh + 4, 20); zp16(lh + 6, 0); zp16(lh + 8, 0);
        zp16(lh + 10, dt); zp16(lh + 12, dd);
        zp32(lh + 14, crc); zp32(lh + 18, sz); zp32(lh + 22, sz);
        zp16(lh + 26, nl); zp16(lh + 28, 0);
        memcpy(lh + 30, nm, nl);
        if (wr_all(fd, lh, 30 + nl) || (sz && wr_all(fd, buf, sz))) ok = 0;
        free(buf);
        if (!ok) break;
        {   /* entrada do diretorio central */
            u8 *c = cd + cdLen;
            memset(c, 0, 46);
            zp32(c, 0x02014B50u); zp16(c + 4, 20); zp16(c + 6, 20); zp16(c + 8, 0); zp16(c + 10, 0);
            zp16(c + 12, dt); zp16(c + 14, dd);
            zp32(c + 16, crc); zp32(c + 20, sz); zp32(c + 24, sz);
            zp16(c + 28, nl);
            zp32(c + 42, off);
            memcpy(c + 46, nm, nl);
            cdLen += 46 + nl;
        }
        off += 30 + nl + sz;
    }
    if (ok) {
        u8 *c = cd + cdLen;
        memset(c, 0, 22);
        zp32(c, 0x06054B50u);
        zp16(c + 8, nf); zp16(c + 10, nf);
        zp32(c + 12, cdLen); zp32(c + 16, off);
        if (wr_all(fd, cd, cdLen + 22)) ok = 0;
    }
    close(fd);
    free(cd);
    if (!ok) { unlink(outp); return 1; }
    return 0;
}

/* despacho por formato */
static int export_one(const Entry *e, const char *stamp)
{
    if (expFmt == FMT_PSU) return export_psu(e, stamp);
    if (expFmt == FMT_MAX) return export_max(e, stamp);
    if (expFmt == FMT_ZIP) return export_zip(e, stamp);
    if (expFmt == FMT_CBS) return export_cbs(e, stamp);
    return export_folder(e, stamp);
}

static int import_one(const Entry *e)
{
    int r;
    if (e->kind == FMT_PSU)      r = import_psu(e);
    else if (e->kind == FMT_MAX) r = import_max(e);
    else if (e->kind == FMT_VMC) r = import_vmc(e);
    else if (e->kind >= FMT_CBS) r = import_sv(e);
    else                         r = import_folder(e);
    if (e->ps1 && r == 0 && ps1_flag(mcSlot, e->name)) r++;      /* recria a marca de save de PS1 */
    return r;
}

/* ============================================================
 *  Acoes
 * ============================================================ */
static int gather(Entry *L, int N, int *idx)
{
    int i, k = 0;
    if (L == mcList) mcsize_all();                       /* acoes precisam dos tamanhos */
    for (i = 0; i < N; i++) if (L[i].sel) idx[k++] = i;
    if (k == 0 && N > 0) idx[k++] = cursorPos[tab];     /* sem marcados: usa o card atual */
    return k;
}

static void do_export(void)
{
    int idx[MAX_ENT], k, i, fails = 0, nDone = 0;
    Now n;
    char stamp[32], m[96];

    if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
    if (!usb_ready()) { usbOk = 0; toast("Conecte um pendrive/HD USB%.0d", 0); return; }
    k = gather(mcList, mcCount, idx);
    if (k == 0) return;

    get_now(&n);
    if (n.y) snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d", n.y, n.mo, n.d, n.h, n.mi, n.s);
    else     snprintf(stamp, sizeof(stamp), "0000-00-00_00-00-%02d", (int)(frameNo % 100));

    g_done = 0; g_total = 0;
    for (i = 0; i < k; i++) g_total += mcList[idx[i]].size;
    if (expFmt == FMT_MAX) g_total *= 2;           /* leitura + compressao */
    if (g_total == 0) g_total = 1;

    snprintf(ui.title, sizeof(ui.title), "EXPORTANDO (%s)", FMT_NAME[expFmt]);
    g_cancel = 0; g_multi = (k > 1);
    ui.busy = 1;
    for (i = 0; i < k; i++) {
        if (cancel_poll()) break;
        fails += export_one(&mcList[idx[i]], stamp);
        nDone++;
    }
    ui.busy = 0; g_multi = 0;

    for (i = 0; i < mcCount; i++) mcList[i].sel = 0;
    refresh_usb();
    if (nDone < k) {
        snprintf(m, sizeof(m), "Cancelado: %d de %d save(s) exportado(s)", nDone, k);
        toast_s(m, 0);
    }
    else if (fails) toast("Concluido com %d falha(s)", fails);
    else            toast("%d save(s) exportado(s) com sucesso", k);
}

static int mc_has(const char *name)
{
    int i;
    for (i = 0; i < mcFullN; i++) if (!strcmp(mcFull[i].name, name)) return 1;
    return 0;
}

static Entry *mc_find(const char *name)
{
    int i;
    mcsize_all();                                          /* o aviso de sobrescrever compara tamanhos */
    for (i = 0; i < mcFullN; i++) if (!strcmp(mcFull[i].name, name)) return &mcFull[i];
    return NULL;
}

/* preenche as linhas extras do dialogo: cartao x backup */
static void fill_compare(const Entry *b, const Entry *c)
{
    char sa[24], sb[24];
    long kb = date_key(b), kc = date_key(c);
    fmt_size(sa, sizeof(sa), c->size);
    fmt_size(sb, sizeof(sb), b->size);
    snprintf(ui.extra[0], sizeof(ui.extra[0]), "No cartao:  %s %s  -  %s", c->date, c->time, sa);
    if (b->kind != FMT_DIR)
        snprintf(ui.extra[1], sizeof(ui.extra[1]), "No backup:  %s %s  -  %s  (%s)", b->date, b->time, sb, FMT_NAME[b->kind]);
    else
        snprintf(ui.extra[1], sizeof(ui.extra[1]), "No backup:  %s %s  -  %s", b->date, b->time, sb);
    ui.extraCol[0] = C_TXT;
    ui.extraCol[1] = C_TXT;
    if (kb < 0 || kc < 0) {
        snprintf(ui.extra[2], sizeof(ui.extra[2]), "Datas indisponiveis para comparar");
        ui.extraCol[2] = C_MUTED;
    } else if (kc > kb) {
        snprintf(ui.extra[2], sizeof(ui.extra[2]), "ATENCAO: o cartao foi alterado depois deste backup");
        ui.extraCol[2] = C_RED;
    } else {
        snprintf(ui.extra[2], sizeof(ui.extra[2]), "O backup e posterior a ultima alteracao do cartao");
        ui.extraCol[2] = C_GREEN;
    }
    ui.extraN = 3;
}

static void do_import(void)
{
    int idx[MAX_ENT], k, i, fails = 0, nDone = 0, nTodo = 0;
    u32 need = 0;

    refresh_mc();
    if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
    k = gather(usbList, usbCount, idx);
    if (k == 0) return;

    for (i = 0; i < k; i++) need += usbList[idx[i]].size + 2048;
    if ((need >> 10) > (u32)mcFreeKB) {
        /* pode ser sobrescrita; so avisa quando claramente nao cabe */
        int allExist = 1;
        for (i = 0; i < k; i++) if (usbList[idx[i]].sel != -1 && !mc_has(usbList[idx[i]].name)) allExist = 0;
        if (!allExist) {
            char m[96];
            snprintf(m, sizeof(m), "Precisa de %u KB, livre %d KB", (unsigned)(need >> 10), mcFreeKB);
            info("SEM ESPACO NO MEMORY CARD", m);
            return;
        }
    }

    for (i = 0; i < k; i++) {
        Entry *b = &usbList[idx[i]], *c = (b->sel == -1) ? NULL : mc_find(b->name);
        if (c) {
            char m[96];
            snprintf(m, sizeof(m), "%.40s ja existe. Sobrescrever?", b->name);
            fill_compare(b, c);
            if (!confirm("SAVE JA EXISTE", m)) { b->sel = -1; }
        }
    }

    g_done = 0; g_total = 0;
    for (i = 0; i < k; i++) if (usbList[idx[i]].sel != -1) g_total += usbList[idx[i]].size;
    if (g_total == 0) g_total = 1;

    snprintf(ui.title, sizeof(ui.title), "IMPORTANDO PARA MEMORY CARD");
    if (k == 1 && usbList[idx[0]].kind != FMT_DIR)
        snprintf(ui.title, sizeof(ui.title), "IMPORTANDO %s", FMT_NAME[usbList[idx[0]].kind]);
    for (i = 0; i < k; i++) if (usbList[idx[i]].sel != -1) nTodo++;
    g_cancel = 0; g_multi = (nTodo > 1);
    ui.busy = 1;
    for (i = 0; i < k; i++) {
        if (usbList[idx[i]].sel == -1) continue;
        if (cancel_poll()) break;
        fails += import_one(&usbList[idx[i]]);
        nDone++;
    }
    ui.busy = 0; g_multi = 0;

    for (i = 0; i < usbCount; i++) usbList[i].sel = 0;
    refresh_mc();
    if (nDone < nTodo) {
        char m[96];
        snprintf(m, sizeof(m), "Cancelado: %d de %d save(s) importado(s)", nDone, nTodo);
        toast_s(m, 0);
    }
    else if (fails) toast("Concluido com %d falha(s)", fails);
    else            toast("%d save(s) importado(s) com sucesso", nDone);
}

__attribute__((unused)) static void do_convert(void)
{
    int idx[MAX_ENT], k, i, n = 0, fails = 0;
    if (!usb_ready()) { usbOk = 0; toast("Conecte um pendrive/HD USB%.0d", 0); return; }
    k = gather(usbList, usbCount, idx);
    g_done = 0; g_total = 0;
    for (i = 0; i < k; i++)
        if (usbList[idx[i]].kind != FMT_DIR) g_total += usbList[idx[i]].size;
    if (g_total == 0) { toast("Nada para converter (so .psu / .max)%.0d", 0); return; }
    snprintf(ui.title, sizeof(ui.title), "CONVERTENDO PARA PASTA");
    ui.busy = 1;
    for (i = 0; i < k; i++) {
        Entry *e = &usbList[idx[i]];
        if (e->kind == FMT_PSU) { fails += convert_psu(e); n++; }
        else if (e->kind == FMT_MAX) { fails += convert_max(e); n++; }
    }
    ui.busy = 0;
    for (i = 0; i < usbCount; i++) usbList[i].sel = 0;
    refresh_usb();
    if (fails) toast("Concluido com %d falha(s)", fails);
    else       toast("%d save(s) convertido(s) para pasta", n);
}

static void do_convert_tab(void)
{
    int idx[MAX_ENT], k, i, n = 0, fails = 0, todo = 0;
    char m[96];
    if (!usb_ready()) { usbOk = 0; toast("Conecte um pendrive/HD USB%.0d", 0); return; }
    k = gather(usbList, usbCount, idx);
    g_done = 0; g_total = 0;
    for (i = 0; i < k; i++)
        if (usbList[idx[i]].kind != convFmt) g_total += usbList[idx[i]].size * 2;
    if (g_total == 0) { toast("Nada a converter: ja esta nesse formato%.0d", 0); return; }
    snprintf(ui.title, sizeof(ui.title), "CONVERTENDO PARA %s", FMT_NAME[convFmt]);
    for (i = 0; i < k; i++) if (usbList[idx[i]].kind != convFmt) todo++;
    g_cancel = 0; g_multi = (todo > 1);
    ui.busy = 1;
    for (i = 0; i < k; i++) {
        Entry *e = &usbList[idx[i]];
        if (e->kind == convFmt) continue;
        if (cancel_poll()) break;
        fails += convert_entry(e, convFmt);
        n++;
    }
    ui.busy = 0; g_multi = 0;
    for (i = 0; i < usbCount; i++) usbList[i].sel = 0;
    refresh_usb();
    if (n < todo) {
        snprintf(m, sizeof(m), "Cancelado: %d de %d save(s) convertido(s)", n, todo);
        toast_s(m, 0);
    }
    else if (fails) toast("Concluido com %d falha(s)", fails);
    else            toast("%d save(s) convertido(s)", n);
}

/* ---- Ferramentas do Memory Card (interface) ---- */
static void mark_all_mc(void)
{
    int i;
    for (i = 0; i < mcCount; i++) mcList[i].sel = 1;
}

/* oferece backup completo (PSU) no USB. 1 = pode continuar */
static int backup_before(void)
{
    int oldFmt = expFmt, ok = 1;
    if (!mcReady || mcCount == 0 || !usb_ready()) return 1;
    if (!confirm("FAZER BACKUP ANTES?", "Salvar todos os saves em PSU no USB (recomendado)")) return 1;
    mark_all_mc();
    expFmt = FMT_PSU;
    ui.toastErr = 0;
    do_export();
    expFmt = oldFmt;
    if (ui.toastErr) ok = confirm("BACKUP COM FALHAS", "Continuar mesmo assim?");
    return ok;
}

static void tool_format(void)
{
    char m[96];
    int r;
    if (!backup_before()) return;
    {
        char nm[8];
        card_name(mcSlot, nm, sizeof(nm));
        snprintf(m, sizeof(m), "Apaga TODOS os saves do slot %s. Continuar?", nm);
    }
    if (!confirm("FORMATAR MEMORY CARD", m)) return;
    if (!confirm("ULTIMO AVISO", "Isso NAO pode ser desfeito. Formatar mesmo?")) return;
    snprintf(ui.title, sizeof(ui.title), "FORMATANDO MEMORY CARD");
    ui.busy = 1;
    progress("Aguarde, nao remova o cartao...", 0, 1);
    r = mc_format_card();
    ui.busy = 0;
    refresh_mc();
    if (r >= 0 && mcReady) toast("Memory Card formatado%.0d", 0);
    else                   toast("Falha ao formatar (erro %d)", r);
}

static void tool_verify(void)
{
    int bad, i;
    char m[96];
    if (!mcReady || mcCount == 0) { toast("Nenhum save para verificar%.0d", 0); return; }
    snprintf(ui.title, sizeof(ui.title), "VERIFICANDO SAVES");
    g_cancel = 0; g_multi = 1;
    ui.busy = 1;
    bad = mc_verify();
    ui.busy = 0; g_multi = 0;
    if (g_cancel) { toast_s("Verificacao cancelada", 0); return; }
    if (bad == 0) {
        snprintf(m, sizeof(m), "%d save(s) verificado(s): nenhum problema", mcCount);
        info("VERIFICACAO CONCLUIDA", m);
        return;
    }
    m[0] = 0;
    for (i = 0; i < bad; i++) {
        if (strlen(m) + strlen(badName[i]) > 70) { strcat(m, "..."); break; }
        if (i) strcat(m, ", ");
        strcat(m, badName[i]);
    }
    {
        char t[48];
        snprintf(t, sizeof(t), "%d SAVE(S) COM PROBLEMA", bad);
        info(t, m);
    }
    if (confirm("APAGAR OS COM PROBLEMA?", "Remove do cartao os saves listados antes")) {
        int fails = 0;
        for (i = 0; i < bad; i++) fails += mc_delete_save(badName[i]);
        refresh_mc();
        if (fails) toast("Concluido com %d falha(s)", fails);
        else       toast("%d save(s) apagado(s)", bad);
    }
}

static void tool_delete(void)
{
    int idx[MAX_ENT], k, i, fails = 0;
    char m[96];
    if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
    k = gather(mcList, mcCount, idx);
    if (k == 0) return;
    if (!backup_before()) return;
    if (k == 1) snprintf(m, sizeof(m), "Apagar %s? Nao pode ser desfeito.", mcList[idx[0]].name);
    else        snprintf(m, sizeof(m), "Apagar %d saves? Nao pode ser desfeito.", k);
    if (!confirm("APAGAR SAVES", m)) return;
    {
        char names[MAX_ENT][40];
        for (i = 0; i < k; i++) snprintf(names[i], sizeof(names[0]), "%s", mcList[idx[i]].name);
        snprintf(ui.title, sizeof(ui.title), "APAGANDO");
        g_total = (u32)k * 1000; g_done = 0; g_multi = 0; delSpan = 1000;
        ui.busy = 1;
        for (i = 0; i < k; i++) { delBase = (u32)i * 1000; fails += mc_delete_save(names[i]); }
        ui.busy = 0; delSpan = 0;
    }
    for (i = 0; i < mcCount; i++) mcList[i].sel = 0;
    refresh_mc();
    if (fails) toast("Concluido com %d falha(s)", fails);
    else       toast("%d save(s) apagado(s)", k);
}

static void tool_recover(void)
{
    int t;
    snprintf(ui.title, sizeof(ui.title), "LENDO MEMORY CARD");
    ui.busy = 1;
    for (t = 0; t < 4; t++) {
        progress("Tentando ler o cartao de novo...", (u32)t, 4);
        refresh_mc();
        if (mcReady) break;
    }
    ui.busy = 0;
    if (mcReady) {
        info("CARTAO LIDO COM SUCESSO", "Use Verificar saves para achar saves corrompidos");
        return;
    }
    if (mcNoFmt) {
        info("CARTAO NAO LEGIVEL", "Sistema de arquivos ausente ou danificado");
        if (confirm("FORMATAR AGORA?", "Recria o sistema de arquivos (apaga tudo)")) tool_format();
    } else {
        info("SEM MEMORY CARD", "Verifique o cartao e o slot e tente de novo");
    }
}

static void tool_copy(void)
{
    int idx[MAX_ENT], k, i, fails = 0, copied = 0, dst = mcSlot ^ 1, freeKB = 0;
    u32 need = 0;
    char m[96], ns[8], nd[8];
    if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
    if (nCards < 2) { toast("Falha: so ha um slot de Memory Card%.0d", 0); return; }
    k = gather(mcList, mcCount, idx);
    if (k == 0) return;

    if (nCards > 2) {                              /* multitap: escolhe o slot de destino */
        static char lbl[8][24];
        const char *it[8];
        int map[8], cnt = 0, c;
        for (i = 0; i < nCards; i++) {
            char nm[8];
            if (i == mcSlot) continue;
            card_name(i, nm, sizeof(nm));
            snprintf(lbl[cnt], sizeof(lbl[cnt]), "Slot %s", nm);
            it[cnt] = lbl[cnt];
            map[cnt++] = i;
        }
        c = menu_run("COPIAR PARA QUAL SLOT?", it, cnt);
        if (c < 0) return;
        dst = map[c];
    }
    card_name(mcSlot, ns, sizeof(ns));
    card_name(dst, nd, sizeof(nd));

    if (!mc_port_ready(dst, &freeKB)) {
        snprintf(m, sizeof(m), "Falha: slot %s sem Memory Card legivel", nd);
        toast_s(m, 1);
        return;
    }
    for (i = 0; i < k; i++) need += mcList[idx[i]].size + 3072;     /* margem p/ clusters e pasta */
    if (need / 1024 > (u32)freeKB) {
        snprintf(m, sizeof(m), "Falha: sem espaco no slot %s", nd);
        toast_s(m, 1);
        return;
    }
    snprintf(m, sizeof(m), "Copiar %d save(s) do slot %s para o slot %s?", k, ns, nd);
    if (!confirm("COPIAR PARA OUTRO CARTAO", m)) return;

    g_done = 0; g_total = 0;
    for (i = 0; i < k; i++) g_total += mcList[idx[i]].size;
    if (g_total == 0) g_total = 1;
    snprintf(ui.title, sizeof(ui.title), "COPIANDO SLOT %s PARA SLOT %s", ns, nd);
    g_cancel = 0; g_multi = (k > 1);
    for (i = 0; i < k; i++) {
        Entry *e = &mcList[idx[i]];
        char pat[64];
        sceMcTblGetDir one[2];
        int exists;
        if (cancel_poll()) break;
        snprintf(pat, sizeof(pat), "/%s", e->name);
        exists = mc_list(dst, pat, one, 2) > 0;
        if (exists) {
            snprintf(m, sizeof(m), "%.40s ja existe no slot %s. Sobrescrever?", e->name, nd);
            if (!confirm("SAVE JA EXISTE", m)) continue;
        }
        ui.busy = 1;
        fails += mc_copy_save(e->name, mcSlot, dst, e->name);
        if (e->ps1 && ps1_flag(dst, e->name)) fails++;      /* recria a marca de save de PS1 no destino */
        ui.busy = 0;
        copied++;
    }
    g_multi = 0;
    for (i = 0; i < mcCount; i++) mcList[i].sel = 0;
    if (g_cancel && copied < k) {
        snprintf(m, sizeof(m), "Cancelado: %d de %d save(s) copiado(s)", copied, k);
        toast_s(m, 0);
    }
    else if (fails)  toast("Concluido com %d falha(s)", fails);
    else if (copied) toast("%d save(s) copiado(s) para o outro cartao", copied);
    else             toast("Nada copiado%.0d", 0);
}

/* ---- trocar icone do save ---- */
static char bmpNames[64][64];

static int pick_bmp(int n)
{
    int page = 0, per = 7;
    for (;;) {
        const char *it[8];
        int cnt = 0, k, more = n > per, c;
        for (k = 0; k < per && page * per + k < n; k++) it[cnt++] = bmpNames[page * per + k];
        if (more) it[cnt++] = "Mais arquivos...";
        c = menu_run("ESCOLHA A IMAGEM (BMP 128x128)", it, cnt);
        if (c < 0) return -1;
        if (more && c == cnt - 1) { page = ((page + 1) * per < n) ? page + 1 : 0; continue; }
        return page * per + c;
    }
}

static int has_bmp_ext(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && (n[l - 3] | 0x20) == 'b' && (n[l - 2] | 0x20) == 'm' && (n[l - 1] | 0x20) == 'p';
}

static void tool_icon(void)
{
    int idx[MAX_ENT], k, nb = 0, pick, sl = 0, i, j, changed = 0, fails = 0, bl = 0, backed = 0;
    char dirp[96], path[300], base[64], names[3][65], m[96];
    const char *err = "";
    DIR *d;
    struct dirent *de;
    u8 *sys, *bmp;
    u16 *tex;
    if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
    if (!usb_ready()) { usbOk = 0; toast("Conecte um pendrive/HD USB%.0d", 0); return; }
    k = gather(mcList, mcCount, idx);
    if (k != 1) { toast("Marque apenas 1 save%.0d", 0); return; }

    snprintf(dirp, sizeof(dirp), ROOT_DIR "/icones");
    mkdir(ROOT_DIR, 0777);
    mkdir(dirp, 0777);
    d = opendir(dirp);
    if (d) {
        while ((de = readdir(d)) != NULL && nb < 64) {
            if (de->d_name[0] == '.' || !has_bmp_ext(de->d_name)) continue;
            snprintf(bmpNames[nb++], sizeof(bmpNames[0]), "%s", de->d_name);
        }
        closedir(d);
    }
    if (nb == 0) { info("NENHUMA IMAGEM", "Coloque BMP 128x128 (24 bits) em SaveManagerReis/icones"); return; }
    pick = pick_bmp(nb);
    if (pick < 0) return;

    snprintf(path, sizeof(path), "%s/%s", dirp, bmpNames[pick]);
    bmp = read_all(1, path, &bl);
    tex = (u16 *)malloc(16384 * sizeof(u16));
    if (!bmp || !tex || !bmp_to_tex(bmp, bl, tex, &err)) {
        info("IMAGEM INVALIDA", bmp ? err : "Nao foi possivel ler o arquivo");
        free(bmp); free(tex);
        return;
    }
    free(bmp);

    snprintf(base, sizeof(base), "/%s", mcList[idx[0]].name);
    snprintf(path, sizeof(path), "%s/icon.sys", base);
    sys = read_all(0, path, &sl);
    if (!sys || sl < 0x1C4 || memcmp(sys, "PS2D", 4) != 0) {
        info("SAVE SEM icon.sys", "Nao da para trocar o icone deste save");
        free(sys); free(tex);
        return;
    }
    for (i = 0; i < 3; i++) { memcpy(names[i], sys + 0x104 + i * 0x40, 64); names[i][64] = 0; }
    free(sys);

    snprintf(m, sizeof(m), "Trocar o icone de %s por %s?", mcList[idx[0]].name, bmpNames[pick]);
    if (!confirm("TROCAR ICONE DO SAVE", m)) { free(tex); return; }

    snprintf(ui.title, sizeof(ui.title), "TROCANDO ICONE");
    ui.busy = 1;
    for (i = 0; i < 3; i++) {
        u8 *ico, *nw;
        int il = 0, nl = 0, dup = 0;
        if (!names[i][0] || !safe_name(names[i])) continue;
        for (j = 0; j < i; j++) if (!strcmp(names[i], names[j])) dup = 1;
        if (dup) continue;
        snprintf(path, sizeof(path), "%s/%s", base, names[i]);
        ico = read_all(0, path, &il);
        if (!ico) continue;                           /* arquivo de icone citado mas ausente */
        nw = ico_replace_tex(ico, il, tex, &nl);
        if (!nw) { fails++; free(ico); continue; }
        {                                             /* guarda o original no USB (so a 1a vez) */
            char bd[160], bf[260];
            struct stat st;
            snprintf(bd, sizeof(bd), ROOT_DIR "/icones/originais");
            mkdir(bd, 0777);
            snprintf(bd, sizeof(bd), ROOT_DIR "/icones/originais/%s", mcList[idx[0]].name);
            mkdir(bd, 0777);
            snprintf(bf, sizeof(bf), "%s/%s", bd, names[i]);
            if (stat(bf, &st) != 0) {
                int fd = open(bf, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd >= 0) { if (wr_all(fd, ico, (u32)il) == 0) backed++; close(fd); }
            }
        }
        g_done = 0; g_total = (u32)nl;
        if (mem_to_mc(nw, (u32)nl, path, mcList[idx[0]].name) < 0) fails++; else changed++;
        free(nw); free(ico);
    }
    ui.busy = 0;
    free(tex);
    icon_free_all(TAB_MC);                            /* recarrega a miniatura */
    if (fails || changed == 0) toast("Falha ao trocar o icone (%d)", fails);
    else toast("Icone trocado (%d arquivo(s))", changed);
}

static int days_in_month(int y, int m)
{
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int d = dm[(m < 1 ? 1 : m > 12 ? 12 : m) - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) d = 29;
    return d;
}

#define TOBCD(x) ((u8)((((x) / 10) << 4) | ((x) % 10)))

/* acerta o relogio do console (RTC), como faz o uLaunchELF */
static void tool_clock(void)
{
    Now n, w;
    sceCdCLOCK c;
    int done = 0, ok = 0, dm, v, dir;

    get_now(&n);
    if (!n.y) { n.y = 2026; n.mo = 1; n.d = 1; n.h = 12; n.mi = 0; }   /* relogio ilegivel: valor inicial */
    if (n.y < 2000) n.y = 2000;
    if (n.y > 2099) n.y = 2099;
    clkEd[0] = n.d; clkEd[1] = n.mo; clkEd[2] = n.y; clkEd[3] = n.h; clkEd[4] = n.mi;
    clkSel = 0; clkEditing = 1;

    while (!done) {
        read_pad();
        if (pad_new & PAD_LEFT)  { clkSel = (clkSel + 4) % 5; snd_play(SND_MOVE); }
        if (pad_new & PAD_RIGHT) { clkSel = (clkSel + 1) % 5; snd_play(SND_MOVE); }
        if ((pad_new & PAD_UP) || (pad_new & PAD_DOWN)) {
            dir = (pad_new & PAD_UP) ? 1 : -1;
            v = clkEd[clkSel] + dir;
            dm = days_in_month(clkEd[2], clkEd[1]);
            switch (clkSel) {
            case 0: if (v < 1) v = dm;      if (v > dm) v = 1;     break;
            case 1: if (v < 1) v = 12;      if (v > 12) v = 1;     break;
            case 2: if (v < 2000) v = 2099; if (v > 2099) v = 2000; break;
            case 3: if (v < 0) v = 23;      if (v > 23) v = 0;     break;
            default: if (v < 0) v = 59;     if (v > 59) v = 0;     break;
            }
            clkEd[clkSel] = v;
            dm = days_in_month(clkEd[2], clkEd[1]);            /* mes/ano mudou: ajusta o dia */
            if (clkEd[0] > dm) clkEd[0] = dm;
            snd_play(SND_MOVE);
        }
        if (pad_new & PAD_CIRCLE) { done = 1; snd_play(SND_BACK); }
        if (pad_new & PAD_CROSS)  { ok = 1; done = 1; snd_play(SND_OK); }
        render();
        present();
    }
    clkEditing = 0;
    if (!ok) return;

    tz_refresh();
    memset(&w, 0, sizeof(w));
    w.d = clkEd[0]; w.mo = clkEd[1]; w.y = clkEd[2]; w.h = clkEd[3]; w.mi = clkEd[4];
    local_to_jst(&w);                                   /* o RTC guarda a hora de Toquio */
    if (w.y < 2000 || w.y > 2099) { toast("Falha: data fora do limite%.0d", 0); return; }

    memset(&c, 0, sizeof(c));
    c.second = TOBCD(w.s);
    c.minute = TOBCD(w.mi);
    c.hour   = TOBCD(w.h);
    c.day    = TOBCD(w.d);
    c.month  = TOBCD(w.mo);
    c.year   = TOBCD(w.y - 2000);
    if (sceCdWriteClock(&c)) {
        get_now(&clk);
        clkT = 30;
        toast("Data e hora acertadas%.0d", 0);
    } else {
        toast("Falha ao gravar o relogio%.0d", 0);
    }
}

/* ---------------- criar cartao virtual novo (VMC do OPL) ---------------- */
static int vmc_prog_cb(void *ctx, uint32_t done, uint32_t total)
{
    (void)ctx;
    progress("Gravando o cartao virtual...", done >> 10, total >> 10);
    return cancel_poll();                          /* segure O para cancelar */
}

static void tool_new_vmc(void)
{
    static const int SZ[3] = { 8, 16, 32 };
    const char *it[4] = { "8 MB  (padrao do OPL)", "16 MB", "32 MB", "Cancelar" };
    char name[24], path[96], m[96];
    struct stat st;
    Now n;
    u8 now[8];
    int c, mb, r;

    if (!usb_ready()) { usbOk = 0; toast("Conecte um pendrive/HD USB%.0d", 0); return; }
    c = menu_run("TAMANHO DO CARTAO VIRTUAL", it, 4);
    if (c < 0 || c > 2) return;
    mb = SZ[c];
    snprintf(name, sizeof(name), "NOVO_VMC");
    if (!text_input("NOME DO VMC (sem .bin)", name, sizeof(name)) || !name[0] || name[0] == '.') return;
    mkdir(VMC_DIR, 0777);
    snprintf(path, sizeof(path), VMC_DIR "/%s.bin", name);
    if (stat(path, &st) == 0) { toast("Falha: ja existe um VMC com esse nome%.0d", 0); return; }
    snprintf(m, sizeof(m), "Criar %s.bin com %d MB em mass:/VMC?", name, mb);
    if (!confirm("CRIAR CARTAO VIRTUAL", m)) return;

    memset(&n, 0, sizeof(n));
    get_now(&n);
    if (!n.y) { n.y = 2000; n.mo = 1; n.d = 1; }          /* relogio ilegivel */
    local_to_jst(&n);                                      /* o cartao guarda a hora de Toquio */
    now[0] = 0; now[1] = (u8)n.s; now[2] = (u8)n.mi; now[3] = (u8)n.h;
    now[4] = (u8)n.d; now[5] = (u8)n.mo; now[6] = (u8)(n.y & 0xFF); now[7] = (u8)(n.y >> 8);

    g_cancel = 0; g_multi = 1;
    snprintf(ui.title, sizeof(ui.title), "CRIANDO VMC DE %d MB", mb);
    ui.busy = 1;
    r = vmc_format(path, (uint32_t)mb, now, vmc_prog_cb, NULL);
    ui.busy = 0; g_multi = 0;
    if (r == 0) {
        refresh_usb();
        snprintf(m, sizeof(m), "VMC criado: %s.bin (%d MB)", name, mb);
        toast_s(m, 0);
    } else if (r == 1) {
        toast_s("Criacao cancelada (arquivo removido)", 0);
    } else {
        toast_s("Falha ao criar o VMC (sem espaco no USB?)", 1);
    }
}

/* ---------------- musica de fundo (mass:/SaveManagerReis/musica.wav) ---------------- */
#define MUSIC_PATH ROOT_DIR "/musica.wav"
static int musTried = 0;

static void music_try_load(void)
{
    int r;
    if (musTried || !usb_ready()) return;
    musTried = 1;
    r = snd_music_load(MUSIC_PATH);
    if (r == -2)      toast_s("musica.wav invalida: use WAV PCM de 8 ou 16 bits", 1);
    else if (r == -3) toast_s("musica.wav grande demais (maximo 14 MB): converta p/ 22 kHz", 1);
}

/* ---------------- configuracoes (mass:/SaveManagerReis/config.ini) ---------------- */
#define CFG_PATH ROOT_DIR "/config.ini"
#define CFG_N 10
static int cfgSaved[CFG_N], cfgSeen[CFG_N], cfgStable = 0, cfgLoaded = 0;

static void cfg_cur(int *v)
{
    v[0] = snd_enabled(); v[1] = sortMode; v[2] = expFmt; v[3] = convFmt; v[4] = mcSlot; v[5] = tab; v[6] = vidMode;
    v[7] = snd_music_enabled(); v[8] = snd_music_volume(); v[9] = listMode;
}

static void config_save(void)
{
    char b[256];
    int v[CFG_N], fd, n;
    cfg_cur(v);
    memcpy(cfgSaved, v, sizeof(v));                          /* mesmo se falhar: nao tenta de novo sem mudanca */
    if (!usbOk) return;
    mkdir(ROOT_DIR, 0777);
    fd = open(CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    n = snprintf(b, sizeof(b),
                 "; Save Manager Reis - configuracoes (gravado automaticamente)\n"
                 "sound=%d\nsort=%d\nexport_format=%d\nconvert_format=%d\nslot=%d\ntab=%d\nvideo=%d\nmusic=%d\nmusvol=%d\nlist=%d\n",
                 v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]);
    if (n > 0) write(fd, b, (size_t)n);
    close(fd);
}

/* le o config.ini e aplica. rebuild = 1 se ja ha listas na tela (reconstroi a vista) */
static void config_load_apply(int rebuild)
{
    char b[512], *p;
    int fd, n, oldSlot = mcSlot, oldSort = sortMode, wantVid = vidMode;
    if (!usb_ready()) return;
    cfgLoaded = 1;
    fd = open(CFG_PATH, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, b, sizeof(b) - 1);
        close(fd);
        if (n > 0) {
            b[n] = 0;
            for (p = b; *p; ) {
                char key[32];
                int val;
                if (sscanf(p, "%31[a-z_]=%d", key, &val) == 2) {
                    if      (!strcmp(key, "sound"))          snd_set(val != 0);
                    else if (!strcmp(key, "sort")           && val >= 0 && val <= 3)       sortMode = val;
                    else if (!strcmp(key, "export_format")  && ((val >= FMT_DIR && val <= FMT_CBS) || val == FMT_ZIP)) expFmt = val;
                    else if (!strcmp(key, "convert_format") && val >= FMT_DIR && val <= FMT_MAX) convFmt = val;
                    else if (!strcmp(key, "slot")           && val >= 0 && val < nCards)   mcSlot = val;
                    else if (!strcmp(key, "tab")            && val >= 0 && val <= 2)       tab = val;
                    else if (!strcmp(key, "video")          && val >= 0 && val <= 2)       wantVid = val;
                    else if (!strcmp(key, "music"))          snd_music_set(val != 0);
                    else if (!strcmp(key, "musvol")         && val >= 0 && val <= 10)      snd_music_volume_set(val);
                    else if (!strcmp(key, "list")           && val >= 0 && val <= 1)       listMode = val;
                }
                while (*p && *p != '\n') p++;
                if (*p) p++;
            }
        }
    }
    /* modo de video salvo: segurar L2+R2 ao abrir ignora (caso a TV nao mostre o modo salvo) */
    if (wantVid != vidMode && !(!rebuild && (pad_old & (PAD_L2 | PAD_R2)) == (PAD_L2 | PAD_R2))) video_apply(wantVid);
    cfg_cur(cfgSaved);
    memcpy(cfgSeen, cfgSaved, sizeof(cfgSaved));
    cfgStable = 0;
    music_try_load();                                        /* o USB ja esta pronto: carrega a musica (uma vez) */
    if (rebuild) {
        if (sortMode != oldSort) view_apply_all();
        if (mcSlot != oldSlot) refresh_mc();
    }
}

static void config_flush(void)                              /* grava ja, se algo mudou */
{
    int v[CFG_N];
    if (!usbOk) return;
    cfg_cur(v);
    if (memcmp(v, cfgSaved, sizeof(v)) != 0) config_save();
}

/* chamada a cada quadro: grava 1,5 s depois da ultima mudanca (nao trava ao trocar de aba rapido) */
static void config_poll(void)
{
    int v[CFG_N];
    if (!usbOk) return;
    if (!cfgLoaded) { config_load_apply(1); return; }       /* USB conectado depois da abertura */
    cfg_cur(v);
    if (memcmp(v, cfgSeen, sizeof(v)) != 0) { memcpy(cfgSeen, v, sizeof(v)); cfgStable = 0; return; }
    if (memcmp(v, cfgSaved, sizeof(v)) != 0 && ++cfgStable >= 90) config_save();
}

/* ---------------- renomear o NOME DO JOGO (titulo do icon.sys) ---------------- */
/* acha um arquivo dentro de um .psu: devolve onde os dados comecam e o tamanho */
static int psu_find_file(const char *path, const char *fname, u32 *dataOff, u32 *size)
{
    u8 h[512];
    int fd, k, found = 0;
    u32 total;
    fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, h, 512) != 512 || !(ld16(h) & MC_ATTR_DIR)) { close(fd); return 0; }
    total = ld32(h + 4);
    if (total > 512) total = 512;
    for (k = 0; k < (int)total; k++) {
        u32 sz, pad;
        if (read(fd, h, 512) != 512) break;
        sz = ld32(h + 4);
        pad = (1024 - (sz & 1023)) & 1023;
        if (!(ld16(h) & MC_ATTR_DIR) && !strncmp((const char *)h + 64, fname, strlen(fname) + 1)) {
            *dataOff = (u32)lseek(fd, 0, SEEK_CUR);
            *size = sz;
            found = 1;
            break;
        }
        if (sz || pad) lseek(fd, (off_t)sz + (off_t)pad, SEEK_CUR);
    }
    close(fd);
    return found;
}

/* le o icon.sys do save (Memory Card, backup em Pasta ou em PSU). Retorna o tamanho lido ou 0 */
static int ics_read(const Entry *e, int isUsb, u8 *dst, int cap)
{
    char p[200];
    int n = 0;
    u8 *b;
    if (isUsb && e->kind == FMT_PSU) return psu_read_sys(e->path, dst, cap);
    if (!isUsb) snprintf(p, sizeof(p), "/%s/icon.sys", e->name);
    else if (e->kind == FMT_DIR) snprintf(p, sizeof(p), "%s/icon.sys", e->path);
    else return 0;
    b = read_all(isUsb ? 1 : 0, p, &n);
    if (!b) return 0;
    if (n > cap) n = cap;
    memcpy(dst, b, (size_t)n);
    free(b);
    return n;
}

/* grava o titulo novo. Cartao: reescreve o icon.sys (se falhar, restaura o original).
 * Pasta/PSU: troca so os 70 bytes no lugar, sem mudar o tamanho do arquivo. 0 = ok */
static int ics_write(const Entry *e, int isUsb, const u8 *orig, const u8 *neu, int len)
{
    char p[200];
    if (!isUsb) {
        snprintf(p, sizeof(p), "/%s/icon.sys", e->name);
        g_done = 0; g_total = (u32)len;
        if (mem_to_mc(neu, (u32)len, p, e->name) < 0) {
            g_done = 0;
            mem_to_mc(orig, (u32)len, p, e->name);             /* restaura o original */
            return 1;
        }
        return 0;
    } else {
        u32 base = 0, sz = 0;
        int fd, bad = 0;
        if (e->kind == FMT_DIR) { snprintf(p, sizeof(p), "%s/icon.sys", e->path); }
        else if (e->kind == FMT_PSU) {
            snprintf(p, sizeof(p), "%s", e->path);
            if (!psu_find_file(p, "icon.sys", &base, &sz) || sz < 0x104) return 1;
        } else return 1;
        fd = open(p, O_RDWR);
        if (fd < 0) return 1;
        if (lseek(fd, (off_t)base + 6, SEEK_SET) < 0 || wr_all(fd, neu + 6, 2)) bad = 1;
        if (!bad && (lseek(fd, (off_t)base + 0xC0, SEEK_SET) < 0 || wr_all(fd, neu + 0xC0, 68))) bad = 1;
        close(fd);
        return bad;
    }
}

static void rename_title(Entry *e, int isUsb)
{
    static u8 orig[2048], neu[2048];
    char l1[20], l2[20], c1[40], c2[40], m[96];
    u8 e1[40], e2[40];
    int len, nl, n1, n2;

    if (e->ps1) { info("SAVE DE PS1", "Saves de PS1 nao tem nome de jogo editavel"); return; }
    if (isUsb && e->kind != FMT_DIR && e->kind != FMT_PSU) {
        info("RENOMEAR NOME DO JOGO", "Neste formato: importe no cartao, renomeie e exporte de novo");
        return;
    }
    len = ics_read(e, isUsb, orig, sizeof(orig));
    if (len < 0x104 || memcmp(orig, "PS2D", 4) != 0) { info("RENOMEAR NOME DO JOGO", "Este save nao tem um icon.sys valido"); return; }

    nl = (int)ld16(orig + 6);
    if (nl <= 0 || nl >= 68) nl = 68;
    sjis_range(orig + 0xC0, 0, nl, c1, sizeof(c1));
    sjis_range(orig + 0xC0, nl, 68, c2, sizeof(c2));
    snprintf(l1, sizeof(l1), "%.16s", c1);
    snprintf(l2, sizeof(l2), "%.16s", c2);
    if (!text_input("NOME DO JOGO - LINHA 1 (ate 16)", l1, 17)) return;
    if (!text_input("NOME DO JOGO - LINHA 2 (pode ficar vazia)", l2, 17)) return;

    n1 = sjis_encode(l1, e1, 32);
    n2 = sjis_encode(l2, e2, 32);
    if (n1 == 0 && n2 == 0) { toast_s("O nome do jogo nao pode ficar vazio", 1); return; }
    if (n1 == 0) { memcpy(e1, e2, (size_t)n2); n1 = n2; n2 = 0; }       /* so a linha 2 preenchida: vira a linha 1 */

    memcpy(neu, orig, (size_t)len);
    memset(neu + 0xC0, 0, 68);
    memcpy(neu + 0xC0, e1, (size_t)n1);
    memcpy(neu + 0xC0 + n1, e2, (size_t)n2);
    neu[6] = (u8)(n1 & 0xFF);                              /* onde comeca a 2a linha */
    neu[7] = (u8)(n1 >> 8);

    snprintf(ui.extra[0], sizeof(ui.extra[0]), "Antes:  %s%s%s", c1[0] ? c1 : "(sem texto latino)", c2[0] ? " / " : "", c2);
    snprintf(ui.extra[1], sizeof(ui.extra[1]), "Depois: %s%s%s", l1[0] ? l1 : l2, (l1[0] && l2[0]) ? " / " : "", l1[0] ? l2 : "");
    ui.extraCol[0] = C_MUTED; ui.extraCol[1] = C_ACC;
    ui.extraN = 2;
    snprintf(m, sizeof(m), "Gravar o novo nome do jogo no icon.sys?");
    if (!confirm("RENOMEAR NOME DO JOGO", m)) return;

    if (ics_write(e, isUsb, orig, neu, len)) { toast_s("Falha ao gravar o novo nome (nada foi perdido)", 1); return; }
    if (isUsb) refresh_usb(); else refresh_mc();
    toast_s("Nome do jogo alterado", 0);
}

/* ---------------- renomear save (Memory Card) ou backup (USB) ---------------- */
static int name_ok(const char *n)
{
    return safe_name(n) && n[0] != '.' && !strpbrk(n, "*?\"<>|");
}

static void tool_rename(void)
{
    int isUsb = (tab != TAB_MC), N = isUsb ? usbCount : mcCount;
    Entry *e;
    char old[40], nn[40], m[96];

    if (N <= 0 || cursorPos[tab] < 0 || cursorPos[tab] >= N) { toast("Selecione um save na lista%.0d", 0); return; }
    e = isUsb ? &usbList[cursorPos[tab]] : &mcList[cursorPos[tab]];

    {
        const char *it[3];
        int c;
        if (!isUsb) { it[0] = "Nome do jogo (titulo mostrado no PS2)";  it[1] = "Nome da pasta do save (tecnico)"; }
        else        { it[0] = "Nome do jogo (titulo dentro do backup)"; it[1] = "Nome do arquivo do backup"; }
        it[2] = "Cancelar";
        c = menu_run("RENOMEAR", it, 3);
        if (c < 0 || c == 2) return;
        if (c == 0) { rename_title(e, isUsb); return; }
    }

    if (!isUsb) {                                           /* pasta do save no Memory Card */
        int r = 0;
        char op[64], np[64];
        if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
        snprintf(old, sizeof(old), "%s", e->name);
        snprintf(nn, sizeof(nn), "%s", old);
        if (!text_input("NOVO NOME DA PASTA DO SAVE", nn, sizeof(nn))) return;
        if (!name_ok(nn)) { toast_s("Nome invalido (ate 31 caracteres, sem * ? / \\ :)", 1); return; }
        if (!strcmp(nn, old)) return;
        if (mc_has(nn)) { toast_s("Falha: ja existe um save com esse nome", 1); return; }
        snprintf(ui.extra[0], sizeof(ui.extra[0]), "O jogo procura o save pelo nome da pasta.");
        snprintf(ui.extra[1], sizeof(ui.extra[1]), "Com outro nome, ele pode nao encontrar o save.");
        ui.extraCol[0] = C_MUTED; ui.extraCol[1] = C_RED;
        ui.extraN = 2;
        snprintf(m, sizeof(m), "Renomear %.30s para %.30s?", old, nn);
        if (!confirm("RENOMEAR SAVE", m)) return;
        snprintf(op, sizeof(op), "/%s", old);
        snprintf(np, sizeof(np), "/%s", nn);
        mcRename(MP(mcSlot), MS(mcSlot), op, np);
        mcSync(0, NULL, &r);
        refresh_mc();                                       /* confere pelo que o cartao realmente tem */
        if (mc_has(nn) && !mc_has(old)) toast_s("Save renomeado", 0);
        else                            toast_s("Falha ao renomear (o cartao nao aceitou)", 1);
        return;
    }

    /* backup no USB: renomeia o "nome do backup" (a parte depois da pasta do save) */
    if (!usb_ready()) { usbOk = 0; toast("Conecte um pendrive/HD USB%.0d", 0); return; }
    if (e->kind == FMT_VMC) { info("CARTAO VIRTUAL", "Para renomear um VMC, renomeie o arquivo .bin em mass:/VMC"); return; }
    {
        char oldp[128], newp[160], parent[128], *sl;
        const char *ext = (e->kind == FMT_DIR) ? "" : e->path + strlen(e->path) - 4;     /* ".psu", ".max"... */
        struct stat st;
        snprintf(oldp, sizeof(oldp), "%s", e->path);
        snprintf(old, sizeof(old), "%s", e->stamp);
        snprintf(nn, sizeof(nn), "%s", old);
        if (!text_input("NOVO NOME DO BACKUP (mantenha a data)", nn, sizeof(nn))) return;
        if (!nn[0] || strlen(nn) > 35 || !name_ok(nn)) { toast_s("Nome invalido para o backup", 1); return; }
        if (!strcmp(nn, old)) return;
        snprintf(parent, sizeof(parent), "%s", oldp);
        sl = strrchr(parent, '/');
        if (!sl) return;
        *sl = 0;
        snprintf(newp, sizeof(newp), "%s/%s%s", parent, nn, ext);
        if (stat(newp, &st) == 0) { toast_s("Falha: ja existe um backup com esse nome", 1); return; }
        snprintf(m, sizeof(m), "Renomear %.30s para %.30s?", old, nn);
        if (!confirm("RENOMEAR BACKUP", m)) return;
        if (rename(oldp, newp) == 0) { refresh_usb(); toast_s("Backup renomeado", 0); }
        else                         toast_s("Falha ao renomear o backup", 1);
    }
}

/* ---------------- recuperar save corrompido ---------------- */
#define SALV_CHUNK 1024

/* le um arquivo do cartao: primeiro inteiro (3 tentativas); se falhar, por pedacos de 1 KB
 * (3 tentativas cada); o que nao for possivel ler vira zeros. 0 = completo, 1 = parcial, -1 = nao abriu */
static int salvage_file(const char *path, u8 *dst, u32 size, u32 *bad)
{
    int fd = -1, r = 0, tries;
    u32 off, n, got;
    *bad = 0;
    if (size == 0) return 0;
    for (tries = 0; tries < 3; tries++) {
        int ok = 1;
        fd = -1;
        mcOpen(MP(mcSlot), MS(mcSlot), path, MC_RDONLY);
        mcSync(0, NULL, &fd);
        if (fd < 0) continue;
        got = 0;
        while (got < size) {
            n = size - got;
            if (n > CHUNK) n = CHUNK;
            mcRead(fd, iobuf, (int)n);
            mcSync(0, NULL, &r);
            if (r <= 0) { ok = 0; break; }
            memcpy(dst + got, iobuf, (size_t)r);
            got += (u32)r;
        }
        mcClose(fd); mcSync(0, NULL, &r);
        if (ok && got == size) return 0;
    }
    fd = -1;
    mcOpen(MP(mcSlot), MS(mcSlot), path, MC_RDONLY);
    mcSync(0, NULL, &fd);
    if (fd < 0) return -1;
    memset(dst, 0, size);
    for (off = 0; off < size; off += SALV_CHUNK) {
        int ok = 0;
        n = size - off;
        if (n > SALV_CHUNK) n = SALV_CHUNK;
        for (tries = 0; tries < 3 && !ok; tries++) {
            mcSeek(fd, (int)off, 0);
            mcSync(0, NULL, &r);
            if (r < 0) continue;
            mcRead(fd, iobuf, (int)n);
            mcSync(0, NULL, &r);
            if (r == (int)n) { memcpy(dst + off, iobuf, n); ok = 1; }
        }
        if (!ok) { memset(dst + off, 0, n); *bad += n; }
    }
    mcClose(fd); mcSync(0, NULL, &r);
    return *bad ? 1 : 0;
}

static void tool_recover_save(void)
{
    Entry tmp;
    Now now;
    char old[40], pat[64], path[96], stamp[40], d1[128], m[96];
    int n, i, nOk = 0, nPart = 0, nFail = 0, hasIcon = 0, isPs1, rc;
    u32 lost = 0;

    if (tab != TAB_MC) { toast("Va para a aba Memory Card%.0d", 0); return; }
    if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
    if (mcCount <= 0 || cursorPos[TAB_MC] < 0 || cursorPos[TAB_MC] >= mcCount) { toast("Selecione um save na lista%.0d", 0); return; }
    if (!usb_ready()) { usbOk = 0; toast("Conecte um pendrive/HD USB (copia segura)%.0d", 0); return; }

    snprintf(old, sizeof(old), "%s", mcList[cursorPos[TAB_MC]].name);
    isPs1 = mcList[cursorPos[TAB_MC]].ps1;
    snprintf(m, sizeof(m), "Recuperar %.31s? Antes, faz uma copia segura no USB.", old);
    if (!confirm("RECUPERAR SAVE", m)) return;

    /* 1) le tudo, com varias tentativas; guarda em memoria */
    pk_free();
    snprintf(pat, sizeof(pat), "/%s/*", old);
    n = mc_list(mcSlot, pat, subtbl, MAX_ENT);
    snprintf(ui.title, sizeof(ui.title), "RECUPERANDO %.30s", old);
    g_multi = 0;
    ui.busy = 1;
    for (i = 0; i < n; i++) {
        PkFile *f;
        u32 bad = 0, sz = subtbl[i].FileSizeByte;
        const char *nm = (const char *)subtbl[i].EntryName;
        int r;
        if (subtbl[i].AttrFile & MC_ATTR_DIR) continue;
        progress(nm, (u32)i, (u32)(n ? n : 1));
        if (!strcmp(nm, "icon.sys")) hasIcon = 1;
        f = pk_add(nm, sz, (const u8 *)&subtbl[i]._Create, (const u8 *)&subtbl[i]._Modify);
        if (!f) { nFail++; continue; }
        snprintf(path, sizeof(path), "/%s/%s", old, nm);
        r = salvage_file(path, f->data, sz, &bad);
        if (r < 0) {                                        /* nem abriu: nao entra na copia */
            free(pk.f[pk.n - 1].data);
            pk.f[pk.n - 1].data = NULL;
            pk.total -= sz;
            pk.n--;
            nFail++;
        } else if (r == 1) { nPart++; lost += bad; }
        else               nOk++;
    }
    ui.busy = 0;
    if (pk.n == 0) { pk_free(); info("RECUPERAR SAVE", "Nao foi possivel ler nenhum arquivo deste save"); return; }
    pk.ps1 = isPs1;

    /* 2) copia segura no USB (Pasta): ROOT/<SAVE>/<data>_RECUP */
    get_now(&now);
    if (now.y) snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d_RECUP", now.y, now.mo, now.d, now.h, now.mi, now.s);
    else       snprintf(stamp, sizeof(stamp), "0000-00-00_00-00-00_RECUP");
    memset(&tmp, 0, sizeof(tmp));
    snprintf(tmp.name, sizeof(tmp.name), "%s", old);
    snprintf(tmp.stamp, sizeof(tmp.stamp), "%s", stamp);
    mkdir(ROOT_DIR, 0777);
    snprintf(d1, sizeof(d1), ROOT_DIR "/%s", old);
    mkdir(d1, 0777);
    rc = write_folder(&tmp, d1);
    if (rc) { pk_free(); info("RECUPERAR SAVE", "Falha ao gravar a copia segura no USB; nada foi alterado"); return; }

    /* 3) relatorio */
    snprintf(ui.extra[0], sizeof(ui.extra[0]), "Arquivos: %d completos, %d parciais, %d ilegiveis", nOk, nPart, nFail);
    if (lost) snprintf(ui.extra[1], sizeof(ui.extra[1]), "Dados que nao puderam ser lidos: %u KB (viraram zeros)", (lost + 1023) >> 10);
    else      snprintf(ui.extra[1], sizeof(ui.extra[1]), "Nenhum erro de leitura nos arquivos que abriram");
    snprintf(ui.extra[2], sizeof(ui.extra[2]), hasIcon ? "Copia segura em SaveManagerReis (pasta _RECUP)" : "Aviso: este save nao tem icon.sys");
    ui.extraCol[0] = C_TXT;
    ui.extraCol[1] = lost ? C_RED : C_GREEN;
    ui.extraCol[2] = hasIcon ? C_MUTED : C_RED;
    ui.extraN = 3;
    info("LEITURA CONCLUIDA", old);
    ui.extraN = 0;

    if (nPart == 0 && nFail == 0) {
        pk_free();
        info("SAVE INTEGRO", "O cartao leu tudo sem erro. Se o jogo ainda reclama, o dado esta ruim.");
        return;
    }

    /* 4) opcional: reescreve o save no cartao a partir do que foi lido (estrutura nova e limpa) */
    if (confirm("REESCREVER NO CARTAO", "Apagar o save e gravar a copia recuperada? A copia segura fica no USB.")) {
        int fails = 0;
        fails += mc_delete_save(old);
        memset(&tmp, 0, sizeof(tmp));
        snprintf(tmp.name, sizeof(tmp.name), "%s", old);
        fails += pk_to_mc(&tmp);                            /* grava e libera o pacote */
        if (isPs1 && ps1_flag(mcSlot, old)) fails++;
        refresh_mc();
        if (fails) toast("Reescrito com %d falha(s); a copia segura esta no USB", fails);
        else       toast_s("Save reescrito a partir da copia recuperada", 0);
    }
    pk_free();
}

/* ---------------- ajustes: som, modo de video, data e hora, creditos ---------------- */
static void tool_video(void)
{
    const char *it[4] = { "Automatico (padrao)", "TV de tubo (CRT): 480i, margem de seguranca", "TV moderna (HDMI): 480p, tela cheia", "Voltar" };
    int c, old, keep = 0, done = 0, t;
    c = menu_run("MODO DE VIDEO", it, 4);
    if (c < 0 || c > 2 || c == vidMode) return;
    if (c == VID_HDMI) {
        snprintf(ui.extra[0], sizeof(ui.extra[0]), "Precisa de cabo componente ou adaptador HDMI do PS2.");
        snprintf(ui.extra[1], sizeof(ui.extra[1]), "Conversor AV (amarelo) para HDMI costuma nao aceitar 480p.");
        ui.extraCol[0] = C_MUTED; ui.extraCol[1] = C_RED;
        ui.extraN = 2;
        if (!confirm("MODO HDMI (480p)", "Trocar para 480p progressivo?")) return;
    }
    old = vidMode;
    video_apply(c);

    /* confirmacao com contagem regressiva: se a tela ficar preta, volta sozinho ao modo anterior */
    snprintf(ui.title, sizeof(ui.title), "MANTER ESTE MODO?");
    ui.modal = 1;
    for (t = 600; t > 0 && !done; t--) {
        read_pad();
        if (pad_new & PAD_CROSS)  { keep = 1; done = 1; }
        if (pad_new & PAD_CIRCLE) { done = 1; }
        snprintf(ui.msg, sizeof(ui.msg), "X mantem, O volta. Volta sozinho em %d s", (t + 59) / 60);
        render();
        present();
    }
    ui.modal = 0;
    if (keep) { snd_play(SND_OK); toast_s("Modo de video aplicado", 0); }
    else      { video_apply(old); toast_s("Modo de video anterior restaurado", 0); }
}

static void show_tutorial(void)
{
    int done = 0;
    tutPage = 0; tutOpen = 1;
    snd_play(SND_TAB);
    while (!done) {
        read_pad();
        if ((pad_new & (PAD_RIGHT | PAD_R1)) && tutPage < TUT_PAGES - 1) { tutPage++; snd_play(SND_TAB); }
        if ((pad_new & (PAD_LEFT | PAD_L1)) && tutPage > 0)              { tutPage--; snd_play(SND_TAB); }
        if (pad_new & (PAD_CIRCLE | PAD_CROSS)) { done = 1; snd_play(SND_BACK); }
        render();
        present();
    }
    tutOpen = 0;
}

static void show_credits(void)
{
    snprintf(ui.extra[0], sizeof(ui.extra[0]), "Gerenciador de saves para PlayStation 2");
    snprintf(ui.extra[1], sizeof(ui.extra[1]), "Desenvolvido por Diego Reis");
    ui.extraCol[0] = C_MUTED; ui.extraCol[1] = C_ACC;
    snprintf(ui.extra[2], sizeof(ui.extra[2]), "Musica: HilalDeep - All for Music (Original Mix)");
    ui.extraCol[2] = C_TXT;
    ui.extraN = 3;
    info("SAVE MANAGER REIS " APP_VERSION, "Creditos");
    ui.extraN = 0;
}

/* seletor de volume da musica: ajusta ao vivo; X confirma, O volta ao valor anterior */
static void tool_volume(void)
{
    int old = snd_music_volume(), done = 0, v;
    volVal = old;
    volEditing = 1;
    snd_play(SND_TAB);
    while (!done) {
        read_pad();
        v = volVal;
        if (pad_new & PAD_RIGHT)  v++;
        if (pad_new & PAD_LEFT)   v--;
        if (pad_new & PAD_UP)     v = 10;
        if (pad_new & PAD_DOWN)   v = 0;
        if (pad_new & PAD_SQUARE) v = 6;
        if (v < 0) v = 0;
        if (v > 10) v = 10;
        if (v != volVal) { volVal = v; snd_music_volume_set(v); snd_play(SND_MOVE); }
        if (pad_new & PAD_CIRCLE) { snd_music_volume_set(old); done = 1; snd_play(SND_BACK); }
        if (pad_new & PAD_CROSS)  { done = 1; snd_play(SND_OK); }
        render();
        present();
    }
    volEditing = 0;
}

static void tool_settings(void)
{
    for (;;) {
        char l0[40], l1[56], l2[64], l3[56];
        const char *it[7];
        int c;
        snprintf(l0, sizeof(l0), "Som: %s", snd_enabled() ? "Ligado" : "Desligado");
        if (!snd_music_present()) snprintf(l1, sizeof(l1), "Musica: sem arquivo (musica.wav)");
        else                      snprintf(l1, sizeof(l1), "Musica: %s", snd_music_enabled() ? "Ligada" : "Desligada");
        {
            char bar[11];
            int k, vv = snd_music_volume();
            for (k = 0; k < 10; k++) bar[k] = (k < vv) ? '|' : '.';
            bar[10] = 0;
            snprintf(l2, sizeof(l2), "Volume da musica: [%s] %d%%", bar, vv * 10);
        }
        snprintf(l3, sizeof(l3), "Modo de video: %s", VID_NAME[vidMode]);
        it[0] = l0; it[1] = l1; it[2] = l2; it[3] = l3; it[4] = "Acertar data e hora"; it[5] = "Creditos"; it[6] = "Voltar";
        c = menu_run("AJUSTES", it, 7);
        if (c < 0 || c == 6) return;
        if (c == 0)      { snd_set(!snd_enabled()); snd_play(SND_OK); }
        else if (c == 1) {
            if (!snd_music_present()) {                          /* tenta carregar de novo (arquivo colocado depois) */
                musTried = 0;
                music_try_load();
                if (!snd_music_present() && !ui.toastT) toast_s("musica.wav nao encontrada em SaveManagerReis", 1);
            } else snd_music_set(!snd_music_enabled());
            snd_play(SND_OK);
        }
        else if (c == 2) tool_volume();
        else if (c == 3) tool_video();
        else if (c == 4) tool_clock();
        else if (c == 5) show_credits();
    }
}

static void do_mc_tools(void)
{
    const char *items[16] = {
        "Verificar saves (detectar corrompidos)",
        "Apagar saves marcados",
        "Copiar marcados para o outro Memory Card",
        "Trocar icone do save (BMP 128x128)",
        "Backup de tudo para USB (escolher formato)",
        "Recuperar cartao ilegivel / corrompido",
        "Formatar Memory Card",
        "Organizar / filtrar / buscar",
        "Detalhes do save  (R3)",
        "Renomear (nome do jogo, pasta ou backup)",
        "Recuperar save selecionado (corrompido)",
        "Criar cartao virtual novo (VMC)",
        "Ajustes (som, musica, video, data e hora, creditos)",
        "Sair do aplicativo",
        "Tutorial: o que cada botao faz",
        "Creditos"
    };
    int c;
    c = menu_run("FERRAMENTAS", items, 16);
    if (c >= 0 && c <= 6) tab = TAB_MC;       /* as ferramentas atuam no Memory Card */
    if (c == 7) { tool_view(); return; }
    if (c == 8) { show_details(tab); return; }
    if (c == 11) { tool_new_vmc(); return; }
    if (c == 10) { tool_recover_save(); return; }
    if (c == 9) { tool_rename(); return; }
    if (c == 12) { tool_settings(); return; }
    if (c == 14) { show_tutorial(); return; }
    if (c == 15) { show_credits(); return; }
    if (c == 13) {
        if (confirm("SAIR", "Fechar o Save Manager Reis?")) { config_flush(); quitReq = 1; }
        return;
    }
    if (c == 0) tool_verify();
    else if (c == 1) tool_delete();
    else if (c == 2) tool_copy();
    else if (c == 3) tool_icon();
    else if (c == 4) {
        const char *fm[5] = { "Pasta (arquivos soltos)", "PSU", "MAX (compactado)", "CBS (CodeBreaker)", "ZIP (abre no computador)" };
        int f = menu_run("FORMATO DO BACKUP", fm, 5);
        if (f >= 0) {
            int oldFmt = expFmt;
            clear_filters();                       /* backup de tudo: ignora filtro e busca */
            view_apply_all();
            mark_all_mc();
            expFmt = (f == 4) ? FMT_ZIP : f;
            do_export();
            expFmt = oldFmt;
        }
    }
    else if (c == 5) tool_recover();
    else if (c == 6) {
        if (!mcReady && !mcNoFmt) { toast("Memory Card nao encontrado%.0d", 0); return; }
        tool_format();
    }
}

/* ============================================================
 *  Inicializacao
 * ============================================================ */
/* monta a lista de cartoes: com multitap numa porta, entram os 4 slots dela */
static void cards_init(void)
{
    int p, sl, n = 0;
    for (p = 0; p < 2; p++) {
        int tap = (mtapGetConnection(p) == 1) ? 1 : 0;
        for (sl = 0; sl < (tap ? 4 : 1); sl++) {
            cardPort[n] = p; cardSlot[n] = sl; cardTap[n] = tap;
            n++;
        }
    }
    if (n != nCards) { nCards = n; mcSlot = 0; }
    if (mcSlot >= nCards) mcSlot = 0;
}

static void init_iop(void)
{
    int ret;
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    SifLoadModule("rom0:XSIO2MAN", 0, NULL);
    SifLoadModule("rom0:XMTAPMAN", 0, NULL);          /* multitap (ignora se a BIOS nao tiver) */
    SifLoadModule("rom0:XPADMAN", 0, NULL);
    SifLoadModule("rom0:XMCMAN", 0, NULL);
    SifLoadModule("rom0:XMCSERV", 0, NULL);

    SifExecModuleBuffer(iomanX_irx, size_iomanX_irx, 0, NULL, &ret);
    SifExecModuleBuffer(fileXio_irx, size_fileXio_irx, 0, NULL, &ret);
    SifExecModuleBuffer(usbd_irx, size_usbd_irx, 0, NULL, &ret);
    SifExecModuleBuffer(bdm_irx, size_bdm_irx, 0, NULL, &ret);
    SifExecModuleBuffer(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx, 0, NULL, &ret);
    SifExecModuleBuffer(usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL, &ret);
    SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &ret);
    SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret);

    fileXioInit();
    sceCdInit(SCECdINoD);
    mtapInit();
    mtapPortOpen(0);
    mtapPortOpen(1);
    mcInit(MC_TYPE_XMC);
    padInit(0);
    padPortOpen(0, 0, padbuf);
    snd_init();
#ifdef EMBED_MUSIC
    if (snd_music_embed(musica_wav, size_musica_wav) != 0)   /* toca desde o inicio, sem precisar do pendrive */
        toast_s("Musica embutida invalida: use WAV PCM de 8 ou 16 bits", 1);
#endif
}

static void init_gfx(void)
{
    gsGlobal = gsKit_init_global();
    vidOrigMode = gsGlobal->Mode; vidOrigW = gsGlobal->Width; vidOrigH = gsGlobal->Height;
    vidOrigInter = gsGlobal->Interlace; vidOrigField = gsGlobal->Field;
    gsGlobal->PSM = GS_PSM_CT32;
    gsGlobal->PSMZ = GS_PSMZ_16S;
    gsGlobal->ZBuffering = GS_SETTING_ON;       /* Z-buffer real (alocado e limpo pelo gsKit_clear) */
    gsGlobal->DoubleBuffering = GS_SETTING_ON;
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);   /* mistura alfa normal */
    gsKit_vram_clear(gsGlobal);
    gsKit_init_screen(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);

    font = gsKit_init_fontm();
    gsKit_fontm_upload(gsGlobal, font);
    font->Spacing = 0.62f;
    icon_slots_init();
    btn_tex_init();

    gsKit_clear(gsGlobal, RGBA(0, 0, 0, 0x80));
}

int main(int argc, char *argv[])
{
    int i, N;
    Entry *L;

    init_iop();
    tz_refresh();
    init_gfx();

    /* aguarda o controle ficar pronto */
    {
        int st;
        for (i = 0; i < 300; i++) {
            st = padGetState(0, 0);
            if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) break;
            gsKit_vsync_wait();
        }
    }

    snprintf(ui.title, sizeof(ui.title), "INICIANDO");
    snprintf(ui.msg, sizeof(ui.msg), "Procurando Memory Card e USB...");
    ui.busy = 1; ui.total = 100;
    splash = 1;
    for (i = 0; i < 120; i++) {                 /* ~2s para o USB montar */
        progress("Procurando Memory Card e USB...", i, 120);
        if (i == 5 || i == 20) cards_init();                   /* o multitap demora para ser detectado */
        if (i > 70 && usb_ready()) break;        /* minimo ~1 s: mostra a logo */
    }
    ui.busy = 0;
    splash = 0;

    config_load_apply(0);                           /* le o config.ini do pendrive (se houver) */
    refresh_mc();
    refresh_usb();
    snd_play(SND_START);

    for (;;) {
        read_pad();

        config_poll();

        if ((frameNo % 180) == 0) {              /* reavalia o multitap de tempos em tempos (detecta tardio) */
            int oldN = nCards;
            cards_init();
            if (nCards != oldN) refresh_mc();
        }

        L = (tab == TAB_MC) ? mcList : usbList;
        N = (tab == TAB_MC) ? mcCount : usbCount;

        if (pad_new & PAD_START) { do_mc_tools(); if (quitReq) break; }
        {
            int ot = tab;
            if (pad_new & PAD_R1) tab = (tab + 1) % 3;
            if (pad_new & PAD_L1) tab = (tab + 2) % 3;
            if (tab != ot) snd_play(SND_TAB);
        }

        if (pad_new & PAD_L3) {                              /* L3 alterna entre grade e lista */
            int t;
            listMode = !listMode;
            for (t = 0; t < 3; t++) topRow[t] = 0;           /* o render reposiciona para mostrar o cursor */
            snd_play(SND_TAB);
            snprintf(ui.toast, sizeof(ui.toast), "Visao: %s", listMode ? "Lista" : "Grade de cards");
            ui.toastErr = 0; ui.toastT = 90;
            continue;
        }
        if ((pad_new & PAD_R3) && N > 0) { show_details(tab); continue; }

        if (N > 0) {
            int *c = &cursorPos[tab];
            int oc = *c;
            int step = listMode ? 1 : 4, side = listMode ? gridRows : 1;       /* lista: Esq/Dir pulam uma pagina */
            if (pad_new & PAD_RIGHT) { *c += side; if (*c > N - 1) *c = N - 1; }
            if (pad_new & PAD_LEFT)  { *c -= side; if (*c < 0) *c = 0; }
            if (pad_new & PAD_DOWN)  { *c += step; if (*c > N - 1) *c = N - 1; }
            if (pad_new & PAD_UP)    { *c -= step; if (*c < 0) *c = 0; }
            if (*c != oc) snd_play(SND_MOVE);
            if (pad_new & PAD_CROSS) { L[*c].sel = !L[*c].sel; snd_play(SND_SELECT); }
            if (pad_new & PAD_SQUARE) {
                snd_play(SND_SELECT);
                int any = 0;
                for (i = 0; i < N; i++) if (!L[i].sel) any = 1;
                for (i = 0; i < N; i++) L[i].sel = any;
            }
            if (pad_new & PAD_TRIANGLE) {
                if (tab == TAB_MC)        do_export();
                else if (tab == TAB_CONV) do_convert_tab();
                else                      do_import();
            }
        }
        if (pad_new & PAD_CIRCLE) {
            if (tab == TAB_MC)  refresh_mc();
            if (tab == TAB_USB || tab == TAB_CONV) refresh_usb();
        }
        if (pad_new & PAD_SELECT) { cards_init(); refresh_mc(); refresh_usb(); toast("Lista atualizada%.0d", 0); }
        if (pad_new & PAD_L2) {
            char nm[8];
            if (nCards > 1) mcSlot = (mcSlot + 1) % nCards;
            snd_play(SND_TAB);
            refresh_mc();
            card_name(mcSlot, nm, sizeof(nm));
            snprintf(ui.toast, sizeof(ui.toast), "Memory Card slot %s", nm);
            ui.toastErr = 0; ui.toastT = 120;
        }
        if ((pad_new & PAD_R2) && tab == TAB_CONV) {
            convFmt = (convFmt + 1) % 3;
            snd_play(SND_SELECT);
            snprintf(ui.toast, sizeof(ui.toast), "Converter para: %s", FMT_NAME[convFmt]);
            ui.toastErr = 0; ui.toastT = 120;
        }
        if ((pad_new & PAD_R2) && tab == TAB_MC) {
            expFmt = next_exp_fmt(expFmt);
            snd_play(SND_SELECT);
            snprintf(ui.toast, sizeof(ui.toast), "Formato de exportacao: %s", FMT_NAME[expFmt]);
            ui.toastErr = 0; ui.toastT = 120;
        }

        if (tab < 2) {
            Entry *LL = (tab == TAB_MC) ? mcList : usbList;
            int NN = (tab == TAB_MC) ? mcCount : usbCount;
            if (NN > 0) {
                int ci = cursorPos[tab];
                if ((frameNo & 1) == 0) icon_pump(tab, LL, NN);        /* 1 icone a cada 2 quadros: sem travadas */
                if (spinTab != tab || spinIdx != ci) {            /* cursor mudou: volta o anterior ao angulo fixo */
                    if (spinTab >= 0 && spinIdx >= 0 && spinIdx < MAX_ENT && icons[spinTab][spinIdx])
                        icon_render(icons[spinTab][spinIdx], ICON_CA0, ICON_SA0);
                    spinTab = tab; spinIdx = ci; gCos = ICON_CA0; gSin = ICON_SA0;
                }
                if (icons[tab][ci]) { icon_spin(); icon_render(icons[tab][ci], gCos, gSin); }   /* icone selecionado gira */
            }
        }

        {
            Entry *LT = (tab == TAB_MC) ? mcList : usbList;
            int NT = (tab == TAB_MC) ? mcCount : usbCount;
            if (NT > 0 && (frameNo & 1) == 1) title_pump(tab, LT, NT);
            if (tab == TAB_MC && (frameNo & 3) == 3) mcsize_pump();
        }

        if (ui.toastT > 0) ui.toastT--;

        render();
        present();
    }

    padPortClose(0, 0);
    SifExitRpc();
    LoadExecPS2("rom0:OSDSYS", 0, NULL);
    return 0;
}
