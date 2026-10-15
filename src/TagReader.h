/*
 * Reads what BeTunes shows about a song from its file: the tags (through
 * the Media Kit, an ID3v1 fallback and the BFS audio attributes), the
 * duration and the format.
 */
#ifndef TAG_READER_H
#define TAG_READER_H


#include <Entry.h>
#include <Message.h>

#include "Library.h"


// Whether a file looks like audio, from its name or MIME type (no
// sniffing: this is called for every file of the folders being imported).
bool is_audio_file(const char* name, const char* mimeType);

// Fills in everything but the id, play counts and date added. Fails for
// files that the Media Kit cannot read or that have no audio track.
status_t read_song_info(const entry_ref& ref, const char* path, Song& song);

void archive_song(const Song& song, BMessage& archive);
Song* unarchive_song(const BMessage& archive);


#endif	// TAG_READER_H
