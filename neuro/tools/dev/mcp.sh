#!/bin/sh
# usage: mcp.sh tool 'json-args'
curl -s -X POST -H Content-Type:application/json -H Accept:application/json,text/event-stream http://127.0.0.1:8000/mcp -d "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"$1\",\"arguments\":${2:-{\}}}}"
echo
