/*
 * DT-42 DrumToy - an interactive audio example for SDL.
 *
 * (C) David Olofson, 2003, 2006
 */

#include "smixer.h"
#include "sseq.h"
#include "gui.h"
#include "version.h"
#include "SDL.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>


/*-------------------------------------------------------------------
	Application data types
-------------------------------------------------------------------*/

/* Maximum length of a file name/path */
#define	FNLENGTH	1024

/* Application states */
typedef enum
{
	DM_NORMAL = 0,
	DM_ASK_EXIT,
	DM_ASK_NEW,
	DM_ASK_LOADNAME,
	DM_ASK_SAVENAME
} DT_dialog_modes;


/*-------------------------------------------------------------------
	Application variables
-------------------------------------------------------------------*/

/* Application state and control */
DT_dialog_modes dialog_mode = DM_NORMAL;	/* GUI mode */
static int die = 0;			/* Exit application */
static GUI_pages page = GUI_PAGE_MAIN;	/* Current GUI page */

/* Audio */
static int abuffer = 2048;		/* Audio buffer size*/
/*
 * On Linux with the ALSA backend, OSCBUFFER needs to be
 * BUFFER * 3 for the oscilloscopes to be in sync with
 * the output. However, this works only up to 8192 samples.
 * I don't know if this works as intended with other
 * backends, or on other operating systems. If it's
 * important (like in interactive applications...), it
 * should probably be user configurable.
 */
static int dbuffer = -1;		/* Sync delay buffer size */

/* Oscilloscopes */
static int oscpos = 0;			/* Grab buffer position */
static int plotpos = 0;			/* Plot position */
static Sint32 *osc_left = NULL;		/* Left audio grab buffer */
static Sint32 *osc_right = NULL;	/* Right audio grab buffer */

/* Sequencer control */
static float tempo = 120.0f;		/* Current sequencer tempo */
static short *playposbuf = NULL;	/* Sequencer pos grab buffer */
static unsigned playpos = 0;		/* Current pos (calculated) */
static unsigned last_playpos = -100000;
static int playing = 0;
static int looping = 0;

/* Video */
static int sdlflags = SDL_SWSURFACE;	/* SDL display init flags */

/* Song file */
static char *songfilename = NULL;	/* File name of current song */
static int must_exist = 1;		/* Exit if file does not exist */

/* Line editor */
static char *ed_buffer = NULL;		/* String buffer */
static int ed_pos = 0;			/* Cursor position */

/* Song editor */
static unsigned scrollpos = 0;		/* Scroll position */
static int edtrack = 0;			/* Cursor track */
static int editing = 0;			/* Editing enabled! */
static int update_edit = 1;		/* Needs updating! */

/* Selection and copy/paste */
static int sel_start_x = -1;		/* Start step */
static int sel_start_y = -1;		/* Start track */
static int sel_end_x = -1;		/* End step */
static int sel_end_y = -1;		/* End track */
static char *block[SSEQ_TRACKS];	/* Clip board */

static int valid_selection(void)
{
	return (sel_start_x >= 0) && (sel_start_x != sel_end_x);
}


/*-------------------------------------------------------------------
	Command line interface
-------------------------------------------------------------------*/

static int parse_args(int argc, char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		if(strncmp(argv[i], "-f", 2) == 0)
		{
			sdlflags |= SDL_FULLSCREEN;
			printf("Requesting fullscreen display.\n");
		}
		else if(strncmp(argv[i], "-b", 2) == 0)
		{
			abuffer = atoi(argv[i] + 2);
			printf("Requested audio buffer size: %d.\n", abuffer);
		}
		else if(strncmp(argv[i], "-d", 2) == 0)
		{
			dbuffer = atoi(argv[i] + 2);
			printf("Requested delay buffer size: %d.\n", dbuffer);
		}
		else if(strncmp(argv[i], "-n", 2) == 0)
			must_exist = 0;
		else if(argv[i][0] != '-')
		{
			free(songfilename);
			if(strchr(argv[i], '.'))
				songfilename = strdup(argv[i]);
			else
			{
				int len = strlen(argv[i]);
				songfilename = malloc(len + 6);
				memcpy(songfilename, argv[i], len);
				memcpy(songfilename + len, ".dt42\0", 6);
			}
		}
		else
			return -1;
	}
	return 0;
}


