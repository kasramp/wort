#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate docs/screenshot.png from the repo root (needs tmux, chromium, built data):

    make && python3 docs/make_screenshot.py

Runs the real wort in tmux, types a search, and draws the screen as an SVG in an
XFCE-style window, then lets headless chromium turn it into a PNG.
"""
import html
import os
import re
import subprocess
import tempfile
import time
import unicodedata

COLS, ROWS, SEARCH = 100, 26, "haus"
CW, LH, PAD, TITLE = 9.6, 20, 14, 34          # cell width, line height, padding, title bar
FG, BG, BAR, BOLD = "#c5c8c6", "#1d1f21", "#2b2d30", "#ffffff"


def capture():
    s = "wort-shot"
    env = "WORT_DATA=%s/build/data" % os.getcwd()
    subprocess.run(["tmux", "new-session", "-d", "-s", s, "-x", str(COLS), "-y", str(ROWS),
                    "env %s ./wort" % env], check=True)
    try:
        time.sleep(1)
        subprocess.run(["tmux", "send-keys", "-t", s, "-l", SEARCH], check=True)
        time.sleep(0.5)
        return subprocess.run(["tmux", "capture-pane", "-e", "-p", "-t", s],
                              check=True, capture_output=True, text=True).stdout
    finally:
        subprocess.run(["tmux", "kill-session", "-t", s])


def runs(line):
    """(column, text, bold, reverse) pieces of one captured line."""
    col, bold, rev, out = 0, False, False, []
    for part in re.split(r"(\x1b\[[0-9;]*m)", line):
        if part.startswith("\x1b["):
            for code in (part[2:-1] or "0").split(";"):
                if code == "0":
                    bold = rev = False
                elif code == "1":
                    bold = True
                elif code == "7":
                    rev = True
            continue
        if part:
            out.append((col, part, bold, rev))
            col += sum(0 if unicodedata.combining(c) else 1 for c in part)
    return out


def svg(screen):
    w, h = COLS * CW + 2 * PAD, TITLE + ROWS * LH + 2 * PAD
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (w, h, w, h),
         '<rect width="100%%" height="100%%" rx="3" fill="%s"/>' % BG,
         '<rect width="100%%" height="%d" rx="3" fill="%s"/>' % (TITLE, BAR),
         '<text x="%d" y="22" fill="%s" font-family="DejaVu Sans" font-size="14" text-anchor="middle">wort</text>'
         % (w / 2, FG)]
    for i, glyph in enumerate("×□–"):          # XFCE: buttons on the right, close rightmost
        o.append('<text x="%d" y="23" fill="%s" font-size="18" text-anchor="middle">%s</text>'
                 % (w - 24 - 30 * i, FG, glyph))
    o.append('<g font-family="DejaVu Sans Mono" font-size="16">')
    for row, line in enumerate(screen.rstrip("\n").split("\n")[:ROWS]):
        y = TITLE + PAD + row * LH
        for col, text, bold, rev in runs(line):
            x = PAD + col * CW
            if rev:
                o.append('<rect x="%.1f" y="%d" width="%.1f" height="%d" fill="%s"/>'
                         % (x, y + 2, len(text) * CW, LH, FG))
            o.append('<text x="%.1f" y="%d" fill="%s"%s>%s</text>'
                     % (x, y + 16, BG if rev else (BOLD if bold else FG),
                        ' font-weight="bold"' if bold else "", html.escape(text).replace(" ", "\u00a0")))  # SVG collapses spaces
    o.append("</g></svg>")
    return "\n".join(o), w, h


def main():
    doc, w, h = svg(capture())
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "s.html")
        with open(page, "w") as f:
            f.write('<html><body style="margin:0;background:transparent">%s</body></html>' % doc)
        subprocess.run(["chromium", "--headless=new", "--force-device-scale-factor=2",
                        "--window-size=%d,%d" % (w, h), "--default-background-color=00000000",
                        "--hide-scrollbars", "--screenshot=%s/docs/screenshot.png" % os.getcwd(),
                        "file://" + page], check=True, capture_output=True)


if __name__ == "__main__":
    main()
