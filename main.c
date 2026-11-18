/*
 * ============================================================
 *   SAVE MANAGER REIS  -  homebrew para PlayStation 2
 * ------------------------------------------------------------
 *   - Lista os saves do Memory Card (slot 1 / slot 2) em um
 *     dashboard com cards organizados (icone, nome, tamanho,
 *     data e hora).
 *   - Exporta saves do Memory Card para pendrive / HD USB:
 *         mass:/SaveManagerReis/<PASTA_DO_SAVE>/<AAAA-MM-DD_HH-MM-SS>/
 *   - Importa backups do pendrive / HD USB para o Memory Card.
 *   - Converte direto: exporta/importa saves em .PSU e .MAX (alem de pasta).
 *   - Aba REDE (SMB) reservada para a proxima versao.
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
#include <sys/stat.h>
#include <sys/types.h>

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
#include <libmc.h>
#include <libpad.h>
#include <libcdvd.h>
#include <osd_config.h>
#include <gsKit.h>
#include <dmaKit.h>

#include "lzari.h"

/* ---------------- IRX embutidos (gerados pelo Makefile) ---------------- */
extern unsigned char iomanX_irx[];       extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];      extern unsigned int size_fileXio_irx;
extern unsigned char usbd_irx[];         extern unsigned int size_usbd_irx;
extern unsigned char bdm_irx[];          extern unsigned int size_bdm_irx;
extern unsigned char bdmfs_fatfs_irx[];  extern unsigned int size_bdmfs_fatfs_irx;
extern unsigned char usbmass_bd_irx[];   extern unsigned int size_usbmass_bd_irx;

/* ---------------- Constantes ---------------- */
#define APP_NAME      "SAVE MANAGER REIS"
#define APP_VERSION   "v1.1"
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

enum { TAB_MC = 0, TAB_USB = 1, TAB_NET = 2 };
enum { FMT_DIR = 0, FMT_PSU = 1, FMT_MAX = 2 };
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
    char stamp[24];    /* backup: AAAA-MM-DD_HH-MM-SS            */
    char date[12];     /* dd/mm/aa                               */
    char time[8];      /* hh:mm                                  */
    u32  size;         /* bytes                                  */
    int  sel;          /* marcado                                */
    u8   kind;         /* FMT_DIR / FMT_PSU / FMT_MAX (aba USB)  */
    char path[128];    /* caminho completo do backup (aba USB)   */
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
} UI;

/* ---------------- Globais ---------------- */
static GSGLOBAL *gsGlobal;
static GSFONTM  *font;
static UI        ui;

static Entry mcList[MAX_ENT],  usbList[MAX_ENT];
static int   mcCount, usbCount;
static int   tab = TAB_MC, mcSlot = 0, mcReady = 0, usbOk = 0, mcFreeKB = 0;
static int   cursorPos[3], topRow[3];
static u32   frameNo;

static sceMcTblGetDir mctbl[MAX_ENT]  __attribute__((aligned(64)));
static sceMcTblGetDir subtbl[MAX_ENT] __attribute__((aligned(64)));
static u8   iobuf[CHUNK]              __attribute__((aligned(64)));
static char padbuf[256]               __attribute__((aligned(64)));

static int expFmt = FMT_DIR, mcRawN = 0;
static const char *FMT_NAME[] = { "Pasta", "PSU", "MAX" };

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

/* O relogio e os cartoes do PS2 guardam a hora de TOQUIO (UTC+9). Converte para o fuso local. */
static void jst_to_local(Now *n)
{
    long days = days_from_civil(n->y, n->mo, n->d);
    long secs = days * 86400L + n->h * 3600L + n->mi * 60L + n->s;
    secs += (long)(tzMin - 540) * 60L;
    days = secs / 86400L;
    secs = secs % 86400L;
    if (secs < 0) { secs += 86400L; days--; }
    civil_from_days(days, &n->y, &n->mo, &n->d);
    n->h  = (int)(secs / 3600);
    n->mi = (int)((secs % 3600) / 60);
    n->s  = (int)(secs % 60);
}

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

static void rect(float x, float y, float w, float h, u64 c)
{
    gsKit_prim_sprite(gsGlobal, x, y, x + w, y + h, 1, c);
}

