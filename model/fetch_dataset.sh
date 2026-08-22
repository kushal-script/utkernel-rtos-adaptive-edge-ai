#!/usr/bin/env bash
# Fetch the Google Speech Commands v0.02 corpus used to train the keyword model.
# The corpus is large and is never committed, see .gitignore. Run from anywhere:
#     bash model/fetch_dataset.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$HERE/datasets"
URL="http://download.tensorflow.org/data/speech_commands_v0.02.tar.gz"
TARBALL="$DEST/speech_commands_v0.02.tar.gz"
CORPUS="$DEST/speech_commands_v0.02"

mkdir -p "$DEST"

if [ -d "$CORPUS" ] && [ -n "$(ls -A "$CORPUS" 2>/dev/null)" ]; then
    echo "corpus already present at $CORPUS"
    exit 0
fi

if [ ! -f "$TARBALL" ]; then
    echo "downloading speech commands v0.02, about 2.4 GB"
    curl -SL --progress-bar -o "$TARBALL" "$URL"
fi

echo "extracting into $CORPUS"
mkdir -p "$CORPUS"
tar xzf "$TARBALL" -C "$CORPUS"

echo "done, $(ls -d "$CORPUS"/*/ | wc -l | tr -d ' ') label directories"
