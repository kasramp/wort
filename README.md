# wort

An offline German–German dictionary for the terminal. Its data comes from the
German Wiktionary and OpenThesaurus.

Status: first draft.

## Usage

```
wort            start the TUI: type to search, Up/Down picks a word,
                PgUp/PgDn scrolls the entry, F1 shows info,
                F2 switches the search between German and English, Esc quits
wort Häuser     print the entry and exit (exit status 1 if not found)
wort -e house   search an English word, show the German entries
```

Without umlauts on the keyboard, type ae/oe/ue/ss: `haeuser` finds *Häuser*.

wort looks for `wort.idx`, `wort.dat` and the optional `wort-en.idx` in this
order: the `WORT_DATA` directory, the program's own directory, then
`PREFIX/share/wort` (default `/usr/local/share/wort`).

## Building the data

The data build step runs on a modern machine (Python 3, no extra packages). It
reads the newest raw snapshots in `data/raw/`, named `<source>-<date>.<ext>`.
All snapshots used so far are listed with their checksums in `data/SOURCES`
and kept in the `data-snapshots` release.

```sh
make data        # writes build/data/wort.idx, wort-en.idx and wort.dat (~2 min, ~130 MB)
```

## Building the program

wort is plain C89 and uses curses. On Linux, FreeBSD, OpenBSD and macOS
(including Panther and Tiger) just run:

```sh
make clean && make
sudo make install    # wort, its data and the man page under /usr/local
```

`make install` takes the data from `build/data/` (see above; the release
archives already contain it). It honours `PREFIX` and `DESTDIR`; give the
same `PREFIX` to `make` too, because the data path is compiled in.
`make uninstall` removes everything again.

The Makefile detects the OS and picks the curses library (`-lncursesw`,
`-lcurses` on OpenBSD, `-lncurses` on macOS) and, on Panther/Tiger, the
Latin-1 build (`-DWORT_LATIN1`, see below). Windows is cross-built on Linux,
see below.

`cursetest/` is a small program that checks text output and keys. Its
Makefile does not detect the OS: use `make` on Linux/FreeBSD,
`make LIBS=-lcurses` on OpenBSD, `make LIBS=-lncurses` on macOS.

### Gotcha: Panther and Tiger need Latin-1

The ncurses that ships with OS X 10.3 and 10.4 has no wide-character support.
It counts UTF-8 bytes as screen columns. An umlaut is 2 bytes but takes only
1 column, so redraws leave leftover characters on screen.

Fix:
1. Build with `-DWORT_LATIN1`.
2. In Terminal, open **Window Settings → Display → Character Set Encoding**
   and choose **Western (ISO Latin 1)**.

Umlauts and ß then display correctly. Characters that Latin-1 doesn't have
are replaced: „“ ‚‘ become `"` `'`, – and — become `-`, … becomes `...`, and
IPA symbols become `?`.

In the macOS Terminal, Page Up/Down scroll the Terminal window. To scroll the
entry in wort, use Shift+Page Up/Down (on laptops: fn+Shift+↑/↓).

Known limit in the Terminal of OS X 10.3 to 10.5: F1 and F2 don't work
(on Panther they quit wort), because Terminal sends key codes the system
doesn't know. Leopard 10.5.8 is tested and otherwise fine.

Tested on Panther 10.3.9 (PPC). Tiger is untested but ships the same kind of
ncurses.

### Windows

Windows builds are cross-compiled on Linux with OpenWatcom v2 (Arch: AUR
package `openwatcom-v2`). PDCurses 3.9 is compiled into the program, so the
`.exe` needs no extra DLLs and runs on Windows 98 through 11.

```sh
. /opt/watcom/owsetenv.sh
make win32           # writes build/win32/wort.exe
```

Copy `wort.exe` and the three data files into one folder.

Gotchas:
- PDCurses is the narrow (non-Unicode) build, because the Unicode build does
  not run on Windows 9x. The console therefore expects the DOS code page
  (CP437/850), and wort converts text to it. The German letters sit at the
  same positions in both code pages. Quotes, dashes and IPA are replaced the
  same way as on Panther.
- On Windows 9x, run wort in a Command Prompt window. It is a Win32 program,
  so it does not run in real "MS-DOS mode".
- Do not use PDCurses' `Makefile.wcc` on Linux: OpenWatcom on Linux writes
  `.o` files, not the `.obj` files that makefile expects.
- Do not define `HAVE_NO_INFOEX`. It clashes with OpenWatcom's headers and
  isn't needed: PDCurses already looks up the newer console functions at run
  time.

### Text consoles

On the plain text consoles of the BSDs and Linux (outside X), some characters
may show as boxes or not at all, depending on the console font. Desktop
terminal emulators show everything.

## CI and releases

Every pull request and push to `main` builds wort on Linux (strict C89,
warnings are errors), macOS, FreeBSD, OpenBSD and Windows
(`.github/workflows/build.yml`).

Releases are made by hand: **Actions → release → Run workflow** on `main`,
with a version such as `v0.1.0`. The workflow downloads today's data from
kaikki.org and OpenThesaurus (`tools/snapshot.sh`). If a source can't be
downloaded, it uses the newest snapshot kept in the `data-snapshots` release,
checked against `data/SOURCES`. It then builds the data and `wort.exe`, adds
new raw files to `data-snapshots`, commits their checksums to `data/SOURCES`,
and publishes a Windows zip and a Unix tarball from that commit. The snapshot
dates show on wort's F1 page.

## License

The code is GPL-3.0-or-later. The dictionary data keeps its own licences:
- German Wiktionary: CC BY-SA 4.0, extracted with
  [wiktextract](https://kaikki.org)
- [OpenThesaurus](https://www.openthesaurus.de): LGPL 2.1 or CC BY-SA 4.0
