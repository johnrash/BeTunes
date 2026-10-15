#!/bin/bash
# Makes a small test music library with tagged files in several formats
# (needs ffmpeg with libmp3lame, libvorbis, libopus, flac and aac encoders).
#   bash tests/make-test-music.sh /tmp/music
# Every song is a few seconds of a chord so playback is audible but tests
# that wait for the end of a song stay quick.
set -e
OUT=${1:-/tmp/BeTunesMusic}
SECONDS_PER_SONG=${SECONDS_PER_SONG:-6}
mkdir -p "$OUT"

# song <file> <frequency> <artist> <album> <title> <track> <genre> <year> [extra ffmpeg args...]
song() {
	local file="$OUT/$1" freq=$2 artist=$3 album=$4 title=$5 track=$6 genre=$7 year=$8
	shift 8
	mkdir -p "$(dirname "$file")"
	local meta=()
	[ -n "$artist" ] && meta+=(-metadata "artist=$artist")
	[ -n "$album" ] && meta+=(-metadata "album=$album")
	[ -n "$title" ] && meta+=(-metadata "title=$title")
	[ -n "$track" ] && meta+=(-metadata "track=$track")
	[ -n "$genre" ] && meta+=(-metadata "genre=$genre")
	[ -n "$year" ] && meta+=(-metadata "date=$year")
	ffmpeg -loglevel error -y \
		-f lavfi -i "sine=frequency=$freq:duration=$SECONDS_PER_SONG:sample_rate=44100" \
		-f lavfi -i "sine=frequency=$(( freq * 5 / 4 )):duration=$SECONDS_PER_SONG:sample_rate=44100" \
		-filter_complex "[0][1]amerge=inputs=2,volume=0.3,afade=t=in:d=0.3,afade=t=out:st=$((SECONDS_PER_SONG - 1)):d=1" \
		"${meta[@]}" "$@" "$file"
}

# MP3 with ID3v2.3 tags
song "Aurora Lane/Northern Lights/01 Polar Night.mp3" 220 "Aurora Lane" "Northern Lights" "Polar Night" "1/4" "Ambient" 2004 -c:a libmp3lame -b:a 128k -id3v2_version 3
song "Aurora Lane/Northern Lights/02 Green Curtain.mp3" 247 "Aurora Lane" "Northern Lights" "Green Curtain" "2/4" "Ambient" 2004 -c:a libmp3lame -b:a 128k -id3v2_version 3
song "Aurora Lane/Northern Lights/03 Solar Wind.mp3" 262 "Aurora Lane" "Northern Lights" "Solar Wind" "3/4" "Ambient" 2004 -c:a libmp3lame -b:a 192k -id3v2_version 3
song "Aurora Lane/Northern Lights/04 Magnetic North.mp3" 294 "Aurora Lane" "Northern Lights" "Magnetic North" "4/4" "Ambient" 2004 -c:a libmp3lame -b:a 192k -id3v2_version 3
# Ogg Vorbis
song "Aurora Lane/Midnight Sun/01 Daybreak.ogg" 330 "Aurora Lane" "Midnight Sun" "Daybreak" "1" "Ambient" 2006 -c:a libvorbis -q:a 3
song "Aurora Lane/Midnight Sun/02 Endless Day.ogg" 349 "Aurora Lane" "Midnight Sun" "Endless Day" "2" "Ambient" 2006 -c:a libvorbis -q:a 3
# FLAC
song "The Bit Shifters/Square Waves/01 Overflow.flac" 392 "The Bit Shifters" "Square Waves" "Overflow" "1" "Electronic" 2005 -c:a flac
song "The Bit Shifters/Square Waves/02 Carry Flag.flac" 440 "The Bit Shifters" "Square Waves" "Carry Flag" "2" "Electronic" 2005 -c:a flac
song "The Bit Shifters/Square Waves/03 Interrupt Vector.flac" 494 "The Bit Shifters" "Square Waves" "Interrupt Vector" "3" "Electronic" 2005 -c:a flac
# AAC in MP4
song "Cosmic Drift/Orbit/01 Launch.m4a" 523 "Cosmic Drift" "Orbit" "Launch" "1" "Rock" 2003 -c:a aac -b:a 128k
song "Cosmic Drift/Orbit/02 Zero Gravity.m4a" 587 "Cosmic Drift" "Orbit" "Zero Gravity" "2" "Rock" 2003 -c:a aac -b:a 128k
# Opus
song "Cosmic Drift/Re-Entry/01 Heat Shield.opus" 659 "Cosmic Drift" "Re-Entry" "Heat Shield" "1" "Rock" 2007 -c:a libopus -b:a 96k
# MP3 with ID3v2.4 and non-ASCII text
song "Sigrún Ólafsdóttir/Ísland/01 Jökulsárlón.mp3" 698 "Sigrún Ólafsdóttir" "Ísland" "Jökulsárlón" "1" "Folk" 2005 -c:a libmp3lame -b:a 160k -id3v2_version 4
song "Sigrún Ólafsdóttir/Ísland/02 Þingvellir.mp3" 784 "Sigrún Ólafsdóttir" "Ísland" "Þingvellir" "2" "Folk" 2005 -c:a libmp3lame -b:a 160k -id3v2_version 4
# MP3 with an ID3v1.1 tag only (ffmpeg does not write one without ID3v2:
# append it by hand). Genre 17 is "Rock", the album is Latin-1 "Café".
song "Misc/Old Tagger.mp3" 880 "" "" "" "" "" "" -c:a libmp3lame -b:a 128k -id3v2_version 0 -map_metadata -1
python3 - "$OUT/Misc/Old Tagger.mp3" <<'PYTHON'
import sys
def field(text, size):
    return text.encode("latin-1").ljust(size, b"\0")
tag = (b"TAG" + field("Old Tagger", 30) + field("Retro Tagger", 30)
       + field("Caf\u00e9 Classics", 30) + b"1998" + field("", 28)
       + bytes([0, 7, 17]))
assert len(tag) == 128
with open(sys.argv[1], "ab") as f:
    f.write(tag)
PYTHON
# WAV without tags: title comes from the file name
song "Misc/Untitled Recording.wav" 988 "" "" "" "" "" "" -c:a pcm_s16le
echo "Test music in $OUT"