static void usage(const char *exename)
{
	fprintf(stderr, ".----------------------------------------------------\n");
	fprintf(stderr, "| DT-42 DrumToy " VERSION "\n");
	fprintf(stderr, "| Copyright (C) 2006 David Olofson\n");
	fprintf(stderr, "|----------------------------------------------------\n");
	fprintf(stderr, "| Usage: %s [switches] <file>\n", exename);
	fprintf(stderr, "| Switches:  -b<x> Audio buffer size\n");
	fprintf(stderr, "|            -d<x> Delay buffer size\n");
	fprintf(stderr, "|            -f    Fullscreen display\n");
	fprintf(stderr, "|            -n    Create ew song\n");
	fprintf(stderr, "|            -h    Help\n");
	fprintf(stderr, "'----------------------------------------------------\n");
}


static void breakhandler(int a)
{
	die = 1;
}


/*-------------------------------------------------------------------
	Audio processing
-------------------------------------------------------------------*/

/* Grab data for the oscilloscopes */
static void grab_process(Sint32 *buf, int frames)
{
	int i;
	short pp = sseq_get_position();
	for(i = 0; i < frames; ++i)
	{
		int ind = (oscpos + i) % dbuffer;
		osc_left[ind] = buf[i * 2];
		osc_right[ind] = buf[i * 2 + 1];
		playposbuf[ind] = pp;
	}
	oscpos = (oscpos + frames) % dbuffer;
	plotpos = oscpos;
}


/* Soft saturation */
static void saturate_process(Sint32 *buf, int frames)
{
	int i;
	frames *= 2;
	for(i = 0; i < frames; ++i)
	{
		float s = (float)buf[i] * (1.0f / 0x800000);
		buf[i] = (int)((1.5f * s - 0.5f * s*s*s) * 0x800000);
	}
}


/* Clip samples so they don't wrap when converted to 16 bits */
static void clip_process(Sint32 *buf, int frames)
{
	int i;
	int ind = 0;
	for(i = 0; i < frames; ++i)
	{
		if(buf[ind] < -0x800000)
			buf[ind] = -0x800000;
		else if (buf[ind] > 0x007fffff)
			buf[ind] = 0x007fffff;
		if(buf[ind + 1] < -0x800000)
			buf[ind + 1] = -0x800000;
		else if (buf[ind + 1] > 0x007fffff)
			buf[ind + 1] = 0x007fffff;
		ind += 2;
	}
}


static void audio_process(Sint32 *buf, int frames)
{
	saturate_process(buf, frames);
	clip_process(buf, frames);
	grab_process(buf, frames);
}


/*-------------------------------------------------------------------
	Sequencer + GUI synchronized operations
-------------------------------------------------------------------*/

static void move(int notes)
{
	int i;
	int pos = sseq_get_position();
	pos += notes;
	if(pos < 0)
		pos = 0;
	sseq_set_position(pos);
	if(playing)
	{
/*
FIXME: This will not do the right thing with looping enabled!
*/
		playpos += notes;
		for(i = 0; i < dbuffer; ++i)
		{
			playposbuf[i] += notes;
			if(playposbuf[i] < 0)
				playposbuf[i] = 0;
		}
		if(playpos < 0)
			playpos = 0;
	}
	else
	{
		playpos = pos;
		for(i = 0; i < dbuffer; ++i)
			playposbuf[i] = pos;
	}
}


static void handle_note(int trk, int note)
{
	if((note >= '0') && (note <= '9'))
	{
		sseq_play_note(trk, note);
		gui_activity(trk);
	}
	if(editing)
	{
		if(note == 127)
		{
			if(!playing)
				move(-1);
			sseq_set_note(playpos, trk, '.');
		}
		else
		{
			sseq_set_note(playpos, trk, note);
			if(!playing)
				move(1);
		}
	}
}


