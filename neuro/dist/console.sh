#!/bin/sh
# Starts the operator console and opens its page. Needs Python 3.9 or newer.
cd "$(dirname "$0")" && exec python3 neuro/console/console.py --open "$@"
