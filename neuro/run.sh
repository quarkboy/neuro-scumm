#!/bin/sh
# Launch the Linux dev build against the Steam copy of the game.
# NEURO_FAST_MODE=always|scenes runs the game fast (scenes: only while the player cannot act).
# Override NEURO_SDK_WS_URL, NEURO_DIALOGUE_MODE, MONKEY1_DIR or BUILD_DIR in the environment as needed.
here=$(cd "$(dirname "$0")" && pwd)
: "${NEURO_SDK_WS_URL:=ws://127.0.0.1:8000}"
: "${MONKEY1_DIR:=/mnt/c/Program Files (x86)/Steam/steamapps/common/The Secret of Monkey Island Special Edition}"
: "${BUILD_DIR:=$here/../../build-linux}"
# Dialogue is reported with speech/started and speech/ended, as the SDK specifies.
# Stock Gary 1.0.x rejects those: use the fork in ../gary, or NEURO_DIALOGUE_MODE=context.
: "${NEURO_DIALOGUE_MODE:=speech}"
: "${NEURO_DESCRIPTIONS:=$here/data/monkey1.ini}"
export NEURO_SDK_WS_URL NEURO_DIALOGUE_MODE NEURO_DESCRIPTIONS
# Savegames stay outside the repository. Start from one with: run.sh -x SLOT
: "${NEURO_SAVE_DIR:=$here/../../saves}"
mkdir -p "$NEURO_SAVE_DIR"
exec "$BUILD_DIR/scummvm" --savepath="$NEURO_SAVE_DIR" --auto-detect -p "$MONKEY1_DIR" "$@"
