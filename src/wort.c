/* wort - offline German-German dictionary for the terminal.
 * Data: German Wiktionary (via wiktextract) and OpenThesaurus, built by tools/build-db.py.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <curses.h>

#ifndef DATADIR
#define DATADIR "/usr/local/share/wort"
#endif
#define LINE 1024
#define MAXROWS 200

#ifdef _WIN32
/* declared by hand: windows.h clashes with curses.h (MOUSE_MOVED) */
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void *, char *, unsigned long);
#endif

/* Terminal text encoding. The data is UTF-8 and gets converted on output. */
enum { ENC_UTF8, ENC_LATIN1, ENC_DOS };
#if defined(_WIN32)
static int enc = ENC_DOS;        /* narrow PDCurses: console uses the OEM code page */
#elif defined(WORT_LATIN1)
static int enc = ENC_LATIN1;     /* OS X 10.3/10.4: ncurses without wide chars */
#else
static int enc = ENC_UTF8;
#endif

static const char about[] =
    "@wort\n"
    "#Offline-W\xc3\xb6rterbuch Deutsch\xe2\x80\x93" "Deutsch\n"
    " Version 0.1 \xc2\xb7 Lizenz GPL-3.0-or-later\n";
static const char about2[] =        /* split: C89 compilers need only support 509-char strings */
    ":Daten\n"
    " Deutsches Wiktionary (de.wiktionary.org), CC BY-SA 4.0, extrahiert mit wiktextract (kaikki.org)\n"
    " OpenThesaurus (www.openthesaurus.de), LGPL 2.1 oder CC BY-SA 4.0\n"
    " Englische \xc3\x9c" "bersetzungen: Deutsches Wiktionary\n";
static const char about3[] =
    ":Tasten\n"
    " Tippen: suchen   Auf/Ab: Wort w\xc3\xa4hlen   Bild auf/ab: Eintrag bl\xc3\xa4ttern\n"
    " F1: diese Seite ein/aus   F2: Suche Deutsch/Englisch   Esc: beenden\n"
    ":Kommandozeile\n"
    " wort Haus   Eintrag ausgeben\n"
    " wort -e house   englisches Wort suchen\n";

/* ---------- encoding ---------- */

/* Code points for bytes 0x80..0xAF where CP437 and CP850 agree; 0 = they differ */
static const unsigned short dos_hi[48] = {
    0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
    0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0, 0xA3, 0, 0, 0,
    0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB
};
/* code point, byte: further characters at the same place in CP437 and CP850 */
static const unsigned short dos_extra[][2] = {
    { 0xDF, 0xE1 }, { 0xB0, 0xF8 }, { 0xB1, 0xF1 }, { 0xB7, 0xFA }, { 0xB5, 0xE6 }, { 0xB2, 0xFD }
};
#define NEXTRA (sizeof dos_extra / sizeof dos_extra[0])

static int dos_byte(unsigned long cp)
{
    unsigned i;
    for (i = 0; i < 48; i++)
        if (dos_hi[i] && dos_hi[i] == cp)
            return 0x80 + i;
    for (i = 0; i < NEXTRA; i++)
        if (dos_extra[i][0] == cp)
            return dos_extra[i][1];
    return 0;
}

static unsigned long dos_cp(int b)
{
    unsigned i;
    if (b >= 0x80 && b < 0xB0)
        return dos_hi[b - 0x80];
    for (i = 0; i < NEXTRA; i++)
        if (dos_extra[i][1] == b)
            return dos_extra[i][0];
    return 0;
}

static unsigned long utf8_next(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    unsigned long c = *p++;
    int n = 0;
    if (c >= 0xF0) { c &= 7; n = 3; }
    else if (c >= 0xE0) { c &= 15; n = 2; }
    else if (c >= 0xC0) { c &= 31; n = 1; }
    while (n-- > 0 && (*p & 0xC0) == 0x80)
        c = (c << 6) | (*p++ & 0x3F);
    *pp = p;
    return c;
}

