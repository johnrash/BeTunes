#!/bin/bash
# Checks the ID3 writer against ffmpeg (on Linux, or anywhere with ffmpeg
# and ffprobe): edits real MP3 files and verifies that ffprobe reads the new
# tags, that a cover picture survives and that the audio is bit for bit the
# same.
#   bash tests/tag-test.sh
set -u
cd "$(dirname "$0")/.."
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FLAGS="-std=c++17 -Wall -Wextra -g"
[ "$(uname)" != "Haiku" ] && FLAGS="$FLAGS -fsanitize=address,undefined"
g++ $FLAGS -Isrc tests/tagtool.cpp src/Id3Tag.cpp src/TextUtils.cpp \
	-o "$WORK/tagtool" || exit 1

PASSED=0
FAILED=0
check() {
	if [ "$2" == "$3" ]; then
		PASSED=$((PASSED + 1))
		echo "ok   $1"
	else
		FAILED=$((FAILED + 1))
		echo "FAIL $1: expected '$2', got '$3'"
	fi
}

tag_of() {
	ffprobe -v error -show_entries format_tags="$2" -of default=nw=1:nk=1 \
		"$1" | head -1
}
audio_md5() {
	ffmpeg -v error -i "$1" -map 0:a -f md5 - 2>/dev/null
}
sine() {
	# sine <file> [ffmpeg options...]
	local file=$1
	shift
	ffmpeg -v error -y -f lavfi -i "sine=frequency=440:duration=3" "$@" \
		-c:a libmp3lame -b:a 128k "$file"
}

# a cover picture
ffmpeg -v error -y -f lavfi -i "color=c=blue:s=64x64" -frames:v 1 \
	"$WORK/cover.png"

echo "== ID3v2.3 with a cover"
sine "$WORK/v23.mp3.tmp" -metadata title="Before" -metadata artist="Old" \
	-metadata album="Album" -metadata track="3/12" -metadata date="2001" \
	-f mp3
ffmpeg -v error -y -i "$WORK/v23.mp3.tmp" -i "$WORK/cover.png" -map 0 -map 1 \
	-c copy -id3v2_version 3 -disposition:v attached_pic "$WORK/v23.mp3"
before=$(audio_md5 "$WORK/v23.mp3")
"$WORK/tagtool" "$WORK/v23.mp3" artist="Sigrún Ólafsdóttir" \
	album="東京 Nights" track=5 year=1999 genre="Ambient"
check "artist (UTF-16)" "Sigrún Ólafsdóttir" "$(tag_of "$WORK/v23.mp3" artist)"
check "album (UTF-16)" "東京 Nights" "$(tag_of "$WORK/v23.mp3" album)"
check "title kept" "Before" "$(tag_of "$WORK/v23.mp3" title)"
check "track keeps its total" "5/12" "$(tag_of "$WORK/v23.mp3" track)"
check "year" "1999" "$(tag_of "$WORK/v23.mp3" date)"
check "genre" "Ambient" "$(tag_of "$WORK/v23.mp3" genre)"
check "cover kept" "1" "$(ffprobe -v error -select_streams v \
	-show_entries stream_disposition=attached_pic -of default=nw=1:nk=1 \
	"$WORK/v23.mp3")"
check "audio unchanged" "$before" "$(audio_md5 "$WORK/v23.mp3")"
size=$(stat -c %s "$WORK/v23.mp3")
"$WORK/tagtool" "$WORK/v23.mp3" title="After"
check "second change in place" "$size" "$(stat -c %s "$WORK/v23.mp3")"
check "second change" "After" "$(tag_of "$WORK/v23.mp3" title)"
check "audio still unchanged" "$before" "$(audio_md5 "$WORK/v23.mp3")"

echo "== ID3v2.4"
sine "$WORK/v24.mp3" -metadata title="Four" -metadata date="2004-05-01" \
	-id3v2_version 4
before=$(audio_md5 "$WORK/v24.mp3")
"$WORK/tagtool" "$WORK/v24.mp3" title="Jökulsárlón" year=2010 disc=2
check "title (UTF-8)" "Jökulsárlón" "$(tag_of "$WORK/v24.mp3" title)"
check "year (TDRC)" "2010" "$(tag_of "$WORK/v24.mp3" date)"
check "disc" "2" "$(tag_of "$WORK/v24.mp3" disc)"
check "audio unchanged" "$before" "$(audio_md5 "$WORK/v24.mp3")"

echo "== no tag at all"
sine "$WORK/none.mp3" -map_metadata -1 -id3v2_version 0 -fflags +bitexact
before=$(audio_md5 "$WORK/none.mp3")
"$WORK/tagtool" "$WORK/none.mp3" title="New" artist="Somebody" track=1
check "title" "New" "$(tag_of "$WORK/none.mp3" title)"
check "artist" "Somebody" "$(tag_of "$WORK/none.mp3" artist)"
check "track" "1" "$(tag_of "$WORK/none.mp3" track)"
check "audio unchanged" "$before" "$(audio_md5 "$WORK/none.mp3")"

echo "== ID3v1 only"
sine "$WORK/v1.mp3" -map_metadata -1 -id3v2_version 0
python3 - "$WORK/v1.mp3" <<'PYTHON'
import sys
tag = (b"TAG" + b"Old Tagger".ljust(30, b"\0") + b"Retro".ljust(30, b"\0")
       + b"Version One".ljust(30, b"\0") + b"1998" + bytes(28)
       + bytes([0, 7, 17]))
open(sys.argv[1], "ab").write(tag)
PYTHON
before=$(audio_md5 "$WORK/v1.mp3")
"$WORK/tagtool" "$WORK/v1.mp3" album="Café Classics"
check "album" "Café Classics" "$(tag_of "$WORK/v1.mp3" album)"
check "ID3v1 updated (Latin-1)" "Café Classics" \
	"$(tail -c 128 "$WORK/v1.mp3" | head -c 93 | tail -c 30 | tr -d '\0' \
		| iconv -f latin1 -t utf-8)"
check "ID3v1 title kept" "Old Tagger" \
	"$(tail -c 125 "$WORK/v1.mp3" | head -c 30 | tr -d '\0')"
check "audio unchanged" "$before" "$(audio_md5 "$WORK/v1.mp3")"

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" == 0 ]