static void set_loop(void)
{
	if(looping)
	{
		if(valid_selection())
		{
			if(sel_start_x < sel_end_x)
				sseq_loop(sel_start_x, sel_end_x);
			else
				sseq_loop(sel_start_x, sel_end_x);
		}
		else
			sseq_loop(scrollpos, scrollpos + 32);
	}
	else
		sseq_loop(-1, -1);
}


/*-------------------------------------------------------------------
	Application page handling
-------------------------------------------------------------------*/
static void switch_page(GUI_pages new_page)
{
	page = new_page;
	last_playpos = -100000;
	update_edit = 1;
	gui_draw_screen(page);
}


/*-------------------------------------------------------------------
	File I/O
-------------------------------------------------------------------*/

static int load_song(const char *fn)
{
	char buf[128];
	int res = sseq_load_song(fn);
	if(res < 0)
		snprintf(buf, sizeof(buf), "ERROR Loading \"%s\"!", fn);
	else
		snprintf(buf, sizeof(buf), "Loaded \"%s\"", fn);
	gui_message(buf, -1);
	move(-10000);
	update_edit = 1;
	return res;
}


static int save_song(const char *fn)
{
	char buf[128];
	int res = sseq_save_song(fn);
	if(res < 0)
		snprintf(buf, sizeof(buf), "ERROR Saving \"%s\"!", fn);
	else
		snprintf(buf, sizeof(buf), "Saved \"%s\"", fn);
	gui_message(buf, -1);
	return res;
}


/*-------------------------------------------------------------------
	Application exit query and checking
-------------------------------------------------------------------*/

static void ask_exit(void)
{
	gui_message("Really Exit DT-42? \001Y/\002N", -1);
	dialog_mode = DM_ASK_EXIT;
}


static void handle_key_ask_exit(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_KP_ENTER:
	  case SDLK_RETURN:
	  case SDLK_y:
		dialog_mode = DM_NORMAL;
		gui_message("Bye!", -1);
		die = 1;
		break;
	  case SDLK_ESCAPE:
	  case SDLK_n:
		gui_message("Aborted - Not Exiting.", -1);
		dialog_mode = DM_NORMAL;
		break;
	  default:
		break;
	}
}


/*-------------------------------------------------------------------
	Ask before clearing song
-------------------------------------------------------------------*/

static void ask_new(void)
{
	gui_message("Really clear and start new song? \001Y/\002N", -1);
	dialog_mode = DM_ASK_NEW;
}


static void handle_key_ask_new(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_KP_ENTER:
	  case SDLK_RETURN:
	  case SDLK_y:
		dialog_mode = DM_NORMAL;
		load_song("default.dt42");
		gui_message("New song - defaults loaded.", -1);
		break;
	  case SDLK_ESCAPE:
	  case SDLK_n:
		gui_message("Aborted.", -1);
		dialog_mode = DM_NORMAL;
		break;
	  default:
		break;
	}
}


/*-------------------------------------------------------------------
	File handling
-------------------------------------------------------------------*/

static void ask_loadname(const char *orig)
{
	SDL_EnableUNICODE(1);
	ed_buffer = calloc(FNLENGTH, 1);
	if(orig)
	{
		strncpy(ed_buffer, orig, FNLENGTH - 1);
		ed_pos = strlen(ed_buffer);
	}
	else
	{
		strncpy(ed_buffer, ".dt42", FNLENGTH - 1);
		ed_pos = 0;
	}
	gui_message(ed_buffer, ed_pos);
	dialog_mode = DM_ASK_LOADNAME;
}


static void ask_savename(const char *orig)
{
	ask_loadname(orig);
	dialog_mode = DM_ASK_SAVENAME;
}



/*-------------------------------------------------------------------
	Selection and block operations
-------------------------------------------------------------------*/