static int put_utf8(char *o, unsigned long c)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | c >> 12);
    o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    o[2] = (char)(0x80 | (c & 0x3F));
    return 3;
}

/* UTF-8 -> terminal encoding. Output never needs more bytes than the input. */
static void to_out(const char *s, char *o)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned long c;
    int b;

    if (enc == ENC_UTF8) {
        if (o != s)
            strcpy(o, s);
        return;
    }
    while (*p) {
        c = utf8_next(&p);
        if (c < 0x80) {
            *o++ = (char)c;
            continue;
        }
        if (c >= 0x300 && c < 0x370)
            continue;               /* combining marks (IPA): drop */
        b = enc == ENC_LATIN1 ? (c >= 0xA0 && c <= 0xFF ? (int)c : 0) : dos_byte(c);
        if (b) {
            *o++ = (char)b;
            continue;
        }
        switch (c) {
        case 0x201E: case 0x201C: case 0x201D: case 0xAB: case 0xBB: *o++ = '"'; break;
        case 0x201A: case 0x2018: case 0x2019: case 0x2032: *o++ = '\''; break;
        case 0x2013: case 0x2014: *o++ = '-'; break;
        case 0x2026: *o++ = '.'; *o++ = '.'; *o++ = '.'; break;
        case 0x2192: *o++ = '-'; *o++ = '>'; break;
        case 0xA0: *o++ = ' '; break;
        default: *o++ = '?';
        }
    }
    *o = '\0';
}

/* One input byte in the given encoding -> UTF-8 appended at o. Returns bytes written. */
static int in_byte(int b, int from, char *o)
{
    unsigned long c;
    if (b < 0x80 || from == ENC_UTF8) {
        *o = (char)b;
        return 1;
    }
    c = from == ENC_LATIN1 ? (unsigned long)b : dos_cp(b);
    return c ? put_utf8(o, c) : 0;
}

/* Search key: lowercase, ä/ö/ü -> ae/oe/ue, ß -> ss, other Latin-1 accents dropped,
 * so "haeuser" and "Häuser" find the same words. Must match fold() in build-db.py.
 * Every Latin-1 letter is 2 bytes in UTF-8 and folds to at most 2, so this works in place. */
static const char *const fold_lat[32] = {   /* U+00E0..U+00FF */
    "a", "a", "a", "a", "ae", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "oe", NULL, "o", "u", "u", "u", "ue", "y", "th", "y"
};

static void fold(char *s)
{
    unsigned char *p = (unsigned char *)s, *o = p;
    const char *m;
    unsigned c;

    while (*p) {
        if (*p >= 'A' && *p <= 'Z') {
            *o++ = *p++ + 32;
            continue;
        }
        if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            c = p[1];
            if (c <= 0x9E && c != 0x97)
                c += 0x20;          /* capital -> small */
            m = c == 0x9F ? "ss" : c >= 0xA0 ? fold_lat[c - 0xA0] : NULL;
            if (m) {
                while (*m)
                    *o++ = (unsigned char)*m++;
                p += 2;
                continue;
            }
            *o++ = *p++;            /* only × and ÷ get here: keep as is */
            *o++ = *p++;
            continue;
        }
        *o++ = *p++;
    }
    *o = '\0';
}

/* Screen width of the character at p (in the terminal encoding) and its byte length */
static int char_len(const unsigned char *p)
{
    if (enc != ENC_UTF8 || *p < 0xC0) return 1;
    if (*p < 0xE0) return 2;
    if (*p < 0xF0) return 3;
    return 4;
}

static int char_w(const unsigned char *p)
{
    return enc == ENC_UTF8 && (*p == 0xCC || *p == 0xCD) ? 0 : 1;  /* U+0300..U+036F */
}

