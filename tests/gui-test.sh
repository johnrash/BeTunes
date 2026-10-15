#!/bin/bash
# End-to-end test of BeTunes, driven with hey (runs on Haiku).
#   bash tests/gui-test.sh [BeTunes executable] [music folder]
# The music folder is made by tests/make-test-music.sh. The test plays
# through the silent audio output, so it works without sound hardware. The
# user's BeTunes settings and library are moved aside and restored.
cd "$(dirname "$0")"
. ./hey-lib.sh
HEY_APP=application/x-vnd.BeTunes
APP=${1:-../objects.x86_64-cc13-release/BeTunes}
case "$APP" in /*) ;; *) APP="$(pwd)/$APP" ;; esac
MUSIC=${2:-/boot/home/BeTunesTest/music}
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/BeTunes"
SCRATCH=/tmp/betunes-test
export BETUNES_AUDIO=silent
# the silent output records what it plays: for measuring the equalizer
export BETUNES_AUDIO_DUMP=$SCRATCH/output.raw

PASSED=0
FAILED=0

check() {
	# check <description> <expected> <actual>
	if [ "$2" == "$3" ]; then
		PASSED=$((PASSED + 1))
		echo "ok   $1"
	else
		FAILED=$((FAILED + 1))
		echo "FAIL $1: expected '$2', got '$3'"
	fi
}

check_true() {
	# check_true <description> <shell condition>
	if eval "$2"; then
		PASSED=$((PASSED + 1))
		echo "ok   $1"
	else
		FAILED=$((FAILED + 1))
		echo "FAIL $1 ($2)"
	fi
}

normalize() {
	sed 's/^"\(.*\)"$/\1/; s/ (0x[-0-9a-fA-F]*)$//; s/^TRUE$/1/; s/^FALSE$/0/'
}

# st <field>: a field of the window state
st() {
	hey $HEY_APP Bsta of Window 0 | sed -n "s/^ *\"$1\" ([A-Z_0-9]*) : //p" \
		| head -1 | normalize
}

# songs <field>: a field of every visible song, one per line
songs() {
	hey $HEY_APP Bgsl of Window 0 | sed -n "s/^ *\"$1\" ([A-Z_0-9]*) : //p" \
		| normalize
}

# song_field <title> <field>: a field of the first visible song with that
# title
song_field() {
	local out index
	out=$(hey $HEY_APP Bgsl of Window 0)
	index=$(echo "$out" | sed -n 's/^ *"title" ([A-Z_]*) : //p' | normalize \
		| grep -n -x -F "$1" | head -1 | cut -d: -f1)
	[ -z "$index" ] && return
	echo "$out" | sed -n "s/^ *\"$2\" ([A-Z_0-9]*) : //p" | sed -n "${index}p" \
		| normalize
}

# row_of <title>: row index of a visible song
row_of() {
	local index
	index=$(songs title | grep -n -x -F "$1" | head -1 | cut -d: -f1)
	echo $((index - 1))
}

sources() {
	hey $HEY_APP Bgsr of Window 0 | sed -n "s/^ *\"$1\" ([A-Z_0-9]*) : //p" \
		| normalize
}

browser() {
	hey $HEY_APP Bgbr of Window 0 | sed -n "s/^ *\"$1\" ([A-Z_0-9]*) : //p" \
		| normalize
}

# cmd <code> [with ...]: a command to the window (replies when done)
cmd() {
	local what=$1
	shift
	hey $HEY_APP "$what" of Window 0 "$@" >/dev/null
}

# wait_for <field> <value> [seconds]
wait_for() {
	local i
	for i in $(seq 1 $(( ${3:-10} * 5 ))); do
		[ "$(st "$1")" == "$2" ] && return 0
		sleep 0.2
	done
	return 1
}

start_app() {
	"$APP" >>/tmp/betunes-test.log 2>&1 &
	waitfor -m $HEY_APP
	wait_window BeTunes
}

quit_app() {
	hey $HEY_APP quit >/dev/null 2>&1
	local i
	for i in $(seq 1 50); do
		roster | grep -q "/BeTunes " || return 0
		sleep 0.2
	done
	FAILED=$((FAILED + 1))
	echo "FAIL BeTunes did not quit"
	kill $(roster | grep "/BeTunes " | awk '{print $1}') 2>/dev/null
	return 1
}

# eq_gain <band>: a band of the equalizer (0 = 32 Hz)
eq_gain() {
	hey $HEY_APP Bsta of Window 0 \
		| sed -n 's/^ *"eq gains" ([A-Z_0-9]*) : //p' | sed -n "$(($1 + 1))p"
}

# eq_window_value <view>: a control of the Equalizer window
eq_window_value() {
	hey $HEY_APP get Value of View "$1" of Window 1 | hey_result
}

# readtag <file> <key>: a tag as the Media Kit reads it
readtag() {
	"$SCRATCH/readtags" "$1" "$2"
}

# info_value <view>: a field of the Get Info window
info_value() {
	hey $HEY_APP get Value of View "$1" of Window 2 | hey_result
}

# info_set <view> <text>: types into a field of the Get Info window
info_set() {
	hey $HEY_APP set Value of View "$1" of Window 2 to "\"$2\"" >/dev/null
}

# select_titles <title>...: selects these songs in the song list
select_titles() {
	local args=(with)
	local title
	for title in "$@"; do
		[ ${#args[@]} -gt 1 ] && args+=(and)
		args+=("select=int32($(row_of "$title"))")
	done
	cmd Bsse "${args[@]}"
}

# view_hidden <view>: whether a view of the main window is hidden (1 or 0)
view_hidden() {
	hey $HEY_APP get Hidden of View "$1" of Window 0 | hey_result
}

# window_height: the main window's height
window_height() {
	hey $HEY_APP get Frame of Window 0 | hey_result \
		| sed 's/BRect(\(.*\))/\1/' | awk -F, '{ printf "%d", $4 - $2 }'
}

# louder <a> <b> <dB>: whether level a is more than dB above level b
louder() {
	awk -v a="$1" -v b="$2" -v d="$3" 'BEGIN { exit !(a - b > d) }'
}

cleanup() {
	quit_app
	rm -rf "$SETTINGS" "$SCRATCH"
	[ -d "$SETTINGS.test-backup" ] && mv "$SETTINGS.test-backup" "$SETTINGS"
	echo
	echo "$PASSED passed, $FAILED failed"
}

if [ ! -d "$MUSIC" ]; then
	echo "no test music in $MUSIC (make it with tests/make-test-music.sh)"
	exit 1
fi

quit_app
if [ -d "$SETTINGS" ]; then
	rm -rf "$SETTINGS.test-backup"
	mv "$SETTINGS" "$SETTINGS.test-backup"
fi
trap cleanup EXIT
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
# Get Info writes tags: work on a copy of the music
cp -r "$MUSIC" "$SCRATCH/music"
MUSIC="$SCRATCH/music"
: > /tmp/betunes-test.log
start_app

echo "== empty library"
check "library is empty" 0 "$(st library)"
check "four built-in sources" 4 "$(sources name | wc -l)"
check "the library is shown" Library "$(st source)"
check "nothing plays" stopped "$(st state)"

echo "== importing"
cmd Bimp with "path=\"$MUSIC\""
wait_for importing 0 60
check "16 songs in the library" 16 "$(st library)"
check "16 rows" 16 "$(st rows)"
check "totals in the status bar" "16 songs, 1.6 minutes, 3.6 MB" "$(st status)"
check "artists (All, 5 named, unknown)" \
	"All (6 Artists)|Aurora Lane|The Bit Shifters|Cosmic Drift|Retro Tagger|Sigrún Ólafsdóttir|Unknown Artist" \
	"$(browser artist | paste -s -d '|')"
check "albums" \
	"All (8 Albums)|Café Classics|Ísland|Midnight Sun|Northern Lights|Orbit|Re-Entry|Square Waves|Unknown Album" \
	"$(browser album | paste -s -d '|')"

echo "== tags"
check "ID3v2.3 title/artist" "Aurora Lane" "$(song_field "Polar Night" artist)"
check "ID3v2 track 1/4" 1 "$(song_field "Polar Night" track)"
check "ID3v2 year" 2004 "$(song_field "Polar Night" year)"
check "ID3v2.4 UTF-8" "Sigrún Ólafsdóttir" "$(song_field "Jökulsárlón" artist)"
check "Ogg Vorbis album" "Midnight Sun" "$(song_field Daybreak album)"
check "Ogg Vorbis year" 2006 "$(song_field Daybreak year)"
check "Opus album" "Re-Entry" "$(song_field "Heat Shield" album)"
check "FLAC genre" Electronic "$(song_field Overflow genre)"
check "AAC year" 2003 "$(song_field Launch year)"
check "ID3v1 artist" "Retro Tagger" "$(song_field "Old Tagger" artist)"
check "ID3v1 Latin-1 album" "Café Classics" "$(song_field "Old Tagger" album)"
check "ID3v1.1 track" 7 "$(song_field "Old Tagger" track)"
check "ID3v1 genre" Rock "$(song_field "Old Tagger" genre)"
check "ID3v1 year" 1998 "$(song_field "Old Tagger" year)"
check "title from the file name" "" "$(song_field "Untitled Recording" artist)"
check_true "durations read" "[ $(song_field Daybreak duration) -gt 5000000 ]"

echo "== importing again adds nothing"
cmd Bimp with "path=\"$MUSIC\""
wait_for importing 0 60
check "still 16 songs" 16 "$(st library)"

echo "== sort order: artist, album, track (\"The\" ignored)"
check "first songs" "Untitled Recording|Daybreak|Endless Day|Polar Night|Green Curtain" \
	"$(songs title | head -5 | paste -s -d '|')"
check "The Bit Shifters sorted under B" "Overflow" "$(songs title | sed -n 8p)"
cmd Bsby with 'column=int32(6)'
sleep 0.5
check "by genre: then artist, album, track" \
	"Untitled Recording|Daybreak|Endless Day|Polar Night|Green Curtain|Solar Wind|Magnetic North|Overflow" \
	"$(songs title | head -8 | paste -s -d '|')"
cmd Bsby with 'column=int32(5)'
sleep 0.5
check "by album: in track order" "Old Tagger|Jökulsárlón|Þingvellir|Daybreak|Endless Day" \
	"$(songs title | sed -n 2,6p | paste -s -d '|')"
cmd Bsby with 'column=int32(8)' and 'ascending=bool(false)'
sleep 0.5
check "by year, newest first" "Heat Shield" "$(songs title | head -1)"
cmd Bsby with 'column=int32(4)'
sleep 0.5

echo "== browser"
hey_select artists 1
check "artist selected" "Aurora Lane" "$(st 'artist selection')"
check "songs of the artist" 6 "$(st rows)"
check "albums of the artist" "All (2 Albums)|Midnight Sun|Northern Lights" \
	"$(browser album | paste -s -d '|')"
hey_select albums 2
check "songs of the album" 4 "$(st rows)"
check "album songs in track order" "Polar Night|Green Curtain|Solar Wind|Magnetic North" \
	"$(songs title | paste -s -d '|')"
hey_select artists 0
check "all artists again" 16 "$(st rows)"
check "all albums again" 9 "$(st albums)"

echo "== search"
cmd Bsch with 'text="aurora"'
check "search for an artist" 6 "$(st rows)"
check "browser follows the search" 2 "$(st artists)"
cmd Bsch with 'text="aurora night"'
check "every word must match" 3 "$(st rows)"
cmd Bsch with 'text="JOKUL"'
check "case and accents ignored" "Jökulsárlón" "$(songs title)"
cmd Bscp with 'scope=int32(3)'
cmd Bsch with 'text="lane"'
check "scope Songs: names only" 0 "$(st rows)"
cmd Bscp with 'scope=int32(1)'
check "scope Artists" 6 "$(st rows)"
cmd Bscp with 'scope=int32(2)'
cmd Bsch with 'text="orbit"'
check "scope Albums" 2 "$(st rows)"
cmd Bscp with 'scope=int32(4)'
cmd Bsch with 'text="folk"'
check "scope Genres" 2 "$(st rows)"
cmd Bscp with 'scope=int32(0)'
cmd Bsch with 'text=""'
check "search cleared" 16 "$(st rows)"

echo "== playing"
cmd Bpsa with 'row=int32(1)'
check "plays the song" "playing" "$(st state)"
check "the song" Daybreak "$(st title)"
check "queue is the list" 16 "$(st queue)"
sleep 1.5
check_true "time advances" "[ $(st position) -gt 1000000 ]"
cmd Bply
check "pause" paused "$(st state)"
before=$(st position)
sleep 1
check "paused: time stands still" "$before" "$(st position)"
cmd Bply
check "resume" playing "$(st state)"
cmd Bsek with 'position=float(0.5)'
position=$(st position)
check_true "seek to the middle" "[ $position -gt 2500000 -a $position -lt 4000000 ]"
cmd Bnxt
check "next song" "Endless Day" "$(st title)"
cmd Bprv
check "previous song" Daybreak "$(st title)"
sleep 3.5
cmd Bprv
check "previous after 3 s: same song" Daybreak "$(st title)"
check_true "previous after 3 s: from the start" "[ $(st position) -lt 1000000 ]"
cmd Bscn with 'direction=int32(1)'
check_true "fast forward" "[ $(st position) -gt 1500000 ]"
cmd Bstp
check "stop" stopped "$(st state)"

echo "== end of a song"
cmd Bpsa with 'row=int32(2)'
cmd Bsek with 'position=float(0.85)'
wait_for title "Polar Night" 6
check "the next song follows" "Polar Night" "$(st title)"
check "play count" 1 "$(song_field "Endless Day" plays)"

echo "== repeat one"
cmd Brpt with 'mode=int32(2)'
check "repeat one" 2 "$(st repeat)"
cmd Bsek with 'position=float(0.85)'
sleep 2.5
check "the song repeats" "Polar Night" "$(st title)"
check "and counts" 1 "$(song_field "Polar Night" plays)"
cmd Bnxt
check "next leaves the song" "Green Curtain" "$(st title)"

echo "== repeat off: stops after the last song"
cmd Brpt with 'mode=int32(0)'
cmd Bpsa with 'row=int32(15)'
cmd Bsek with 'position=float(0.85)'
wait_for state stopped 6
check "stopped at the end" stopped "$(st state)"

echo "== repeat all: wraps around"
cmd Brpt
check "the button cycles: off -> all" 1 "$(st repeat)"
cmd Bpsa with 'row=int32(15)'
cmd Bsek with 'position=float(0.85)'
wait_for title "Untitled Recording" 6
check "back to the first song" "Untitled Recording" "$(st title)"
cmd Bstp

echo "== shuffle"
cmd Bshf
check "shuffle on" 1 "$(st shuffle)"
cmd Bpsa with 'row=int32(4)'
check "starts with the chosen song" "Green Curtain" "$(st title)"
order=""
for i in $(seq 1 16); do
	order="$order|$(st song)"
	cmd Bnxt
done
check "every song once" 16 "$(echo "$order" | tr '|' '\n' | grep -v '^$' | sort -u | wc -l)"
check_true "not in list order" "[ \"$order\" != \"$(songs id | head -16 | sed 's/^/|/' | tr -d '\n')\" ]"
cmd Bshf
check "shuffle off" 0 "$(st shuffle)"
cmd Bstp

echo "== volume"
cmd Bvol with 'volume=int32(30)'
check "volume" 30 "$(st volume)"
cmd Bvup
check "volume up" 40 "$(st volume)"
cmd Bvdn
check "volume down" 30 "$(st volume)"
cmd Bmut
check "mute" 0 "$(st volume)"
cmd Bmut
check "unmute" 30 "$(st volume)"

echo "== equalizer"
check "off at first" 0 "$(st eq)"
check "flat at first" Flat "$(st 'eq preset')"
check "window hidden" 0 "$(st 'eq window')"
cmd Beqw
wait_for 'eq window' 1 5
check "the button shows the window" 1 "$(st 'eq window')"
cmd Beqc with 'preset="Rock"'
check "a preset switches it on" 1 "$(st eq)"
check "preset" Rock "$(st 'eq preset')"
check "preset values" "5.000" "$(eq_gain 0)"
sleep 0.3
check "the window shows the preset" -10 "$(eq_window_value "band 1K")"
check "the window shows On" 1 "$(eq_window_value on)"
cmd Beqc with 'preset="Bass Booster"'
check "names with spaces" "Bass Booster" "$(st 'eq preset')"
cmd Beqc with 'band=int32(5)' and 'gain=float(-3)'
check "editing a band: Manual" Manual "$(st 'eq preset')"
check "the band" "-3.000" "$(eq_gain 5)"
check "the others stay" "5.500" "$(eq_gain 0)"
cmd Beqc with 'band=int32(2)' and 'gain=float(30)'
check "gains are limited to 12 dB" "12.000" "$(eq_gain 2)"
cmd Beqm with 'name="Mine"'
check "make a preset" 1 "$(st 'eq user presets')"
check "it is selected" Mine "$(st 'eq preset')"
cmd Beqc with 'preset="Flat"'
check "flat" "0.000" "$(eq_gain 5)"
cmd Beqc with 'preset="Mine"'
check "own preset recalled" "-3.000" "$(eq_gain 5)"
cmd Beqd with 'name="Mine"'
check "delete the preset" 0 "$(st 'eq user presets')"
check "back to Manual" Manual "$(st 'eq preset')"
cmd Beqc with 'enabled=bool(false)'
check "off" 0 "$(st eq)"
sleep 0.3
check "the window shows Off" 0 "$(eq_window_value on)"

# the window's own controls: change a control, then send what it sends
hey $HEY_APP set Value of View "band 250" of Window 1 to -50 >/dev/null
hey $HEY_APP Esld of Window 1 with 'band=int32(3)' >/dev/null
wait_for 'eq preset' Manual 5
check "moving a slider: the band" "-5.000" "$(eq_gain 3)"
check "moving a slider switches it on" 1 "$(st eq)"
check "the On box follows" 1 "$(eq_window_value on)"
hey $HEY_APP set Value of View on of Window 1 to 0 >/dev/null
hey $HEY_APP 'Eon ' of Window 1 >/dev/null
wait_for eq 0 5
check "the On box switches it off" 0 "$(st eq)"
hey $HEY_APP Epre of Window 1 with 'preset="Dance"' >/dev/null
wait_for 'eq preset' Dance 5
check "choosing a preset in the window" Dance "$(st 'eq preset')"
sleep 0.3
check "the sliders follow" 50 "$(eq_window_value "band 2K")"
hey $HEY_APP Enam of Window 1 with 'name="From Window"' >/dev/null
wait_for 'eq user presets' 1 5
check "making a preset in the window" "From Window" "$(st 'eq preset')"
hey $HEY_APP Edel of Window 1 >/dev/null
wait_for 'eq user presets' 0 5
check "deleting it in the window" Manual "$(st 'eq preset')"
cmd Beqc with 'enabled=bool(false)'
cmd Beqw
wait_for 'eq window' 0 5
check "the button hides the window" 0 "$(st 'eq window')"

echo "== the equalizer changes the sound"
g++ -O2 -o "$SCRATCH/levels" levels.cpp
cmd Bvol with 'volume=int32(100)'
overflow_row=$(row_of Overflow)
# measure: plays "Overflow" (392 and 490 Hz) for a moment, prints its level
measure() {
	local size
	size=$(stat -c %s "$BETUNES_AUDIO_DUMP" 2>/dev/null || echo 0)
	cmd Bpsa with "row=int32($overflow_row)"
	sleep 1.5
	cmd Bstp
	sleep 0.3
	"$SCRATCH/levels" "$BETUNES_AUDIO_DUMP" "$size"
}
cmd Beqc with 'preset="Flat"'
flat=$(measure)
cmd Beqc with 'band=int32(4)' and 'gain=float(12)'
boosted=$(measure)
cmd Beqc with 'band=int32(4)' and 'gain=float(-12)'
cut=$(measure)
cmd Beqc with 'band=int32(4)' and 'gain=float(0)' and 'preamp=float(-6)'
preamp=$(measure)
cmd Beqc with 'enabled=bool(false)'
off=$(measure)
echo "     levels: flat $flat, 500 Hz +12 $boosted, -12 $cut, preamp -6 $preamp, off $off dB"
check_true "boosting 500 Hz: louder" "louder $boosted $flat 6"
check_true "cutting 500 Hz: quieter" "louder $flat $cut 6"
check_true "preamp -6 dB" "louder $flat $preamp 5.5 && ! louder $flat $preamp 6.5"
check_true "off: like flat" "! louder $off $flat 0.3 && ! louder $flat $off 0.3"
cmd Beqc with 'preset="Flat"' and 'enabled=bool(false)'

echo "== playlists"
daybreak=$(song_field Daybreak id)
overflow=$(song_field Overflow id)
launch=$(song_field Launch id)
cmd Bnpl with 'name="Road Trip"'
check "playlist made" 1 "$(st playlists)"
check "and shown" "Road Trip" "$(st source)"
check "empty" 0 "$(st rows)"
playlist=$(st 'source playlist')
cmd Batp with "playlist=int32($playlist)" and "song=int32($daybreak)" \
	and "song=int32($overflow)" and "song=int32($launch)"
check "songs added" "Daybreak|Overflow|Launch" "$(songs title | paste -s -d '|')"
check "numbered" "1|2|3" "$(songs number | paste -s -d '|')"
cmd Bmov with 'from=int32(0)' and 'to=int32(3)'
check "moved to the end" "Overflow|Launch|Daybreak" "$(songs title | paste -s -d '|')"
cmd Bsse with 'select=int32(0)'
cmd Bdel
check "removed from the playlist" "Launch|Daybreak" "$(songs title | paste -s -d '|')"
check "but not from the library" 16 "$(st library)"
cmd Brnp with "playlist=int32($playlist)" and 'name="Summer"'
check "renamed" "Summer" "$(st source)"
cmd Bpsa with 'row=int32(-1)'
check "plays the playlist" Launch "$(st title)"
check "speaker at the playlist" 1 "$(sources playing | sed -n 5p)"
cmd Bsrs with 'select=int32(0)'
cmd Bcur
check "show current song: back to the playlist" Summer "$(st source)"
check "and selects it" 0 "$(st selected)"
cmd Bstp

cmd Bsrs with 'select=int32(0)'
cmd Bsse with 'select=int32(1)' and 'select=int32(2)'
cmd Bnps with 'name="Two"'
check "new playlist from the selection" 2 "$(st rows)"
check "two playlists" 2 "$(st playlists)"
two=$(st 'source playlist')

cmd Bsrs with 'select=int32(0)'
cmd Bsdr with 'playlist=int32(-2)' and "song=int32($launch)"
check "dropping on the empty area makes a playlist" 3 "$(st playlists)"
check "renaming in place" 1 "$(st renaming)"
cmd Bfsr
check "renaming ends when focus moves" 0 "$(st renaming)"
check "default name" "untitled playlist" "$(st source)"
check "with the dropped song" Launch "$(songs title)"
untitled=$(st 'source playlist')
cmd Bsdr with "playlist=int32($two)" and "song=int32($launch)"
cmd Bsrs with 'select=int32(5)'
check "dropping on a playlist adds" 3 "$(st rows)"
cmd Bdpl with "playlist=int32($untitled)" and 'confirmed=bool(true)'
check "playlist deleted" 2 "$(st playlists)"

echo "== get info: one song"
g++ -o "$SCRATCH/readtags" readtags.cpp -lbe -lmedia
cmd Bsrs with 'select=int32(0)'
polar="$MUSIC/Aurora Lane/Northern Lights/01 Polar Night.mp3"
addattr -t string BeTunes:Test "keep me" "$polar"
mimeType=$(catattr -d BEOS:TYPE "$polar" 2>/dev/null)
select_titles "Polar Night"
cmd Binf
wait_window "Polar Night"
check "the window" "Polar Night" "$(hey $HEY_APP get Title of Window 2 | hey_result)"
check "the fields" "Aurora Lane" "$(info_value artist)"
check "track" 1 "$(info_value track)"
info_set artist "Aurora Borealis"
info_set year 2006
info_set title "Polar Night"
hey $HEY_APP 'Iok ' of Window 2 >/dev/null
wait_for 'tag writes' 0 15
check "library: artist" "Aurora Borealis" "$(song_field "Polar Night" artist)"
check "library: year" 2006 "$(song_field "Polar Night" year)"
check "library: album kept" "Northern Lights" "$(song_field "Polar Night" album)"
check "file: artist" "Aurora Borealis" "$(readtag "$polar" artist)"
check "file: year" 2006 "$(readtag "$polar" date)"
check "file: title kept" "Polar Night" "$(readtag "$polar" title)"
check "file: track kept" "1/4" "$(readtag "$polar" track)"
check "file: attributes kept" "keep me" "$(catattr -d BeTunes:Test "$polar" 2>/dev/null)"
check "file: MIME type kept" "$mimeType" "$(catattr -d BEOS:TYPE "$polar" 2>/dev/null)"
check "no failures" 0 "$(st 'tag failures')"
cmd Bpsa with "row=int32($(row_of "Polar Night"))"
sleep 1
check_true "the changed file plays" "[ $(st position) -gt 500000 ]"
cmd Bstp

select_titles "Solar Wind"
cmd Binf
wait_window "Solar Wind"
info_set title "Changed My Mind"
hey $HEY_APP quit of Window 2 >/dev/null
wait_window_gone "Solar Wind"
check "cancel changes nothing" 0 "$(songs title | grep -c -x "Changed My Mind")"

echo "== get info: several songs"
select_titles "Polar Night" "Green Curtain" "Solar Wind" "Magnetic North"
cmd Binf
wait_window "Multiple Song Information"
check "different values are empty" "" "$(info_value artist)"
check "shared values are shown" "Northern Lights" "$(info_value album)"
check "no name field" "" "$(hey $HEY_APP get Value of View title of Window 2 2>&1 | hey_result)"
info_set album "Northern Lights (Remastered)"
info_set artist "Aurora Lane"
sleep 0.3
check "typing ticks the box" 1 "$(info_value "use album")"
check "untouched fields stay unticked" 0 "$(info_value "use genre")"
hey $HEY_APP 'Iok ' of Window 2 >/dev/null
wait_for 'tag writes' 0 20
check "all four changed" 4 "$(songs album | grep -c -x "Northern Lights (Remastered)")"
check "artist for all" "Aurora Lane" "$(song_field "Polar Night" artist)"
check "track numbers kept" 3 "$(song_field "Solar Wind" track)"
for title in "02 Green Curtain" "04 Magnetic North" "01 Polar Night"; do
	file="$MUSIC/Aurora Lane/Northern Lights/$title.mp3"
	check "file: $title" "Northern Lights (Remastered)" "$(readtag "$file" album)"
done
check "file: artist back" "Aurora Lane" "$(readtag "$polar" artist)"

echo "== get info: a file that cannot be written"
solar="$MUSIC/Aurora Lane/Northern Lights/03 Solar Wind.mp3"
# a damaged tag: the first frame claims to be huge
printf '\x7f' | dd of="$solar" bs=1 seek=14 conv=notrunc 2>/dev/null
before=$(md5sum < "$solar")
select_titles "Solar Wind"
cmd Binf
wait_window "Solar Wind"
info_set genre "Space Rock"
hey $HEY_APP 'Iok ' of Window 2 >/dev/null
wait_for 'tag writes' 0 15
check "the failure is counted" 1 "$(st 'tag failures')"
check "the file is untouched" "$before" "$(md5sum < "$solar")"
check "the library keeps the change" "Space Rock" "$(song_field "Solar Wind" genre)"
wait_window "BeTunes: Get Info"
check "an alert says so" "BeTunes: Get Info" \
	"$(hey $HEY_APP get Title of Window 2 | hey_result)"
hey $HEY_APP quit of Window 2 >/dev/null

echo "== get info: other formats"
daybreak_file="$MUSIC/Aurora Lane/Midnight Sun/01 Daybreak.ogg"
tagger_file="$MUSIC/Misc/Old Tagger.mp3"
select_titles Daybreak Overflow Launch "Old Tagger"
cmd Binf
wait_window "Multiple Song Information"
info_set year 2020
hey $HEY_APP 'Iok ' of Window 2 >/dev/null
wait_for 'tag writes' 0 15
check "library: all four" 4 "$(songs year | grep -c -x 2020)"
check "Ogg file untouched" 2006 "$(readtag "$daybreak_file" date)"
check "MP3 with only ID3v1: now ID3v2" 2020 "$(readtag "$tagger_file" date)"
check "its ID3v1 artist kept" "Retro Tagger" "$(tail -c 95 "$tagger_file" | head -c 12)"
check "ID3v1 year" 2020 "$(tail -c 35 "$tagger_file" | head -c 4)"
check "no new failures" 1 "$(st 'tag failures')"

echo "== mini player"
g++ -I/boot/system/develop/headers/private/app -o "$SCRATCH/sendkey" \
	sendkey.cpp -lbe
fullHeight=$(window_height)
cmd Bpsa with 'row=int32(1)'
cmd Bmin
check "mini player" 1 "$(st mini)"
check "song list hidden" 1 "$(view_hidden songs)"
check "source list hidden" 1 "$(view_hidden sources)"
check "menu bar hidden" 1 "$(view_hidden "menu bar")"
check "search hidden" 1 "$(view_hidden search)"
check "bottom bar hidden" 1 "$(view_hidden "bottom bar")"
check "buttons shown" 0 "$(view_hidden play)"
check "volume shown" 0 "$(view_hidden volume)"
check "display shown" 0 "$(view_hidden display)"
check_true "only as tall as the controls" "[ $(window_height) -lt 120 ]"
check "no resize knob (titled look)" 1 \
	"$(hey $HEY_APP get Look of Window 0 | hey_result)"
check "the song plays on" playing "$(st state)"
"$SCRATCH/sendkey" $HEY_APP 0 space
sleep 0.3
check "space pauses" paused "$(st state)"
"$SCRATCH/sendkey" $HEY_APP 0 space
"$SCRATCH/sendkey" $HEY_APP 0 cmd+right
sleep 0.3
check "shortcuts work without the menu bar" "$(songs title | sed -n 3p)" \
	"$(st title)"
cmd Bmtp
check "keep on top" 1 "$(st 'mini on top')"
check "floats above all windows" 6 \
	"$(hey $HEY_APP get Feel of Window 0 | hey_result)"
cmd Bmtp
check "normal again" 0 "$(hey $HEY_APP get Feel of Window 0 | hey_result)"
"$SCRATCH/sendkey" $HEY_APP 0 cmd+shift+m
sleep 0.3
check "shift+command+M: full player" 0 "$(st mini)"
check "everything is back" 0 "$(view_hidden songs)"
check "menu bar back" 0 "$(view_hidden "menu bar")"
check "the old size" "$fullHeight" "$(window_height)"
check "document look again" 11 "$(hey $HEY_APP get Look of Window 0 | hey_result)"
hey $HEY_APP _WZM of Window 0 >/dev/null
sleep 0.3
check "the zoom button: mini player" 1 "$(st mini)"
hey $HEY_APP _WZM of Window 0 >/dev/null
sleep 0.3
check "the zoom button: full player" 0 "$(st mini)"
cmd Bmin
cmd Bcur
check "show current song: full player" 0 "$(st mini)"
cmd Bstp

echo "== removing from the library"
cmd Bsrs with 'select=int32(0)'
cmd Bsse with "select=int32($(row_of "Untitled Recording"))"
cmd Bdel with 'confirmed=bool(true)'
check "song removed" 15 "$(st library)"
check "list updated" 15 "$(st rows)"

echo "== smart playlists"
cmd Bsrs with 'select=int32(1)'
check "recently added" 15 "$(st rows)"
cmd Bsrs with 'select=int32(2)'
check_true "recently played: only played songs" \
	"[ $(st rows) -ge 3 ] && ! songs plays | grep -q -x 0"
cmd Bsrs with 'select=int32(3)'
check "top 25: most played first" "$(songs plays | sort -n -r | head -1)" \
	"$(songs plays | head -1)"

echo "== hiding the browser"
cmd Bsrs with 'select=int32(0)'
hey_select artists 1
check "artist filter" 6 "$(st rows)"
cmd Bbrw
check "browser hidden" 0 "$(st browser)"
check "no artist filter" 15 "$(st rows)"
cmd Bbrw
check "browser shown" 1 "$(st browser)"

echo "== a missing file"
mkdir -p "$SCRATCH/missing"
cp "$MUSIC/Misc/Untitled Recording.wav" "$SCRATCH/missing/Vanishing Act.wav"
cmd Bimp with "path=\"$SCRATCH/missing\""
wait_for importing 0 30
rm "$SCRATCH/missing/Vanishing Act.wav"
cmd Bsch with 'text="vanishing"'
cmd Bpsa with 'row=int32(0)'
wait_for state stopped 5
check "stops when nothing can be played" stopped "$(st state)"
check_true "says why" "st message | grep -q 'could not be played'"
cmd Bsch with 'text=""'

echo "== settings and library survive a restart"
cmd Bvol with 'volume=int32(55)'
cmd Bshf
cmd Brpt with 'mode=int32(1)'
cmd Bsrs with 'select=int32(4)'
cmd Beqc with 'preset="Jazz"'
cmd Beqm with 'name="Keep"'
cmd Beqw with 'show=bool(true)'
wait_for 'eq window' 1 5
cmd Bmin
quit_app
check "quits with the Equalizer window open" 0 "$(roster | grep -c '/BeTunes ')"
start_app
check "library" 16 "$(st library)"
check "playlists" 2 "$(st playlists)"
check "source" Summer "$(st source)"
check "playlist songs" "Launch|Daybreak" "$(songs title | paste -s -d '|')"
check "volume" 55 "$(st volume)"
check "shuffle" 1 "$(st shuffle)"
check "repeat" 1 "$(st repeat)"
check "equalizer on" 1 "$(st eq)"
check "equalizer preset" Keep "$(st 'eq preset')"
check "own presets" 1 "$(st 'eq user presets')"
check "equalizer values" "4.000" "$(eq_gain 0)"
wait_for 'eq window' 1 5
check "Equalizer window open again" 1 "$(st 'eq window')"
check "the mini player again" 1 "$(st mini)"
check_true "mini size" "[ $(window_height) -lt 120 ]"
cmd Bmin
check "full size" "$fullHeight" "$(window_height)"
cmd Bsrs with 'select=int32(0)'
check "play counts" 1 "$(song_field "Endless Day" plays)"
cmd Bshf
cmd Brpt with 'mode=int32(0)'

if [ -n "$SCREENSHOT" ]; then
	cmd Bpsa with 'row=int32(3)'
	hey $HEY_APP set Active of Window 0 to "bool(true)" >/dev/null
	sleep 1
	take_screenshot "$SCREENSHOT"
fi
