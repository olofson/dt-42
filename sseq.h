/*
 * sseq.h - Very simple drum sequencer
 *
 * (C) David Olofson, 2003, 2006
 */

#ifndef	SSEQ_H
#define	SSEQ_H

/* Number of sequencer tracks */
#define	SSEQ_TRACKS	16

void sseq_open(void);
void sseq_close(void);

int sseq_load_song(const char *fn);
int sseq_save_song(const char *fn);
void sseq_clear(void);

/* Real time control */
float sseq_get_tempo(void);
void sseq_set_tempo(float bpm);
void sseq_pause(int pause);
int sseq_get_position(void);
int sseq_get_next_position(void);
void sseq_set_position(unsigned pos);
void sseq_loop(int start, int end);
void sseq_play_note(int trk, char note);
void sseq_mute(int trk, int do_mute);
int sseq_muted(int trk);

/* Editing */
void sseq_add(int track, const char *data);
int sseq_get_note(unsigned pos, unsigned track);
void sseq_set_note(unsigned pos, unsigned track, int note);

#endif	/* SSEQ_H */
