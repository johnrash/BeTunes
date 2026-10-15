#!/bin/bash
# Development helper (runs on Haiku): starts BeTunes if it is not running
# (with the silent audio output unless BETUNES_AUDIO is set), optionally
# imports a folder and waits for the import, then takes a screenshot.
#   tests/snap.sh /tmp/shot.png [folder to import]
cd "$(dirname "$0")"
. ./hey-lib.sh
HEY_APP=application/x-vnd.BeTunes
APP=${APP:-../objects.x86_64-cc13-release/BeTunes}
OUT=${1:-/tmp/betunes.png}
export BETUNES_AUDIO=${BETUNES_AUDIO:-silent}

if ! roster | grep -q BeTunes; then
	"$APP" >/tmp/betunes.log 2>&1 &
	waitfor -m $HEY_APP
fi

if [ -n "$2" ]; then
	hey $HEY_APP Bimp of Window 0 with path="\"$2\"" >/dev/null
	for i in $(seq 1 120); do
		hey $HEY_APP Bsta of Window 0 | grep -q '"importing" (B_BOOL_TYPE) : FALSE' && break
		sleep 0.5
	done
fi
hey $HEY_APP set Active of Window 0 to "bool(true)" >/dev/null
sleep 1
take_screenshot "$OUT"
hey $HEY_APP get Frame of Window 0 | sed -n 's/.*BRect(\(.*\))/frame \1/p'
echo "screenshot: $OUT"