static void block_free(void)
{
	int i;
	for(i = 0; i < SSEQ_TRACKS; ++i)
	{
		free(block[i]);
		block[i] = NULL;
	}
}


static void block_copy(void)
{
	int i, x, w, y1, y2;

	if(!valid_selection())
		return;

	block_free();

	/* Select steps exclusively */
	if(sel_start_x <= sel_end_x)
	{
		x = sel_start_x;
		w = sel_end_x - sel_start_x;
	}
	else
	{
		x = sel_end_x;
		w = sel_start_x - sel_end_x;
	}

	/* Select tracks *inclusively*! */
	if(sel_start_y <= sel_end_y)
	{
		y1 = sel_start_y;
		y2 = sel_end_y;
	}
	else
	{
		y1 = sel_end_y;
		y2 = sel_start_y;
	}

	/* Copy, filling undefs with '.' */
	for(i = y1; i <= y2; ++i)
	{
		int j;
		block[i - y1] = malloc(w + 1);
		block[i - y1][w] = 0;
		for(j = 0; j < w; ++j)
		{
			int n = sseq_get_note(x + j, i);
			if(n < 0)
				n = '.';
			block[i - y1][j] = n;
		}
	}
}


static void block_delete(void)
{
	int i, x, w, y1, y2;

	if(!valid_selection())
		return;

	/* Select steps exclusively */
	if(sel_start_x <= sel_end_x)
	{
		x = sel_start_x;
		w = sel_end_x - sel_start_x;
	}
	else
	{
		x = sel_end_x;
		w = sel_start_x - sel_end_x;
	}

	/* Select tracks *inclusively*! */
	if(sel_start_y <= sel_end_y)
	{
		y1 = sel_start_y;
		y2 = sel_end_y;
	}
	else
	{
		y1 = sel_end_y;
		y2 = sel_start_y;
	}

	/* Copy, filling undefs with '.' */
	for(i = y1; i <= y2; ++i)
	{
		int j;
		for(j = 0; j < w; ++j)
			sseq_set_note(x + j, i, '.');
	}
}


static void block_paste(int x, int y)
{
	int i, w = 0;
	for(i = 0; i < SSEQ_TRACKS; ++i, y = (y + 1) % SSEQ_TRACKS)
	{
		int j;
		if(!block[i])
			continue;
		w = strlen(block[i]);
		for(j = 0; block[i][j]; ++j)
			sseq_set_note(x + j, y, block[i][j]);
	}
	if(w)
		move(w);
	update_edit = 1;
}


static void block_select(int x, int y)
{
	if(!editing)
		return;
	if(x < 0)
	{
		if(sel_start_x >= 0)
		{
			sel_start_x = -1;
			update_edit = 1;
			set_loop();
		}
		return;
	}
	if(sel_start_x < 0)
	{
		sel_start_x = x;
		sel_start_y = y;
		update_edit = 1;
		set_loop();
	}
	else
	{
		sel_end_x = x;
		sel_end_y = y;
		update_edit = 1;
		set_loop();
	}
}


/*-------------------------------------------------------------------
	Keyboard input
-------------------------------------------------------------------*/

