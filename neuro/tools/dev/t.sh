#!/bin/bash
# Test driver. usage: t.sh start | act NAME 'JSON' | log | stop
S=$(cd "$(dirname "$0")" && pwd)
B=/home/samuelp/projects/neuro-scumm
case "$1" in
start)
	pkill -x scummvm; sleep 1
	(env ${VISIBLE:+-u NOOP_VAR} $([ -z "$VISIBLE" ] && echo SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=${AUDIODRV:-dummy}) timeout ${2:-900} $B/scummvm/neuro/run.sh --debuglevel=2 --screenshotpath=$S/shots $DBGFLAGS $GAMEARGS > $S/game.out 2>&1 &)
	for i in $(seq 1 40); do grep -aq "Neuro: Connected" $S/game.out && break; sleep 1; done; sleep 3
	# Tests drive the game by hand; a watched game is played by the engine
	[ -z "$VISIBLE" ] && $S/mcp.sh set_paused '{"paused":true}' > /dev/null
	$S/mcp.sh list_games | grep -o '\\"id\\":\\"[^\\]*' | head -1 | sed 's/.*\\"//' > $S/gid
	echo 0 > $S/pos ;;
act)
	$S/mcp.sh send_action "{\"game\":\"$(cat $S/gid)\",\"action\":\"$2\",\"data\":${3:-{\}}}" | grep -v '"cursor' ;;
log)
	n=$(cat $S/pos); total=$(wc -l < $S/game.out)
	tail -n +$((n+1)) $S/game.out | grep -a "Neuro: \|WARNING\|ERROR" | grep -v 'speech./ended\|"command":"startup"\|reregister\|actions./register' \
	  | sed 's/Neuro: \([<>]\) .*"command":"\([^"]*\)","data":/\1 \2 /; s/\\n/ | /g' | cut -c1-${2:-700}
	echo $total > $S/pos ;;
waitfor)
	for i in $(seq 1 ${3:-60}); do grep -aq "$2" $S/game.out && exit 0; sleep 1; done; echo "TIMEOUT waiting for $2"; exit 1 ;;
boot)
	$0 start; $0 waitfor "Deep in the Caribbean" 60; sleep 2; $0 act skip_cutscene > /dev/null; $0 waitfor "Location: Dock" 120; $0 log > /dev/null ;;
dbg)
	python3 - "$(cat $S/gid)" "$2" "$3" <<'PY' | curl -s -X POST -H Content-Type:application/json -H Accept:application/json,text/event-stream http://127.0.0.1:8000/mcp -d @- > /dev/null
import json,sys
gid,name,data=sys.argv[1:4]
d={"id":"dbg","name":name}
if data: d["data"]=data
msg=json.dumps({"command":"action","data":d})
print(json.dumps({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"send_raw_message","arguments":{"game":gid,"message":msg}}}))
PY
	;;
shot)
	rm -f $S/shots/*.bmp; $0 dbg debug_screenshot; sleep 1; ffmpeg -loglevel error -y -i $S/shots/scummvm-monkey-00000.bmp -vf scale=640:-1:flags=neighbor $S/shots/${2:-shot}.png ;;
room)
	$0 dbg debug_room $2; sleep ${3:-3}; $0 dbg debug_dump; sleep 1; grep -a NEURODUMP $S/game.out | awk -v r="room $2 " 'index($0,"NEURODUMP room"){on=index($0,r)>0} on' | tail -60 ;;
load)
	GAMEARGS="-x $2" $0 start; $0 waitfor "Location:" 60; sleep 1; $0 log > /dev/null ;;
stop) pkill -x scummvm ;;
esac
