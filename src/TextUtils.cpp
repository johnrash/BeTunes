/*
 * Text helpers that do not depend on the Haiku API.
 */
#include "TextUtils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>


namespace {


// Base letters of U+00C0..U+00FF; '1' is "ae", '2' is "th", '3' is "ss",
// '*' keeps the character.
const char kLatin1[] =
	"aaaaaa1ceeeeiiii"	// C0..CF
	"dnooooo*ouuuuy23"	// D0..DF
	"aaaaaa1ceeeeiiii"	// E0..EF
	"dnooooo*ouuuuy2y";	// F0..FF

// Base letters of U+0100..U+017F; '1' is "ij", '2' is "oe".
const char kLatinExtendedA[] =
	"aaaaaacccccccc"	// 0100..010D
	"dddd"				// 010E..0111
	"eeeeeeeeee"		// 0112..011B
	"gggggggg"			// 011C..0123
	"hhhh"				// 0124..0127
	"iiiiiiiiii"		// 0128..0131
	"11"				// 0132..0133
	"jj"				// 0134..0135
	"kkk"				// 0136..0138
	"llllllllll"		// 0139..0142
	"nnnnnnnnn"			// 0143..014B
	"oooooo"			// 014C..0151
	"22"				// 0152..0153
	"rrrrrr"			// 0154..0159
	"ssssssss"			// 015A..0161
	"tttttt"			// 0162..0167
	"uuuuuuuuuuuu"		// 0168..0173
	"ww"				// 0174..0175
	"yyy"				// 0176..0178
	"zzzzzz"			// 0179..017E
	"s";				// 017F


uint32_t
decode_utf8(const unsigned char*& p)
{
	uint32_t c = *p++;
	int extra;
	if (c < 0x80)
		return c;
	if ((c & 0xe0) == 0xc0) {
		c &= 0x1f;
		extra = 1;
	} else if ((c & 0xf0) == 0xe0) {
		c &= 0x0f;
		extra = 2;
	} else if ((c & 0xf8) == 0xf0) {
		c &= 0x07;
		extra = 3;
	} else
		return 0xfffd;

	for (; extra > 0; extra--) {
		if ((*p & 0xc0) != 0x80)
			return 0xfffd;
		c = (c << 6) | (*p++ & 0x3f);
	}
	return c;
}


void
append_utf8(std::string& out, uint32_t c)
{
	if (c < 0x80)
		out += (char)c;
	else if (c < 0x800) {
		out += (char)(0xc0 | (c >> 6));
		out += (char)(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		out += (char)(0xe0 | (c >> 12));
		out += (char)(0x80 | ((c >> 6) & 0x3f));
		out += (char)(0x80 | (c & 0x3f));
	} else {
		out += (char)(0xf0 | (c >> 18));
		out += (char)(0x80 | ((c >> 12) & 0x3f));
		out += (char)(0x80 | ((c >> 6) & 0x3f));
		out += (char)(0x80 | (c & 0x3f));
	}
}


const char* const kGenres[] = {
	"Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge",
	"Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B",
	"Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
	"Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient",
	"Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical",
	"Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
	"AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative",
	"Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic", "Darkwave",
	"Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream",
	"Southern Rock", "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap",
	"Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave",
	"Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal",
	"Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll",
	"Hard Rock", "Folk", "Folk-Rock", "National Folk", "Swing", "Fast Fusion",
	"Bebop", "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde",
	"Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock",
	"Slow Rock", "Big Band", "Chorus", "Easy Listening", "Acoustic", "Humour",
	"Speech", "Chanson", "Opera", "Chamber Music", "Sonata", "Symphony",
	"Booty Bass", "Primus", "Porn Groove", "Satire", "Slow Jam", "Club",
	"Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul",
	"Freestyle", "Duet", "Punk Rock", "Drum Solo", "A Cappella", "Euro-House",
	"Dance Hall", "Goa", "Drum & Bass", "Club-House", "Hardcore", "Terror",
	"Indie", "BritPop", "Negerpunk", "Polsk Punk", "Beat",
	"Christian Gangsta Rap", "Heavy Metal", "Black Metal", "Crossover",
	"Contemporary Christian", "Christian Rock", "Merengue", "Salsa",
	"Thrash Metal", "Anime", "JPop", "Synthpop"
};


}	// namespace


std::string
fold_for_search(const char* text)
{
	std::string out;
	if (text == NULL)
		return out;

	const unsigned char* p = (const unsigned char*)text;
	while (*p != '\0') {
		uint32_t c = decode_utf8(p);
		if (c < 0x80) {
			out += (char)tolower((int)c);
		} else if (c >= 0xc0 && c <= 0xff) {
			char base = kLatin1[c - 0xc0];
			if (base == '1')
				out += "ae";
			else if (base == '2')
				out += "th";
			else if (base == '3')
				out += "ss";
			else if (base == '*')
				append_utf8(out, c);
			else
				out += base;
		} else if (c >= 0x100 && c <= 0x17f) {
			char base = kLatinExtendedA[c - 0x100];
			if (base == '1')
				out += "ij";
			else if (base == '2')
				out += "oe";
			else
				out += base;
		} else if (c >= 0x391 && c <= 0x3a9 && c != 0x3a2) {
			append_utf8(out, c + 0x20);
		} else if (c >= 0x410 && c <= 0x42f) {
			append_utf8(out, c + 0x20);
		} else if (c >= 0x400 && c <= 0x40f) {
			append_utf8(out, c + 0x50);
		} else if (c == 0x2018 || c == 0x2019) {
			out += '\'';
		} else if (c == 0x201c || c == 0x201d) {
			out += '"';
		} else
			append_utf8(out, c);
	}
	return out;
}


std::vector<std::string>
search_words(const char* text)
{
	std::vector<std::string> words;
	std::string folded = fold_for_search(text);
	size_t start = 0;
	while (start < folded.size()) {
		while (start < folded.size() && isspace((unsigned char)folded[start]))
			start++;
		size_t end = start;
		while (end < folded.size() && !isspace((unsigned char)folded[end]))
			end++;
		if (end > start)
			words.push_back(folded.substr(start, end - start));
		start = end;
	}
	return words;
}


bool
contains_all_words(const std::string& folded,
	const std::vector<std::string>& words)
{
	for (size_t i = 0; i < words.size(); i++) {
		if (folded.find(words[i]) == std::string::npos)
			return false;
	}
	return true;
}


int
parse_leading_int(const char* text)
{
	if (text == NULL)
		return 0;
	while (isspace((unsigned char)*text))
		text++;
	int value = 0;
	while (isdigit((unsigned char)*text) && value < 100000000)
		value = value * 10 + (*text++ - '0');
	return value;
}


int
parse_year(const char* text)
{
	if (text == NULL)
		return 0;
	for (const char* p = text; *p != '\0'; p++) {
		if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])
			&& isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])
			&& !isdigit((unsigned char)p[4])) {
			return (p[0] - '0') * 1000 + (p[1] - '0') * 100
				+ (p[2] - '0') * 10 + (p[3] - '0');
		}
		while (isdigit((unsigned char)*p))
			p++;
		if (*p == '\0')
			break;
	}
	return 0;
}


