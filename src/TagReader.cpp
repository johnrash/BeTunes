/*
 * Reading song information from audio files.
 */
#include "TagReader.h"

#include <File.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Node.h>
#include <Path.h>
#include <TypeConstants.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "TextUtils.h"


namespace {


struct Format {
	const char*	extension;
	const char*	kind;
};

const Format kFormats[] = {
	{ "mp3", "MPEG audio file" },
	{ "mp2", "MPEG audio file" },
	{ "mpga", "MPEG audio file" },
	{ "ogg", "Ogg Vorbis file" },
	{ "oga", "Ogg Vorbis file" },
	{ "opus", "Opus audio file" },
	{ "spx", "Speex audio file" },
	{ "flac", "FLAC audio file" },
	{ "m4a", "AAC audio file" },
	{ "m4b", "AAC audio file" },
	{ "aac", "AAC audio file" },
	{ "wav", "WAV audio file" },
	{ "wave", "WAV audio file" },
	{ "aif", "AIFF audio file" },
	{ "aiff", "AIFF audio file" },
	{ "aifc", "AIFF audio file" },
	{ "au", "Sun audio file" },
	{ "snd", "Sun audio file" },
	{ "wma", "Windows Media audio file" },
	{ "ape", "Monkey's Audio file" },
	{ "wv", "WavPack audio file" },
	{ "mpc", "Musepack audio file" },
	{ "mka", "Matroska audio file" },
	{ "ac3", "AC-3 audio file" },
	{ "tta", "True Audio file" }
};


const char*
extension_of(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (dot == NULL || dot == name || strchr(dot, '/') != NULL)
		return NULL;
	return dot + 1;
}


const Format*
format_for(const char* name)
{
	const char* extension = extension_of(name);
	if (extension == NULL)
		return NULL;
	for (size_t i = 0; i < sizeof(kFormats) / sizeof(kFormats[0]); i++) {
		if (strcasecmp(extension, kFormats[i].extension) == 0)
			return &kFormats[i];
	}
	return NULL;
}


// Tags come from the file (MP3, FLAC, MP4) or from the audio track (Ogg),
// with the key spelling of the container: look everywhere, ignoring case.
class Tags {
public:
	Tags(BMediaFile& file, BMediaTrack* track)
	{
		file.GetMetaData(&fFile);
		if (track != NULL)
			track->GetMetaData(&fTrack);
	}

	const char* Find(const char* key) const
	{
		const char* value = _Find(fFile, key);
		if (value == NULL)
			value = _Find(fTrack, key);
		return value;
	}

	const char* Find(const char* key, const char* alternative) const
	{
		const char* value = Find(key);
		return value != NULL ? value : Find(alternative);
	}

private:
	static const char* _Find(const BMessage& message, const char* key)
	{
		char* name;
		type_code type;
		int32 count;
		for (int32 i = 0; message.GetInfo(B_STRING_TYPE, i, &name, &type,
				&count) == B_OK; i++) {
			if (strcasecmp(name, key) != 0)
				continue;
			const char* value;
			if (message.FindString(name, &value) == B_OK && value[0] != '\0')
				return value;
		}
		return NULL;
	}

	BMessage	fFile;
	BMessage	fTrack;
};


// Tag text, trimmed and in UTF-8 (the Media Kit passes ID3v1 tags on as
// they are, often in ISO-8859-1).
BString
trimmed(const char* text)
{
	BString string(ensure_utf8(text).c_str());
	string.Trim();
	return string;
}


void
read_id3v1(const char* path, Song& song)
{
	BFile file(path, B_READ_ONLY);
	off_t size;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size < 128)
		return;

	uint8 data[128];
	if (file.ReadAt(size - 128, data, sizeof(data)) != (ssize_t)sizeof(data))
		return;

	Id3v1Tag tag;
	if (!parse_id3v1(data, sizeof(data), tag))
		return;

	if (song.title.IsEmpty())
		song.title = tag.title.c_str();
	if (song.artist.IsEmpty())
		song.artist = tag.artist.c_str();
	if (song.album.IsEmpty())
		song.album = tag.album.c_str();
	if (song.genre.IsEmpty())
		song.genre = tag.genre.c_str();
	if (song.year == 0)
		song.year = tag.year;
	if (song.track == 0)
		song.track = tag.track;
}


// The audio attributes some Haiku tools write.
void
read_attributes(const char* path, Song& song)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return;

	BString value;
	if (song.title.IsEmpty() && node.ReadAttrString("Media:Title", &value)
			== B_OK)
		song.title = trimmed(value);
	if (song.artist.IsEmpty() && node.ReadAttrString("Audio:Artist", &value)
			== B_OK)
		song.artist = trimmed(value);
	if (song.album.IsEmpty() && node.ReadAttrString("Audio:Album", &value)
			== B_OK)
		song.album = trimmed(value);
	if (song.genre.IsEmpty() && node.ReadAttrString("Media:Genre", &value)
			== B_OK)
		song.genre = trimmed(value);

	int32 number;
	if (song.track == 0 && node.ReadAttr("Audio:Track", B_INT32_TYPE, 0,
			&number, sizeof(number)) == (ssize_t)sizeof(number))
		song.track = number;
	if (song.year == 0 && node.ReadAttr("Media:Year", B_INT32_TYPE, 0,
			&number, sizeof(number)) == (ssize_t)sizeof(number))
		song.year = number;
}


}	// namespace


