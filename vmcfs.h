/*
 * vmcfs.h - leitor (somente leitura) de cartoes virtuais de PS2 (VMC do OPL, .bin / .ps2).
 * Entende o sistema de arquivos do Memory Card do PS2 (FAT indireta, 1 KB por cluster),
 * com ou sem bytes de ECC. Baseado na documentacao do mymc (Ross Ridge, dominio publico).
 */
#ifndef VMCFS_H
#define VMCFS_H

#include <stdint.h>

#define VMC_DF_FILE    0x0010
#define VMC_DF_DIR     0x0020
#define VMC_DF_PSX     0x1000
#define VMC_DF_EXISTS  0x8000

typedef struct {
    int      fd;
    int      ecc;                         /* arquivo tem 16 bytes de ECC por pagina */
    uint32_t page_size, ppc, cpc, aoff, aend, root, raw_page;
    uint32_t ifc[32];
    int      indN, fatN;                  /* clusters da FAT em cache (-1 = nenhum) */
    uint32_t ind[256], fat[256];
} Vmc;

typedef struct {
    uint16_t mode;
    uint32_t length;                      /* arquivo: bytes; pasta: n de entradas */
    uint32_t cluster;                     /* primeiro cluster (relativo a area alocavel) */
    uint8_t  cr[8], mo[8];                /* datas no formato do PS2 (hora de Toquio) */
    char     name[33];
} VmcEnt;

typedef int (*vmc_ent_fn)(void *ctx, const VmcEnt *e);     /* retorna 0 para continuar */

int   vmc_open(Vmc *v, const char *path);                  /* 0 = ok */
void  vmc_close(Vmc *v);
int   vmc_list_root(Vmc *v, vmc_ent_fn fn, void *ctx);     /* entradas da raiz (sem . e ..), so as que existem */
int   vmc_list_dir(Vmc *v, const VmcEnt *dir, vmc_ent_fn fn, void *ctx);
int   vmc_find_root(Vmc *v, const char *name, VmcEnt *out);/* 0 = achou */
long  vmc_read_file(Vmc *v, const VmcEnt *f, uint8_t *dst, uint32_t cap);   /* bytes lidos ou -1 */

#endif
