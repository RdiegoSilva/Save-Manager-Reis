/*
 * vmcfs.c - leitor de cartoes virtuais de PS2 (VMC). Veja vmcfs.h.
 */
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include "vmcfs.h"

#define CLUSTER 1024u
#define EPC     256u                       /* entradas de FAT por cluster */
#define DIRENT  512u

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static int rd_at(int fd, off_t off, uint8_t *dst, uint32_t n)
{
    uint32_t got = 0;
    if (lseek(fd, off, SEEK_SET) != off) return -1;
    while (got < n) {
        int r = read(fd, dst + got, n - got);
        if (r <= 0) return -1;
        got += (uint32_t)r;
    }
    return 0;
}

/* le o cluster absoluto n (1024 bytes) */
static int read_cluster(Vmc *v, uint32_t n, uint8_t *dst)
{
    uint32_t p;
    if (n >= v->cpc) return -1;
    if (!v->ecc) return rd_at(v->fd, (off_t)n * CLUSTER, dst, CLUSTER);
    for (p = 0; p < v->ppc; p++)
        if (rd_at(v->fd, (off_t)(n * v->ppc + p) * v->raw_page, dst + p * v->page_size, v->page_size)) return -1;
    return 0;
}

static int read_words(Vmc *v, uint32_t cl, uint32_t *w)
{
    uint8_t b[CLUSTER];
    uint32_t i;
    if (read_cluster(v, cl, b)) return -1;
    for (i = 0; i < EPC; i++) w[i] = le32(b + i * 4);
    return 0;
}

/* proximo cluster da cadeia (relativo). 1 = tem proximo, 0 = fim, -1 = erro */
static int fat_next(Vmc *v, uint32_t cur, uint32_t *next)
{
    uint32_t fc, off, dbl, ioff, ic, fcl, val;
    if (cur >= v->aend) return -1;
    fc = cur / EPC; off = cur % EPC;
    dbl = fc / EPC; ioff = fc % EPC;
    if (dbl >= 32) return -1;
    ic = v->ifc[dbl];
    if (ic == 0) return -1;
    if (v->indN != (int)ic) {
        if (read_words(v, ic, v->ind)) return -1;
        v->indN = (int)ic;
    }
    fcl = v->ind[ioff];
    if (fcl == 0xFFFFFFFFu || fcl >= v->cpc) return -1;
    if (v->fatN != (int)fcl) {
        if (read_words(v, fcl, v->fat)) return -1;
        v->fatN = (int)fcl;
    }
    val = v->fat[off];
    if (val == 0xFFFFFFFFu) return 0;
    if (!(val & 0x80000000u)) return 0;            /* nao alocado / corrompido: trata como fim */
    *next = val & 0x7FFFFFFFu;
    return 1;
}

static void parse_ent(const uint8_t *b, VmcEnt *e)
{
    int i;
    e->mode    = (uint16_t)le16(b);
    e->length  = le32(b + 4);
    memcpy(e->cr, b + 8, 8);
    e->cluster = le32(b + 16);
    memcpy(e->mo, b + 24, 8);
    for (i = 0; i < 32 && b[64 + i]; i++) e->name[i] = (char)b[64 + i];
    e->name[i] = 0;
}

int vmc_open(Vmc *v, const char *path)
{
    uint8_t sb[0x154], c[CLUSTER];
    off_t size;
    uint32_t total, i, sp;
    VmcEnt dot, dotdot;

    memset(v, 0, sizeof(*v));
    v->indN = v->fatN = -1;
    v->fd = open(path, O_RDONLY);
    if (v->fd < 0) return -1;
    if (rd_at(v->fd, 0, sb, sizeof(sb)) || memcmp(sb, "Sony PS2 Memory Card Format ", 28) != 0) goto bad;
    v->page_size = le16(sb + 40);
    v->ppc       = le16(sb + 42);
    v->cpc       = le32(sb + 48);
    v->aoff      = le32(sb + 52);
    v->aend      = le32(sb + 56);
    v->root      = le32(sb + 60);
    for (i = 0; i < 32; i++) v->ifc[i] = le32(sb + 80 + i * 4);
    if (v->page_size * v->ppc != CLUSTER || v->page_size == 0) goto bad;      /* so o formato padrao (1 KB) */
    if (v->cpc == 0 || v->cpc > (1u << 22) || v->aoff >= v->cpc || v->aend == 0 || v->aend > v->cpc) goto bad;

    size = lseek(v->fd, 0, SEEK_END);
    total = v->cpc * v->ppc;                               /* paginas */
    sp = ((v->page_size + 127) / 128) * 4;                  /* bytes de ECC por pagina */
    v->raw_page = v->page_size;
    if (size >= (off_t)total * (off_t)(v->page_size + sp)) { v->ecc = 1; v->raw_page = v->page_size + sp; }
    else if (size >= (off_t)total * (off_t)v->page_size)   v->ecc = 0;
    else goto bad;

    /* confere a raiz: "." e ".." */
    if (read_cluster(v, v->aoff + v->root, c)) goto bad;
    parse_ent(c, &dot);
    parse_ent(c + DIRENT, &dotdot);
    if (strcmp(dot.name, ".") != 0 || strcmp(dotdot.name, "..") != 0 || !(dot.mode & VMC_DF_DIR) || dot.length < 2) goto bad;
    return 0;
bad:
    close(v->fd);
    v->fd = -1;
    return -1;
}

