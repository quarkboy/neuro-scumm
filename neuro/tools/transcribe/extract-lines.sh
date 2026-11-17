#!/bin/bash
# Dump every line of text in the game's scripts into EXTRACT_DIR/lines.json,
# for the review page's Lines view.
#
#   extract-lines.sh EXTRACT_DIR [SAVE_SLOT]
#
# Runs the game headless just long enough to write out its raw rooms and
# scripts (nothing is played), then lines.py picks the text out of them.
# Environment: NEURO_GAME, GAME_DIR, BUILD_DIR and NEURO_SAVE_DIR, as for neuro/run.sh.
set -eu
out=${1:?usage: extract-lines.sh EXTRACT_DIR [SAVE_SLOT]}
slot=${2:-1}
here=$(cd "$(dirname "$0")/../.." && pwd)
. "$here/game-env.sh"
mkdir -p "$out/scripts"
out=$(cd "$out" && pwd)
rm -f "$out"/scripts/*.room "$out"/scripts/*.scrp "$out/scripts/index.txt"

NEURO_DUMP_SCRIPTS="$out/scripts" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	timeout 300 "$BUILD_DIR/scummvm" --savepath="$NEURO_SAVE_DIR" --auto-detect -p "$GAME_DIR" -x "$slot" > "$out/scripts/dump.log" 2>&1 || true
[ -f "$out/scripts/index.txt" ] || { echo "The script dump did not finish; see $out/scripts/dump.log" >&2; exit 1; }
python3 -I "$here/tools/transcribe/lines.py" "$out/scripts" "$out/lines.json" "$GAME_FOLDER/game.json"
