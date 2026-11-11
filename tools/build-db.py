#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build wort.dat and wort.idx from the raw snapshots listed in data/SOURCES.

usage: build-db.py <dewiktionary.jsonl.gz> <openthesaurus.zip> <outdir>

wort.dat: a first line "!Wiktionary <date>, OpenThesaurus <date>", then
  one record per headword, UTF-8 lines, each starting with a type char:
  @ headword   # part of speech   : section heading   ' ' text   > example
  A record ends with a line "%".
wort.idx: "key\tshown\toffset\n", sorted by the key's bytes. key = fold(word);
  offset = byte offset of the record in wort.dat. The C side folds the same way.
wort-en.idx: the same for English translations ("house -> Haus"), for reverse search.
"""
import gzip
import json
import os
import re
import sys
import zipfile

GENDER = {"masculine": "Maskulinum", "feminine": "Femininum", "neuter": "Neutrum"}
TAGS = {
    "colloquial": "ugs.", "transitive": "transitiv", "intransitive": "intransitiv",
    "figurative": "übertragen", "outdated": "veraltend", "archaic": "veraltet",
    "Austrian German": "österr.", "historical": "historisch", "reflexive": "reflexiv",
    "derogatory": "abwertend", "pejorative": "abwertend", "no-plural": "ohne Plural",
    "regional": "regional", "rare": "selten", "Swiss Standard German": "schweiz.",
    "literary": "literarisch", "gehoben": "gehoben", "casual": "salopp",
    "jargon": "Jargon", "Germany": "bundesdt.", "humorous": "scherzhaft",
    "jocular": "scherzhaft", "formal": "förmlich", "North German": "norddt.",
    "South German": "süddt.", "South-German": "süddt.", "Bavarian": "bairisch",
    "Swabian": "schwäbisch", "Alemannic": "alemannisch", "impolite": "derb",
    "vulgar": "vulgär", "impersonal": "unpersönlich", "plural-only": "nur Plural",
    "poetic": "dichterisch", "slang": "Slang", "with-dative": "mit Dativ",
    "with-accusative": "mit Akkusativ", "geography": "Geografie", "physics": "Physik",
    "geometry": "Geometrie", "finance": "Finanzwesen", "law": "Recht",
}


FOLD_LAT = ["a", "a", "a", "a", "ae", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
            "d", "n", "o", "o", "o", "o", "oe", None, "o", "u", "u", "u", "ue", "y", "th", "y"]


def fold(s):
    """Search key: lowercase, ä/ö/ü -> ae/oe/ue, ß -> ss, other Latin-1 accents dropped.
    Only ASCII and Latin-1 letters change. Must match fold() in wort.c."""
    out = []
    for c in s:
        if "A" <= c <= "Z" or ("\xc0" <= c <= "\xde" and c != "\xd7"):
            c = chr(ord(c) + 32)
        if c == "\xdf":
            c = "ss"
        elif "\xe0" <= c <= "\xff" and FOLD_LAT[ord(c) - 0xE0]:
            c = FOLD_LAT[ord(c) - 0xE0]
        out.append(c)
    return "".join(out)


assert fold("Häuser") == fold("haeuser") == "haeuser" and fold("Maße") == "masse"
assert fold("Café ÄÖÜ×") == "cafe aeoeue\xd7"


def clean(s):
    return " ".join(s.split())  # no tabs/newlines inside fields


def labels(item):
    out = [TAGS[t] for t in item.get("tags", []) if t in TAGS]
    out += [r for r in item.get("raw_tags", []) if r]
    seen = []
    for x in out:
        if x not in seen:
            seen.append(x)
    return " (" + ", ".join(seen) + ")" if seen else ""


def redirects(o):
    """Lemmas this entry is an inflected form or alternative spelling of."""
    out = []
    for s in o.get("senses", []):
        for f in s.get("form_of", []) + s.get("alt_of", []):
            if f.get("word") and f["word"] not in out:
                out.append(clean(f["word"]))
    return out


def ot_norm(term):
    return clean(re.sub(r"\([^)]*\)", " ", term))


def load_thesaurus(path):
    """Returns (groups, word -> [group index])."""
    with zipfile.ZipFile(path) as z:
        text = z.read("openthesaurus.txt").decode("utf-8")
    groups, by_word = [], {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        terms = [clean(t.replace("(Hauptform)", "")) for t in line.split(";")]
        terms = [t for t in terms if t]
        for t in terms:
            by_word.setdefault(ot_norm(t), []).append(len(groups))
        groups.append(terms)
    return groups, by_word


def pos_block(o, seen_syn):
    """Lines for one part-of-speech entry. Adds shown synonym words to seen_syn."""
    head = o.get("pos_title") or o.get("pos", "")
    genders = [GENDER[t] for t in o.get("tags", []) if t in GENDER]
    lines = ["#" + clean(", ".join([head] + genders))]
    ipa = next((x["ipa"] for x in o.get("sounds", []) if x.get("ipa")), "")
    hyph = next((h["parts"] for h in o.get("hyphenations", []) if h.get("parts")), [])
    info = []
    if ipa:
        info.append("IPA: " + clean(ipa))
    if hyph:
        info.append("Silbentrennung: " + "·".join(clean(p) for p in hyph))
    if info:
        lines.append(" " + "   ".join(info))

    senses = []
    for i, s in enumerate(o.get("senses", []), 1):
        gloss = clean("; ".join(s.get("glosses", [])))
        if not gloss:
            continue
        senses.append(" [%s] %s%s" % (clean(s.get("sense_index") or str(i)), gloss, labels(s)))
        examples = [clean(e.get("text", "")) for e in s.get("examples", [])]
        examples = [e for e in examples if e]
        if examples:
            senses.append(">" + min(examples, key=len))  # shortest is usually the plain one
    if senses:
        lines.append(":Bedeutungen")
        lines += senses

    for key, title in (("synonyms", "Synonyme"), ("antonyms", "Gegenwörter"),
                       ("translations", "Englisch")):
        by_sense = {}
        for x in o.get(key, []):
            w = clean(x.get("word", ""))
            if not w or (key == "translations" and x.get("lang_code") != "en"):
                continue
            if key == "synonyms":
                seen_syn.add(w)
            idx = clean(x.get("sense_index", "")).replace(" ", "")
            by_sense.setdefault(idx, []).append(w + labels(x))
        if by_sense:
            lines.append(":" + title)
            for idx, words in by_sense.items():
                lines.append(" " + ("[%s] " % idx if idx else "· ") + ", ".join(words))
    return lines


def thesaurus_lines(word, groups, by_word, seen_syn):
    out = []
    for g in by_word.get(word, []):
        rest = [t for t in groups[g] if ot_norm(t) != word and ot_norm(t) not in seen_syn]
        for t in rest:
            seen_syn.add(ot_norm(t))
        if rest:
            out.append(" · " + ", ".join(rest))
    return out


def record(word, entries, groups, by_word):
    lines, seen_syn = ["@" + word], set()
    for o in entries:
        lines += pos_block(o, seen_syn)
    ot = thesaurus_lines(word, groups, by_word, seen_syn)
    if ot:
        # merge into the first entry's synonym section, or add one after it
        first_end = next((i for i in range(2, len(lines)) if lines[i].startswith("#")), len(lines))
        syn = next((i for i in range(first_end) if lines[i] == ":Synonyme"), None)
        if syn is None:  # goes before Gegenwörter/Englisch, which stay last
            at = next((i for i in range(first_end) if lines[i] in (":Gegenwörter", ":Englisch")),
                      first_end)
            lines[at:at] = [":Synonyme"] + ot
        else:
            end = syn + 1
            while end < first_end and not lines[end].startswith(":"):
                end += 1
            lines[end:end] = ot
    return "\n".join(lines) + "\n%\n"


def main(wikt, thes, outdir):
    # snapshot dates for wort.dat's first line, from the raw file names; checked before the long parse
    dates = [re.search(r"\d{4}-\d{2}-\d{2}", os.path.basename(p)) for p in (wikt, thes)]
    if not all(dates):
        sys.exit("raw file names must contain the snapshot date (see data/SOURCES)")
    groups, by_word = load_thesaurus(thes)
    lemmas, forms = {}, {}
    with gzip.open(wikt, "rt", encoding="utf-8") as f:
        for line in f:
            o = json.loads(line)
            if o.get("lang_code") != "de" or not o.get("word"):
                continue
            word = clean(o["word"])
            targets = redirects(o)
            if targets:
                for t in targets:
                    forms.setdefault(word, []).append(t)
            elif any(s.get("glosses") for s in o.get("senses", [])):
                lemmas.setdefault(word, []).append(o)

    os.makedirs(outdir, exist_ok=True)
    offsets, index, index_en = {}, set(), set()
    with open(os.path.join(outdir, "wort.dat"), "wb") as dat:
        # first line, shown on wort's F1 page
        dat.write(("!Wiktionary %s, OpenThesaurus %s\n" % tuple(d.group() for d in dates)).encode("ascii"))
        for word in sorted(lemmas):
            offsets[word] = dat.tell()
            dat.write(record(word, lemmas[word], groups, by_word).encode("utf-8"))
            index.add((fold(word), word, offsets[word]))
            for o in lemmas[word]:
                for x in o.get("translations", []):
                    en = clean(x.get("word", ""))
                    if en and x.get("lang_code") == "en":
                        index_en.add((fold(en), "%s → %s" % (en, word), offsets[word]))
    for form, targets in forms.items():
        for t in targets:
            if t in offsets and t != form:
                index.add((fold(form), "%s → %s" % (form, t), offsets[t]))

    for name, rows, must in (("wort.idx", index, ("Haus", "ging → gehen", "Häuser → Haus")),
                             ("wort-en.idx", index_en, ("house → Haus", "go → gehen"))):
        rows = sorted(rows, key=lambda r: (r[0].encode("utf-8"), r[1].encode("utf-8")))
        with open(os.path.join(outdir, name), "wb") as idx:
            for key, shown, off in rows:
                idx.write(("%s\t%s\t%d\n" % (key, shown, off)).encode("utf-8"))
        # self-check: sorted by bytes, and the obvious words resolve
        keys = [r[0].encode("utf-8") for r in rows]
        assert keys == sorted(keys)
        shown = {r[1] for r in rows}
        for w in must:
            assert w in shown, w
        print("%s: %d lines" % (name, len(rows)))
    print("lemmas %d" % len(offsets))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