void vmc_close(Vmc *v)
{
    if (v->fd >= 0) close(v->fd);
    v->fd = -1;
}

/* percorre as entradas de uma pasta; first = primeiro cluster, count = n de entradas (0 = le do "." ) */
static int iter_dir(Vmc *v, uint32_t first, uint32_t count, vmc_ent_fn fn, void *ctx)
{
    uint8_t c[CLUSTER];
    uint32_t cur = first, idx = 0, steps = 0, k;
    for (;;) {
        int nx;
        if (read_cluster(v, v->aoff + cur, c)) return -1;
        for (k = 0; k < CLUSTER / DIRENT; k++, idx++) {
            VmcEnt e;
            if (idx == 0 && count == 0) {
                parse_ent(c, &e);
                count = e.length;
                if (count < 2 || count > 65536) return -1;
            }
            if (idx >= count) return 0;
            if (idx < 2) continue;                          /* "." e ".." */
            parse_ent(c + k * DIRENT, &e);
            if (!(e.mode & VMC_DF_EXISTS) || e.name[0] == 0) continue;
            if (fn(ctx, &e)) return 0;
        }
        nx = fat_next(v, cur, &cur);
        if (nx <= 0) return nx < 0 ? -1 : 0;
        if (++steps > v->cpc) return -1;                    /* protecao contra ciclo */
    }
}

int vmc_list_root(Vmc *v, vmc_ent_fn fn, void *ctx)
{
    return iter_dir(v, v->root, 0, fn, ctx);
}

int vmc_list_dir(Vmc *v, const VmcEnt *dir, vmc_ent_fn fn, void *ctx)
{
    if (!(dir->mode & VMC_DF_DIR) || dir->length < 2) return -1;
    return iter_dir(v, dir->cluster, dir->length, fn, ctx);
}

typedef struct { const char *name; VmcEnt *out; int found; } FindCtx;

static int find_cb(void *ctx, const VmcEnt *e)
{
    FindCtx *f = (FindCtx *)ctx;
    if ((e->mode & VMC_DF_DIR) && !strcmp(e->name, f->name)) { *f->out = *e; f->found = 1; return 1; }
    return 0;
}

int vmc_find_root(Vmc *v, const char *name, VmcEnt *out)
{
    FindCtx f;
    f.name = name; f.out = out; f.found = 0;
    if (iter_dir(v, v->root, 0, find_cb, &f) < 0 && !f.found) return -1;
    return f.found ? 0 : -1;
}

long vmc_read_file(Vmc *v, const VmcEnt *f, uint8_t *dst, uint32_t cap)
{
    uint8_t c[CLUSTER];
    uint32_t want = f->length < cap ? f->length : cap, done = 0, cur = f->cluster, steps = 0;
    if (want == 0) return 0;
    while (done < want) {
        uint32_t n = want - done;
        int nx;
        if (n > CLUSTER) n = CLUSTER;
        if (read_cluster(v, v->aoff + cur, c)) return -1;
        memcpy(dst + done, c, n);
        done += n;
        if (done >= want) break;
        nx = fat_next(v, cur, &cur);
        if (nx <= 0) return -1;                              /* cadeia menor que o tamanho */
        if (++steps > v->cpc) return -1;
    }
    return (long)done;
}

/* ---------------- criar cartao virtual novo ---------------- */
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int wr_at(int fd, off_t off, const uint8_t *src, uint32_t n)
{
    uint32_t put = 0;
    if (lseek(fd, off, SEEK_SET) != off) return -1;
    while (put < n) {
        int r = write(fd, src + put, n - put);
        if (r <= 0) return -1;
        put += (uint32_t)r;
    }
    return 0;
}

static void make_dirent(uint8_t *b, uint32_t mode, uint32_t length, const uint8_t now[8], const char *name)
{
    memset(b, 0, DIRENT);
    put16(b, mode);
    put32(b + 4, length);
    memcpy(b + 8, now, 8);
    memcpy(b + 24, now, 8);
    strncpy((char *)b + 64, name, 31);
}