/* UTF-8 literal -> terminal encoding, in a static buffer (for short UI strings) */
static const char *O(const char *s)
{
    static char buf[LINE];
    to_out(s, buf);
    return buf;
}

static int width(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    int w = 0;
    for (; *p; p += char_len(p))
        w += char_w(p);
    return w;
}

/* Bytes of s that fit into cols columns */
static int fit(const char *s, int cols)
{
    const unsigned char *p = (const unsigned char *)s;
    int w = 0;
    while (*p && w + char_w(p) <= cols) {
        w += char_w(p);
        p += char_len(p);
    }
    return (int)(p - (const unsigned char *)s);
}

/* Length of the next wrapped row of s; *next = where the row after it starts */
static int wrap_piece(const char *s, int cols, int *next)
{
    int len = fit(s, cols), brk = -1, i;
    if (!s[len]) {
        *next = len;
        return len;
    }
    for (i = 0; i <= len; i++)
        if (s[i] == ' ')
            brk = i;
    if (brk > 0)
        len = brk;
    if (len == 0)
        len = char_len((const unsigned char *)s);
    *next = len;
    while (s[*next] == ' ')
        (*next)++;
    return len;
}

/* ---------- data files ---------- */

/* idx/idx_size point at the German or the English index, see use_index() */
static FILE *idx, *dat, *idx_de, *idx_en;
static long idx_size, size_de, size_en;
static int english;                 /* searching the English index */

static long file_size(FILE *f)
{
    fseek(f, 0, SEEK_END);
    return ftell(f);
}

/* Switches lookups to the English index (1) or the German one (0). Returns 0 if not there. */
static int use_index(int en)
{
    if (en && !idx_en)
        return 0;
    english = en;
    idx = en ? idx_en : idx_de;
    idx_size = en ? size_en : size_de;
    return 1;
}

static FILE *open_in(const char *dir, const char *name)
{
    char path[LINE];
    if (strlen(dir) + strlen(name) + 2 > sizeof path)
        return NULL;
    strcpy(path, dir);
    strcat(path, "/");
    strcat(path, name);
    return fopen(path, "rb");
}

static int open_dir(const char *dir)
{
    if (!dir || !*dir)
        return 0;
    idx_de = open_in(dir, "wort.idx");
    dat = open_in(dir, "wort.dat");
    if (idx_de && dat) {
        idx_en = open_in(dir, "wort-en.idx");   /* optional: English reverse search */
        return 1;
    }
    if (idx_de) fclose(idx_de);
    if (dat) fclose(dat);
    return 0;
}

/* Data lookup order: $WORT_DATA, the program's own directory, DATADIR */
static int open_data(const char *argv0)
{
    char dir[LINE];
    char *slash;

    if (open_dir(getenv("WORT_DATA")))
        goto ok;
#ifdef _WIN32
    (void)argv0;
    if (!GetModuleFileNameA(NULL, dir, sizeof dir))
        dir[0] = '\0';
#else
    strncpy(dir, argv0, sizeof dir - 1);
    dir[sizeof dir - 1] = '\0';
#endif
    slash = strrchr(dir, '/');
    if (strrchr(dir, '\\') > slash)
        slash = strrchr(dir, '\\');
    if (slash) {
        *slash = '\0';
        if (open_dir(dir))
            goto ok;
    }
    if (open_dir(DATADIR))
        goto ok;
    return 0;
ok:
    size_de = file_size(idx_de);
    if (idx_en)
        size_en = file_size(idx_en);
    use_index(0);
    return 1;
}

/* Start of the first index line at or after pos */
static long line_start(long pos)
{
    int c;
    if (pos <= 0)
        return 0;
    fseek(idx, pos - 1, SEEK_SET);
    while ((c = getc(idx)) != EOF && c != '\n')
        ;
    return ftell(idx);
}

