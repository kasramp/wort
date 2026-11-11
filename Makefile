# `make clean && make` works as is on Linux, FreeBSD, OpenBSD and macOS (GNU or BSD make).
# The OS check runs in the shell because GNU make 3.x (old macOS) and BSD make share no
# syntax for it. Darwin 7/8 = OS X 10.3/10.4: their ncurses has no wide chars, use Latin-1.
CC = cc
CFLAGS = -O2
PREFIX = /usr/local
DESTDIR =

.PHONY: data clean install uninstall win32

wort: src/wort.c
	@case "`uname -s` `uname -r`" in \
	OpenBSD*) flags=""; libs="-lcurses" ;; \
	"Darwin 7."*|"Darwin 8."*) flags="-DWORT_LATIN1"; libs="-lncurses" ;; \
	Darwin*) flags=""; libs="-lncurses" ;; \
	*) flags=""; libs="-lncursesw" ;; \
	esac; \
	flags="$$flags -DDATADIR=\"$(PREFIX)/share/wort\""; \
	echo "$(CC) $(CFLAGS) $$flags -o wort src/wort.c $$libs"; \
	$(CC) $(CFLAGS) $$flags -o wort src/wort.c $$libs

# Needs the raw snapshots listed in data/SOURCES in data/raw/
data:
	python3 -I tools/build-db.py data/raw/dewiktionary-2026-11-01.jsonl.gz \
	    data/raw/openthesaurus-2026-11-09.zip build/data

# Windows .exe (Win98-11), cross-built with OpenWatcom v2; source its owsetenv.sh first
P = ../../third_party/PDCurses-3.9
win32:
	mkdir -p build/win32
	cd build/win32 && INCLUDE="$$WATCOM/h:$$WATCOM/h/nt" wcl386 -q -bt=nt -l=nt -oneatx \
	    -i=$(P) -fe=wort.exe ../../src/wort.c $(P)/pdcurses/*.c $(P)/wincon/*.c && rm -f *.o

# Program to bin/, data from build/data/ to share/wort/, man page (OpenBSD: man/, else share/man/)
install: wort
	@case "`uname -s`" in OpenBSD) man=man ;; *) man=share/man ;; esac; \
	set -x; \
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/wort $(DESTDIR)$(PREFIX)/$$man/man1 && \
	install -m 755 wort $(DESTDIR)$(PREFIX)/bin/wort && \
	install -m 644 build/data/wort.idx build/data/wort-en.idx build/data/wort.dat $(DESTDIR)$(PREFIX)/share/wort/ && \
	install -m 644 wort.1 $(DESTDIR)$(PREFIX)/$$man/man1/wort.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wort $(DESTDIR)$(PREFIX)/man/man1/wort.1 $(DESTDIR)$(PREFIX)/share/man/man1/wort.1
	rm -rf $(DESTDIR)$(PREFIX)/share/wort

clean:
	rm -f wort
