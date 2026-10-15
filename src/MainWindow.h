/*
 * The BeTunes window: control bar at the top (transport buttons, volume,
 * status display, search, browse), the source list on the left, the
 * artist and album browser at the top right, the song list below it and
 * new playlist, shuffle, repeat and the totals at the bottom.
 */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H


#include <Messenger.h>
#include <Window.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "BeTunes.h"
#include "Equalizer.h"
#include "Library.h"
#include "PlayQueue.h"


class BFilePanel;
class BMenu;
class BMenuBar;
class BMenuItem;
class BMessageRunner;
class BSplitView;
class BarLabel;
class BrowserListView;
class IconButton;
class Importer;
class TagWriter;
class Player;
class SearchField;
class SongListView;
class SourceListView;
class StatusDisplay;
class TransportButton;
class VolumeSlider;


class MainWindow : public BWindow {
public:
								MainWindow(const BMessage& settings);
	virtual						~MainWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MenusBeginning();
	// The zoom button switches between the full and the mini player, like
	// iTunes' green button.
	virtual	void				Zoom(BPoint origin, float width,
									float height);

			bool				IsMiniPlayer() const { return fMiniPlayer; }

private:
			void				_BuildLayout();
			BMenuBar*			_BuildMenuBar();
			void				_ApplySettings(const BMessage& settings);
			void				_SaveSettings();

			// sources and filtering
			void				_RebuildSourceList();
			void				_SelectSource(source_kind kind,
									int32 playlist);
			void				_SourceSelectionChanged();
			void				_RefreshAll(bool keepSelection);
			void				_FilterSearch();
			void				_UpdateArtists();
			void				_UpdateAlbums();
			void				_UpdateSongs(bool keepSelection);
			void				_UpdateStatusText();
			bool				_MatchesSearch(const Song* song) const;
			bool				_IsFiltered() const;
			void				_ScheduleRefresh();
			void				_SetBrowserVisible(bool visible);
			bool				_IsUserPlaylist() const
									{ return fSourceKind == SOURCE_PLAYLIST; }

			// playback
			void				_PlayFromView(int32 index);
			void				_PlaySong(int32 song, bool paused = false);
			void				_PlayPause();
			void				_Next();
			void				_Previous();
			void				_Stop();
			void				_Advance(bool afterError);
			void				_SongEnded(int32 song);
			void				_PlayerError(BMessage* message);
			void				_UpdatePlayState();
			void				_UpdatePlayingSource();
			void				_SetVolume(int32 volume);
			void				_SetShuffle(bool shuffle);
			void				_SetRepeat(int32 mode);
			void				_ShowCurrentSong();
			void				_SetMiniPlayer(bool mini);
			void				_SetMiniOnTop(bool onTop);
			void				_ShowMiniContextMenu(BPoint where);

			// equalizer
			void				_CreateEqualizerWindow();
			void				_EqualizerChanged(BMessage* message);
			void				_ApplyEqualizer(bool updateWindow);
			bool				_FindEqualizerPreset(const char* name,
									EqualizerSettings& settings) const;
			void				_MakeEqualizerPreset(const char* name);
			void				_DeleteEqualizerPreset(const char* name);

			// library and playlists
			void				_Import(const BMessage& refs, int32 playlist,
									bool play);
			void				_ImportedSongs(BMessage* message);
			void				_ImportDone(BMessage* message);
			Playlist*			_NewPlaylist(const char* name,
									const std::vector<int32>& songs,
									bool rename);
	// refreshNow: update the song list at once (otherwise soon, for the
	// many small batches of an import)
			void				_AddToPlaylist(int32 playlist,
									const std::vector<int32>& songs,
									bool refreshNow);
			void				_RenamePlaylist(int32 playlist,
									const char* name);
			void				_DeletePlaylist(int32 playlist,
									bool confirmed);
			void				_DeleteSongs(bool confirmed);
			void				_RemoveFromLibrary(
									const std::set<int32>& songs);
			void				_MoveSongs(BMessage* message);
			void				_ShowInTracker(int32 song = -1);
			void				_GetInfo();
			void				_ApplyInfo(BMessage* message);
			void				_TagsWritten(BMessage* message);
			void				_SaveLibrary();

