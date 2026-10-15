/*
 * The music library. Stored as a flattened BMessage with one array per song
 * field (fast to load for many thousands of songs).
 */
#include "Library.h"

#include <Autolock.h>
#include <Entry.h>
#include <File.h>
#include <Locale.h>
#include <Message.h>

#include <algorithm>
#include <stdio.h>

#include "TextUtils.h"


static const int32 kLibraryVersion = 1;
static const time_t kRecentPeriod = 14 * 24 * 60 * 60;
	// "Recently Added" and "Recently Played": two weeks, like iTunes


Song::Song()
	:
	id(-1),
	track(0),
	disc(0),
	year(0),
	duration(0),
	size(0),
	bitRate(0),
	playCount(0),
	lastPlayed(0),
	dateAdded(0)
{
}


Library::Library()
	:
	fPathLock("library paths"),
	fNextSongId(1),
	fNextPlaylistId(1),
	fDirty(false)
{
	BLocale::Default()->GetCollator(&fCollator);
	fCollator.SetNumericSorting(true);
		// "Track 2" before "Track 10"
}


Library::~Library()
{
	for (size_t i = 0; i < fSongs.size(); i++)
		delete fSongs[i];
	for (size_t i = 0; i < fPlaylists.size(); i++)
		delete fPlaylists[i];
}


