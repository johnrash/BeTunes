/*
 * The music library: songs, playlists, play counts. Lives in the window
 * thread; only SongIdForPath() may be called from other threads (the
 * importer).
 */
#ifndef LIBRARY_H
#define LIBRARY_H


#include <Collator.h>
#include <Locker.h>
#include <String.h>

#include <time.h>

#include <map>
#include <set>
#include <string>
#include <vector>


struct Song {
								Song();

			int32				id;
			BString				path;
			BString				title;
			BString				artist;
			BString				album;
			BString				genre;
			BString				kind;
			int32				track;
			int32				disc;
			int32				year;
			bigtime_t			duration;
			off_t				size;
			int32				bitRate;
				// kbit/s
			int32				playCount;
			time_t				lastPlayed;
			time_t				dateAdded;

			// derived from the above, not stored
			BString				titleKey;
			BString				artistKey;
			BString				albumKey;
			BString				genreKey;
			BString				kindKey;
				// collation keys for sorting
			std::string			searchTitle;
			std::string			searchArtist;
			std::string			searchAlbum;
			std::string			searchGenre;
				// folded for searching
};


enum source_kind {
	SOURCE_LIBRARY = 0,
	SOURCE_RECENTLY_ADDED,
	SOURCE_RECENTLY_PLAYED,
	SOURCE_TOP_25,
	SOURCE_PLAYLIST
};


struct Playlist {
			int32				id;
			BString				name;
			std::vector<int32>	songs;
};


class Library {
public:
								Library();
								~Library();

			status_t			Load(const char* path);
			status_t			Save();
			bool				IsDirty() const { return fDirty; }
			void				SetDirty() { fDirty = true; }

			int32				CountSongs() const
									{ return (int32)fSongs.size(); }
			Song*				SongAt(int32 index) const;
			Song*				SongById(int32 id) const;
	// Thread safe: -1 if the file is not in the library.
			int32				SongIdForPath(const char* path) const;

	// Takes ownership. Assigns an id and the date added. Returns NULL (and
	// deletes the song) if the file is already in the library.
			Song*				AddSong(Song* song);
			void				RemoveSongs(const std::set<int32>& ids);
	// Call after changing the song's text fields.
			void				UpdateKeys(Song* song);
			void				SongPlayed(Song* song);

			int32				CountPlaylists() const
									{ return (int32)fPlaylists.size(); }
			Playlist*			PlaylistAt(int32 index) const;
			Playlist*			PlaylistById(int32 id) const;
			Playlist*			AddPlaylist(const char* name);
			void				RemovePlaylist(int32 id);
			BString				UniquePlaylistName(const char* base) const;

	// The songs of a source, in the source's own order.
			void				GetSongs(source_kind kind, int32 playlist,
									std::vector<int32>& songs) const;
	static	const char*			SourceName(source_kind kind);

	// Total duration and size of songs.
			void				GetTotals(const std::vector<int32>& songs,
									bigtime_t& duration, off_t& size) const;

private:
	mutable	BLocker				fPathLock;
				// protects fByPath for the importer thread
			std::vector<Song*>	fSongs;
			std::map<int32, Song*> fById;
			std::map<std::string, int32> fByPath;
			std::vector<Playlist*> fPlaylists;
			int32				fNextSongId;
			int32				fNextPlaylistId;
			BString				fPath;
			bool				fDirty;
			BCollator			fCollator;
};


#endif	// LIBRARY_H
