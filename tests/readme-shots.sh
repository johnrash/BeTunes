#!/bin/bash
# Development helper (runs on Haiku): takes the README screenshots with the
# test music and prints the window frame for cropping. Uses its own
# settings (the user's are moved aside and restored).
#   bash tests/readme-shots.sh [BeTunes executable] [music folder]
cd "$(dirname "$0")"
. ./hey-lib.sh
A=application/x-vnd.BeTunes
HEY_APP=$A
APP=${1:-../objects.x86_64-cc13-release/BeTunes}
MUSIC=${2:-/boot/home/BeTunesTest/music}
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/BeTunes"
export BETUNES_AUDIO=silent

st() {
	hey $A Bsta of Window 0 | sed -n "s/^ *\"$1\" ([A-Z_0-9]*) : //p" \
		| head -1 | sed 's/^"\(.*\)"$/\1/; s/ (0x[-0-9a-fA-F]*)$//'
}
id_of() {
	local out index
	out=$(hey $A Bgsl of Window 0)
	index=$(echo "$out" | sed -n 's/^ *"title" ([A-Z_]*) : "\(.*\)"$/\1/p' \
		| grep -n -x -F "$1" | head -1 | cut -d: -f1)
	echo "$out" | sed -n 's/^ *"id" ([A-Z_0-9]*) : \([0-9]*\).*/\1/p' \
		| sed -n "${index}p"
}

hey $A quit >/dev/null 2>&1
wait_app_gone BeTunes
[ -d "$SETTINGS" ] && mv "$SETTINGS" "$SETTINGS.shots-backup"

"$APP" >/dev/null 2>&1 &
waitfor -m $A
hey $A set Frame of Window 0 to "BRect(120,70,1119,689)" >/dev/null
hey $A Bimp of Window 0 with path="\"$MUSIC\"" >/dev/null
for i in $(seq 1 60); do
	[ "$(st importing)" == "FALSE" ] && break
	sleep 0.5
done

# two playlists
songs=()
for title in "Launch" "Polar Night" "Overflow" "Jökulsárlón" "Heat Shield"; do
	songs+=(and "song=int32($(id_of "$title"))")
done
hey $A Bnpl of Window 0 with 'name="Road Trip"' >/dev/null
playlist=$(st 'source playlist')
hey $A Batp of Window 0 with "playlist=int32($playlist)" "${songs[@]}" \
	>/dev/null
hey $A Bnpl of Window 0 with 'name="Chill Out"' >/dev/null
hey $A Bsrs of Window 0 with 'select=int32(0)' >/dev/null

# the library, playing
hey $A Bpsa of Window 0 with 'row=int32(4)' >/dev/null
hey $A Bsse of Window 0 with 'select=int32(4)' >/dev/null
hey $A Bsek of Window 0 with 'position=float(0.35)' >/dev/null
hey $A set Active of Window 0 to "bool(true)" >/dev/null
sleep 1.2
hey $A Bply of Window 0 >/dev/null
take_screenshot /tmp/shot-library.png

# a playlist with shuffle and repeat, and a search in the library
hey $A Bply of Window 0 >/dev/null
hey $A Bsrs of Window 0 with 'select=int32(4)' >/dev/null
hey $A Bshf of Window 0 >/dev/null
hey $A Brpt of Window 0 with 'mode=int32(1)' >/dev/null
hey $A Bpsa of Window 0 with 'row=int32(1)' >/dev/null
sleep 1.5
hey $A Bply of Window 0 >/dev/null
take_screenshot /tmp/shot-playlist.png

hey $A Bsrs of Window 0 with 'select=int32(0)' >/dev/null
hey $A Bsch of Window 0 with 'text="aurora"' >/dev/null
hey_select albums 2
sleep 0.5
take_screenshot /tmp/shot-search.png

# the equalizer, and the dialog for a new preset
hey $A Bsch of Window 0 with 'text=""' >/dev/null
hey $A Bsrs of Window 0 with 'select=int32(0)' >/dev/null
hey_select albums 0
hey $A Bpsa of Window 0 with 'row=int32(8)' >/dev/null
hey $A Bsse of Window 0 with 'select=int32(8)' >/dev/null
hey $A Beqc of Window 0 with 'preset="Rock"' >/dev/null
hey $A Beqw of Window 0 with 'show=bool(true)' >/dev/null
sleep 0.5
hey $A set Frame of Window 1 to "BRect(560,360,1035,568)" >/dev/null
sleep 1.5
take_screenshot /tmp/shot-equalizer.png
hey $A Emak of Window 1 >/dev/null
sleep 1
take_screenshot /tmp/shot-preset-dialog.png
hey $A quit of Window 2 >/dev/null 2>&1

# Get Info (nothing is changed: the windows are closed with Cancel)
hey $A Beqw of Window 0 with 'show=bool(false)' >/dev/null
hey $A Bstp of Window 0 >/dev/null
hey $A Bsse of Window 0 with 'select=int32(3)' >/dev/null
hey $A Binf of Window 0 >/dev/null
sleep 1.5
take_screenshot /tmp/shot-info.png
hey $A get Frame of Window 2 | sed -n 's/.*BRect(\(.*\))/info \1/p'
hey $A Itab of Window 2 with 'index=int32(0)' >/dev/null
sleep 0.5
take_screenshot /tmp/shot-summary.png
hey $A quit of Window 2 >/dev/null
sleep 0.5
hey $A Bsse of Window 0 with 'select=int32(3)' and 'select=int32(4)' \
	and 'select=int32(5)' and 'select=int32(6)' >/dev/null
hey $A Binf of Window 0 >/dev/null
sleep 1
hey $A set Value of View album of Window 2 to \
	'"Northern Lights (Remastered)"' >/dev/null
sleep 0.5
take_screenshot /tmp/shot-info-multiple.png
hey $A get Frame of Window 2 | sed -n 's/.*BRect(\(.*\))/multiple \1/p'
hey $A quit of Window 2 >/dev/null

# the mini player
hey $A Bpsa of Window 0 with 'row=int32(3)' >/dev/null
hey $A Bmin of Window 0 with 'mini=bool(true)' >/dev/null
sleep 1.5
take_screenshot /tmp/shot-mini.png
hey $A get Frame of Window 0 | sed -n 's/.*BRect(\(.*\))/mini \1/p'
# (not its menu: a pop-up menu opened by a script stays open, and an open
# menu keeps the application from quitting)
hey $A Bmin of Window 0 with 'mini=bool(false)' >/dev/null
sleep 0.5

hey $A get Frame of Window 0 | sed -n 's/.*BRect(\(.*\))/frame \1/p'
hey $A quit >/dev/null
wait_app_gone BeTunes
rm -rf "$SETTINGS"
[ -d "$SETTINGS.shots-backup" ] && mv "$SETTINGS.shots-backup" "$SETTINGS"
true