static void handle_key_ask_filename(SDL_Event *ev)
{
	int len = strlen(ed_buffer);
	switch(ev->key.keysym.sym)
	{
	  case SDLK_HOME:
		ed_pos = 0;
		gui_message(ed_buffer, ed_pos);
		break;
	  case SDLK_END:
		ed_pos = len;
		gui_message(ed_buffer, ed_pos);
		break;
	  case SDLK_LEFT:
		if(ed_pos > 0)
			--ed_pos;
		gui_message(ed_buffer, ed_pos);
		break;
	  case SDLK_RIGHT:
		if(ed_pos < len)
			++ed_pos;
		gui_message(ed_buffer, ed_pos);
		break;
	  case SDLK_BACKSPACE:
		if(!ed_pos)
			break;
		--ed_pos;
		/* Fall through to DELETE! */
	  case SDLK_DELETE:
		if(!len)
			break;
		if(ed_pos == len)
			break;
		memmove(ed_buffer + ed_pos, ed_buffer + ed_pos + 1,
				len - ed_pos);
		gui_message(ed_buffer, ed_pos);
		break;
	  case SDLK_KP_ENTER:
	  case SDLK_RETURN:
		switch(dialog_mode)
		{
		  case DM_ASK_SAVENAME:
			if(save_song(ed_buffer) >= 0)
			{
				free(songfilename);
				songfilename = strdup(ed_buffer);
			}
			break;
		  case DM_ASK_LOADNAME:
			if(load_song(ed_buffer) >= 0)
			{
				free(songfilename);
				songfilename = strdup(ed_buffer);
			}
		  default:
			break;
		}
		free(ed_buffer);
		SDL_EnableUNICODE(0);
		dialog_mode = DM_NORMAL;
		break;
	  case SDLK_ESCAPE:
		gui_message("Aborted.", -1);
		free(ed_buffer);
		SDL_EnableUNICODE(0);
		dialog_mode = DM_NORMAL;
		break;
	  default:
	  {
		char c;
		if((ev->key.keysym.unicode & 0xff80) == 0)
			c = ev->key.keysym.unicode & 0x7f;
		else
			c = 127;
		if(c < ' ')
			break;
		if(len >= FNLENGTH - 1)
		{
			if(ed_pos < FNLENGTH - 2)
				memmove(ed_buffer + ed_pos + 1, ed_buffer + ed_pos,
					FNLENGTH - 1 - ed_pos);
		}
		else
			memmove(ed_buffer + ed_pos + 1, ed_buffer + ed_pos,
				len - ed_pos + 1);
		ed_buffer[ed_pos] = c;
		++ed_pos;
		gui_message(ed_buffer, ed_pos);
		break;
	  }
	}
}


static void handle_move_keys(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_LEFT:
		move(-1);
		break;
	  case SDLK_RIGHT:
		move(1);
		break;
	  case SDLK_PAGEUP:
	  {
		int pos = sseq_get_position();
		int newpos = pos;
		if(!(pos & 0xf))
		{
			newpos -= 16;
			if(newpos < 0)
				newpos = 0;
		}
		else
			newpos &= 0xfffffff0;
		move(newpos - pos);
		break;
	  }
	  case SDLK_PAGEDOWN:
	  {
		int pos = sseq_get_position();
		int newpos = pos;
		newpos &= 0xfffffff0;
		newpos += 16;
		move(newpos - pos);
		break;
	  }
	  case SDLK_UP:
		--edtrack;
		if(edtrack < 0)
			edtrack = SSEQ_TRACKS - 1;
		update_edit = 1;
		break;
	  case SDLK_DOWN:
		++edtrack;
		if(edtrack >= SSEQ_TRACKS)
			edtrack = 0;
		update_edit = 1;
		break;
	  default:
		break;
	}
}


static void handle_key_shift(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_INSERT:
		block_paste(playpos, edtrack);
		break;
	  case SDLK_LEFT:
	  case SDLK_RIGHT:
	  case SDLK_UP:
	  case SDLK_DOWN:
	  case SDLK_PAGEUP:
	  case SDLK_PAGEDOWN:
		block_select(playpos, edtrack);
		handle_move_keys(ev);
		block_select(playpos, edtrack);
		break;
	  default:
		break;
	}
}


static void handle_key_ctrl(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_INSERT:
	  case SDLK_c:
		block_copy();
		block_select(-1, -1);
		break;
	  case SDLK_x:
		block_copy();
		block_delete();
		block_select(-1, -1);
		break;
	  case SDLK_v:
		block_paste(playpos, edtrack);
		break;
	  case SDLK_o:
		ask_loadname(songfilename);
		break;
	  case SDLK_s:
		ask_savename(songfilename);
		break;
	  case SDLK_n:
		ask_new();
		break;
	  case SDLK_q:
		ask_exit();
		break;
	  default:
		break;
	}
}