bool
is_audio_file(const char* name, const char* mimeType)
{
	if (format_for(name) != NULL)
		return true;
	if (mimeType == NULL || strncasecmp(mimeType, "audio/", 6) != 0)
		return false;
	// playlists have audio types, too
	BString type(mimeType);
	return type.IFindFirst("mpegurl") < 0 && type.IFindFirst("scpls") < 0;
}


status_t
read_song_info(const entry_ref& ref, const char* path, Song& song)
{
	BMediaFile file(&ref);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	BMediaTrack* audio = NULL;
	for (int32 i = 0; i < file.CountTracks(); i++) {
		BMediaTrack* track = file.TrackAt(i);
		if (track == NULL)
			continue;
		media_format format;
		if (audio == NULL && track->EncodedFormat(&format) == B_OK
			&& (format.type == B_MEDIA_ENCODED_AUDIO
				|| format.type == B_MEDIA_RAW_AUDIO)) {
			audio = track;
			continue;
		}
		file.ReleaseTrack(track);
	}
	if (audio == NULL)
		return B_MEDIA_BAD_FORMAT;

	song.path = path;
	song.duration = audio->Duration();

	Tags tags(file, audio);
	song.title = trimmed(tags.Find("title"));
	song.artist = trimmed(tags.Find("artist", "album_artist"));
	if (song.artist.IsEmpty())
		song.artist = trimmed(tags.Find("albumartist", "performer"));
	song.album = trimmed(tags.Find("album"));
	song.genre = genre_from_tag(trimmed(tags.Find("genre"))).c_str();
	song.track = parse_leading_int(tags.Find("track", "tracknumber"));
	song.disc = parse_leading_int(tags.Find("disc", "discnumber"));
	song.year = parse_year(tags.Find("date", "year"));

	const Format* format = format_for(ref.name);
	media_codec_info codec;
	if (format != NULL)
		song.kind = format->kind;
	else if (audio->GetCodecInfo(&codec) == B_OK)
		song.kind = codec.pretty_name;
	else
		song.kind = "Audio file";
	if (audio->GetCodecInfo(&codec) == B_OK
		&& strstr(codec.pretty_name, "ALAC") != NULL)
		song.kind = "Apple Lossless audio file";

	file.ReleaseTrack(audio);

	if (format != NULL && (strcmp(format->extension, "mp3") == 0
			|| strcmp(format->extension, "mp2") == 0)
		&& (song.title.IsEmpty() || song.artist.IsEmpty()
			|| song.album.IsEmpty())) {
		// the Media Kit does not report ID3v1 tags
		read_id3v1(path, song);
	}
	read_attributes(path, song);

	if (song.title.IsEmpty()) {
		song.title = ref.name;
		int32 dot = song.title.FindLast('.');
		if (dot > 0)
			song.title.Truncate(dot);
	}

	BEntry entry(&ref);
	off_t size = 0;
	if (entry.GetSize(&size) == B_OK)
		song.size = size;
	if (song.duration > 0)
		song.bitRate = (int32)(song.size * 8 * 1000 / song.duration);
			// bytes * 8 / seconds / 1000

	return B_OK;
}


void
archive_song(const Song& song, BMessage& archive)
{
	archive.AddString("path", song.path);
	archive.AddString("title", song.title);
	archive.AddString("artist", song.artist);
	archive.AddString("album", song.album);
	archive.AddString("genre", song.genre);
	archive.AddString("kind", song.kind);
	archive.AddInt32("track", song.track);
	archive.AddInt32("disc", song.disc);
	archive.AddInt32("year", song.year);
	archive.AddInt64("duration", song.duration);
	archive.AddInt64("size", song.size);
	archive.AddInt32("bitrate", song.bitRate);
}


Song*
unarchive_song(const BMessage& archive)
{
	const char* path;
	if (archive.FindString("path", &path) != B_OK)
		return NULL;

	Song* song = new Song;
	song->path = path;
	song->title = archive.GetString("title", "");
	song->artist = archive.GetString("artist", "");
	song->album = archive.GetString("album", "");
	song->genre = archive.GetString("genre", "");
	song->kind = archive.GetString("kind", "");
	song->track = archive.GetInt32("track", 0);
	song->disc = archive.GetInt32("disc", 0);
	song->year = archive.GetInt32("year", 0);
	song->duration = archive.GetInt64("duration", 0);
	song->size = archive.GetInt64("size", 0);
	song->bitRate = archive.GetInt32("bitrate", 0);
	return song;
}
