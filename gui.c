/*
 * gui.c - Tracker style GUI
 *
 * (C) David Olofson, 2006
 */

#include <stdlib.h>
#include <string.h>
#include "gui.h"
#include "sseq.h"

#define	MAXACTIVITY	400

static int dirtyrects = 0;
static SDL_Rect dirtytab[MAXRECTS];

static SDL_Surface *screen = NULL;
static SDL_Surface *font = NULL;

static char *message_text = NULL;
static int activity[SSEQ_TRACKS];


void gui_dirty(SDL_Rect *r)
{
	if(dirtyrects < 0)
		return;
	if((dirtyrects >= MAXRECTS) || !r)
		dirtyrects = -1;
	else
	{
		dirtytab[dirtyrects] = *r;
		++dirtyrects;
	}
}


void gui_refresh(void)
{
	if(dirtyrects < 0)
	{
		SDL_UpdateRect(screen, 0, 0, 0, 0);
		dirtyrects = 0;
	}
	else
		SDL_UpdateRects(screen, dirtyrects, dirtytab);
	dirtyrects = 0;
}


void gui_box(int x, int y, int w, int h, Uint32 c, SDL_Surface *dst)
{
	SDL_Rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = 1;
	SDL_FillRect(dst, &r, c);

	r.x = x;
	r.y = y + h - 1;
	r.w = w;
	r.h = 1;
	SDL_FillRect(dst, &r, c);

	r.x = x;
	r.y = y + 1;
	r.w = 1;
	r.h = h - 2;
	SDL_FillRect(dst, &r, c);

	r.x = x + w - 1;
	r.y = y + 1;
	r.w = 1;
	r.h = h - 2;
	SDL_FillRect(dst, &r, c);

	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	gui_dirty(&r);
}


void gui_bar(int x, int y, int w, int h, Uint32 c, SDL_Surface *dst)
{
	SDL_Rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	SDL_FillRect(dst, &r, SDL_MapRGB(dst->format, 0, 0, 0));
	gui_box(x, y, w, h, c, dst);
}


void gui_oscilloscope(Sint32 *buf, int bufsize,
		int start, int x, int y, int w, int h,
		SDL_Surface *dst)
{
	int i;
	Uint32 green, red;
	SDL_Rect r;
	int xscale = bufsize / w;
	if(xscale < 1)
		xscale = 1;
	else if(xscale > 8)
		xscale = 8;

	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	SDL_FillRect(dst, &r, SDL_MapRGB(dst->format, 0, 0, 0));
	gui_dirty(&r);

	green = SDL_MapRGB(dst->format, 0, 200, 0);
	red = SDL_MapRGB(dst->format, 255, 0, 0);
	r.w = 1;
	for(i = 0; i < w; ++i)
	{
		Uint32 c = green;
		int s = -buf[(start + i * xscale) % bufsize] >> 8;
		s *= h;
		s >>= 16;
		r.x = x + i;
		if(s < 0)
		{
			if(s <= -h / 2)
			{
				s = -h / 2;
				c = red;
			}
			r.y = y + h / 2 + s;
			r.h = -s;
		}
		else
		{
			++s;
			if(s >= h / 2)
			{
				s = h / 2;
				c = red;
			}
			r.y = y + h / 2;
			r.h = s;
		}
		SDL_FillRect(dst, &r, c);
	}

	r.x = x;
	r.y = y + h / 2;
	r.w = w;
	r.h = 1;
	SDL_FillRect(dst, &r, SDL_MapRGB(dst->format, 128, 128, 255));
}



SDL_Surface *gui_load_image(const char *fn)
{
	SDL_Surface *cvt;
	SDL_Surface *img = SDL_LoadBMP(fn);
	if(!img)
		return NULL;
	cvt = SDL_DisplayFormat(img);
	SDL_FreeSurface(img);
	return cvt;
}


