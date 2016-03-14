CC =		gcc
CLIBS =		$(shell sdl-config --libs) #-lefence
CFLAGS =	-O3 -Wall $(shell sdl-config --cflags) -g -Wall -Werror

HEADERS =	smixer.h sseq.h gui.h version.h
SOURCES =	dt42.c smixer.c sseq.c gui.c

all:		dt42

clean:
		rm -f *.o
		rm dt42

dt42:		${SOURCES} ${HEADERS}
		${CC} ${CFLAGS} -o dt42 ${SOURCES} ${CLIBS}
