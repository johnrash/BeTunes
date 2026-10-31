/*
 * The music library: songs, playlists, play counts. Lives in the window
 * thread; only SongIdForPath() may be called from other threads (the
 * importer). The songs and playlists of the connected shared libraries are
 * here too, apart from the user's: they are never saved, never counted as
 * played, and go when their share is disconnected.
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
			int32				share;
				// the shared library the song is on, 0 for the user's own

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
	SOURCE_PLAYLIST,
	SOURCE_SHARE,
		// a shared library: the playlist id is the share's
	SOURCE_SHARE_PLAYLIST,
	SOURCE_KIND_COUNT
};


struct Playlist {
			int32				id;
			BString				name;
			std::vector<int32>	songs;
			int32				share = 0;
};


class Library {
public:
								Library();
								~Library();

			status_t			Load(const char* path);
			status_t			Save();
			bool				IsDirty() const { return fDirty; }
			void				SetDirty() { fDirty = true; }

	// The user's own songs; SongById() finds shared ones too.
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

	// Takes the songs (the vector's entries become NULL) and gives their
	// ids, in the same order.
			void				AddSharedSongs(int32 share,
									std::vector<Song*>& songs,
									std::vector<int32>& ids);
			Playlist*			AddSharedPlaylist(int32 share,
									const char* name,
									const std::vector<int32>& songs);
			void				GetSharedPlaylists(int32 share,
									std::vector<const Playlist*>& playlists)
									const;
			Playlist*			SharedPlaylistById(int32 id) const;
			int32				CountSharedSongs(int32 share) const;
			void				RemoveShare(int32 share);

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

			std::map<int32, std::vector<Song*> > fSharedSongs;
			std::vector<Playlist*> fSharedPlaylists;
			int32				fNextSharedId;
				// for shared songs and playlists, far from the saved ids
			BString				fPath;
			bool				fDirty;
			BCollator			fCollator;
};


#endif	// LIBRARY_H