void gui_text(int x, int y, const char *txt, SDL_Surface *dst)
{
	int sx = x;
	int sy = y;
	const char *stxt = txt;
	int highlights = 0;
	SDL_Rect sr;
	sr.w = FONT_CW;
	sr.h = FONT_CH;
	while(*txt)
	{
		int c = *txt++;
		switch(c)
		{
		  case 0:	/* terminator */
			break;
		  case '\n':	/* newline */
			x = sx;
			y += FONT_CH;
			break;
		  case '\t':	/* tab */
			x -= sx;
			x += 8 * FONT_CW;
			x %= 8 * FONT_CW;
			x += sx;
			break;
		  case '\001':	/* red highlight */
		  case '\002':	/* green highlight */
		  case '\003':	/* yellow highlight */
		  case '\004':	/* blue highlight */
		  case '\005':	/* purple highlight */
		  case '\006':	/* cyan highlight */
		  case '\007':	/* white highlight */
			highlights = 1;
			if(*txt == '\001')
				txt += 2;
			break;
		  case '\021':	/* red bullet */
		  case '\022':	/* green bullet */
		  case '\023':	/* yellow bullet */
		  case '\024':	/* blue bullet */
		  case '\025':	/* purple bullet */
		  case '\026':	/* cyan bullet */
		  case '\027':	/* white bullet */
		  {
			SDL_Rect r;
			int hlr = c & 1 ? 255 : 0;
			int hlg = c & 2 ? 255 : 0;
			int hlb = c & 4 ? 255 : 0;
			Uint32 hlc = SDL_MapRGB(dst->format, hlr, hlg, hlb);
			r.x = x;
			r.y = y;
			r.w = FONT_CW;
			r.h = FONT_CH;
			SDL_FillRect(dst, &r,
					SDL_MapRGB(dst->format, 0, 0, 0));
			gui_dirty(&r);
			r.x = x + 2;
			r.y = y + 2;
			r.w = FONT_CW - 6;
			r.h = FONT_CH - 6;
			SDL_FillRect(dst, &r, hlc);
			x += FONT_CW;
			break;
		  }
		  default:	/* printables */
		  {
			SDL_Rect dr;
			if(c < ' ' || c > 127)
				c = 127;
			c -= 32;
			sr.x = (c % (font->w / FONT_CW)) * FONT_CW;
			sr.y = (c / (font->w / FONT_CW)) * FONT_CH;
			dr.x = x;
			dr.y = y;
			SDL_BlitSurface(font, &sr, dst, &dr);
			gui_dirty(&dr);
			x += FONT_CW;
			break;
		  }
		}
	}
	if(!highlights)
		return;
	x = sx;
	y = sy;
	txt = stxt;
	while(*txt)
	{
		int c = *txt++;
		switch(c)
		{
		  case 0:	/* terminator */
			break;
		  case '\n':	/* newline */
			x = sx;
			y += FONT_CH;
			break;
		  case '\t':	/* tab */
			x -= sx;
			x += 8 * FONT_CW;
			x %= 8 * FONT_CW;
			x += sx;
			break;
		  case '\001':	/* red highlight */
		  case '\002':	/* green highlight */
		  case '\003':	/* yellow highlight */
		  case '\004':	/* blue highlight */
		  case '\005':	/* purple highlight */
		  case '\006':	/* cyan highlight */
		  case '\007':	/* white highlight */
		  {
			int hlr = c & 1 ? 255 : 0;
			int hlg = c & 2 ? 255 : 0;
			int hlb = c & 4 ? 255 : 0;
			Uint32 hlc = SDL_MapRGB(screen->format, hlr, hlg, hlb);
			int hlw = 1;
			if(*txt == '\001')
			{
				hlw = txt[1];
				txt += 2;
			}
			gui_box(x - 2, y - 2,
					FONT_CW * hlw + 2, FONT_CH + 2,
					hlc, dst);
			break;
		  }
		  default:	/* printables */
			x += FONT_CW;
			break;
		}
	}
}


void gui_tempo(int v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "  Tempo: %4.1d", v);
	gui_text(12, 52, buf, screen);
}


void gui_songpos(int v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "SongPos: %4.1d", v);
	gui_text(12, 52 + FONT_CH, buf, screen);
}