const char*
id3v1_genre(int index)
{
	if (index < 0 || index >= (int)(sizeof(kGenres) / sizeof(kGenres[0])))
		return NULL;
	return kGenres[index];
}


std::string
genre_from_tag(const char* text)
{
	if (text == NULL)
		return std::string();

	// "(17)" or "(17)Rock": a refinement after the number wins
	if (text[0] == '(' && isdigit((unsigned char)text[1])) {
		const char* end = strchr(text, ')');
		if (end != NULL) {
			if (end[1] != '\0')
				return std::string(end + 1);
			const char* name = id3v1_genre(atoi(text + 1));
			if (name != NULL)
				return std::string(name);
		}
	}

	// "17"
	const char* p = text;
	while (isdigit((unsigned char)*p))
		p++;
	if (p != text && *p == '\0') {
		const char* name = id3v1_genre(atoi(text));
		if (name != NULL)
			return std::string(name);
	}
	return std::string(text);
}


std::string
latin1_to_utf8(const char* data, size_t length)
{
	size_t end = 0;
	while (end < length && data[end] != '\0')
		end++;
	while (end > 0 && data[end - 1] == ' ')
		end--;

	std::string out;
	for (size_t i = 0; i < end; i++)
		append_utf8(out, (unsigned char)data[i]);
	return out;
}


bool
is_valid_utf8(const char* text)
{
	const unsigned char* p = (const unsigned char*)text;
	while (*p != '\0') {
		int extra;
		if (*p < 0x80)
			extra = 0;
		else if ((*p & 0xe0) == 0xc0 && *p >= 0xc2)
			extra = 1;
		else if ((*p & 0xf0) == 0xe0)
			extra = 2;
		else if ((*p & 0xf8) == 0xf0 && *p <= 0xf4)
			extra = 3;
		else
			return false;
		p++;
		for (; extra > 0; extra--, p++) {
			if ((*p & 0xc0) != 0x80)
				return false;
		}
	}
	return true;
}


std::string
ensure_utf8(const char* text)
{
	if (text == NULL)
		return std::string();
	if (is_valid_utf8(text))
		return std::string(text);
	std::string out;
	for (const unsigned char* p = (const unsigned char*)text; *p != '\0'; p++)
		append_utf8(out, *p);
	return out;
}


bool
parse_id3v1(const uint8_t* data, size_t size, Id3v1Tag& tag)
{
	if (size < 128 || memcmp(data, "TAG", 3) != 0)
		return false;

	const char* raw = (const char*)data;
	tag.title = latin1_to_utf8(raw + 3, 30);
	tag.artist = latin1_to_utf8(raw + 33, 30);
	tag.album = latin1_to_utf8(raw + 63, 30);
	char year[5];
	memcpy(year, raw + 93, 4);
	year[4] = '\0';
	tag.year = parse_year(year);
	// ID3v1.1: a zero byte before the last comment byte marks a track number
	tag.track = data[125] == 0 ? data[126] : 0;
	const char* genre = id3v1_genre(data[127]);
	tag.genre = genre != NULL ? genre : "";
	return true;
}


std::string
format_duration(int64_t microseconds)
{
	if (microseconds < 0)
		microseconds = 0;
	int64_t seconds = microseconds / 1000000;
	char buffer[32];
	if (seconds >= 3600) {
		snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", (int)(seconds / 3600),
			(int)(seconds / 60 % 60), (int)(seconds % 60));
	} else {
		snprintf(buffer, sizeof(buffer), "%d:%02d", (int)(seconds / 60),
			(int)(seconds % 60));
	}
	return buffer;
}


const char*
skip_article(const char* name)
{
	if (name != NULL && strncasecmp(name, "the ", 4) == 0 && name[4] != '\0')
		return name + 4;
	return name;
}
