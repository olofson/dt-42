/*
 * smixer.h - Very simple audio mixer for SDL
 *
 * (C) David Olofson, 2003, 2006
 */

#ifndef	SMIXER_H
#define	SMIXER_H

#include "SDL.h"

/*
 * Maximum number of sample frames that will ever be
 * processed in one go. Audio processing callbacks
 * rely on never getting a 'frames' argument greater
 * than this value.
 */
#define	SM_MAXFRAGMENT	256

/* Number of slots for loaded waveforms */
#define	SM_SOUNDS	16

/* Number of playback voices */
#define	SM_VOICES	16

#define	SM_C0		16.3515978312874


/*--------------------------------------------------------
	Application Interface
--------------------------------------------------------*/

int sm_open(int buffer);
void sm_close(void);
int sm_load(int sound, const char *file);
int sm_load_synth(int sound, const char *def);
void sm_unload(int sound);

/*
 * IMPORTANT! IMPORTANT! IMPORTANT! IMPORTANT! IMPORTANT!
 *
 *	The callbacks installed by the functions below
 *	will run in the SDL audio callback context!
 *	Thus, you must be very careful what you do in
 *	these callbacks, and what data you touch.
 *
 * IMPORTANT! IMPORTANT! IMPORTANT! IMPORTANT! IMPORTANT!
 */

/*
 * Install a control tick callback. This will be called
 * once as soon as possible, and then the callback's return
 * value determines how many audio samples to process before
 * the callback is called again.
 *    If the callback returns 0, it is uninstalled and never
 * called again.
 *    Use sm_set_control_cb(NULL) to remove any installed
 * callback instantly.
 */
typedef unsigned (*sm_control_cb)(void);
void sm_set_control_cb(sm_control_cb cb);

/*
 * Install an audio processing callback. This callback runs
 * right after the voice mixer, and may be used to analyze
 * or modify the audio stream before it is converted and
 * passed on to the audio output buffer.
 *    The buffer handed to the callback is in 32 bit signed
 * stereo format, and the 'frames' argument is the number
 * of full stereo samples to process.
 *    Use sm_set_audio_cb(NULL) to remove any installed
 * callback instantly.
 */
typedef void (*sm_audio_cb)(Sint32 *buf, int frames);
void sm_set_audio_cb(sm_audio_cb cb);


/*--------------------------------------------------------
	Real Time Control Interface
	(Use only from inside a control callback,
	or with the SDL audio thread locked!)
--------------------------------------------------------*/

/* Start playing 'sound' on 'voice' at L/R volumes 'lvol'/'rvol' */
void sm_play(unsigned voice, unsigned sound, float lvol, float rvol);

/* Set voice decay speed */
void sm_decay(unsigned voice, float decay);

/* If the pending interval > interval, cut it short. */
void sm_force_interval(unsigned interval);

/* Get the pending interval length */
int sm_get_interval(void);

/* Get number of frames left to next control callback */
int sm_get_next_tick(void);

#endif	/* SMIXER_H */
