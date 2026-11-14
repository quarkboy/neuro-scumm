#!/bin/bash
# Dump every room of a SCUMM game: a picture of the whole room and a JSON
# list of its objects and actors. Uses the extraction tool built into the
# Neuro module (engines/scumm/neuro/extract.cpp).
#
#   extract-rooms.sh OUTPUT_DIR [SAVE_SLOT]
#
# The game is started from a savegame (default slot 1) so that it is in a
# normal state. A room that crashes or hangs the game is skipped and listed
# in OUTPUT_DIR/skipped. Environment: GAME_DIR, BUILD_DIR, NEURO_SAVE_DIR as
# for neuro/run.sh.
set -u
out=${1:?usage: extract-rooms.sh OUTPUT_DIR [SAVE_SLOT]}
slot=${2:-1}
here=$(cd "$(dirname "$0")" && pwd)
: "${GAME_DIR:=${MONKEY1_DIR:-/mnt/c/Program Files (x86)/Steam/steamapps/common/The Secret of Monkey Island Special Edition}}"
: "${BUILD_DIR:=$here/../../../../build-linux}"
: "${NEURO_SAVE_DIR:=$here/../../../../saves}"
mkdir -p "$out"
out=$(cd "$out" && pwd)
: > "$out/skipped"
rm -f "$out/progress"

from=1
skip=""
while :; do
	NEURO_EXTRACT_DIR="$out" NEURO_EXTRACT_FROM=$from NEURO_EXTRACT_SKIP="$skip" \
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	"$BUILD_DIR/scummvm" --savepath="$NEURO_SAVE_DIR" --auto-detect -p "$GAME_DIR" -x "$slot" > "$out/extract.log" 2>&1 &
	pid=$!

	# Watch for a hang: the progress file should keep changing
	last=""; stalled=0
	while kill -0 $pid 2>/dev/null; do
		sleep 2
		now=$(cat "$out/progress" 2>/dev/null)
		if [ "$now" = "$last" ]; then stalled=$((stalled + 2)); else stalled=0; last=$now; fi
		if [ $stalled -ge 40 ]; then kill -9 $pid 2>/dev/null; break; fi
	done
	wait $pid 2>/dev/null

	at=$(cat "$out/progress" 2>/dev/null)
	[ "$at" = "done" ] && break
	if [ -z "$at" ]; then echo "The game never got going; see $out/extract.log" >&2; exit 1; fi
	echo "room $at crashed or hung the game: skipped"
	echo "$at" >> "$out/skipped"
	skip="$skip,$at"
	from=$((at + 1))
done

# PPM to PNG, with nothing but Python's standard library
python3 - "$out" <<'PY'
import glob, os, struct, sys, zlib
def png(path, width, height, rgb):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
count = 0
for ppm in sorted(glob.glob(os.path.join(sys.argv[1], "room*.ppm"))):
    data = open(ppm, "rb").read()
    magic, size, maxval, rest = data.split(b"\n", 3)
    width, height = map(int, size.split())
    png(ppm[:-4] + ".png", width, height, rest)
    os.remove(ppm)
    count += 1
print(f"{count} rooms written to {sys.argv[1]}")
PY
[ -s "$out/skipped" ] && echo "skipped rooms: $(tr '\n' ' ' < "$out/skipped")"
exit 0
