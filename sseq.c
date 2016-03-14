/*
 * sseq.c - Very simple drum sequencer
 *
 * Copyright 2003, 2006, 2016 David Olofson
 */

#include "sseq.h"
#include "smixer.h"
#include "version.h"
#include "SDL_audio.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define	SONG_FILE_VERSION	1


/* A sequencer track */
typedef struct
{
	char	*data;
	int	length;
	int	skip;
	int	mute;
	float	decay;
	float	lvol;
	float	rvol;
} SSEQ_track;


/* A song (file) tag */
typedef struct SSEQ_tag SSEQ_tag;
struct SSEQ_tag
{
	SSEQ_tag	*next;
	char		*label;
	char		*data;
};


/* A simple pattern sequencer */
typedef struct
{
	SSEQ_track	tracks[SSEQ_TRACKS];
	SSEQ_tag	*tags;
	int		last_position;
	int		position;
	int		interval;
	int		loop_start;
	int		loop_end;
} SSEQ_sequencer;


static SSEQ_sequencer seq;
static int paused = 0;


/*
 * Try to read an integer value.
 * Returns -1 if the string does not contain a valid
 * decimal integer value.
 */
static int get_index(const char *s, int *v)
{
	int i;
	if(!*s)
		return -1;	/* Empty string! */
	for(i = 0; s[i]; ++i)
		if((s[i] < '0' || s[i] > '9') && (s[i] != '-'))
			return -1;
	*v = atoi(s);
	return 0;
}


static void _set_tempo(float bpm)
{
	if(bpm <= 0)
		seq.interval = 0;
	else
		seq.interval = (int)(44100.0 / bpm * 60.0 / 4.0);
	sm_force_interval(seq.interval);
}


static void _set_defaults(void)
{
	int i;
	_set_tempo(120.0f);
	for(i = 0; i < SSEQ_TRACKS; ++i)
	{
		seq.tracks[i].decay = 0.0f;
		seq.tracks[i].lvol = 1.0f;
		seq.tracks[i].rvol = 1.0f;
	}
}


static void _play_note(int trk, char note)
{
	float vel = (note - '0') * (1.0f / 9.0f);
	if(vel)
		vel = 0.3f + vel * 0.7f;
	sm_play(trk, trk, vel * seq.tracks[trk].lvol,
			vel * seq.tracks[trk].rvol);
	sm_decay(trk, seq.tracks[trk].decay);
}


void sseq_mute(int trk, int do_mute)
{
	seq.tracks[trk].mute = do_mute;
}


int sseq_muted(int trk)
{
	return seq.tracks[trk].mute;
}


/* Find specified tag by label */
static SSEQ_tag *find_tag(const char *label)
{
	SSEQ_tag *tag = seq.tags;
	while(tag)
	{
		if(!strcmp(tag->label, label))
			return tag;
		tag = tag->next;
	}
	return NULL;
}


/* Add a new tag, even if there are others with the same label */
static SSEQ_tag *add_tag(const char *label, const char *data)
{
	SSEQ_tag *tag = malloc(sizeof(SSEQ_tag));
	if(!tag)
		return NULL;
	tag->label = strdup(label);
	tag->data = strdup(data);
	tag->next = NULL;
	if(!seq.tags)
		seq.tags = tag;
	else
	{
		SSEQ_tag *lt = seq.tags;
		while(lt->next)
			lt = lt->next;
		lt->next = tag;
	}
	return tag;
}


/* Set or create tag 'label' and assign 'data' to it */
static SSEQ_tag *set_tag(const char *label, const char *data)
{
	SSEQ_tag *tag = find_tag(label);
	if(tag)
	{
		free(tag->data);
		tag->data = strdup(data);
		return tag;
	}
	return add_tag(label, data);
}


static void remove_tags(void)
{
	while(seq.tags)
	{
		SSEQ_tag *tag = seq.tags;
		seq.tags = tag->next;
		free(tag->label);
		free(tag->data);
		free(tag);
	}
}