status_t
Library::Load(const char* path)
{
	fPath = path;

	BFile file(path, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	BMessage archive;
	status = archive.Unflatten(&file);
	if (status != B_OK) {
		fprintf(stderr, "BeTunes: cannot read library %s: %s\n", path,
			strerror(status));
		return status;
	}

	const char* songPath;
	for (int32 i = 0; archive.FindString("path", i, &songPath) == B_OK; i++) {
		Song* song = new Song;
		song->path = songPath;
		song->id = archive.GetInt32("id", i, -1);
		song->title = archive.GetString("title", i, "");
		song->artist = archive.GetString("artist", i, "");
		song->album = archive.GetString("album", i, "");
		song->genre = archive.GetString("genre", i, "");
		song->kind = archive.GetString("kind", i, "");
		song->track = archive.GetInt32("track", i, 0);
		song->disc = archive.GetInt32("disc", i, 0);
		song->year = archive.GetInt32("year", i, 0);
		song->duration = archive.GetInt64("duration", i, 0);
		song->size = archive.GetInt64("size", i, 0);
		song->bitRate = archive.GetInt32("bitrate", i, 0);
		song->playCount = archive.GetInt32("plays", i, 0);
		song->lastPlayed = (time_t)archive.GetInt64("played", i, 0);
		song->dateAdded = (time_t)archive.GetInt64("added", i, 0);

		if (song->id <= 0 || fById.find(song->id) != fById.end()
			|| fByPath.find(song->path.String()) != fByPath.end()) {
			delete song;
			continue;
		}
		UpdateKeys(song);
		fSongs.push_back(song);
		fById[song->id] = song;
		fByPath[song->path.String()] = song->id;
		fNextSongId = std::max(fNextSongId, song->id + 1);
	}

	BMessage playlistArchive;
	for (int32 i = 0; archive.FindMessage("playlist", i, &playlistArchive)
			== B_OK; i++) {
		Playlist* playlist = new Playlist;
		playlist->id = playlistArchive.GetInt32("id", -1);
		playlist->name = playlistArchive.GetString("name", "untitled playlist");
		int32 songId;
		for (int32 j = 0; playlistArchive.FindInt32("song", j, &songId) == B_OK;
				j++) {
			if (fById.find(songId) != fById.end())
				playlist->songs.push_back(songId);
		}
		if (playlist->id <= 0 || PlaylistById(playlist->id) != NULL) {
			delete playlist;
			continue;
		}
		fPlaylists.push_back(playlist);
		fNextPlaylistId = std::max(fNextPlaylistId, playlist->id + 1);
	}

	fNextSongId = std::max(fNextSongId, archive.GetInt32("next song", 1));
	fNextPlaylistId = std::max(fNextPlaylistId,
		archive.GetInt32("next playlist", 1));
	fDirty = false;
	return B_OK;
}


status_t
Library::Save()
{
	if (fPath.IsEmpty())
		return B_NO_INIT;

	BMessage archive;
	archive.AddInt32("version", kLibraryVersion);
	archive.AddInt32("next song", fNextSongId);
	archive.AddInt32("next playlist", fNextPlaylistId);
	for (size_t i = 0; i < fSongs.size(); i++) {
		const Song* song = fSongs[i];
		archive.AddInt32("id", song->id);
		archive.AddString("path", song->path);
		archive.AddString("title", song->title);
		archive.AddString("artist", song->artist);
		archive.AddString("album", song->album);
		archive.AddString("genre", song->genre);
		archive.AddString("kind", song->kind);
		archive.AddInt32("track", song->track);
		archive.AddInt32("disc", song->disc);
		archive.AddInt32("year", song->year);
		archive.AddInt64("duration", song->duration);
		archive.AddInt64("size", song->size);
		archive.AddInt32("bitrate", song->bitRate);
		archive.AddInt32("plays", song->playCount);
		archive.AddInt64("played", song->lastPlayed);
		archive.AddInt64("added", song->dateAdded);
	}
	for (size_t i = 0; i < fPlaylists.size(); i++) {
		const Playlist* playlist = fPlaylists[i];
		BMessage playlistArchive;
		playlistArchive.AddInt32("id", playlist->id);
		playlistArchive.AddString("name", playlist->name);
		for (size_t j = 0; j < playlist->songs.size(); j++)
			playlistArchive.AddInt32("song", playlist->songs[j]);
		archive.AddMessage("playlist", &playlistArchive);
	}

	// write a new file and replace the old one, so a crash cannot leave a
	// half written library behind
	BString temporaryPath(fPath);
	temporaryPath << ".new";
	BFile file(temporaryPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status == B_OK)
		status = archive.Flatten(&file);
	if (status == B_OK)
		status = file.Sync();
	file.Unset();
	if (status == B_OK) {
		BEntry entry(temporaryPath);
		status = entry.Rename(fPath, true);
	}
	if (status != B_OK) {
		fprintf(stderr, "BeTunes: cannot save library %s: %s\n",
			fPath.String(), strerror(status));
		return status;
	}
	fDirty = false;
	return B_OK;
}


Song*
Library::SongAt(int32 index) const
{
	if (index < 0 || index >= (int32)fSongs.size())
		return NULL;
	return fSongs[index];
}


Song*
Library::SongById(int32 id) const
{
	std::map<int32, Song*>::const_iterator found = fById.find(id);
	return found != fById.end() ? found->second : NULL;
}


int32
Library::SongIdForPath(const char* path) const
{
	BAutolock _(fPathLock);
	std::map<std::string, int32>::const_iterator found = fByPath.find(path);
	return found != fByPath.end() ? found->second : -1;
}


Song*
Library::AddSong(Song* song)
{
	BAutolock _(fPathLock);
	if (fByPath.find(song->path.String()) != fByPath.end()) {
		delete song;
		return NULL;
	}
	song->id = fNextSongId++;
	if (song->dateAdded == 0)
		song->dateAdded = time(NULL);
	UpdateKeys(song);
	fSongs.push_back(song);
	fById[song->id] = song;
	fByPath[song->path.String()] = song->id;
	fDirty = true;
	return song;
}


void
Library::RemoveSongs(const std::set<int32>& ids)
{
	if (ids.empty())
		return;

	BAutolock _(fPathLock);
	std::vector<Song*> kept;
	kept.reserve(fSongs.size());
	for (size_t i = 0; i < fSongs.size(); i++) {
		Song* song = fSongs[i];
		if (ids.find(song->id) == ids.end()) {
			kept.push_back(song);
			continue;
		}
		fById.erase(song->id);
		fByPath.erase(song->path.String());
		delete song;
	}
	fSongs.swap(kept);

	for (size_t i = 0; i < fPlaylists.size(); i++) {
		std::vector<int32>& songs = fPlaylists[i]->songs;
		std::vector<int32> remaining;
		for (size_t j = 0; j < songs.size(); j++) {
			if (ids.find(songs[j]) == ids.end())
				remaining.push_back(songs[j]);
		}
		songs.swap(remaining);
	}
	fDirty = true;
}


void
Library::UpdateKeys(Song* song)
{
	fCollator.GetSortKey(song->title, &song->titleKey);
	fCollator.GetSortKey(skip_article(song->artist), &song->artistKey);
	fCollator.GetSortKey(skip_article(song->album), &song->albumKey);
	fCollator.GetSortKey(song->genre, &song->genreKey);
	fCollator.GetSortKey(song->kind, &song->kindKey);
	song->searchTitle = fold_for_search(song->title);
	song->searchArtist = fold_for_search(song->artist);
	song->searchAlbum = fold_for_search(song->album);
	song->searchGenre = fold_for_search(song->genre);
}


void
Library::SongPlayed(Song* song)
{
	song->playCount++;
	song->lastPlayed = time(NULL);
	fDirty = true;
}


Playlist*
Library::PlaylistAt(int32 index) const
{
	if (index < 0 || index >= (int32)fPlaylists.size())
		return NULL;
	return fPlaylists[index];
}


Playlist*
Library::PlaylistById(int32 id) const
{
	for (size_t i = 0; i < fPlaylists.size(); i++) {
		if (fPlaylists[i]->id == id)
			return fPlaylists[i];
	}
	return NULL;
}


Playlist*
Library::AddPlaylist(const char* name)
{
	Playlist* playlist = new Playlist;
	playlist->id = fNextPlaylistId++;
	playlist->name = name;
	fPlaylists.push_back(playlist);
	fDirty = true;
	return playlist;
}


void
Library::RemovePlaylist(int32 id)
{
	for (size_t i = 0; i < fPlaylists.size(); i++) {
		if (fPlaylists[i]->id == id) {
			delete fPlaylists[i];
			fPlaylists.erase(fPlaylists.begin() + i);
			fDirty = true;
			return;
		}
	}
}


BString
Library::UniquePlaylistName(const char* base) const
{
	BString name(base);
	for (int32 number = 2; ; number++) {
		bool used = false;
		for (size_t i = 0; i < fPlaylists.size(); i++) {
			if (fPlaylists[i]->name == name) {
				used = true;
				break;
			}
		}
		if (!used)
			return name;
		name = base;
		name << " " << number;
	}
}


void
Library::GetSongs(source_kind kind, int32 playlistId,
	std::vector<int32>& songs) const
{
	songs.clear();
	time_t now = time(NULL);

	switch (kind) {
		case SOURCE_LIBRARY:
			for (size_t i = 0; i < fSongs.size(); i++)
				songs.push_back(fSongs[i]->id);
			break;

		case SOURCE_RECENTLY_ADDED:
		{
			std::vector<const Song*> found;
			for (size_t i = 0; i < fSongs.size(); i++) {
				if (fSongs[i]->dateAdded >= now - kRecentPeriod)
					found.push_back(fSongs[i]);
			}
			std::stable_sort(found.begin(), found.end(),
				[](const Song* a, const Song* b) {
					return a->dateAdded > b->dateAdded;
				});
			for (size_t i = 0; i < found.size(); i++)
				songs.push_back(found[i]->id);
			break;
		}

		case SOURCE_RECENTLY_PLAYED:
		{
			std::vector<const Song*> found;
			for (size_t i = 0; i < fSongs.size(); i++) {
				if (fSongs[i]->lastPlayed != 0
					&& fSongs[i]->lastPlayed >= now - kRecentPeriod)
					found.push_back(fSongs[i]);
			}
			std::stable_sort(found.begin(), found.end(),
				[](const Song* a, const Song* b) {
					return a->lastPlayed > b->lastPlayed;
				});
			for (size_t i = 0; i < found.size(); i++)
				songs.push_back(found[i]->id);
			break;
		}

		case SOURCE_TOP_25:
		{
			std::vector<const Song*> found;
			for (size_t i = 0; i < fSongs.size(); i++) {
				if (fSongs[i]->playCount > 0)
					found.push_back(fSongs[i]);
			}
			std::stable_sort(found.begin(), found.end(),
				[](const Song* a, const Song* b) {
					if (a->playCount != b->playCount)
						return a->playCount > b->playCount;
					return a->lastPlayed > b->lastPlayed;
				});
			for (size_t i = 0; i < found.size() && i < 25; i++)
				songs.push_back(found[i]->id);
			break;
		}

		case SOURCE_PLAYLIST:
		{
			const Playlist* playlist = PlaylistById(playlistId);
			if (playlist != NULL)
				songs = playlist->songs;
			break;
		}
	}
}


const char*
Library::SourceName(source_kind kind)
{
	switch (kind) {
		case SOURCE_LIBRARY:
			return "Library";
		case SOURCE_RECENTLY_ADDED:
			return "Recently Added";
		case SOURCE_RECENTLY_PLAYED:
			return "Recently Played";
		case SOURCE_TOP_25:
			return "Top 25 Most Played";
		case SOURCE_PLAYLIST:
			break;
	}
	return "";
}


void
Library::GetTotals(const std::vector<int32>& songs, bigtime_t& duration,
	off_t& size) const
{
	duration = 0;
	size = 0;
	for (size_t i = 0; i < songs.size(); i++) {
		const Song* song = SongById(songs[i]);
		if (song != NULL) {
			duration += song->duration;
			size += song->size;
		}
	}
}
