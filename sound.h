/*
 * sound.h - efeitos sonoros do Save Manager Reis (gerados por codigo, sem arquivos de audio).
 * Usa audsrv (audsrv.irx + freesd.irx).
 */
#ifndef SOUND_H
#define SOUND_H

enum { SND_MOVE, SND_SELECT, SND_TAB, SND_OK, SND_BACK, SND_DONE, SND_ERR, SND_START, SND_COUNT };

void snd_init(void);          /* chame depois de carregar audsrv.irx e freesd.irx */
void snd_play(int id);        /* nao bloqueia; ignora se o som estiver desligado ou indisponivel */
void snd_update(void);        /* chame uma vez por quadro (alimenta o audsrv aos poucos) */
void snd_set(int on);
int  snd_enabled(void);

#endif
