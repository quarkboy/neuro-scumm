#!/bin/bash
S=$(cd "$(dirname "$0")" && pwd)
grep -a "Neuro: [<>]" $S/game.out | grep -a '"action"\|"context"\|"actions./force"\|"success":false\|"success":true,"message"' | sed 's/Neuro: < .*"name":"\([a-z_]*\)","data":\(.*\)}}$/ACT \1 \2/; s/Neuro: > .*"command":"context","data":{"silent":\([a-z]*\),"message":"/CTX(\1) /; s/Neuro: > .*actions..force.*/FORCE/; s/Neuro: > .*"success":false,"message":"/FAIL /; s/Neuro: > .*"success":true,"message":"/OK+ /; s/\\n/ | /g; s/\\"/"/g; s/","id":"[0-9a-f-]*"}}$//; s/ | "}}$//' | cut -c1-${1:-210}