void _clear(void)
{
	int i;
	remove_tags();
	_set_defaults();
	for(i = 0; i < SSEQ_TRACKS; ++i)
	{
		free(seq.tracks[i].data);
		seq.tracks[i].data = NULL;
		seq.tracks[i].length = 0;
		seq.tracks[i].mute = 0;
		sm_unload(i);
	}
}


void sseq_clear(void)
{
	SDL_LockAudio();
	_clear();
	SDL_UnlockAudio();
}


static int load_line(const char *label, const char *data)
{
	int i;
	if(label[0] == 'I')
	{
		/* Instrument file reference? */
		if(get_index(label + 1, &i) >= 0)
		{
			add_tag(label, data);
			return sm_load(i, data);
		}
	}
	else if(label[0] == 'S')
	{
		/* Synth instrument definition? */
		if(get_index(label + 1, &i) >= 0)
		{
			add_tag(label, data);
			return sm_load_synth(i, data);
		}
	}
	else if(get_index(label, &i) >= 0)
	{
		sseq_add(i, data);	/* Track data */
		return 0;
	}

	/* Check for tags */
	i = 0;
	if(!strcmp(label, "CREATOR"))
		printf("        File creator: %s\n", data);
	else if(!strcmp(label, "VERSION"))
		printf("File creator version: %s\n", data);
	else if(!strcmp(label, "AUTHOR"))
		printf("         Song author: %s\n", data);
	else if(!strcmp(label, "TITLE"))
		printf("          Song title: %s\n", data);
	else
	{
		fprintf(stderr, "WARNING: Unknown tag \"%s\"\n", label);
		i = 1;
	}

	/* Store the tag, so we can write it back when saving */
	add_tag(label, data);
	return i;
}