static void vgrad(float x, float y, float w, float h, u64 c1, u64 c2)
{
    gsKit_prim_quad_gouraud(gsGlobal, x, y, x + w, y, x, y + h, x + w, y + h, 1, c1, c1, c2, c2);
}

static void hgrad(float x, float y, float w, float h, u64 c1, u64 c2)
{
    gsKit_prim_quad_gouraud(gsGlobal, x, y, x + w, y, x, y + h, x + w, y + h, 1, c1, c2, c1, c2);
}

static void text(float x, float y, float scale, u64 col, const char *s)
{
    gsKit_fontm_print_scaled(gsGlobal, font, x, y, 1, scale, col, s);
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

/* botao do controle (18x18): kind 0=X 1=quadrado 2=triangulo 3=circulo */
static void pad_btn(float x, float y, int kind, u64 col)
{
    int i;
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
    rect(x, y, w, 18, C_CARD);
    text(x + 6, y + 4, 0.38f, C_ACC, keys);
    text(x + w + 6, y + 3, 0.45f, C_TXT, label);
    return x + w + 6 + tw(label, 0.45f) + 18;
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
static const char *REG_TAG[] = { "US", "EU", "JP", "KR", "?" };

static u64 reg_col(int r)
{
    switch (r) {
        case 0:  return RGBA(40, 110, 255, 0x80);
        case 1:  return RGBA(40, 190, 120, 0x80);
        case 2:  return RGBA(240, 70, 100, 0x80);
        case 3:  return RGBA(240, 170, 40, 0x80);
        default: return RGBA(120, 120, 160, 0x80);
    }
}

/* ============================================================
 *  Icones 3D dos saves (icon.sys + arquivo .ico) -> miniaturas
 * ============================================================ */
#define ICON_SZ     64
#define ICON_SLOTS  16
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
static float     gCos = ICON_CA0, gSin = ICON_SA0;
static GSTEXTURE iconTex[ICON_SLOTS];
static const Icon *slotOwner[ICON_SLOTS];
static u32       slotVer[ICON_SLOTS];

static void icon_free_all(int t);

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
    gsKit_prim_quad_gouraud(gsGlobal, x, y, x + sz, y, x, y + sz, x + sz, y + sz, 1,
                            ic->bg[0], ic->bg[1], ic->bg[2], ic->bg[3]);
    gsKit_prim_sprite_texture(gsGlobal, &iconTex[slot], x, y, 0.0f, 0.0f, x + sz, y + sz,
                              (float)ICON_SZ, (float)ICON_SZ, 1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
}

static void draw_card(float x, float y, float w, float h, const Entry *e, int isCur, int isUsb, const Icon *ic, int slot)
{
    char buf[48];
    int r = region_of(e->name);

    rect(x + 3, y + 4, w, h, C_SHADOW);
    if (isCur) {
        int p = (int)(frameNo % 60), a = p < 30 ? p : 60 - p;
        rect(x - 3, y - 3, w + 6, h + 6, RGBA(0, 140 + a * 4, 255, 0x80));
    }
    rect(x, y, w, h, e->sel ? C_CARD_S : C_CARD);
    rect(x, y, w, 3, isUsb ? C_ACC2 : reg_col(r));

    /* icone original do save (3D) ou, se nao houver, o icone da regiao */
    if (ic) {
        draw_icon(slot, ic, x + 7, y + 10, 44);
    } else {
        vgrad(x + 8, y + 11, 42, 42, reg_col(r), RGBA(20, 22, 50, 0x80));
        fill_circle(x + 29, y + 32, 17, RGBA(255, 255, 255, 0x22));
        ctext(x + 29, y + 22, 0.8f, C_TXT, REG_TAG[r]);
    }

    /* tamanho / data / hora */
    if (e->size >= 1024 * 1024) snprintf(buf, sizeof(buf), "%u.%u MB", e->size >> 20, ((e->size & 0xFFFFF) * 10) >> 20);
    else                         snprintf(buf, sizeof(buf), "%u KB", (e->size + 1023) >> 10);
    text(x + 58, y + 10, 0.50f, C_ACC, buf);
    text(x + 58, y + 26, 0.45f, C_TXT, e->date);
    text(x + 58, y + 40, 0.45f, C_MUTED, e->time);
    if (isUsb && e->kind != FMT_DIR) {
        const char *tg = FMT_NAME[e->kind];
        text(x + w - 8 - tw(tg, 0.45f), y + 40, 0.45f, C_ACC2, tg);
    }

    /* nome */
    fit(buf, e->name, w - 16, 0.50f);
    text(x + 8, y + 58, 0.50f, C_TXT, buf);

    if (e->sel) {
        rect(x + w - 22, y + 8, 16, 16, C_GREEN);
        ctext(x + w - 14, y + 11, 0.38f, C_DARK, "OK");
    }
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

    /* ---- cabecalho ---- */
    rect(0, 0, W, 54, C_BAR);
    hgrad(0, 54, W, 3, C_ACC, C_ACC2);
    hgrad(M, 10, 34, 34, C_ACC, C_ACC2);
    ctext(M + 17, 16, 0.9f, C_TXT, "R");
    text(M + 46, 7, 0.78f, C_TXT, APP_NAME);
    text(M + 48, 33, 0.40f, C_MUTED, "Gerenciador de saves  " APP_VERSION);
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
    text(W - M - tw(buf, 0.85f), 5, 0.85f, C_ACC, buf);
    text(W - M - tw(buf2, 0.40f), 34, 0.40f, C_MUTED, buf2);

    /* ---- abas ---- */
    {
        static const char *names[3] = { "MEMORY CARD", "USB / HD", "REDE" };
        x = M;
        for (i = 0; i < 3; i++) {
            float w = tw(names[i], 0.55f) + 26;
            if (i == tab) { hgrad(x, 64, w, 24, C_ACC, C_ACC2); text(x + 13, 69, 0.55f, C_DARK, names[i]); }
            else          { rect(x, 64, w, 24, C_CARD);         text(x + 13, 69, 0.55f, C_MUTED, names[i]); }
            x += w + 8;
        }
    }
    if (tab == TAB_MC) {
        float bx = W - M - 60;
        text(bx - 8 - tw("SLOT", 0.45f), 70, 0.45f, C_MUTED, "SLOT");
        for (i = 0; i < 2; i++) {
            float sx = bx + i * 32;
            if (i == mcSlot) { hgrad(sx, 64, 28, 24, C_ACC, C_ACC2); ctext(sx + 14, 69, 0.55f, C_DARK, i ? "2" : "1"); }
            else             { rect(sx, 64, 28, 24, C_CARD);         ctext(sx + 14, 69, 0.55f, C_MUTED, i ? "2" : "1"); }
        }
    } else if (tab == TAB_USB) {
        const char *s = usbOk ? "USB CONECTADO" : "SEM USB";
        float w = tw(s, 0.45f);
        fill_circle(W - M - w - 12, 76, 4, usbOk ? C_GREEN : C_RED);
        text(W - M - w, 70, 0.45f, usbOk ? C_GREEN : C_RED, s);
    }

    for (i = 0; i < N; i++) marked += L[i].sel;

    /* ---- quadros de resumo ---- */
    if (tab != TAB_NET) {
        const float tgap = 10, tile = (W - 2 * M - 2 * tgap) / 3.0f;
        const char *lab[3];
        char val[3][32];
        if (tab == TAB_MC) {
            lab[0] = "SAVES"; lab[1] = "MARCADOS"; lab[2] = "LIVRE";
            snprintf(val[0], 32, "%d", mcCount);
            snprintf(val[1], 32, "%d", marked);
            if (mcReady) snprintf(val[2], 32, "%d.%d MB", mcFreeKB / 1024, (mcFreeKB % 1024) * 10 / 1024);
            else         snprintf(val[2], 32, "--");
        } else {
            lab[0] = "BACKUPS"; lab[1] = "MARCADOS"; lab[2] = "PASTA";
            snprintf(val[0], 32, "%d", usbCount);
            snprintf(val[1], 32, "%d", marked);
            snprintf(val[2], 32, "SaveManagerReis");
        }
        for (i = 0; i < 3; i++) {
            float tx = M + i * (tile + tgap);
            rect(tx, 96, tile, 26, C_CARD);
            rect(tx, 96, 3, 26, i == 1 ? C_GREEN : C_ACC);
            text(tx + 11, 103, 0.40f, C_MUTED, lab[i]);
            text(tx + tile - 8 - tw(val[i], 0.50f), 102, 0.50f, C_TXT, val[i]);
        }
    }

    /* ---- grade de cards ---- */
    if (tab == TAB_NET) {
        rect(M, 110, W - 2 * M, 150, C_CARD);
        rect(M, 110, W - 2 * M, 3, C_ACC2);
        ctext(W / 2, 140, 0.8f, C_TXT, "REDE (SMB)  -  EM BREVE");
        ctext(W / 2, 178, 0.48f, C_MUTED, "Envio e recebimento de saves pela rede local");
        ctext(W / 2, 198, 0.48f, C_MUTED, "ficara disponivel na proxima versao.");
    } else if (N == 0) {
        rect(M, 134, W - 2 * M, 120, C_CARD);
        rect(M, 134, W - 2 * M, 3, C_ACC);
        if (tab == TAB_MC) {
            ctext(W / 2, 160, 0.75f, C_TXT, mcReady ? "Nenhum save neste Memory Card" : "Insira um Memory Card");
            ctext(W / 2, 200, 0.48f, C_MUTED, "SELECT troca entre slot 1 e slot 2");
        } else {
            ctext(W / 2, 160, 0.75f, C_TXT, usbOk ? "Nenhum backup ainda" : "Conecte um pendrive / HD USB");
            ctext(W / 2, 200, 0.48f, C_MUTED, usbOk ? "Exporte um save ou coloque .psu / .max em SaveManagerReis" : "Depois pressione O para atualizar");
        }
    } else {
        const float gap = 10, vgap = 8, top = 130, ch = 78;
        const float cw = (W - 2 * M - 3 * gap) / 4.0f;
        int rows = (int)((H - 58 - top + vgap) / (ch + vgap));
        int cur = cursorPos[tab], curRow = cur / 4, first, last;
        if (rows < 1) rows = 1;
        gridRows = rows;
        if (curRow < topRow[tab]) topRow[tab] = curRow;
        if (curRow >= topRow[tab] + rows) topRow[tab] = curRow - rows + 1;
        first = topRow[tab] * 4;
        last  = (topRow[tab] + rows) * 4;
        if (last > N) last = N;
        for (i = first; i < last; i++) {
            int r = i / 4 - topRow[tab], c = i % 4;
            draw_card(M + c * (cw + gap), top + r * (ch + vgap), cw, ch, &L[i], i == cur, tab == TAB_USB, icons[tab][i], i - first);
        }
        {
            int totalRows = (N + 3) / 4;
            if (totalRows > rows) {
                float th = rows * (ch + vgap) - vgap, bh = th * rows / totalRows;
                rect(W - 14, top, 4, th, C_CARD);
                rect(W - 14, top + (th - bh) * topRow[tab] / (totalRows - rows), 4, bh, C_ACC);
            }
        }
    }

    /* ---- rodape ---- */
    rect(0, H - 56, W, 56, C_BAR);
    hgrad(0, H - 56, W, 2, C_ACC2, C_ACC);
    if (tab == TAB_NET) {
        x = key_pill(M, H - 40, "L1/R1", "Trocar aba");
        key_pill(x, H - 40, "START", "Sair");
    } else {
        x = M;
        x = key_btn(x, H - 50, 0, C_BLUE, "Marcar");
        x = key_btn(x, H - 50, 1, C_PINK, "Todos");
        x = key_btn(x, H - 50, 2, C_GREEN, tab == TAB_MC ? "Exportar p/ USB" : "Importar p/ Memory Card");
        x = key_btn(x, H - 50, 3, C_RED, "Atualizar");
        x = M;
        x = key_pill(x, H - 30, "L1/R1", "Abas");
        if (tab == TAB_MC) {
            char fl[32];
            snprintf(fl, sizeof(fl), "Formato: %s", FMT_NAME[expFmt]);
            x = key_pill(x, H - 30, "R2", fl);
            x = key_pill(x, H - 30, "SELECT", "Slot");
        }
        key_pill(x, H - 30, "START", "Sair");
    }

    /* ---- aviso (toast) ---- */
    if (ui.toastT > 0) {
        float bw = tw(ui.toast, 0.50f) + 30;
        rect((W - bw) / 2 + 2, H - 94, bw, 26, C_SHADOW);
        rect((W - bw) / 2, H - 96, bw, 26, ui.toastErr ? C_RED : C_GREEN);
        ctext(W / 2, H - 90, 0.50f, C_DARK, ui.toast);
    }

    /* ---- janela de progresso / confirmacao ---- */
    if (ui.busy || ui.modal) {
        float bx = (W - 440) / 2, by = (H - 130) / 2;
        rect(0, 0, W, H, C_DIM);
        rect(bx + 4, by + 5, 440, 130, C_SHADOW);
        rect(bx, by, 440, 130, C_BAR);
        hgrad(bx, by, 440, 4, C_ACC, C_ACC2);
        ctext(W / 2, by + 16, 0.7f, C_TXT, ui.title);
        ctext(W / 2, by + 52, 0.48f, C_MUTED, ui.msg);
        if (ui.busy) {
            float pct = ui.total ? (float)ui.done / (float)ui.total : 0.0f;
            if (pct > 1.0f) pct = 1.0f;
            rect(bx + 30, by + 92, 380, 16, C_CARD);
            hgrad(bx + 30, by + 92, 380 * pct, 16, C_ACC, C_ACC2);
        } else {
            key_btn(W / 2 - 100, by + 92, 0, C_BLUE, "Sim");
            key_btn(W / 2 + 20, by + 92, 3, C_RED, "Nao");
        }
    }
}

static void present(void)
{
    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);
    frameNo++;
}

static void progress(const char *msg, u32 done, u32 total)
{
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

static int confirm(const char *title, const char *msg)
{
    int res = -1;
    snprintf(ui.title, sizeof(ui.title), "%s", title);
    snprintf(ui.msg, sizeof(ui.msg), "%s", msg);
    ui.modal = 1;
    while (res < 0) {
        read_pad();
        if (pad_new & PAD_CROSS)  res = 1;
        if (pad_new & PAD_CIRCLE) res = 0;
        render();
        present();
    }
    ui.modal = 0;
    return res;
}

/* ============================================================
 *  Memory Card
 * ============================================================ */
static int mc_list(int slot, const char *pat, sceMcTblGetDir *tbl, int max)
{
    int n = 0;
    mcGetDir(slot, 0, (char *)pat, 0, max, tbl);
    mcSync(0, NULL, &n);
    return n < 0 ? 0 : n;
}

static void mc_check(void)
{
    int type, freec, fmt, ret = -10;
    mcGetInfo(mcSlot, 0, &type, &freec, &fmt);
    mcSync(0, NULL, &ret);
    mcGetInfo(mcSlot, 0, &type, &freec, &fmt);   /* 2a chamada: estabiliza apos troca de cartao */
    mcSync(0, NULL, &ret);
    mcReady = (ret >= -1) && (ret != -2) ? 1 : 0;
    if (ret == -1) mcReady = (fmt != 0);
    mcFreeKB = mcReady ? freec : 0;
}

static void refresh_mc(void)
{
    int i, j, n, m;
    mcCount = 0;
    cursorPos[TAB_MC] = 0; topRow[TAB_MC] = 0;
    icon_free_all(TAB_MC);
    mc_check();
    if (!mcReady) return;
    n = mc_list(mcSlot, "/*", mctbl, MAX_ENT);
    mcRawN = n;
    for (i = 0; i < n && mcCount < MAX_ENT; i++) {
        sceMcTblGetDir *t = &mctbl[i];
        Entry *e;
        char pat[64];
        if (!(t->AttrFile & MC_ATTR_DIR)) continue;
        if (t->EntryName[0] == '.') continue;
        e = &mcList[mcCount++];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", (char *)t->EntryName);
        {
            Now mt;
            mt.y = t->_Modify.Year; mt.mo = t->_Modify.Month; mt.d = t->_Modify.Day;
            mt.h = t->_Modify.Hour; mt.mi = t->_Modify.Min;   mt.s = t->_Modify.Sec;
            if (mt.y >= 2000) jst_to_local(&mt);
            snprintf(e->date, sizeof(e->date), "%02d/%02d/%02d", mt.d, mt.mo, mt.y % 100);
            snprintf(e->time, sizeof(e->time), "%02d:%02d", mt.h, mt.mi);
        }
        snprintf(pat, sizeof(pat), "/%s/*", e->name);
        m = mc_list(mcSlot, pat, subtbl, MAX_ENT);
        for (j = 0; j < m; j++)
            if (!(subtbl[j].AttrFile & MC_ATTR_DIR)) e->size += subtbl[j].FileSizeByte;
    }
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

static int cmp_usb(const void *a, const void *b)
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

/* .psu / .max (sem diferenciar maiusculas) */
static int ext_kind(const char *n)
{
    size_t l = strlen(n);
    if (l > 4 && n[l - 4] == '.') {
        char a = n[l - 3] | 0x20, b = n[l - 2] | 0x20, c = n[l - 1] | 0x20;
        if (a == 'p' && b == 's' && c == 'u') return FMT_PSU;
        if (a == 'm' && b == 'a' && c == 'x') return FMT_MAX;
    }
    return FMT_DIR;
}

/* le o cabecalho de um .psu/.max: nome da pasta do save e tamanho. 0 = invalido */
static int peek_pack(const char *path, int kind, char *name, int nmax, u32 *size)
{
    u8 h[512];
    struct stat st;
    int fd, r;
    fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    r = read(fd, h, sizeof(h));
    close(fd);
    if (stat(path, &st) != 0) return 0;
    if (kind == FMT_PSU) {
        if (r < 512 || !(ld16(h) & MC_ATTR_DIR)) return 0;
        snprintf(name, nmax, "%.31s", (char *)h + 64);
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
    add_usb(name, stamp, path, kind, size);
}

static void refresh_usb(void)
{
    struct dirent *ge, *se;
    char path[160], gname[64];
    DIR *g, *s;
    usbCount = 0;
    cursorPos[TAB_USB] = 0; topRow[TAB_USB] = 0;
    icon_free_all(TAB_USB);
    usbOk = usb_ready();
    if (!usbOk) return;
    g = opendir(ROOT_DIR);
    if (!g) return;
    while ((ge = readdir(g)) != NULL && usbCount < MAX_ENT) {
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
                if (strlen(sub) < sizeof(((Entry *)0)->path))
                    add_usb(gname, se->d_name, sub, FMT_DIR, dir_size(sub));
            } else if ((k = ext_kind(se->d_name)) != FMT_DIR) {
                add_pack_file(sub, se->d_name, k);
            }
        }
        closedir(s);
    }
    closedir(g);
    qsort(usbList, usbCount, sizeof(Entry), cmp_usb);
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
    else { mcOpen(mcSlot, 0, path, MC_RDONLY); mcSync(0, NULL, &fd); }
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
    int first = topRow[t] * 4, last = first + gridRows * 4, i, c = cursorPos[t];
    if (last > N) last = N;
    if (c >= first && c < last && !icons[t][c] && !iconFail[t][c]) i = c;
    else {
        for (i = first; i < last; i++) if (!icons[t][i] && !iconFail[t][i]) break;
        if (i >= last) return;
    }
    icons[t][i] = icon_load(t == TAB_USB, &L[i]);
    if (!icons[t][i]) iconFail[t][i] = 1;
}

static int copy_mc_to_usb(const char *src, const char *dst, const char *label)
{
    int fd = -1, r = 0, out, err = 0;
    mcOpen(mcSlot, 0, src, MC_RDONLY);
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
    mcOpen(mcSlot, 0, dst, MC_WRONLY | MC_CREAT | MC_TRUNC);
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
    mcMkDir(mcSlot, 0, dst);
    mcSync(0, NULL, &r);                    /* ja existir nao e erro: sobrescreve arquivos */

    snprintf(dir, sizeof(dir), ROOT_DIR "/%s/%s", e->name, e->stamp);
    d = opendir(dir);
    if (!d) return 1;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
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
    mcOpen(mcSlot, 0, src, MC_RDONLY);
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
    mcOpen(mcSlot, 0, src, MC_RDONLY);
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
    mcOpen(mcSlot, 0, dst, MC_WRONLY | MC_CREAT | MC_TRUNC);
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
static int mem_to_mc(const u8 *src, u32 size, const char *dst, const char *label)
{
    int fd = -1, w = 0, r, err = 0;
    u32 off = 0;
    mcOpen(mcSlot, 0, dst, MC_WRONLY | MC_CREAT | MC_TRUNC);
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

    psu_hdr(hdr, 0x8427, (u32)nf + 2, cr, mo, e->name);  if (wr_all(fd, hdr, 512)) ok = 0;
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
    mcMkDir(mcSlot, 0, dst);
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
    mcMkDir(mcSlot, 0, dst);
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

/* despacho por formato */
static int export_one(const Entry *e, const char *stamp)
{
    if (expFmt == FMT_PSU) return export_psu(e, stamp);
    if (expFmt == FMT_MAX) return export_max(e, stamp);
    return export_folder(e, stamp);
}

static int import_one(const Entry *e)
{
    if (e->kind == FMT_PSU) return import_psu(e);
    if (e->kind == FMT_MAX) return import_max(e);
    return import_folder(e);
}

/* ============================================================
 *  Acoes
 * ============================================================ */
static int gather(Entry *L, int N, int *idx)
{
    int i, k = 0;
    for (i = 0; i < N; i++) if (L[i].sel) idx[k++] = i;
    if (k == 0 && N > 0) idx[k++] = cursorPos[tab];     /* sem marcados: usa o card atual */
    return k;
}

static void do_export(void)
{
    int idx[MAX_ENT], k, i, fails = 0;
    Now n;
    char stamp[32];

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
    ui.busy = 1;
    for (i = 0; i < k; i++) fails += export_one(&mcList[idx[i]], stamp);
    ui.busy = 0;

    for (i = 0; i < mcCount; i++) mcList[i].sel = 0;
    refresh_usb();
    if (fails) toast("Concluido com %d falha(s)", fails);
    else       toast("%d save(s) exportado(s) com sucesso", k);
}

static int mc_has(const char *name)
{
    int i;
    for (i = 0; i < mcCount; i++) if (!strcmp(mcList[i].name, name)) return 1;
    return 0;
}

static void do_import(void)
{
    int idx[MAX_ENT], k, i, fails = 0;
    u32 need = 0;

    refresh_mc();
    if (!mcReady) { toast("Memory Card nao encontrado%.0d", 0); return; }
    k = gather(usbList, usbCount, idx);
    if (k == 0) return;

    for (i = 0; i < k; i++) need += usbList[idx[i]].size + 2048;
    if ((need >> 10) > (u32)mcFreeKB) {
        /* pode ser sobrescrita; so avisa quando claramente nao cabe */
        int allExist = 1;
        for (i = 0; i < k; i++) if (!mc_has(usbList[idx[i]].name)) allExist = 0;
        if (!allExist) { toast("Sem espaco no Memory Card%.0d", 0); return; }
    }

    for (i = 0; i < k; i++) {
        if (mc_has(usbList[idx[i]].name)) {
            char m[96];
            snprintf(m, sizeof(m), "%s ja existe. Sobrescrever?", usbList[idx[i]].name);
            if (!confirm("SAVE JA EXISTE", m)) { usbList[idx[i]].sel = -1; }
        }
    }

    g_done = 0; g_total = 0;
    for (i = 0; i < k; i++) if (usbList[idx[i]].sel != -1) g_total += usbList[idx[i]].size;
    if (g_total == 0) g_total = 1;

    snprintf(ui.title, sizeof(ui.title), "IMPORTANDO PARA MEMORY CARD");
    if (k == 1 && usbList[idx[0]].kind != FMT_DIR)
        snprintf(ui.title, sizeof(ui.title), "IMPORTANDO %s", FMT_NAME[usbList[idx[0]].kind]);
    ui.busy = 1;
    for (i = 0; i < k; i++) if (usbList[idx[i]].sel != -1) fails += import_one(&usbList[idx[i]]);
    ui.busy = 0;

    for (i = 0; i < usbCount; i++) usbList[i].sel = 0;
    refresh_mc();
    if (fails) toast("Concluido com %d falha(s)", fails);
    else       toast("%d save(s) importado(s) com sucesso", k);
}

/* ============================================================
 *  Inicializacao
 * ============================================================ */
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
    SifLoadModule("rom0:XPADMAN", 0, NULL);
    SifLoadModule("rom0:XMCMAN", 0, NULL);
    SifLoadModule("rom0:XMCSERV", 0, NULL);

    SifExecModuleBuffer(iomanX_irx, size_iomanX_irx, 0, NULL, &ret);
    SifExecModuleBuffer(fileXio_irx, size_fileXio_irx, 0, NULL, &ret);
    SifExecModuleBuffer(usbd_irx, size_usbd_irx, 0, NULL, &ret);
    SifExecModuleBuffer(bdm_irx, size_bdm_irx, 0, NULL, &ret);
    SifExecModuleBuffer(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx, 0, NULL, &ret);
    SifExecModuleBuffer(usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL, &ret);

    fileXioInit();
    sceCdInit(SCECdINoD);
    mcInit(MC_TYPE_XMC);
    padInit(0);
    padPortOpen(0, 0, padbuf);
}

static void init_gfx(void)
{
    gsGlobal = gsKit_init_global();
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
    for (i = 0; i < 120; i++) {                 /* ~2s para o USB montar */
        progress("Procurando Memory Card e USB...", i, 120);
        if (i > 20 && usb_ready()) break;
    }
    ui.busy = 0;

    refresh_mc();
    refresh_usb();

    for (;;) {
        read_pad();

        L = (tab == TAB_MC) ? mcList : usbList;
        N = (tab == TAB_MC) ? mcCount : usbCount;

        if (pad_new & PAD_START) break;
        if (pad_new & PAD_R1) tab = (tab + 1) % 3;
        if (pad_new & PAD_L1) tab = (tab + 2) % 3;

        if (tab != TAB_NET && N > 0) {
            int *c = &cursorPos[tab];
            if (pad_new & PAD_RIGHT) { if (*c < N - 1) (*c)++; }
            if (pad_new & PAD_LEFT)  { if (*c > 0) (*c)--; }
            if (pad_new & PAD_DOWN)  { *c += 4; if (*c > N - 1) *c = N - 1; }
            if (pad_new & PAD_UP)    { *c -= 4; if (*c < 0) *c = 0; }
            if (pad_new & PAD_CROSS) L[*c].sel = !L[*c].sel;
            if (pad_new & PAD_SQUARE) {
                int any = 0;
                for (i = 0; i < N; i++) if (!L[i].sel) any = 1;
                for (i = 0; i < N; i++) L[i].sel = any;
            }
            if (pad_new & PAD_TRIANGLE) {
                if (tab == TAB_MC) do_export();
                else               do_import();
            }
        }
        if (pad_new & PAD_CIRCLE) {
            if (tab == TAB_MC)  refresh_mc();
            if (tab == TAB_USB) refresh_usb();
        }
        if ((pad_new & PAD_SELECT) && tab == TAB_MC) { mcSlot ^= 1; refresh_mc(); }
        if ((pad_new & PAD_R2) && tab == TAB_MC) {
            expFmt = (expFmt + 1) % 3;
            snprintf(ui.toast, sizeof(ui.toast), "Formato de exportacao: %s", FMT_NAME[expFmt]);
            ui.toastErr = 0; ui.toastT = 120;
        }

        if (tab != TAB_NET) {
            Entry *LL = (tab == TAB_MC) ? mcList : usbList;
            int NN = (tab == TAB_MC) ? mcCount : usbCount;
            if (NN > 0) {
                int ci = cursorPos[tab];
                icon_pump(tab, LL, NN);
                if (spinTab != tab || spinIdx != ci) {            /* cursor mudou: volta o anterior ao angulo fixo */
                    if (spinTab >= 0 && spinIdx >= 0 && spinIdx < MAX_ENT && icons[spinTab][spinIdx])
                        icon_render(icons[spinTab][spinIdx], ICON_CA0, ICON_SA0);
                    spinTab = tab; spinIdx = ci; gCos = ICON_CA0; gSin = ICON_SA0;
                }
                if (icons[tab][ci]) { icon_spin(); icon_render(icons[tab][ci], gCos, gSin); }   /* icone selecionado gira */
            }
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
