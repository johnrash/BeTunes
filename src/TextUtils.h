/*
 * Text helpers that do not depend on the Haiku API (unit tested on any
 * system by tests/unit-tests.cpp).
 */
#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H


#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>


// Lower case without diacritics ("Jökulsárlón" -> "jokulsarlon") for
// searching. Covers Latin-1, Latin Extended-A, Greek and Cyrillic; other
// characters are kept as they are.
std::string fold_for_search(const char* text);

// The words of a search string, folded. Searching matches songs that
// contain every word.
std::vector<std::string> search_words(const char* text);
bool contains_all_words(const std::string& folded,
	const std::vector<std::string>& words);

// "3/12" -> 3, " 7" -> 7, "" -> 0
int parse_leading_int(const char* text);
// "2004-05-01" -> 2004, "May 2004" -> 2004, "" -> 0
int parse_year(const char* text);

// Genre strings from ID3 tags may be "(17)", "17" or "(17)Rock".
std::string genre_from_tag(const char* text);
const char* id3v1_genre(int index);

struct Id3v1Tag {
	std::string	title;
	std::string	artist;
	std::string	album;
	std::string	genre;
	int			year;
	int			track;
};

// Parses the 128 byte ID3v1 tag at the end of an MP3 file.
bool parse_id3v1(const uint8_t* data, size_t size, Id3v1Tag& tag);

// ISO-8859-1 to UTF-8, without trailing spaces and NULs.
std::string latin1_to_utf8(const char* data, size_t length);
bool is_valid_utf8(const char* text);
// The text itself if it is valid UTF-8, otherwise read as ISO-8859-1 (old
// tags often are).
std::string ensure_utf8(const char* text);

// "3:07", "1:02:03"
std::string format_duration(int64_t microseconds);

// The name without a leading "The " for sorting artists like iTunes did.
const char* skip_article(const char* name);


#endif	// TEXT_UTILS_H