/* Start of the index line before the one at pos, or -1 */
static long prev_line(long pos)
{
    if (pos <= 0)
        return -1;
    pos--;                          /* the '\n' ending the previous line */
    while (pos > 0) {
        fseek(idx, pos - 1, SEEK_SET);
        if (getc(idx) == '\n')
            break;
        pos--;
    }
    return pos;
}

/* Reads the index line at pos into buf. Returns the next line's position, or -1 at the end. */
static long read_line(long pos, char *buf)
{
    size_t n;
    if (pos < 0 || pos >= idx_size)
        return -1;
    fseek(idx, pos, SEEK_SET);
    if (!fgets(buf, LINE, idx))
        return -1;
    n = strlen(buf);
    if (n && buf[n - 1] == '\n')
        buf[--n] = '\0';
    return ftell(idx);
}

/* Compares an index line's key (up to the tab) with t, bytewise */
static int keycmp(const char *line, const char *t)
{
    const unsigned char *a = (const unsigned char *)line, *b = (const unsigned char *)t;
    while (*a && *a != '\t' && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\t' || !*a ? 0 : *a) - *b;
}

/* Position of the first index line whose key is >= t (binary search on the sorted file) */
static long lookup(const char *t)
{
    long lo = 0, hi = idx_size, mid;
    char buf[LINE];
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        if (read_line(line_start(mid), buf) < 0 || keycmp(buf, t) >= 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return line_start(lo);
}

/* Splits "key\tshown\toffset" -> shown (in place), returns offset or -1 */
static long parse_line(char *line, char **shown)
{
    char *t1 = strchr(line, '\t'), *t2;
    if (!t1 || !(t2 = strchr(t1 + 1, '\t')))
        return -1;
    *t2 = '\0';
    *shown = t1 + 1;
    return atol(t2 + 1);
}

/* Reads the record at off, converted to the terminal encoding. Caller frees. */
static char *load_record(long off)
{
    char line[LINE], conv[LINE];
    char *rec = NULL, *grown;
    size_t len = 0, cap = 0, n;

    fseek(dat, off, SEEK_SET);
    while (fgets(line, sizeof line, dat) && strcmp(line, "%\n") != 0) {
        to_out(line, conv);
        n = strlen(conv);
        if (len + n + 1 > cap) {
            cap = (len + n + 1) * 2;
            if (!(grown = realloc(rec, cap)))
                break;
            rec = grown;
        }
        memcpy(rec + len, conv, n + 1);
        len += n;
    }
    return rec;
}

/* ---------- rendering ---------- */

static int indent_of(int type)
{
    switch (type) {
    case ':': return 2;
    case ' ': return 4;
    case '>': return 8;
    default: return 0;
    }
}

/* Lays out a record (lines with a type char) into rows of width cols.
 * Rows from skip to skip+maxrows-1 are drawn at y0,x0 (tui) or printed (!tui).
 * Returns the total number of rows. */
static int render(const char *rec, int cols, int skip, int maxrows, int y0, int x0, int tui)
{
    const char *l = rec, *end, *s;
    char row[LINE];
    int rows = 0, type, ind, len, next, first = 1;

    while (l && *l) {
        end = strchr(l, '\n');
        if (!end)
            end = l + strlen(l);
        type = *l;
        ind = indent_of(type);
        if (type == '#' && !first) {
            rows++;                 /* blank row before each part of speech */
            if (!tui)
                printf("\n");
        }
        first = 0;
        if (end - l - 1 >= LINE)
            end = l + LINE;
        memcpy(row, l + 1, end - l - 1);
        row[end - l - 1] = '\0';
        s = row;
        do {
            len = wrap_piece(s, cols - ind > 8 ? cols - ind : 8, &next);
            if (!tui) {
                printf("%*s%.*s\n", ind, "", len, s);
            } else if (rows >= skip && rows < skip + maxrows) {
                if (type == '@' || type == '#' || type == ':')
                    attron(A_BOLD);
                mvaddnstr(y0 + rows - skip, x0 + ind, s, len);
                attroff(A_BOLD);
            }
            rows++;
            s += next;
        } while (*s);
        l = *end ? end + 1 : end;
    }
    return rows;
}

/* ---------- plain mode: wort <word> ---------- */

static int plain(int argc, char **argv)
{
    /* static: big stack frames skip Windows' stack guard page and crash */
    static char word[LINE], orig[LINE], typed[LINE], conv[LINE * 2], line[LINE];
    char *shown, *rec;
    long pos, off, done[32];
    int i, j, n = 0, from, found = 0, pass, exact;
    size_t len = 0;
    const unsigned char *p;

#ifdef _WIN32
    from = ENC_LATIN1;              /* argv is in the ANSI code page; same as Latin-1 for German */
#else
    from = enc;
#endif
    for (i = 1; i < argc; i++) {
        for (p = (const unsigned char *)argv[i]; *p && len + 4 < sizeof word; p++)
            len += in_byte(*p, from, word + len);
        if (i + 1 < argc && len + 1 < sizeof word)
            word[len++] = ' ';
    }
    word[len] = '\0';
    to_out(word, typed);
    strcpy(orig, word);
    fold(word);

    /* pass 0: entries spelled exactly as typed ("Straße"); pass 1: the rest ("Strass") */
    for (pass = 0; pass < 2; pass++) {
        for (pos = lookup(word); (pos = read_line(pos, line)) >= 0 && keycmp(line, word) == 0; ) {
            if ((off = parse_line(line, &shown)) < 0)
                continue;
            exact = strncmp(shown, orig, len) == 0 && (shown[len] == '\0' || shown[len] == ' ');
            if (exact != (pass == 0))
                continue;
            for (j = 0; j < n && done[j] != off; j++)
                ;
            if (j < n || n == 32)
                continue;
            done[n++] = off;
            if (strstr(shown, "\xe2\x86\x92")) {    /* a form: say which word it belongs to */
                to_out(shown, conv);
                printf("%s\n\n", conv);
            }
            if ((rec = load_record(off)) != NULL) {
                if (found)
                    printf("\n");
                /* shortcut: fixed 79 columns, upgrade to the real terminal width if people ask */
                render(rec, 79, 0, 0, 0, 0, 0);
                free(rec);
            }
            found = 1;
        }
    }
    if (found)
        return 0;

    printf("%s \"%s\".\n", O("Kein Eintrag f\xc3\xbcr"), typed);
    printf("%s\n", O("\xc3\x84hnliche W\xc3\xb6rter:"));
    pos = lookup(word);
    for (i = 0; i < 5 && (pos = read_line(pos, line)) >= 0; i++)
        if (parse_line(line, &shown) >= 0) {
            to_out(shown, conv);
            printf("  %s\n", conv);
        }
    return 1;
}

/* ---------- TUI ---------- */

static int tui(void)
{
    /* static: big stack frames skip Windows' stack guard page and crash */
    static char query[256], key[256], lines[MAXROWS][LINE], conv[LINE];
    static long offs[MAXROWS];
    char *shown, *rec = NULL;
    long top = 0, pos, cur = -1;
    int ch, sel = 0, count, scroll = 0, total = 0, show_about = 0;
    int lw, h, i, n, qlen = 0, from = enc;

    query[0] = '\0';
#ifdef NCURSES_VERSION
    ESCDELAY = 50;                  /* Esc quits; don't wait a second for an escape sequence */
#endif
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    for (;;) {
        h = LINES - 3;
        if (h > MAXROWS)
            h = MAXROWS;
        lw = COLS / 3 > 30 ? 30 : COLS / 3;

        /* list rows */
        count = 0;
        for (pos = top; count < h && (pos = read_line(pos, lines[count])) >= 0; count++)
            offs[count] = parse_line(lines[count], &shown);   /* line is now "key\tshown" */
        if (sel >= count)
            sel = count ? count - 1 : 0;
        if (count && offs[sel] != cur) {
            free(rec);
            cur = offs[sel];
            rec = cur >= 0 ? load_record(cur) : NULL;
            scroll = 0;
        }

        erase();
        to_out(query, conv);
        mvaddstr(0, 0, english ? " wort   Englisch: " : " wort   Deutsch: ");
        addstr(conv);
        mvhline(1, 0, ACS_HLINE, COLS);
        mvvline(2, lw, ACS_VLINE, h);
        for (i = 0; i < count; i++) {
            char *t = strchr(lines[i], '\t');
            to_out(t ? t + 1 : lines[i], conv);
            if (i == sel)
                attron(A_REVERSE);
            mvaddnstr(2 + i, 0, conv, fit(conv, lw - 1));
            if (i == sel)
                attroff(A_REVERSE);
        }
        if (show_about) {
            char *a = malloc(sizeof about + sizeof about2 + sizeof about3 + LINE + 8);
            if (a) {
                strcpy(a, about);
                strcat(a, about2);
                fseek(dat, 0, SEEK_SET);
                if (fgets(conv, LINE, dat) && conv[0] == '!') { /* snapshot dates, ASCII */
                    strcat(a, " Stand: ");
                    strcat(a, conv + 1);
                }
                strcat(a, about3);
                to_out(a, a);       /* in place is fine: output is never longer */
                total = render(a, COLS - lw - 2, 0, h, 2, lw + 2, 1);
                free(a);
            }
        } else if (rec) {
            total = render(rec, COLS - lw - 2, scroll, h, 2, lw + 2, 1);
        }
        mvaddstr(LINES - 1, 0, O(" Auf/Ab: Wort   Bild auf/ab: bl\xc3\xa4ttern   F1: Info   F2: DE/EN   Esc: Ende"));
        to_out(query, conv);
        move(0, (english ? 18 : 17) + width(conv));
        refresh();

        ch = getch();
        if (ch == 27)
            break;
        switch (ch) {
        case KEY_F(1):
            show_about = !show_about;
            break;
        case KEY_F(2):
            if (!use_index(!english)) {
                beep();             /* no wort-en.idx */
                break;
            }
            goto search;
        case KEY_DOWN:
            if (sel < count - 1)
                sel++;
            else if (count == h)    /* window full: there may be more below */
                top = read_line(top, conv);
            break;
        case KEY_UP:
            if (sel > 0)
                sel--;
            else if (prev_line(top) >= 0)
                top = prev_line(top);
            break;
        case KEY_NPAGE:
            if (scroll + h < total)
                scroll += h - 1;
            break;
        case KEY_PPAGE:
            scroll = scroll > h - 1 ? scroll - (h - 1) : 0;
            break;
        case KEY_BACKSPACE: case 8: case 127:
            while (qlen > 0 && (query[--qlen] & 0xC0) == 0x80)
                ;
            query[qlen] = '\0';
            goto search;
        default:
            if (ch < 32 || ch > 255 || qlen + 4 >= (int)sizeof query)
                break;
            n = in_byte(ch, from, query + qlen);
            qlen += n;
            query[qlen] = '\0';
        search:
            strcpy(key, query);
            fold(key);
            top = qlen ? lookup(key) : 0;
            sel = 0;
            show_about = 0;
        }
    }
    endwin();
    free(rec);
    return 0;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    if (!open_data(argv[0])) {
        fprintf(stderr, "wort: wort.idx/wort.dat not found (set WORT_DATA, or put them next to the program or in %s)\n", DATADIR);
        return 2;
    }
    if (argc > 1 && strcmp(argv[1], "-e") == 0) {
        if (!use_index(1)) {
            fprintf(stderr, "wort: wort-en.idx not found\n");
            return 2;
        }
        argv[1] = argv[0];
        argc--;
        argv++;
    }
    return argc > 1 ? plain(argc, argv) : tui();
}