static void handle_key_main(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_KP_PLUS:
	  case SDLK_PLUS:
		tempo += 1.0f;
		if(tempo > 999.0f)
			tempo = 999.0f;
		sseq_set_tempo(tempo);
		gui_tempo(tempo);
		break;
	  case SDLK_KP_MINUS:
	  case SDLK_MINUS:
		tempo -= 1.0f;
		if(tempo < 0.0f)
			tempo = 0.0f;
		sseq_set_tempo(tempo);
		gui_tempo(tempo);
		break;
	  case SDLK_LEFT:
	  case SDLK_RIGHT:
	  case SDLK_UP:
	  case SDLK_DOWN:
	  case SDLK_PAGEUP:
	  case SDLK_PAGEDOWN:
		handle_move_keys(ev);
		block_select(-1, -1);
		break;
	  case SDLK_F1:
	  case SDLK_F2:
	  case SDLK_F3:
	  case SDLK_F4:
	  case SDLK_F5:
	  case SDLK_F6:
	  case SDLK_F7:
	  case SDLK_F8:
	  case SDLK_F9:
	  case SDLK_F10:
	  case SDLK_F11:
	  case SDLK_F12:
	  {
		int trk = ev->key.keysym.sym - SDLK_F1;
		if(edtrack != trk)
		{
			edtrack = trk;
			update_edit = 1;
		}
		handle_note(edtrack, '9');
		break;
	  }
	  case SDLK_PERIOD:
	  case SDLK_DELETE:
		if(sel_start_x >= 0)
		{
			block_delete();
			block_select(-1, -1);
		}
		else
			handle_note(edtrack, '.');
		break;
	  case SDLK_BACKSPACE:
		if(sel_start_x >= 0)
		{
			block_delete();
			block_select(-1, -1);
		}
		else
			handle_note(edtrack, 127);
		break;
	  case SDLK_c:
		handle_note(edtrack, 'C');
		break;
	  case SDLK_v:
		handle_note(edtrack, 'V');
		break;
	  case SDLK_d:
		handle_note(edtrack, 'D');
		break;
	  case SDLK_j:
		handle_note(edtrack, 'J');
		break;
	  case SDLK_t:
		handle_note(edtrack, 'T');
		break;
	  case SDLK_z:
		handle_note(edtrack, 'Z');
		break;
	  case SDLK_0:
	  case SDLK_1:
	  case SDLK_2:
	  case SDLK_3:
	  case SDLK_4:
	  case SDLK_5:
	  case SDLK_6:
	  case SDLK_7:
	  case SDLK_8:
	  case SDLK_9:
		handle_note(edtrack, '0' + ev->key.keysym.sym - SDLK_0);
		break;
	  case SDLK_KP0:
	  case SDLK_KP1:
	  case SDLK_KP2:
	  case SDLK_KP3:
	  case SDLK_KP4:
	  case SDLK_KP5:
	  case SDLK_KP6:
	  case SDLK_KP7:
	  case SDLK_KP8:
	  case SDLK_KP9:
		handle_note(edtrack, '0' + ev->key.keysym.sym - SDLK_KP0);
		break;
	  case SDLK_SPACE:
		playing = !playing;
		sseq_pause(!playing);
		gui_status(playing, editing, looping);
		break;
	  case SDLK_l:
		looping = !looping;
		set_loop();
		gui_status(playing, editing, looping);
		break;
	  case SDLK_r:
		editing = !editing;
		update_edit = 1;
		gui_status(playing, editing, looping);
		if(!editing)
			block_select(-1, -1);
		break;
	  case SDLK_m:
		sseq_mute(edtrack, !sseq_muted(edtrack));
		break;
	  case SDLK_h:
		switch_page(GUI_PAGE_HELP1);
		break;
	  case SDLK_TAB:
		++page;
		if(page > GUI_PAGE__CYCLE)
			page = GUI_PAGE__FIRST;
		switch_page(page);
		break;
	  case SDLK_ESCAPE:
		ask_exit();
		break;
	  default:
		break;
	}
}


