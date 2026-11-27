#!/bin/bash
# Shared library test of BeTunes, driven with hey (runs on Haiku): finds a
# library shared by tests/daap-test-server.py on the network, connects to it
# with a wrong and then the right password, lists, plays and seeks a shared
# song, and disconnects.
#   bash tests/daap-test.sh <server address> <password> [BeTunes executable]
#       [number of songs shared, 16]
# The server runs on another machine on the same network, shares the test
# music (tests/make-test-music.sh) as one folder, with a password, and
# should send slowly (--rate 30000), so that seeking ahead fetches a part of
# the song on its own. The user's BeTunes settings and library are moved
# aside and restored.
SERVER=$1
PASS=$2
APP=${3:-}
case "$APP" in ""|/*) ;; *) APP="$(pwd)/$APP" ;; esac
	# relative to where the test is started from
SONGS=${4:-16}
cd "$(dirname "$0")"
. ./hey-lib.sh
[ -z "$APP" ] && APP="$(pwd)/../objects.x86_64-cc13-release/BeTunes"
HEY_APP=application/x-vnd.BeTunes
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/BeTunes"
export BETUNES_AUDIO=silent
PASSED=0; FAILED=0

check() {
	if [ "$2" == "$3" ]; then PASSED=$((PASSED + 1)); echo "ok   $1"
	else FAILED=$((FAILED + 1)); echo "FAIL $1: expected '$2', got '$3'"; fi
}
normalize() {
	sed 's/^"\(.*\)"$/\1/; s/ (0x[-0-9a-fA-F]*)$//; s/^TRUE$/1/; s/^FALSE$/0/'
}
# reply <code> <field>: every value of a field of a window reply
reply() {
	hey $HEY_APP "$1" of Window 0 | sed -n "s/^ *\"$2\" ([A-Z_0-9]*) : //p" | normalize
}
st() { reply Bsta "$1" | head -1; }
cmd() { local what=$1; shift; hey $HEY_APP "$what" of Window 0 "$@" >/dev/null; }
# share_field <field>: that field of the share on $SERVER
share_field() {
	local index
	index=$(reply Bgsh host | grep -n -x -F "$SERVER" | head -1 | cut -d: -f1)
	[ -z "$index" ] && return
	reply Bgsh "$1" | sed -n "${index}p"
}
# poll <seconds> <shell condition>
poll() {
	local i
	for i in $(seq 1 $(( $1 * 5 ))); do
		eval "$2" && return 0
		sleep 0.2
	done
	return 1
}

cleanup() {
	hey $HEY_APP quit >/dev/null 2>&1
	poll 10 '! roster | grep -q "/BeTunes "' \
		|| kill $(roster | grep "/BeTunes " | awk '{print $1}') 2>/dev/null
	rm -rf "$SETTINGS"
	[ -d "$SETTINGS.daap-backup" ] && mv "$SETTINGS.daap-backup" "$SETTINGS"
}
hey $HEY_APP quit >/dev/null 2>&1
poll 10 '! roster | grep -q "/BeTunes "'
if [ -d "$SETTINGS" ]; then
	rm -rf "$SETTINGS.daap-backup"
	mv "$SETTINGS" "$SETTINGS.daap-backup"
fi
trap cleanup EXIT

"$APP" >>/tmp/betunes-daap-test.log 2>&1 &
waitfor -m $HEY_APP
wait_window BeTunes

echo "== found on the network (multicast DNS)"
cmd Bfsh
poll 70 '[ "$(share_field found)" == 1 ]'
check "share found on the network" 1 "$(share_field found)"
check "share name" "BeTunes Test Share" "$(share_field name)"
check "share not connected yet" idle "$(share_field state)"

echo "== wrong password"
PW='Shared Library Password'
cmd Bcsh with address="\"$SERVER\"" and password='"not-the-password"'
poll 20 '[ "$(hey $HEY_APP get Title of Window "$PW" | hey_result)" == "$PW" ]'
check "asks again for a wrong password" "$PW" "$(hey $HEY_APP get Title of Window "$PW" | hey_result)"
check "still not connected" idle "$(share_field state)"
hey $HEY_APP quit Window "$PW" >/dev/null 2>&1
poll 10 '[ -z "$(hey $HEY_APP get Title of Window "$PW" | hey_result)" ]'
check "password window closed" "" "$(hey $HEY_APP get Title of Window "$PW" | hey_result)"

echo "== connect with the password"
cmd Bcsh with address="\"$SERVER\"" and password="\"$PASS\""
poll 30 '[ "$(share_field state)" == connected ] && [ "$(share_field songs)" == "$SONGS" ]'
check "connected" connected "$(share_field state)"
check "shared songs" "$SONGS" "$(share_field songs)"
ID=$(share_field id)
cmd Bcsh with share="int32($ID)"
poll 10 '[ "$(st rows)" == "$SONGS" ]'
check "shared songs listed" "$SONGS" "$(st rows)"
check "source is the share" "BeTunes Test Share" "$(st source)"
# the test server names songs after their files, "-" read as " "
check "a title from the share" 1 "$(reply Bgsl title | grep -c -x '01 Polar Night')"

echo "== play and seek a shared song"
# the WAV file of 1 MB: seeking to 80 % reads more than 512 KB past what a
# slow server (--rate) has sent, so BeTunes fetches that part on its own
SONG='Untitled Recording'
ROW=$(( $(reply Bgsl title | grep -n -x -F "$SONG" | head -1 | cut -d: -f1) - 1 ))
cmd Bpsa with row="int32($ROW)"
poll 20 '[ "$(st state)" == playing ] && [ "$(st position)" -gt 0 ]'
check "playing a shared song" playing "$(st state)"
check "the song asked for" "$SONG" "$(st title)"
check "position moves" 1 "$([ "$(st position)" -gt 0 ] && echo 1 || echo 0)"
DURATION=$(st duration)
cmd Bsek with position='float(0.8)'
poll 15 '[ "$(st position)" -ge $(( DURATION * 75 / 100 )) ]'
POS=$(st position)
echo "     position $POS of $DURATION us after seeking to 80 %"
check "seek ahead in a shared song" 1 "$([ "$POS" -ge $(( DURATION * 75 / 100 )) ] && echo 1 || echo 0)"
poll 10 '[ "$(st position)" -gt $(( POS + 500000 )) ] || [ "$(st title)" != "$SONG" ]'
AFTER=$(st position)
echo "     position $AFTER us a little later"
check "plays on after the seek" 1 "$([ "$(st title)" == "$SONG" ] && [ "$AFTER" -gt $(( POS + 500000 )) ] && echo 1 || echo 0)"
cmd Bstp
poll 5 '[ "$(st state)" == stopped ]'
check "stopped" stopped "$(st state)"

echo "== disconnect"
cmd Bdsh with share="int32($ID)"
poll 10 '[ "$(share_field state)" == idle ]'
check "disconnected" idle "$(share_field state)"
check "no shared songs after disconnecting" 0 "$(share_field songs)"

echo
echo "$PASSED passed, $FAILED failed"
[ $FAILED -eq 0 ]
