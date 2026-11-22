/*
 * sound.h - efeitos sonoros do Save Manager Reis (gerados por codigo, sem arquivos de audio)
 * e musica de fundo em loop (mass:/SaveManagerReis/musica.wav). Usa audsrv (audsrv.irx + freesd.irx).
 */
#ifndef SOUND_H
#define SOUND_H

enum { SND_MOVE, SND_SELECT, SND_TAB, SND_OK, SND_BACK, SND_DONE, SND_ERR, SND_START, SND_COUNT };

void snd_init(void);          /* chame depois de carregar audsrv.irx e freesd.irx */
void snd_play(int id);        /* nao bloqueia; ignora se o som estiver desligado ou indisponivel */
void snd_update(void);        /* chame uma vez por quadro (mistura musica + efeitos e alimenta o audsrv) */
void snd_set(int on);         /* liga/desliga os EFEITOS */
int  snd_enabled(void);

/* musica de fundo: WAV PCM de 8 ou 16 bits, mono ou estereo, qualquer taxa. Pode vir embutida no ELF
 * (musica.wav na pasta do projeto) ou de mass:/SaveManagerReis/musica.wav (esta substitui a embutida). */
int  snd_music_embed(const unsigned char *wav, unsigned size);   /* WAV embutido no ELF (sem copiar): 0 = ok, -2 = invalido */
int  snd_music_load(const char *path);   /* 0 = ok, -1 = sem arquivo, -2 = formato invalido, -3 = grande demais / sem memoria */
int  snd_music_present(void);            /* 1 = ha musica carregada */
void snd_music_set(int on);
int  snd_music_enabled(void);
void snd_music_volume_set(int v);        /* 0..10 */
int  snd_music_volume(void);

#endif
