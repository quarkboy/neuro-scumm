#!/bin/bash
# raw.sh ACTION 'exact data text'  - send an action whose data is sent verbatim
S=$(cd "$(dirname "$0")" && pwd)
python3 - "$(cat $S/gid)" "$1" "$2" <<'PY' | curl -s -X POST -H Content-Type:application/json -H Accept:application/json,text/event-stream http://127.0.0.1:8000/mcp -d @- | grep -o '"isError":true.*' | cut -c1-200
import json,sys
gid,name,data=sys.argv[1:4]
print(json.dumps({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"send_action","arguments":{"game":gid,"action":name,"rawData":data}}}))
PY
