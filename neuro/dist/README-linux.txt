Neuro-sama plays The Secret of Monkey Island - Linux build

What you need
  - The Secret of Monkey Island: Special Edition (the Steam release); the
    folder that holds Monkey1.pak.
  - Python 3.9 or newer, for the console.
  - The libraries the game was built against, from your distribution:
    SDL2, SDL2_net, libcurl, FLAC, Vorbis, MAD, libpng, FreeType, zlib.
    On Debian or Ubuntu:
      sudo apt-get install libsdl2-2.0-0 libsdl2-net-2.0-0 libcurl4 libflac12t64 \
          libvorbisfile3 libmad0 libpng16-16t64 libfreetype6
    It is built on Ubuntu 24.04; on a much older or very different system,
    build from source instead (README.md).
  - A Neuro API server to connect to (Neuro herself, or a stand-in such as Gary).

To run
  1. Unpack this folder anywhere.
  2. Run ./console.sh. A page opens in your browser.
  3. In Settings, set the Neuro server address and the game folder, then
     press "Start the game".

The game opens in a 16:9 window made to be captured for a stream: the game
on one side, a column about what she is doing on the other.

Savegames go to the "saves" folder here. Settings are kept in
neuro/console/console.local.json.

Without the console, the game can be started by hand: see README.md.

This is free software under the GNU General Public License, version 3 or
later (COPYING). The source code for this build:
https://github.com/quarkboy/neuro-scumm