int vmc_format(const char *path, uint32_t size_mb, const uint8_t now[8], vmc_prog_fn prog, void *ctx)
{
    static uint8_t zero[65536];
    uint8_t buf[CLUSTER];
    uint32_t cpc, alloc_clusters, fat_clusters, ifc_n, first_fat, aoff, erase_blocks, gb1, gb2, aend;
    uint32_t i, e, total, done = 0;
    int fd, rc = -1;
    const uint32_t first_ifc = 8;

    if (size_mb != 8 && size_mb != 16 && size_mb != 32 && size_mb != 64) return -1;
    cpc = size_mb * 1024;                                  /* clusters de 1 KB */
    alloc_clusters = cpc - (first_ifc + 2);
    fat_clusters = (alloc_clusters + EPC - 1) / EPC;
    ifc_n = (fat_clusters + EPC - 1) / EPC;
    if (ifc_n > 32) { ifc_n = 32; fat_clusters = ifc_n * EPC; }
    first_fat = first_ifc + ifc_n;
    aoff = first_fat + fat_clusters;
    erase_blocks = (cpc * 2) / 16;                         /* blocos de apagamento de 16 paginas */
    gb1 = erase_blocks - 1;
    gb2 = erase_blocks - 2;
    aend = gb2 * 8 - aoff;                                 /* 8 clusters por bloco */
    if (aend < 1) return -1;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;

    /* 1) arquivo inteiro zerado (o sistema FAT nao zera area estendida: precisa gravar de verdade) */
    total = size_mb * 1024 * 1024;
    memset(zero, 0, sizeof(zero));
    while (done < total) {
        if (prog && prog(ctx, done, total)) { rc = 1; goto fail; }
        if (write(fd, zero, sizeof(zero)) != (int)sizeof(zero)) goto fail;
        done += sizeof(zero);
    }

    /* 2) superbloco (pagina 0) */
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "Sony PS2 Memory Card Format ", 28);
    memcpy(buf + 28, "1.2.0.0", 7);
    put16(buf + 40, 512);                                  /* tamanho da pagina */
    put16(buf + 42, 2);                                    /* paginas por cluster */
    put16(buf + 44, 16);                                   /* paginas por bloco de apagamento */
    put16(buf + 46, 0xFF00);
    put32(buf + 48, cpc);
    put32(buf + 52, aoff);
    put32(buf + 56, aend);
    put32(buf + 60, 0);                                    /* raiz: cluster 0 da area alocavel */
    put32(buf + 64, gb1);
    put32(buf + 68, gb2);
    put32(buf + 80, first_ifc);                            /* ifc_list[0]; as demais ficam em 0 */
    for (i = 1; i < ifc_n; i++) put32(buf + 80 + i * 4, first_ifc + i);
    for (i = 0; i < 32; i++) put32(buf + 208 + i * 4, 0xFFFFFFFFu);    /* lista de blocos ruins: vazia */
    buf[336] = 2;                                          /* tipo do cartao */
    buf[337] = 0x2B;                                       /* flags */
    if (wr_at(fd, 0, buf, 512)) goto fail;

    /* 3) clusters da FAT indireta */
    for (i = 0; i < ifc_n; i++) {
        memset(buf, 0, sizeof(buf));
        for (e = 0; e < EPC; e++) {
            uint32_t idx = i * EPC + e;
            put32(buf + e * 4, idx < fat_clusters ? first_fat + idx : 0xFFFFFFFFu);
        }
        if (wr_at(fd, (off_t)(first_ifc + i) * CLUSTER, buf, CLUSTER)) goto fail;
    }

    /* 4) clusters da FAT */
    for (i = 0; i < fat_clusters; i++) {
        for (e = 0; e < EPC; e++) {
            uint32_t idx = i * EPC + e;
            put32(buf + e * 4, (idx != 0 && idx < aend) ? 0x7FFFFFFFu : 0xFFFFFFFFu);
        }
        if (wr_at(fd, (off_t)(first_fat + i) * CLUSTER, buf, CLUSTER)) goto fail;
    }

    /* 5) pasta raiz (cluster 0 da area alocavel): "." e ".." */
    memset(buf, 0, sizeof(buf));
    make_dirent(buf, 0x8427, 2, now, ".");
    make_dirent(buf + DIRENT, 0xA426, 0, now, "..");
    if (wr_at(fd, (off_t)aoff * CLUSTER, buf, CLUSTER)) goto fail;

    /* 6) bloco de apagamento "bom" de reserva (todo 0xFF), como o mymc */
    memset(zero, 0xFF, 8192);
    if (wr_at(fd, (off_t)gb2 * 8192, zero, 8192)) goto fail;

    if (prog) prog(ctx, total, total);
    close(fd);
    return 0;
fail:
    close(fd);
    unlink(path);
    return rc;
}