int _load_song(const char *fn)
{
	int i;
	char *buf;
	int size;

	_clear();

	printf("Loading Song \"%s\"...\n", fn);

	/* Read file */
	FILE *f = fopen(fn, "rb");
	if(!f)
	{
		fprintf(stderr, "Could not open song \"%s\": %s\n",
				fn, strerror(errno));
		return -1;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if(size < 0)
	{
		fprintf(stderr, "Could not load song \"%s\": %s\n",
				fn, strerror(errno));
		fclose(f);
		return -1;
	}
	fseek(f, 0, SEEK_SET);
	buf = malloc(size + 1);
	if(!buf)
	{
		fprintf(stderr, "Could not load song \"%s\": "
				"Out of memory!\n", fn);
		fclose(f);
		return -1;
	}
	buf[size] = 0;		/* Safety NUL terminator */
	if(fread(buf, size, 1, f) < 1)
	{
		fprintf(stderr, "Could not load song \"%s\": %s\n",
				fn, strerror(errno));
		fclose(f);
		return -1;
	}
	fclose(f);

	/* Check format */
	if(strncmp(buf, "DT42", 4) != 0)
	{
		fprintf(stderr, "\"%s\" is not a DT42 file!\n", fn);
		free(buf);
		return -1;
	}
	if(strncmp(buf + 4, "SONG", 4) != 0)
	{
		fprintf(stderr, "\"%s\" is not a SONG file!\n", fn);
		free(buf);
		return -1;
	}
	if(atoi(buf + 8) > SONG_FILE_VERSION)
	{
		fprintf(stderr, "\"%s\" was created by a newer version"
				" of DT-42!\n", fn);
		free(buf);
		return -1;
	}

	/* First byte after header */
	for(i = 8; (i < size) && (buf[i] != '\n') ; ++i)
		;

	/* Parse! */
	while(i < size)
	{
		const char *label, *data;

		/* Find start of a label */
		for( ; (i < size) && (buf[i] < ' ') ; ++i)
			;
		if(i >= size)
			break;		/* EOF - Done! */
		label = buf + i;

		/* Find end of label */
		for( ; (i < size) && (buf[i] != ':') ; ++i)
			;
		if(i >= size)
		{
			fprintf(stderr, "Could not load song \"%s\": "
					"Tag parse error in label!\n", fn);
			free(buf);
			return -1;
		}
		buf[i] = 0;	/* Terminate. */

		/* Find start of data */
		++i;
		data = buf + i;

		/* Find end of data (EOLN) */
		for( ; (i < size) && (buf[i] != '\n') ; ++i)
			;
		if(i >= size)
		{
			fprintf(stderr, "Could not load song \"%s\": "
					"Tag parse error in data!\n", fn);
			free(buf);
			return -1;
		}
		buf[i] = 0;	/* Terminate. */

		/* Process the tag! */
		if(load_line(label, data) < 0)
		{
			fprintf(stderr, "Could not load song \"%s\": "
					"Critical parse error!\n", fn);
			free(buf);
			return -1;
		}
	}

	printf("Song \"%s\" loaded!\n", fn);
	free(buf);
	return 0;
}


int sseq_load_song(const char *fn)
{
	int res;
	SDL_LockAudio();
	res = _load_song(fn);
	SDL_UnlockAudio();
	return res;
}


int sseq_save_song(const char *fn)
{
	int t;
	int errs = 0;
	SSEQ_tag *tag;

	printf("Saving Song \"%s\"...\n", fn);

	/* Open file */
	FILE *f = fopen(fn, "wb");
	if(!f)
	{
		fprintf(stderr, "Could not open/create file \"%s\": %s\n",
				fn, strerror(errno));
		return -1;
	}

	/* Write header */
	errs += fprintf(f, "DT42SONG%d\n", SONG_FILE_VERSION) < 0;

	/* Set application metatags */
	set_tag("CREATOR", "DT-42 DrumToy");
	set_tag("VERSION", VERSION);

	/* Fill in any missing info tags */
	if(!find_tag("AUTHOR"))
		set_tag("AUTHOR", "Unknown");
	if(!find_tag("TITLE"))
		set_tag("TITLE", fn);

	/* Write tags */
	tag = seq.tags;
	while(tag)
	{
		errs += fprintf(f, "%s:%s\n", tag->label, tag->data) < 0;
		tag = tag->next;
	}
	errs += fprintf(f, "\n") < 0;

	/* Write track data */
	for(t = 0; t < SSEQ_TRACKS; ++t)
	{
/*
TODO: Nicer formatting...
 */
		if(!seq.tracks[t].data)
			continue;
		errs += fprintf(f, "%d:%s\n", t, seq.tracks[t].data) < 0;
	}

	if(errs)
	{
		fprintf(stderr, "Error writing \"%s\": %s\n",
				fn, strerror(errno));
		fclose(f);
		return -1;
	}

	printf("Song \"%s\" saved!\n", fn);
	fclose(f);
	return 0;
}


/*
 * Run the sequencer time for 'frames' sample frames,
 * and execute any events for that time period.
 */
static unsigned sseq_process(void)
{
	seq.last_position = seq.position;
	if(paused || !seq.interval)
		return 16;
	while(1)
	{
		int again = 0;
		int t;
		int newpos = seq.position + 1;
		if(seq.position == 0)
			_set_defaults();
		if(seq.loop_end >= 0)
		{
			if(newpos >= seq.loop_end)
			{
				if(seq.loop_start >= 0)
					newpos = seq.loop_start;
				else
					newpos = 0;
			}
		}
		for(t = 0; t < SSEQ_TRACKS; ++t)
		{
			char *d = seq.tracks[t].data + seq.position;
			int skip = seq.tracks[t].mute;
			if(seq.tracks[t].skip)
			{
				--seq.tracks[t].skip;
				skip = 1;
			}
			if(seq.position >= seq.tracks[t].length)
				continue;
			switch(*d)
			{
			  /* Note */
			  case '0':
			  case '1':
			  case '2':
			  case '3':
			  case '4':
			  case '5':
			  case '6':
			  case '7':
			  case '8':
			  case '9':
				/* Don't play command arguments! */
				if(skip)
					break;
				_play_note(t, *d);
				break;
			  /* Cut note */
			  case 'C':
				sm_decay(t, 0.9f);
				break;
			  /* Set note decay */
			  case 'D':
				seq.tracks[t].decay = (d[1] - '0') * 0.1f;
				seq.tracks[t].skip = 1;
				break;
			  /* Jump to position */
			  case 'J':
			  {
				int v = (d[1] - '0') * 100;
				v += (d[2] - '0') * 10;
				v += (d[3] - '0');
				newpos = v;
				again = 2;
				break;
			  }
			  /* Set tempo */
			  case 'T':
			  {
				int v = (d[1] - '0') * 100;
				v += (d[2] - '0') * 10;
				v += (d[3] - '0');
				_set_tempo(v);
				seq.tracks[t].skip = 3;
				break;
			  }
			  /* Set volume/balance */
			  case 'V':
				seq.tracks[t].lvol = (d[1] - '0') * (1.0f / 9.0f);
				seq.tracks[t].rvol = (d[2] - '0') * (1.0f / 9.0f);
				seq.tracks[t].skip = 2;
				break;
			  /* Zero time step */
			  case 'Z':
				again = 1;
				break;
			}
		}
		seq.position = newpos;
		if(!again)
			break;
		/*
		 * Prevent the skip feature from killing notes after a jump.
		 * Note that this will break if you jump to some place where
		 * you land in the middle of the arguments to a command!
		 * (But that probably wouldn't play correctly anyway, so who
		 * cares?)
		 */
		if(again == 2)
			for(t = 0; t < SSEQ_TRACKS; ++t)
				seq.tracks[t].skip = 0;
	}
	return seq.interval;
}


void sseq_pause(int pause)
{
	paused = pause;
}


void sseq_set_tempo(float bpm)
{
	SDL_LockAudio();
	_set_tempo(bpm);
	SDL_UnlockAudio();
}


float sseq_get_tempo(void)
{
	return 44100.0 / seq.interval * 60.0 / 4.0;
}


void sseq_play_note(int trk, char note)
{
	SDL_LockAudio();
	_play_note(trk, note);
	SDL_UnlockAudio();
}


int sseq_get_position(void)
{
	/*
	 * Note: We don't want seq.position, because that's
	 * actually the NEXT step in the sequence, whereas
	 * we want the CURRENTLY PLAYING step.
	 */
	return seq.last_position;
}


int sseq_get_next_position(void)
{
	return seq.position;
}


void sseq_set_position(unsigned pos)
{
	seq.position = pos;
}


void sseq_loop(int start, int end)
{
	SDL_LockAudio();
	seq.loop_start = start;
	seq.loop_end = end;
	SDL_UnlockAudio();
}


int sseq_get_note(unsigned pos, unsigned track)
{
	if(track >= SSEQ_TRACKS)
		return -1;
	if(pos >= seq.tracks[track].length)
		return -1;
	return seq.tracks[track].data[pos];
}


void sseq_set_note(unsigned pos, unsigned track, int note)
{
	SDL_LockAudio();
	if(track < SSEQ_TRACKS)
	{
		if(pos >= seq.tracks[track].length)
		{
			char *nt = realloc(seq.tracks[track].data,
					pos + 2);
			if(!nt)
			{
				SDL_UnlockAudio();
				return;
			}
			memset(nt + seq.tracks[track].length, '.',
					pos - seq.tracks[track].length);
			seq.tracks[track].data = nt;
			seq.tracks[track].data[pos + 1] = 0;
			seq.tracks[track].length = pos + 1;
		}
		seq.tracks[track].data[pos] = note;
	}
	SDL_UnlockAudio();
}


void sseq_open(void)
{
	memset(&seq, 0, sizeof(seq));
	sm_set_control_cb(sseq_process);
	sseq_loop(-1, -1);
	sseq_clear();
}


void sseq_close(void)
{
	sm_set_control_cb(NULL);
	sseq_clear();
	memset(&seq, 0, sizeof(seq));
}


void sseq_add(int track, const char *data)
{
	SDL_LockAudio();
	if(!seq.tracks[track].data)
		seq.tracks[track].data = strdup(data);
	else
	{
		char *new_track = malloc(strlen(seq.tracks[track].data) +
				strlen(data) + 1);
		strcpy(new_track, seq.tracks[track].data);
		strcat(new_track, data);
		free(seq.tracks[track].data);
		seq.tracks[track].data = new_track;
	}
	seq.tracks[track].length = strlen(seq.tracks[track].data);
	SDL_UnlockAudio();
}
