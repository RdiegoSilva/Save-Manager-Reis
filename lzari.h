/*
 * lzari.h - compressao LZARI (Haruhiko Okumura) + CRC32.
 * Usado pelo formato .max (Action Replay MAX).
 *
 * Formato do fluxo: [u32 tamanho_descomprimido (LE)] [dados aritmeticos].
 */
#ifndef LZARI_H
#define LZARI_H

#include <stdint.h>

/* chamada a cada ~8 KB processados (pode ser NULL) */
typedef void (*lz_tick_fn)(void *ctx, uint32_t pos);
/* grava n bytes; retorna 0 se ok */
typedef int (*lz_put_fn)(void *ctx, const uint8_t *p, uint32_t n);
/* devolve o proximo byte (0..255) ou -1 no fim */
typedef int (*lz_get_fn)(void *ctx);

/* Comprime in[0..n). Retorna o total de bytes gerados (incluindo os 4 do cabecalho) ou -1. */
long lzari_encode(const uint8_t *in, uint32_t n,
                  lz_put_fn put, void *pctx,
                  lz_tick_fn tick, void *tctx);

/* Descomprime para out (capacidade cap). Le o u32 de tamanho do proprio fluxo.
 * Retorna o tamanho descomprimido ou -1 em erro. */
long lzari_decode(lz_get_fn get, void *gctx,
                  uint8_t *out, uint32_t cap,
                  lz_tick_fn tick, void *tctx);

/* CRC32 padrao (igual ao do zlib). Comece com crc = 0. */
uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n);

#endif