void gui_songedit(int pos, int ppos, int track, int editing)
{
	int t, n;
	char buf[128];
	SDL_Rect r;
	const int y0 = 146;

	/* Clear */
	r.x = 12 - 2;
	r.y = y0 - 2;
	r.w = FONT_CW * 38 + 4;
	r.h = FONT_CH * (SSEQ_TRACKS + 2) + 4 + 5;
	SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0, 0, 0));
	gui_dirty(&r);

	/* Upper time bar */
	snprintf(buf, sizeof(buf), "\027...\022...\022...\022..."
			"\027...\022...\022...\022...");
	gui_text(12 + 6 * FONT_CW, y0, buf, screen);

	/* Track names + cursor */
	gui_text(12, y0 + FONT_CH + 3, "Kick\nClap\nBell\nHiHat\n"
			"Trk04\nTrk05\nTrk06\nTrk07\n"
			"Trk08\nTrk09\nTrk10\nTrk11\n"
			"Trk12\nTrk13\nTrk14\nTrk15",
			screen);
	gui_text(12, y0 + FONT_CH * (1 + track) + 3,
			"\003\001\005", screen);

	/* Lower time bar */
	snprintf(buf, sizeof(buf), "\007%.4d\022...\007%.4d\022..."
			"\007%.4d\022...\007%.4d\022...",
			pos, pos + 8, pos + 16, pos + 24);
	gui_text(12 + 6 * FONT_CW,
			y0 + FONT_CH * (SSEQ_TRACKS + 1) + 6, buf, screen);

	/* Notes */
	buf[1] = 0;
	for(t = 0; t < SSEQ_TRACKS; ++t)
		for(n = 0; n < 32; ++n)
		{
			int note = sseq_get_note(pos + n, t);
			if(note < 0)
				continue;
			else
				buf[0] = note;
			gui_text(12 + FONT_CW * (6 + n),
					y0 + FONT_CH * (1 + t) + 3,
					buf, screen);
		}

	/* Cursors */
	gui_text(12 + FONT_CW * (6 + (ppos & 0x1f)), y0, "\003", screen);
	gui_text(12 + FONT_CW * (6 + (ppos & 0x1f)),
			y0 + FONT_CH * (SSEQ_TRACKS + 1) + 6,
			"\003", screen);
	if(editing)
		gui_text(12 + FONT_CW * (6 + (ppos & 0x1f)),
				y0 + FONT_CH * (1 + track) + 3,
				"\007", screen);
}


void gui_draw_activity(int dt)
{
	int t;
	const int x0 = 12 + FONT_CW * 5;
	const int y0 = 146 + FONT_CH + 3;
	SDL_Rect r;
	r.x = x0 + 1;
	r.y = y0 + 1;
	r.w = FONT_CW - 3;
	r.h = FONT_CH * SSEQ_TRACKS - 3;
	gui_dirty(&r);

	for(t = 0; t < SSEQ_TRACKS; ++t)
	{
		Uint32 c;
		r.x = x0 + 1;
		r.y = y0 + t * FONT_CH + 1;
		r.w = FONT_CW - 3;
		r.h = FONT_CH - 3;
		activity[t] -= dt;
		if(activity[t] < 0)
			activity[t] = 0;
		c = activity[t] * 255 / MAXACTIVITY;
		c = c * c * c / (255 * 255);
		if(sseq_muted(t))
			c = SDL_MapRGB(screen->format, c, c, 255);
		else
			c = SDL_MapRGB(screen->format, c, c, c);
		SDL_FillRect(screen, &r, c);
	}
}


void gui_activity(int trk)
{
	activity[trk] = MAXACTIVITY;
}


void gui_status(int playing, int editing, int looping)
{
	char buf[64];
	const char *pe, *l;
	SDL_Rect r;
	r.x = 12 - 2;
	r.y = 100 - 2;
	r.w = FONT_CW * 13 + 2;
	r.h = FONT_CH + 2;
	SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0, 0, 0));
	gui_dirty(&r);
	if(playing)
	{
		pe = editing ? "\021\001\001\003REC" : "PLAY";
		l = looping ? " LOOP " : " SONG ";
	}
	else
	{
		pe = editing ? "\001\001\004EDIT" : "STOP";
		l = looping ? "(loop)" : "      ";
	}
	snprintf(buf, sizeof(buf), "%s %s", pe, l);
	gui_text(12, 100, buf, screen);
}


static void gui_select_range(int from, int to)
{
	char buf[32];
	if(from != to)
		snprintf(buf, sizeof(buf), "Sel:%4.1d-%4.1d", from, to);
	else
		snprintf(buf, sizeof(buf), "No Selection.");
	gui_text(12, 116, buf, screen);
}


