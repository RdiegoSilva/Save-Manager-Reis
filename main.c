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

/* ---------------- IRX embutidos (gerados pelo Makefile) ---------------- */
extern unsigned char iomanX_irx[];       extern unsigned int size_iomanX_irx;
extern unsigned char fileXio_irx[];      extern unsigned int size_fileXio_irx;
extern unsigned char usbd_irx[];         extern unsigned int size_usbd_irx;
extern unsigned char bdm_irx[];          extern unsigned int size_bdm_irx;
extern unsigned char bdmfs_fatfs_irx[];  extern unsigned int size_bdmfs_fatfs_irx;
extern unsigned char usbmass_bd_irx[];   extern unsigned int size_usbmass_bd_irx;

/* ---------------- Constantes ---------------- */
#define APP_NAME      "SAVE MANAGER REIS"
#define APP_VERSION   "v1.0"
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

static void draw_card(float x, float y, float w, float h, const Entry *e, int isCur, int isUsb)
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

    /* icone da regiao */
    vgrad(x + 8, y + 11, 42, 42, reg_col(r), RGBA(20, 22, 50, 0x80));
    fill_circle(x + 29, y + 32, 17, RGBA(255, 255, 255, 0x22));
    ctext(x + 29, y + 22, 0.8f, C_TXT, REG_TAG[r]);

    /* tamanho / data / hora */
    if (e->size >= 1024 * 1024) snprintf(buf, sizeof(buf), "%u.%u MB", e->size >> 20, ((e->size & 0xFFFFF) * 10) >> 20);
    else                         snprintf(buf, sizeof(buf), "%u KB", (e->size + 1023) >> 10);
    text(x + 58, y + 10, 0.50f, C_ACC, buf);
    text(x + 58, y + 26, 0.45f, C_TXT, e->date);
    text(x + 58, y + 40, 0.45f, C_MUTED, e->time);

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
            ctext(W / 2, 200, 0.48f, C_MUTED, usbOk ? "Exporte um save na aba MEMORY CARD" : "Depois pressione O para atualizar");
        }
    } else {
        const float gap = 10, vgap = 8, top = 130, ch = 78;
        const float cw = (W - 2 * M - 3 * gap) / 4.0f;
        int rows = (int)((H - 58 - top + vgap) / (ch + vgap));
        int cur = cursorPos[tab], curRow = cur / 4, first, last;
        if (rows < 1) rows = 1;
        if (curRow < topRow[tab]) topRow[tab] = curRow;
        if (curRow >= topRow[tab] + rows) topRow[tab] = curRow - rows + 1;
        first = topRow[tab] * 4;
        last  = (topRow[tab] + rows) * 4;
        if (last > N) last = N;
        for (i = first; i < last; i++) {
            int r = i / 4 - topRow[tab], c = i % 4;
            draw_card(M + c * (cw + gap), top + r * (ch + vgap), cw, ch, &L[i], i == cur, tab == TAB_USB);
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
        if (tab == TAB_MC) x = key_pill(x, H - 30, "SELECT", "Slot");
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
    mc_check();
    if (!mcReady) return;
    n = mc_list(mcSlot, "/*", mctbl, MAX_ENT);
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

static void refresh_usb(void)
{
    struct dirent *ge, *se;
    char path[160], gname[64];
    DIR *g, *s;
    usbCount = 0;
    cursorPos[TAB_USB] = 0; topRow[TAB_USB] = 0;
    usbOk = usb_ready();
    if (!usbOk) return;
    g = opendir(ROOT_DIR);
    if (!g) return;
    while ((ge = readdir(g)) != NULL && usbCount < MAX_ENT) {
        if (ge->d_name[0] == '.') continue;
        snprintf(gname, sizeof(gname), "%s", ge->d_name);
        snprintf(path, sizeof(path), ROOT_DIR "/%s", gname);
        if (!is_dir(path)) continue;
        s = opendir(path);
        if (!s) continue;
        while ((se = readdir(s)) != NULL && usbCount < MAX_ENT) {
            Entry *e;
            int y = 0, mo = 0, d = 0, h = 0, mi = 0, sc = 0;
            char sub[256];
            if (se->d_name[0] == '.') continue;
            snprintf(sub, sizeof(sub), "%s/%s", path, se->d_name);
            if (!is_dir(sub)) continue;
            e = &usbList[usbCount++];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", gname);
            snprintf(e->stamp, sizeof(e->stamp), "%s", se->d_name);
            if (sscanf(se->d_name, "%d-%d-%d_%d-%d-%d", &y, &mo, &d, &h, &mi, &sc) >= 5) {
                snprintf(e->date, sizeof(e->date), "%02d/%02d/%02d", d, mo, y % 100);
                snprintf(e->time, sizeof(e->time), "%02d:%02d", h, mi);
            } else {
                snprintf(e->date, sizeof(e->date), "--/--/--");
                snprintf(e->time, sizeof(e->time), "--:--");
            }
            e->size = dir_size(sub);
        }
        closedir(s);
    }
    closedir(g);
    qsort(usbList, usbCount, sizeof(Entry), cmp_usb);
}

/* ============================================================
 *  Copia de arquivos
 * ============================================================ */
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
static int export_one(const Entry *e, const char *stamp)
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
static int import_one(const Entry *e)
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
    if (g_total == 0) g_total = 1;

    snprintf(ui.title, sizeof(ui.title), "EXPORTANDO PARA USB");
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

        if (ui.toastT > 0) ui.toastT--;

        render();
        present();
    }

    padPortClose(0, 0);
    SifExitRpc();
    LoadExecPS2("rom0:OSDSYS", 0, NULL);
    return 0;
}
