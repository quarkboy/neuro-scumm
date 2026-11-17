# Sourced by run.sh and the tools: works out which game is meant and where things are.
# Set "here" to the neuro folder first. Sets NEURO_GAME, GAME_FOLDER (its folder under
# neuro/games), GAME_DIR (the installed game), BUILD_DIR and NEURO_SAVE_DIR, each only
# if the caller's environment has not set it.
: "${NEURO_GAME:=monkey1}"
GAME_FOLDER=$here/games/$NEURO_GAME
if [ ! -f "$GAME_FOLDER/game.json" ]; then
	echo "No game called '$NEURO_GAME': $GAME_FOLDER/game.json does not exist" >&2
	exit 1
fi
# game.json keeps one "key": "value" to a line
game_field() { sed -n "s/^ *\"$1\": *\"\(.*\)\",\{0,1\} *\$/\1/p" "$GAME_FOLDER/game.json"; }

# The installed game: where Steam puts it on Windows (seen from WSL), or on Linux
# MONKEY1_DIR is the older name of GAME_DIR
: "${GAME_DIR:=${MONKEY1_DIR:-}}"
if [ -z "$GAME_DIR" ]; then
	steam="steamapps/common/$(game_field steam_folder)"
	GAME_DIR="/mnt/c/Program Files (x86)/Steam/$steam"
	[ -d "$GAME_DIR" ] || GAME_DIR="$HOME/.steam/steam/$steam"
fi

# The build: a "build" folder at the top of the repository, or "build-linux" beside it
if [ -z "${BUILD_DIR:-}" ]; then
	BUILD_DIR=$here/../build
	[ -x "$BUILD_DIR/scummvm" ] || BUILD_DIR=$here/../../build-linux
fi

# Savegames stay outside the repository
: "${NEURO_SAVE_DIR:=$here/../../saves}"
