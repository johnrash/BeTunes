/*
 * Writing ID3 tags into MP3 files (the Media Kit only reads tags). Does not
 * depend on the Haiku API (unit tested by tests/unit-tests.cpp, and against
 * ffmpeg by tests/tag-test.sh).
 *
 * The ID3v2 tag is updated in its version (2.3 or 2.4; a 2.2 tag becomes
 * 2.3, a file without a tag gets a 2.3 one); every frame that is not
 * changed is kept as it is (pictures, comments, ...). An ID3v1 tag at the
 * end of the file is updated as well. When the new tag fits into the space
 * of the old one it is written in place; otherwise the file is copied with
 * the new tag and the copy replaces the original, so the audio data is
 * never left half-written.
 */
#ifndef ID3_TAG_H
#define ID3_TAG_H


#include <stddef.h>
#include <stdint.h>

#include <map>
#include <string>


enum tag_field {
	TAG_TITLE = 0,
	TAG_ARTIST,
	TAG_ALBUM,
	TAG_GENRE,
	TAG_YEAR,
	TAG_TRACK,
	TAG_DISC,
	TAG_FIELD_COUNT
};


// "title", "artist", "album", "genre", "year", "track", "disc"
extern const char* const kTagFieldNames[TAG_FIELD_COUNT];


// The fields to change. An empty value removes the field. Year, track and
// disc are decimal numbers (a track or disc total in the file is kept).
struct TagChanges {
								TagChanges();

			void				Set(tag_field field, const std::string& value);
			bool				IsEmpty() const;

			bool				set[TAG_FIELD_COUNT];
			std::string			value[TAG_FIELD_COUNT];
};


// Called when the copy of a file is complete, before it replaces the
// original (on Haiku, to copy the file's attributes). Returns 0 or an error
// code.
typedef int (*tag_copy_hook)(const char* original, const char* copy,
	void* cookie);

// Returns 0, or an errno style error code and a message in "error". The
// file is not touched if its tag cannot be read safely.
int write_mp3_tags(const char* path, const TagChanges& changes,
	tag_copy_hook hook, void* cookie, std::string& error);

// The text frames of an ID3v2 tag (in UTF-8, 2.2 frame IDs converted), for
// checking what was written. "version" gets 2, 3 or 4.
bool parse_id3v2_text(const uint8_t* data, size_t size,
	std::map<std::string, std::string>& frames, int* version = NULL);
bool read_id3v2_text(const char* path,
	std::map<std::string, std::string>& frames, int* version = NULL);


#endif	// ID3_TAG_H
