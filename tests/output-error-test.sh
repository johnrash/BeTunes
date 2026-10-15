#!/bin/bash
# Checks what happens when the audio output cannot be opened (runs on
# Haiku; on a machine without sound hardware the system mixer cannot take
# the connection). BeTunes must stay responsive, stop and say why.
#   bash tests/output-error-test.sh [BeTunes executable] [music folder]
cd "$(dirname "$0")"
. ./hey-lib.sh
HEY_APP=application/x-vnd.BeTunes
APP=${1:-../objects.x86_64-cc13-release/BeTunes}
MUSIC=${2:-/boot/home/BeTunesTest/music}
unset BETUNES_AUDIO

st() {
	hey $HEY_APP Bsta of Window 0 | sed -n "s/^ *\"$1\" ([A-Z_0-9]*) : //p" \
		| head -1 | sed 's/^"\(.*\)"$/\1/'
}

hey $HEY_APP quit >/dev/null 2>&1
wait_app_gone BeTunes
"$APP" >/tmp/betunes-output.log 2>&1 &
waitfor -m $HEY_APP
hey $HEY_APP Bimp of Window 0 with path="\"$MUSIC\"" >/dev/null
for i in $(seq 1 60); do
	[ "$(st importing)" == "FALSE" ] && break
	sleep 0.5
done
hey $HEY_APP Bpsa of Window 0 with "row=int32(1)" >/dev/null
start=$(date +%s)
for i in $(seq 1 120); do
	# the window must answer while the output is being opened
	state=$(st state)
	[ "$state" == "stopped" ] && break
	sleep 0.5
done
echo "state after $(( $(date +%s) - start )) s: $state"
echo "message: $(st message)"
echo "windows: $(hey $HEY_APP count Window | sed -n 's/.*"result" ([A-Z_0-9]*) : \([0-9]*\).*/\1/p')"
