/* cursetest - checks umlauts and keys on each target before wort is written.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <locale.h>
#include <stdio.h>
#include <curses.h>

static const char *lines[] = {
    "Umlaute: \xc3\x84 \xc3\x96 \xc3\x9c  \xc3\xa4 \xc3\xb6 \xc3\xbc  \xc3\x9f",
    "Zitat:   \xe2\x80\x9eHaus\xe2\x80\x9c",
    "IPA:     [ha\xca\x8a\xcc\xafs]"
};
#define NLINES 3

static const char *modes[] = { "UTF-8", "Latin-1", "DOS CP437/850" };

/* Latin-1 -> DOS code page; German letters share positions in CP437 and CP850 */
static int to_dos(int c)
{
    switch (c) {
    case 0xC4: return 0x8E; case 0xD6: return 0x99; case 0xDC: return 0x9A;
    case 0xE4: return 0x84; case 0xF6: return 0x94; case 0xFC: return 0x81;
    case 0xDF: return 0xE1;
    }
    return '?';
}

/* UTF-8 -> Latin-1 (mode 1) or DOS (mode 2); unknown chars become '?' */
static void convert(const char *s, char *out, int mode)
{
    const unsigned char *p = (const unsigned char *)s;
    int c;
    while (*p) {
        if (*p < 0x80) {
            *out++ = (char)*p++;
        } else if ((*p == 0xC2 || *p == 0xC3) && (p[1] & 0xC0) == 0x80) {
            c = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            *out++ = (char)(mode == 2 ? to_dos(c) : c);
            p += 2;
        } else {
            *out++ = '?';
            p++;
            while ((*p & 0xC0) == 0x80)
                p++;
        }
    }
    *out = '\0';
}

int main(void)
{
    int sel = 0, mode = 0, ch = 0, i;
    char buf[128];

    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    for (;;) {
        erase();
#if defined(NCURSES_VERSION)
        mvprintw(0, 0, "wort cursetest - ncurses %s", NCURSES_VERSION);
#elif defined(PDCURSES)
        mvprintw(0, 0, "wort cursetest - PDCurses %d", PDC_BUILD);
#else
        mvprintw(0, 0, "wort cursetest - unknown curses");
#endif
        mvprintw(1, 0, "Terminal %dx%d   Mode: %s", COLS, LINES, modes[mode]);
        for (i = 0; i < NLINES; i++) {
            if (mode)
                convert(lines[i], buf, mode);
            if (i == sel)
                attron(A_REVERSE);
            mvaddstr(3 + i, 2, mode ? buf : lines[i]);
            if (i == sel)
                attroff(A_REVERSE);
        }
        mvprintw(7, 0, "Last key: %d %s", ch, ch ? keyname(ch) : "");
        mvaddstr(9, 0, "Up/Down: move   m: next mode   q: quit");
        refresh();

        ch = getch();
        if (ch == 'q')
            break;
        if (ch == 'm')
            mode = (mode + 1) % 3;
        if (ch == KEY_UP && sel > 0)
            sel--;
        if (ch == KEY_DOWN && sel < NLINES - 1)
            sel++;
    }
    endwin();
    return 0;
}