			// menus
			void				_ShowSongContextMenu(BPoint where);
			void				_ShowSourceContextMenu(int32 index,
									BPoint where);
			BMenu*				_BuildAddToPlaylistMenu(const char* label);

			// scripting
			void				_ReplyState(BMessage* message);
			void				_ReplySongs(BMessage* message);
			void				_ReplyBrowser(BMessage* message);
			void				_ReplySources(BMessage* message);

			Library				fLibrary;
			Player*				fPlayer;
			Importer*			fImporter;
			TagWriter*			fTagWriter;
			int32				fTagWritesPending;
			int32				fTagWriteFailures;
			PlayQueue			fQueue;

			// views
			TransportButton*	fPreviousButton;
			TransportButton*	fPlayButton;
			TransportButton*	fNextButton;
			VolumeSlider*		fVolumeSlider;
			StatusDisplay*		fDisplay;
			SearchField*		fSearchField;
			IconButton*			fBrowseButton;
			SourceListView*		fSourceList;
			BrowserListView*	fArtistList;
			BrowserListView*	fAlbumList;
			SongListView*		fSongList;
			IconButton*			fAddButton;
			IconButton*			fShuffleButton;
			IconButton*			fRepeatButton;
			BarLabel*			fStatusText;
			BSplitView*			fMainSplit;
			BSplitView*			fRightSplit;
			BSplitView*			fBrowserSplit;

			// menu items
			BMenuItem*			fPlayItem;
			BMenuItem*			fShuffleItem;
			BMenuItem*			fRepeatItems[3];
			BMenuItem*			fBrowserItem;
			BMenuItem*			fDeleteItem;
			BMenuItem*			fRenameItem;
			BMenuItem*			fDeletePlaylistItem;
			BMenuItem*			fFromSelectionItem;
			BMenuItem*			fShowInTrackerItem;
			BMenuItem*			fShowCurrentItem;
			BMenuItem*			fGetInfoItem;
			BMenuItem*			fMiniPlayerItem;
			BMenuItem*			fMiniOnTopItem;

			// the mini player: only the control bar
			BMenuBar*			fMenuBar;
			BView*				fBottomBar;
			BView*				fSearchLabel;
			BView*				fBrowseLabel;
			bool				fMiniPlayer;
			bool				fMiniOnTop;
			BRect				fFullFrame;
			BRect				fMiniFrame;

			// the source shown
			source_kind			fSourceKind;
			int32				fSourcePlaylist;
			std::vector<int32>	fSourceSongs;
				// song ids in the source's order
			std::vector<int32>	fSearched;
			std::vector<int32>	fVisible;
				// positions in fSourceSongs
			std::vector<std::string> fSearchWords;
			int32				fSearchScope;
			bool				fArtistAll;
			BString				fArtist;
			bool				fAlbumAll;
			BString				fAlbum;
			bool				fBrowserVisible;
			std::map<int32, BMessage> fSortStates;
				// per source kind (all playlists share one)
			int32				fSortKind;

			// playing
			int32				fPlayingSerial;
			source_kind			fPlayingKind;
			int32				fPlayingPlaylist;
			int32				fErrorsInARow;
			bool				fOutputErrorShown;
			int32				fRepeat;
			bool				fShuffle;
			int32				fVolume;
			int32				fVolumeBeforeMute;

			// importing
			int32				fImportsPending;
			int32				fNextImportRequest;
			std::map<int32, std::vector<int32> > fImportedForPlay;
			BFilePanel*			fFilePanel;

			std::set<int32>		fPendingDelete;
			BMessageRunner*		fTickRunner;
			BMessageRunner*		fRefreshRunner;
			BMessageRunner*		fSaveRunner;

			// equalizer
			struct UserPreset {
				BString			name;
				EqualizerSettings settings;
			};

			EqualizerSettings	fEqualizer;
			BString				fEqualizerPreset;
			std::vector<UserPreset> fUserPresets;
			BMessenger			fEqualizerWindow;
			bool				fEqualizerVisible;
			BRect				fEqualizerFrame;
				// kept up to date by the Equalizer window
			IconButton*			fEqualizerButton;
			BMenuItem*			fEqualizerItem;
};


#endif	// MAIN_WINDOW_H
