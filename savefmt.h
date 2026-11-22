/*
 * savefmt.h - leitura de saves PS2 nos formatos CodeBreaker (.cbs) e
 * SharkPort / X-Port (.sps / .xps). Codigo portatil (sem dependencias do PS2SDK).
 * Baseado na especificacao do mymc (Ross Ridge, dominio publico).
 */
#ifndef SAVEFMT_H
#define SAVEFMT_H

#include <stdint.h>

/* chamada para cada arquivo do save. cr/mo: datas de 8 bytes do PS2 (podem ser NULL). retorna 0 se ok */
typedef int (*sv_file_fn)(void *ctx, const char *name, const uint8_t *data, uint32_t size,
                          const uint8_t *cr, const uint8_t *mo);

/* le so o nome da pasta do save (cabecalho). 0 = ok */
int sv_peek_cbs(const uint8_t *h, uint32_t n, char *dirname, int max);
int sv_peek_sps(const uint8_t *h, uint32_t n, char *dirname, int max);

/* le o save inteiro. dirname recebe o nome da pasta (>= 64 bytes). 0 = ok, <0 = erro */
int sv_parse_cbs(const uint8_t *in, uint32_t n, uint32_t max_out, char *dirname, sv_file_fn fn, void *ctx);
int sv_parse_sps(const uint8_t *in, uint32_t n, char *dirname, sv_file_fn fn, void *ctx);

/* gera um .cbs (CodeBreaker): zlib sem compactacao + RC4. cr/mo: datas de 8 bytes do PS2 (podem ser NULL).
 * Devolve um buffer (malloc) com o arquivo pronto e o tamanho em *outLen, ou NULL se faltar memoria. */
typedef struct { const char *name; const uint8_t *data; uint32_t size; const uint8_t *cr, *mo; } SvFile;
uint8_t *sv_build_cbs(const char *dirname, const SvFile *f, int n, const uint8_t *dcr, const uint8_t *dmo, uint32_t *outLen);

/* zlib (RFC 1950) -> out. retorna o tamanho gerado ou -1 */
long sv_zlib_inflate(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);

#endif