static void handle_key_help(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_ESCAPE:
		switch_page(GUI_PAGE_MAIN);
		break;
	  case SDLK_m:
		switch_page(GUI_PAGE_LOG);
		break;
	  case SDLK_TAB:
		++page;
		if(page > GUI_PAGE__CYCLE)
			page = GUI_PAGE__FIRST;
		switch_page(page);
		break;
	  default:
		++page;
		if(page > GUI_PAGE_HELP4)
			page = GUI_PAGE_HELP1;
		switch_page(page);
		break;
	}
}


static void handle_key_log(SDL_Event *ev)
{
	switch(ev->key.keysym.sym)
	{
	  case SDLK_ESCAPE:
		switch_page(GUI_PAGE_MAIN);
		break;
	  case SDLK_h:
		switch_page(GUI_PAGE_HELP1);
		break;
	  case SDLK_TAB:
		++page;
		if(page > GUI_PAGE__CYCLE)
			page = GUI_PAGE__FIRST;
		switch_page(page);
		break;
	  default:
		break;
	}
}


static void handle_key(SDL_Event *ev)
{
	switch(page)
	{
	  case GUI_PAGE_MAIN:
		if(ev->key.keysym.mod & KMOD_CTRL)
			handle_key_ctrl(ev);
		else if(ev->key.keysym.mod & KMOD_SHIFT)
			handle_key_shift(ev);
		else
			handle_key_main(ev);
		break;
	  case GUI_PAGE_LOG:
		handle_key_log(ev);
		break;
	  case GUI_PAGE_HELP1:
	  case GUI_PAGE_HELP2:
	  case GUI_PAGE_HELP3:
	  case GUI_PAGE_HELP4:
		handle_key_help(ev);
		break;
	}
}


/*-------------------------------------------------------------------
	Graphics
-------------------------------------------------------------------*/

static void update_main(SDL_Surface *screen, int dt)
{
	unsigned pos;

	/* Oscilloscopes */
	gui_oscilloscope(osc_left, dbuffer, plotpos,
			240, 8, 192, 128, screen);
	gui_oscilloscope(osc_right, dbuffer, plotpos,
			440, 8, 192, 128, screen);

	/* Update song info and editor */
	pos = playpos;
	if(pos != last_playpos)
	{
		int i;
		gui_tempo(sseq_get_tempo());
		gui_songpos(pos);
		last_playpos = pos;
		pos &= 0xffffffe0;
		if((pos >= scrollpos + 32) || (pos < scrollpos))
		{
			scrollpos = pos;
			if(looping)
				sseq_loop(scrollpos, scrollpos + 32);
		}
		update_edit = 1;
		for(i = 0; i < SSEQ_TRACKS; ++i)
		{
			int n = sseq_get_note(playpos, i);
			if((n >= '0') && (n <= '9') && playing &&
					!sseq_muted(i))
				gui_activity(i);
		}
	}

	if(update_edit)
	{
		gui_songedit(scrollpos, last_playpos, edtrack, editing);
		if(valid_selection())
			gui_songselect(sel_start_x - scrollpos,
					sel_start_y,
					sel_end_x - scrollpos,
					sel_end_y);
		else
			gui_songselect(-1, -1, -1, -1);
		update_edit = 0;
	}

	gui_draw_activity(dt);
}


