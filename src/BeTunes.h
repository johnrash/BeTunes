/*
 * BeTunes - a music player for Haiku in the spirit of iTunes from the
 * mid 2000s.
 *
 * Message codes shared by the windows and views. Every command in the menus
 * can also be sent with "hey" (they reply when the sender waits), see
 * README.md.
 */
#ifndef BETUNES_H
#define BETUNES_H


#include <SupportDefs.h>


#define APP_SIGNATURE "application/x-vnd.BeTunes"
#define APP_NAME "BeTunes"


enum {
	// Playback
	kMsgPlayPause			= 'Bply',
	kMsgStop				= 'Bstp',
	kMsgNext				= 'Bnxt',
	kMsgPrevious			= 'Bprv',
	kMsgScan				= 'Bscn',	// "direction" int32 (-1, 1)
	kMsgSeek				= 'Bsek',	// "position" float 0..1 or "time" int64
	kMsgVolume				= 'Bvol',	// "volume" int32 0..100 (or a slider)
	kMsgVolumeUp			= 'Bvup',
	kMsgVolumeDown			= 'Bvdn',
	kMsgMute				= 'Bmut',
	kMsgToggleShuffle		= 'Bshf',
	kMsgSetRepeat			= 'Brpt',	// "mode" int32; none: cycle
	kMsgShowCurrentSong		= 'Bcur',
	kMsgPlaySongAt			= 'Bpsa',	// "row" int32 (-1: first or random)

	// Library and playlists
	kMsgAddToLibrary		= 'Badd',	// opens the file panel
	kMsgImportPaths			= 'Bimp',	// "path" strings, "playlist" int32
	kMsgNewPlaylist			= 'Bnpl',	// "name" string (optional)
	kMsgNewPlaylistFromSelection = 'Bnps',
	kMsgStartRename			= 'Bsrn',
	kMsgRenamePlaylist		= 'Brnp',	// "playlist" int32, "name" string
	kMsgDeletePlaylist		= 'Bdpl',	// "playlist" int32 (optional)
	kMsgDeleteSongs			= 'Bdel',
	kMsgReallyDelete		= 'Bdlc',	// alert reply ("which")
	kMsgAddToPlaylist		= 'Batp',	// "playlist" int32, "song" int32s
	kMsgSongsDropped		= 'Bsdr',	// songs dropped on the source list
	kMsgShowInTracker		= 'Btrk',
	kMsgGetInfo				= 'Binf',	// opens Get Info for the selection
	kMsgApplyInfo			= 'Bapi',	// "song" int32s, changed fields
		// ("title", "artist", "album", "genre", "year", "track", "disc")
	kMsgTagsWritten			= 'Btgw',	// from the tag writer
	kMsgSelectAll			= 'Bsla',
	kMsgSelectNone			= 'Bsln',

	// View
	kMsgToggleBrowser		= 'Bbrw',
	kMsgMiniPlayer			= 'Bmin',	// "mini" bool (none: switch)
	kMsgMiniOnTop			= 'Bmtp',	// "on top" bool (none: switch)
	kMsgMiniContextMenu		= 'Bmcx',	// "where" screen point
	kMsgSearchChanged		= 'Bsch',	// "text" string (hey: set the search)
	kMsgSearchScope			= 'Bscp',	// "scope" int32
	kMsgFocusSearch			= 'Bfsr',
	kMsgSortBy				= 'Bsby',	// "column" int32, "ascending" bool
	kMsgSourceSelected		= 'Bsrs',	// "select" int32 (hey: select a source)
	kMsgArtistSelected		= 'Bars',
	kMsgAlbumSelected		= 'Bals',
	kMsgSongInvoked			= 'Bsiv',
	kMsgSongSelected		= 'Bsse',	// "select" int32s (hey: select rows)
	kMsgSongDrag			= 'Bsdg',	// "song" int32s
	kMsgMoveSongs			= 'Bmov',	// "from" int32s, "to" int32
	kMsgSongContextMenu		= 'Bctx',	// "where" screen point
	kMsgSourceContextMenu	= 'Bsct',	// "index" int32, "where"

	// Equalizer
	kMsgShowEqualizer		= 'Beqw',	// "show" bool (none: toggle)
	kMsgEqualizerChanged	= 'Beqc',	// "enabled" bool, "preamp" float,
		// "gains" floats, "band" int32 + "gain" float, "preset" string
	kMsgEqualizerMakePreset	= 'Beqm',	// "name" string
	kMsgEqualizerDeletePreset = 'Beqd',	// "name" string
	kMsgEqualizerUpdate		= 'Bequ',	// to the Equalizer window
	kMsgEqualizerWindowState = 'Beqs',	// from it: "visible", "frame"

	// Internal
	kMsgTick				= 'Btck',
	kMsgRefreshViews		= 'Bref',
	kMsgSaveLibrary			= 'Bsav',
	kMsgCycleDisplay		= 'Bcyd',

	// Player notifications ("serial" int32, "song" int32)
	kMsgPlayerStarted		= 'Pstr',	// "duration" int64
	kMsgPlayerEnded			= 'Pend',
	kMsgPlayerError			= 'Perr',	// "error" string, "output" bool

	// Importer notifications
	kMsgImportSongs			= 'Isng',	// "song" messages, "existing" paths
	kMsgImportProgress		= 'Iprg',	// "name" string, "count" int32
	kMsgImportDone			= 'Idon',	// "added" int32, "playlist" int32

	// Scripting (tests)
	kMsgGetState			= 'Bsta',
	kMsgGetSongs			= 'Bgsl',	// visible songs in display order
	kMsgGetBrowser			= 'Bgbr',	// artist and album list contents
	kMsgGetSources			= 'Bgsr'	// source list contents
};


enum repeat_mode {
	REPEAT_OFF = 0,
	REPEAT_ALL,
	REPEAT_ONE
};


enum search_scope {
	SEARCH_ALL = 0,
	SEARCH_ARTISTS,
	SEARCH_ALBUMS,
	SEARCH_SONGS,
	SEARCH_GENRES
};


#endif	// BETUNES_H
