# Linux/FreeBSD: make            OpenBSD: make LIBS=-lcurses
# macOS (Tiger, Catalina): make LIBS=-lncurses
CC = cc
LIBS = -lncursesw

cursetest: cursetest.c
	$(CC) $(CFLAGS) -o cursetest cursetest.c $(LIBS)

clean:
	rm -f cursetest
