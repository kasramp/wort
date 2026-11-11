#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Release step: put one snapshot of each raw source in data/raw/.
# Downloads today's file from upstream and adds it to data/SOURCES; if upstream
# fails, takes the newest snapshot from the GitHub release "data-snapshots",
# checked against data/SOURCES. Needs gh with GH_TOKEN.
set -e
today=`date -u +%Y-%m-%d`
mkdir -p data/raw

snapshot() {  # name ext url
    f=$1-$today.$2
    if ! grep -q "^$f " data/SOURCES && curl -fsSL --retry 3 -o data/raw/$f "$3"; then
        echo "$f  $today  `sha256sum data/raw/$f | cut -d' ' -f1`  $3" >> data/SOURCES
        echo "$1: downloaded $f"
        return
    fi
    rm -f data/raw/$f
    f=`gh release view data-snapshots --json assets -q '.assets[].name' | grep "^$1-" | sort | tail -1`
    echo "$1: using snapshot $f"
    gh release download data-snapshots -p "$f" -D data/raw
    (cd data/raw && grep "^$f " ../SOURCES | awk '{print $3 "  " $1}' | sha256sum -c -)
}

snapshot dewiktionary jsonl.gz https://kaikki.org/dewiktionary/raw-wiktextract-data.jsonl.gz
snapshot openthesaurus zip https://www.openthesaurus.de/export/OpenThesaurus-Textversion.zip
