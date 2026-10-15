/*
 * The song list: a BColumnListView with one row per song, sortable by any
 * column (click a title; shift-click adds a secondary sort column), columns
 * can be moved, resized and hidden (right-click the titles). Songs can be
 * dragged to playlists and, in a playlist sorted by "#", to a new place.
 */
#ifndef SONG_LIST_H
#define SONG_LIST_H


#include <ColumnListView.h>
#include <ColumnTypes.h>

#include <set>
#include <vector>

#include "Library.h"


enum song_column {
	COLUMN_INDICATOR = 0,
	COLUMN_NUMBER,
	COLUMN_NAME,
	COLUMN_TIME,
	COLUMN_ARTIST,
	COLUMN_ALBUM,
	COLUMN_GENRE,
	COLUMN_TRACK,
	COLUMN_YEAR,
	COLUMN_PLAYS,
	COLUMN_LAST_PLAYED,
	COLUMN_DATE_ADDED,
	COLUMN_KIND,
	COLUMN_BIT_RATE,
	COLUMN_SIZE,
	COLUMN_COUNT
};


class SongListView;


// Display helpers, also used by Get Info: the locale's short date and time
// ("" for 0), sizes like "3.4 MB".
BString format_date(time_t time);
BString format_size(off_t size);


// What decides between songs that are equal in the sort column: like
// iTunes, sorting by artist (or genre, year...) keeps albums together and
// in track order.
struct SortTie {
			BString				artist;
			BString				album;
			int64				track;
};


class SongRow : public BRow {
public:
								SongRow(const Song* song, int32 number);

			int32				SongId() const { return fSong; }
	// Position in the source (1 based): the playlist order.
			int32				Number() const { return fNumber; }
			void				Update(const Song* song);

private:
			int32				fSong;
			int32				fNumber;
			SortTie				fTie;
};


class SongListView : public BColumnListView {
public:
								SongListView(const char* name);

	// Replaces the rows. "positions" are the songs' places in the source
	// (shown in the "#" column). With keepSelection, selected songs stay
	// selected and the list keeps its scroll position.
			void				SetSongs(const std::vector<int32>& songs,
									const std::vector<int32>& positions,
									const Library& library,
									bool keepSelection);
			void				UpdateSong(const Song* song);

			int32				PlayingSong() const { return fPlayingSong; }
			void				SetPlayingSong(int32 song);
			bool				HasFailed(int32 song) const
									{ return fFailed.count(song) > 0; }
			void				SetFailed(int32 song, bool failed);

	// Songs in display order.
			void				GetSongs(std::vector<int32>& songs) const;
			void				GetSelectedSongs(std::vector<int32>& songs)
									const;
	// Positions (Number() - 1) of the selected rows.
			void				GetSelectedPositions(
									std::vector<int32>& positions) const;
			int32				SelectedIndex() const;
			int32				IndexOfSong(int32 song);
			void				SelectSong(int32 song);
			void				SelectAllRows();

	// Shows the "#" column (playlists).
			void				SetPlaylistMode(bool playlist);
	// True when sorted by "#" only, ascending: the playlist order.
			bool				IsInPlaylistOrder();
			void				GetSortState(BMessage& state);
			void				SetSortState(const BMessage& state);
			BColumn*			ColumnFor(song_column which) const;

	virtual	bool				InitiateDrag(BPoint where, bool wasSelected);
	virtual	void				MessageDropped(BMessage* message,
									BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 count);
	virtual	void				AttachedToWindow();

private:
			int32				fPlayingSong;
			std::set<int32>		fFailed;
			BColumn*			fColumns[COLUMN_COUNT];
			bool				fFilterAdded;
};


#endif	// SONG_LIST_H
