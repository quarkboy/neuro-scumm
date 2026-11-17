#!/bin/sh
# Launch the build against your copy of the game (MONKEY1_DIR: the folder with Monkey1.pak).
# NEURO_FAST_MODE=always|scenes runs the game fast (scenes: only while the player cannot act).
# Override NEURO_SDK_WS_URL, NEURO_DIALOGUE_MODE, MONKEY1_DIR or BUILD_DIR in the environment as needed.
here=$(cd "$(dirname "$0")" && pwd)
: "${NEURO_SDK_WS_URL:=ws://127.0.0.1:8000}"
: "${MONKEY1_DIR:=/mnt/c/Program Files (x86)/Steam/steamapps/common/The Secret of Monkey Island Special Edition}"
# The build: a "build" folder at the top of the repository, or "build-linux" beside it
if [ -z "${BUILD_DIR:-}" ]; then
	BUILD_DIR=$here/../build
	[ -x "$BUILD_DIR/scummvm" ] || BUILD_DIR=$here/../../build-linux
fi
# Dialogue is reported with speech/started and speech/ended, as the SDK specifies.
# For a server that does not know those commands yet, use NEURO_DIALOGUE_MODE=context.
: "${NEURO_DIALOGUE_MODE:=speech}"
: "${NEURO_DESCRIPTIONS:=$here/data/monkey1.ini}"
export NEURO_SDK_WS_URL NEURO_DIALOGUE_MODE NEURO_DESCRIPTIONS
# Savegames stay outside the repository. Start from one with: run.sh -x SLOT
: "${NEURO_SAVE_DIR:=$here/../../saves}"
mkdir -p "$NEURO_SAVE_DIR"
exec "$BUILD_DIR/scummvm" --savepath="$NEURO_SAVE_DIR" --auto-detect -p "$MONKEY1_DIR" "$@"