void gui_songselect(int x1, int y1, int x2, int y2)
{
	int i;
	const int x0 = 12 + FONT_CW * 6;
	const int y0 = 146 + FONT_CH + 3;
	SDL_Rect r;
	Uint32 c = SDL_MapRGB(screen->format, 255, 128, 255);

	/* Sort coordinates */
	if(x1 > x2)
	{
		i = x1;
		x1 = x2;
		x2 = i;
	}
	if(y1 > y2)
	{
		i = y1;
		y1 = y2;
		y2 = i;
	}
	++y2;

	gui_select_range(x1, x2);

	if(x1 == x2)
		return;		/* No selection! */

	/* Draw selection box */
	r.x = x0 - 2;
	r.y = y0 - 2;
	r.w = FONT_CW * 32 + 4;
	r.h = FONT_CH * SSEQ_TRACKS + 4;
	SDL_SetClipRect(screen, &r);

	r.x = x0 + x1 * FONT_CW - 2;
	r.y = y0 + y1 * FONT_CH - 2;
	r.w = (x2 - x1) * FONT_CW + 4;
	r.h = 2;
	SDL_FillRect(screen, &r, c);

	r.x = x0 + x1 * FONT_CW - 2;
	r.y = y0 + y2 * FONT_CH;
	r.w = (x2 - x1) * FONT_CW + 4;
	r.h = 2;
	SDL_FillRect(screen, &r, c);

	r.x = x0 + x1 * FONT_CW - 2;
	r.y = y0 + y1 * FONT_CH;
	r.w = 2;
	r.h = (y2 - y1) * FONT_CW;
	SDL_FillRect(screen, &r, c);

	r.x = x0 + x2 * FONT_CW;
	r.y = y0 + y1 * FONT_CH;
	r.w = 2;
	r.h = (y2 - y1) * FONT_CW;
	SDL_FillRect(screen, &r, c);

	SDL_SetClipRect(screen, NULL);
}


void gui_message(const char *message, int curspos)
{
	int y0 = screen->h - FONT_CH - 12;
	SDL_Rect r;
	r.x = 10;
	r.y = y0 - 2;
	r.w = screen->w - 20;
	r.h = FONT_CH + 4;
	SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0, 0, 0));
	gui_dirty(&r);
	if(message)
	{
		free(message_text);
		message_text = strdup(message);
	}
	if(message_text)
		gui_text(12, y0, message_text, screen);
	if(curspos >= 0)
		gui_text(12 + FONT_CW * curspos, y0, "\007", screen);
}


static void logo(Uint32 fwc)
{
	gui_bar(6, 6, 228, 36, fwc, screen);
	gui_text(18, 17, "DT-42 DrumToy", screen);
	gui_box(6 + 3, 6 + 3, 228 - 6, 36 - 6, fwc, screen);
}


static void draw_help(GUI_pages page)
{
	Uint32 fwc = SDL_MapRGB(screen->format, 128, 64, 128);

	/* Clear */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 48, 24, 48));
	gui_dirty(NULL);

	logo(fwc);

	gui_bar(232 + 6, 6, screen->w - 238 - 6, 36, fwc, screen);
	gui_bar(6, 46, screen->w - 12, screen->h - 46 - 6, fwc, screen);

	switch(page)
	{
	  case GUI_PAGE_HELP1:
		gui_text(232 + 18, 17, "1/4:Keyboard Controls 1",
				screen);
		gui_text(12, 52,
				"\027Escape or Ctrl+Q\n"
				"    \005Quit DT-42.\n\n"
				"\027Ctrl+O\n"
				"    \005Open song file.\n\n"
				"\027Ctrl+S\n"
				"    \005Save current song to file.\n\n"
				"\027Ctrl+N\n"
				"    Clear and create \005New song.\n\n"
				"\027Tab\n"
				"    Cycle application pages;\n"
				"    Main->Messages->",
				screen);
		break;
	  case GUI_PAGE_HELP2:
		gui_text(232 + 18, 17, "2/4:Keyboard Controls 2",
				screen);
		gui_text(12, 52,
				"\027Space Bar\n"
				"    Start/stop.\n\n"
				"\027Left/Right Arrows\n"
				"    Prev/next step/note.\n\n"
				"\027PgUp/PgDn\n"
				"    Prev/next bar.\n\n"
				"\027Up/Down Arrows\n"
				"    Prev/next track.\n\n"
				"\027+/-\n"
				"    Tempo up/down 1 BPM.\n\n"
				"\027L\n"
				"    Toggle \005Looping.\n\n"
				"\027R\n"
				"    Toggle \005Recording/editing.\n\n"
				"\027M\n"
				"    \005Mute/Un\005Mute current track.",
				screen);
		break;
	  case GUI_PAGE_HELP3:
		gui_text(232 + 18, 17, "3/4:Keyboard Controls 3",
				screen);
		gui_text(12, 52,
				"\027F1-F12\n"
				"    Select track & play note.\n\n"
				"\0270-9 (Main or NumPad)\n"
				"    Play note on current track.\n\n"
				"\027Ctrl+C or Ctrl+Insert\n"
				"    \005Copy current selection.\n\n"
				"\027Ctrl+X\n"
				"    Copy and delete current selection.\n\n"
				"\027Ctrl+V or Shift+Insert\n"
				"    Paste last copied selection.\n\n"
				"\027 When Editing/Recording, F1-F12 and\n"
				"  0-9 will also insert/record notes.\n\n"
				"\027 Moving the cursor while holding the\n"
				"  Shift key selects notes.",
				screen);
		break;
	  case GUI_PAGE_HELP4:
		gui_text(232 + 18, 17, "4/4:Song Commands", screen);
		gui_text(12, 52,
				"\027C    \005Cut note. Switches to a volume\n"
				"      decay rate corresponding to D9,\n"
				"      until the next note is played.\n\n"
				"\027Vlr  Set \005Volumes to l/r.\n\n"
				"\027Dn   Set volume \005Decay rate to n.\n"
				"      Applies a volume envelope to\n"
				"      subsequent notes. Higher values\n"
				"      mean faster decay.\n\n"
				"\027Jnnn \005Jump to song position nnn.\n\n"
				"\027Tnnn Set \005Tempo to nnn BPM\n\n"
				"\027Z    \005Zero duration step. Advances\n"
				"      all tracks and plays the next\n"
				"      step instantly.\n\n",
				screen);
		break;
	  default:
		break;
	}
}


