#!/bin/sh
# Launch the build against your copy of a game, without the console.
#
#   NEURO_GAME   which game: a folder under neuro/games (default monkey1)
#   GAME_DIR     the installed game's folder (default: where Steam puts it)
#   BUILD_DIR    the folder with the scummvm binary
#
# NEURO_FAST_MODE=always|scenes runs the game fast (scenes: only while the player cannot act).
# Start from a savegame with: run.sh -x SLOT
here=$(cd "$(dirname "$0")" && pwd)
. "$here/game-env.sh"
: "${NEURO_SDK_WS_URL:=ws://127.0.0.1:8000}"
# Dialogue is reported with speech/started and speech/ended, as the SDK specifies.
# For a server that does not know those commands yet, use NEURO_DIALOGUE_MODE=context.
: "${NEURO_DIALOGUE_MODE:=speech}"
: "${NEURO_DESCRIPTIONS:=$GAME_FOLDER/descriptions.ini}"
export NEURO_SDK_WS_URL NEURO_DIALOGUE_MODE NEURO_DESCRIPTIONS
mkdir -p "$NEURO_SAVE_DIR"
exec "$BUILD_DIR/scummvm" --savepath="$NEURO_SAVE_DIR" --auto-detect -p "$GAME_DIR" "$@"
