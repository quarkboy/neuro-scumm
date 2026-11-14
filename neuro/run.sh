#!/bin/sh
# Launch the Linux dev build against the Steam copy of the game.
# Override NEURO_SDK_WS_URL, MONKEY1_DIR or BUILD_DIR in the environment as needed.
here=$(cd "$(dirname "$0")" && pwd)
: "${NEURO_SDK_WS_URL:=ws://127.0.0.1:8000}"
: "${MONKEY1_DIR:=/mnt/c/Program Files (x86)/Steam/steamapps/common/The Secret of Monkey Island Special Edition}"
: "${BUILD_DIR:=$here/../../build-linux}"
export NEURO_SDK_WS_URL
exec "$BUILD_DIR/scummvm" --auto-detect -p "$MONKEY1_DIR" "$@"