static void draw_log(void)
{
	Uint32 fwc = SDL_MapRGB(screen->format, 128, 64, 0);

	/* Clear */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 64, 32, 0));
	gui_dirty(NULL);

	logo(fwc);

	gui_bar(232 + 6, 6, screen->w - 238 - 6, 36, fwc, screen);
	gui_text(232 + 18, 17, "Message Log", screen);
	gui_bar(6, 46, screen->w - 12, screen->h - 46 - 6, fwc, screen);
	gui_text(12, 52, "(Not implemented.)", screen);
}


static void draw_main(void)
{
	Uint32 fwc = SDL_MapRGB(screen->format, 0, 128, 0);

	/* Clear */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 48, 0));
	gui_dirty(NULL);

	logo(fwc);

	/* Oscilloscope frames */
	gui_bar(240 - 2, 8 - 2, 192 + 4, 128 + 4, fwc, screen);
	gui_bar(440 - 2, 8 - 2, 192 + 4, 128 + 4, fwc, screen);

	/* Song info panel */
	gui_bar(6, 46, 228, 44, fwc, screen);
	gui_tempo(0);
	gui_songpos(0);

	/* Status box */
	gui_bar(6, 94, 228, 44, fwc, screen);
	gui_status(0, 0, 0);

	/* Song editor */
	gui_bar(6, 142, 640 - 12, FONT_CH * (SSEQ_TRACKS + 2) + 12,
			fwc, screen);
	gui_songedit(0, 0, 0, 0);

	/* Message bar */
	gui_bar(6, screen->h - FONT_CH - 12 - 6,
			640 - 12, FONT_CH + 12, fwc, screen);
	gui_message(NULL, -1);
}


void gui_draw_screen(GUI_pages page)
{
	switch(page)
	{
	  case GUI_PAGE_MAIN:
		draw_main();
		break;
	  case GUI_PAGE_LOG:
		draw_log();
		break;
	  case GUI_PAGE_HELP1:
	  case GUI_PAGE_HELP2:
	  case GUI_PAGE_HELP3:
	  case GUI_PAGE_HELP4:
		draw_help(page);
		break;
	}
}


int gui_open(SDL_Surface *scrn)
{
	screen = scrn;
	font = gui_load_image("font.bmp");
	if(!font)
	{
		fprintf(stderr, "Couldn't load font!\n");
		return -1;
	}
	SDL_EnableKeyRepeat(250, 25);
	memset(activity, 0, sizeof(activity));
	return 0;
}


void gui_close(void)
{
	SDL_FreeSurface(font);
	font = NULL;
}