/*-------------------------------------------------------------------
	main()
-------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
	SDL_Surface *screen;
	int res;
	int last_tick;

	if(parse_args(argc, argv) < 0)
	{
		usage(argv[0]);
		return 0;
	}

	if(SDL_Init(0) < 0)
		return -1;

	atexit(SDL_Quit);
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	if(dbuffer < 0)
		dbuffer = abuffer * 3;
	osc_left = calloc(dbuffer, sizeof(Sint32));
	osc_right = calloc(dbuffer, sizeof(Sint32));
	playposbuf = calloc(dbuffer, sizeof(short));
	if(!osc_left || !osc_right || !playposbuf)
	{
		fprintf(stderr, "Couldn't allocate delay buffers!\n");
		SDL_Quit();
		return -1;
	}

	screen = SDL_SetVideoMode(640, 480, 0, sdlflags);
	if(!screen)
	{
		fprintf(stderr, "Couldn't open display!\n");
		SDL_Quit();
		return -1;
	}
	SDL_WM_SetCaption("DT-42 DrumToy", "DrumToy");

	if(gui_open(screen) < 0)
	{
		fprintf(stderr, "Couldn't start GUI!\n");
		SDL_Quit();
		return -1;
	}
	switch_page(GUI_PAGE_MAIN);

	if(sm_open(abuffer) < 0)
	{
		fprintf(stderr, "Couldn't start mixer!\n");
		SDL_Quit();
		return -1;
	}

	sseq_open();
	sm_set_audio_cb(audio_process);

	/* Try to load song if specified */
	res = -1;
	if(songfilename)
	{
		res = load_song(songfilename);
		if(must_exist && (res < 0))
		{
			sm_close();
			SDL_Quit();
			fprintf(stderr, "Giving up! (Use the -n option"
					" to create a new song by name.)\n");
			return -1;
		}
		if(res >= 0)
		{
			playing = 1;
			res = 0;
		}
	}

	/* If no song was loaded, load default.dt42 instead */
	if(res < 0)
	{
		if(load_song("default.dt42") < 0)
		{
			sm_close();
			SDL_Quit();
			fprintf(stderr, "Couldn't load default song!\n");
			return -1;
		}
		gui_message("Welcome to DT-42 " VERSION
				".  (\005H for \005Help!)", -1);
	}

	gui_status(playing, editing, looping);

	sseq_pause(!playing);

	last_tick = SDL_GetTicks();
	while(!die)
	{
		SDL_Event ev;
		int tick = SDL_GetTicks();
		int dt = tick - last_tick;
		last_tick = tick;

		/* Handle GUI events */
		while(SDL_PollEvent(&ev))
		{
			switch(ev.type)
			{
			  case SDL_KEYDOWN:
				switch(dialog_mode)
				{
				  case DM_NORMAL:
					handle_key(&ev);
					break;
				  case DM_ASK_EXIT:
					handle_key_ask_exit(&ev);
					break;
				  case DM_ASK_NEW:
					handle_key_ask_new(&ev);
					break;
				  case DM_ASK_LOADNAME:
				  case DM_ASK_SAVENAME:
					handle_key_ask_filename(&ev);
					break;
				}
				break;
			  case SDL_QUIT:
				ask_exit();
				break;
			  default:
				break;
			}
		}

		/*
		 * Update the calculated current play position.
		 *	We know that the mixer generates 44100 samples/s.
		 *	Thus, plotpos should advance 44100 samples/s too.
		 *	osc_process() will resync plotpos every time it
		 *	runs, so it doesn't drift off over time.
		 */
		plotpos += 44100 * dt / 1000;

		/* Figure out current playback song position */
		playpos = playposbuf[plotpos % dbuffer];

		/* Update the screen */
		switch(page)
		{
		  case GUI_PAGE_MAIN:
			update_main(screen, dt);
			break;
		  case GUI_PAGE_LOG:
		  case GUI_PAGE_HELP1:
		  case GUI_PAGE_HELP2:
		  case GUI_PAGE_HELP3:
		  case GUI_PAGE_HELP4:
			break;
		}

		last_playpos = playpos;

		/* Refresh dirty areas of the screen */
		gui_refresh();

		/* Try to look less like a CPU hog */
		SDL_Delay(10);
	}

	sm_close();
	sseq_close();
	gui_close();
	SDL_Quit();
	free(osc_left);
	free(osc_right);
	free(playposbuf);
	free(songfilename);
	return 0;
}
