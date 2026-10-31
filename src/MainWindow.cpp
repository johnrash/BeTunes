/*
 * The BeTunes window.
 */
#include "MainWindow.h"

#include <Alert.h>
#include <ControlLook.h>
#include <Application.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Node.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <TextView.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "BrowserList.h"
#include "Controls.h"
#include "Daap.h"
#include "DaapProtocol.h"
#include "EqualizerWindow.h"
#include "Importer.h"
#include "Player.h"
#include "PromptWindow.h"
#include "SearchField.h"
#include "Settings.h"
#include "ShareFinder.h"
#include "SongList.h"
#include "SourceList.h"
#include "InfoWindow.h"
#include "StatusDisplay.h"
#include "TagReader.h"
#include "TagWriter.h"
#include "TextUtils.h"


static const bigtime_t kTickInterval = 250000;
static const bigtime_t kRefreshDelay = 400000;
static const bigtime_t kSaveInterval = 60000000;
static const bigtime_t kRestartThreshold = 3000000;
	// "previous" goes back to the start of the song after 3 seconds
static const bigtime_t kScanStep = 2000000;
// a key held for Command + > / < scans as a held transport button does
// (Controls.cpp): after 3 ticks, a step every tick
static const uint32 kMsgScanTick = 'Bkst';
static const bigtime_t kScanTickInterval = 150000;
static const int32 kScanTicksBeforeHold = 3;
	// fast forward and rewind while the button is held
static const int32 kVolumeStep = 10;
static const bigtime_t kKeepAliveInterval = 5 * 60000000LL;
	// shared libraries end sessions unused for about 30 minutes
static const int32 kMissedRoundsBeforeGone = 3;
	// a share that did not answer this many rounds left the network
static const bigtime_t kLogoutTimeout = 2000000;
	// at most this long at quit

enum {
	kShareIdle = 0,
	kShareConnecting,
	kShareConnected
};


namespace {


// Right-clicking the mini player opens a menu with the main commands (it
// has no menu bar).
class MiniPlayerMenuFilter : public BMessageFilter {
public:
	MiniPlayerMenuFilter(MainWindow* window)
		:
		BMessageFilter(B_MOUSE_DOWN),
		fWindow(window)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		if (!fWindow->IsMiniPlayer()
			|| (message->GetInt32("buttons", 0) & B_SECONDARY_MOUSE_BUTTON)
				== 0)
			return B_DISPATCH_MESSAGE;
		BPoint where;
		if (message->FindPoint("screen_where", &where) != B_OK)
			return B_DISPATCH_MESSAGE;
		BMessage menu(kMsgMiniContextMenu);
		menu.AddPoint("where", where);
		fWindow->PostMessage(&menu);
		return B_SKIP_MESSAGE;
	}

private:
	MainWindow*	fWindow;
};


// Space plays and pauses, unless the user is typing.
class SpaceFilter : public BMessageFilter {
public:
	SpaceFilter()
		:
		BMessageFilter(B_KEY_DOWN)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		const char* bytes;
		if (message->FindString("bytes", &bytes) != B_OK || bytes[0] != ' ')
			return B_DISPATCH_MESSAGE;
		if ((message->GetInt32("modifiers", 0)
				& (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY)) != 0)
			return B_DISPATCH_MESSAGE;
		BWindow* window = dynamic_cast<BWindow*>(Looper());
		if (window == NULL
			|| dynamic_cast<BTextView*>(window->CurrentFocus()) != NULL)
			return B_DISPATCH_MESSAGE;
		if (message->GetInt32("be:key_repeat", 0) == 0)
			window->PostMessage(kMsgPlayPause);
		return B_SKIP_MESSAGE;
	}
};


/*!	What \a key types with Shift as \a modifiers have it: with Command held,
	Haiku's keymap gives the unshifted character whatever Shift does, so
	Command + > arrives as Command + Shift + "." (on US keyboards).
	0 for keys that type more than one byte.
*/
static char
typed_character(uint32 key, uint32 modifiers)
{
	key_map* keys = NULL;
	char* chars = NULL;
	get_key_map(&keys, &chars);
	char character = 0;
	if (keys != NULL && chars != NULL && key < 128) {
		int32 offset = (modifiers & B_SHIFT_KEY) != 0
			? keys->shift_map[key] : keys->normal_map[key];
		if (chars[offset] == 1)
			character = chars[offset + 1];
	}
	free(keys);
	free(chars);
	return character;
}


// Command + > and Command + < fast forward and rewind: one step when
// pressed, and on and on while held, until that key goes up.
class ScanKeyFilter : public BMessageFilter {
public:
	ScanKeyFilter(MainWindow* window)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
		fWindow(window)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		if (message->what == B_KEY_UP) {
			fWindow->StopKeyScan(message->GetInt32("key", 0));
			return B_DISPATCH_MESSAGE;
		}
		if (message->what != B_KEY_DOWN)
			return B_DISPATCH_MESSAGE;
		uint32 modifiers = message->GetInt32("modifiers", 0);
		if ((modifiers & B_COMMAND_KEY) == 0
			|| (modifiers & (B_CONTROL_KEY | B_OPTION_KEY)) != 0)
			return B_DISPATCH_MESSAGE;
		int32 key = message->GetInt32("key", 0);
		char character = typed_character(key, modifiers);
		if (character != '>' && character != '<')
			return B_DISPATCH_MESSAGE;
		// the key's own repeats change nothing: the scan has its pace
		if (message->GetInt32("be:key_repeat", 0) == 0)
			fWindow->StartKeyScan(character == '>' ? 1 : -1, key);
		return B_SKIP_MESSAGE;
	}

private:
	MainWindow*	fWindow;
};


BMessage
sort_state(song_column column, bool ascending)
{
	BMessage state;
	state.AddInt32("sortID", column);
	state.AddBool("sortascending", ascending);
	return state;
}


BString
format_totals(int32 count, bigtime_t duration, off_t size)
{
	BString text;
	if (count == 0)
		return text;

	text << count << (count == 1 ? " song, " : " songs, ");
	double minutes = duration / 60000000.0;
	char buffer[64];
	if (minutes < 60)
		snprintf(buffer, sizeof(buffer), "%.1f minutes", minutes);
	else if (minutes < 24 * 60)
		snprintf(buffer, sizeof(buffer), "%.1f hours", minutes / 60);
	else
		snprintf(buffer, sizeof(buffer), "%.1f days", minutes / 60 / 24);
	text << buffer << ", ";
	if (size < 1024LL * 1024 * 1024)
		snprintf(buffer, sizeof(buffer), "%.1f MB", size / 1048576.0);
	else
		snprintf(buffer, sizeof(buffer), "%.2f GB", size / 1073741824.0);
	text << buffer;
	return text;
}


struct BrowserEntry {
	BString	key;
	BString	value;
};


bool
operator<(const BrowserEntry& a, const BrowserEntry& b)
{
	int compare = strcmp(a.key.String(), b.key.String());
	if (compare != 0)
		return compare < 0;
	return a.value < b.value;
}


// Fills a browser list, unless it already shows exactly these entries
// (the import refreshes often: the list should not jump around).
void
set_browser_items(BrowserListView* list, const std::vector<BrowserEntry>& entries,
	const char* unknownLabel, const BString& allLabel)
{
	bool same = list->CountItems() == (int32)entries.size() + 1;
	for (int32 i = 0; same && i < (int32)entries.size(); i++) {
		if (list->BrowserAt(i + 1)->Value() != entries[i].value)
			same = false;
	}
	if (same) {
		BrowserItem* all = list->BrowserAt(0);
		if (all != NULL && allLabel != all->Text()) {
			all->SetText(allLabel);
			list->InvalidateItem(0);
		}
		return;
	}

	float top = list->Bounds().top;
	for (int32 i = list->CountItems() - 1; i >= 0; i--)
		delete list->RemoveItem(i);

	BList items;
	items.AddItem(new BrowserItem(allLabel, "", true));
	for (size_t i = 0; i < entries.size(); i++) {
		const char* label = entries[i].value.IsEmpty() ? unknownLabel
			: entries[i].value.String();
		items.AddItem(new BrowserItem(label, entries[i].value, false));
	}
	list->AddList(&items);
	list->ScrollTo(BPoint(0, top));
}


}	// namespace


MainWindow::MainWindow(const BMessage& settings)
	:
	BWindow(BRect(80, 60, 1079, 699), APP_NAME, B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS
			| B_QUIT_ON_WINDOW_CLOSE),
	fPlayer(NULL),
	fImporter(NULL),
	fTagWriter(NULL),
	fTagWritesPending(0),
	fTagWriteFailures(0),
	fPlayItem(NULL),
	fMiniPlayerItem(NULL),
	fMiniOnTopItem(NULL),
	fDisconnectItem(NULL),
	fMenuBar(NULL),
	fBottomBar(NULL),
	fSearchAndBrowse(NULL),
	fMiniPlayer(false),
	fMiniOnTop(false),
	fSourceKind(SOURCE_LIBRARY),
	fSourcePlaylist(-1),
	fSearchScope(SEARCH_ALL),
	fArtistAll(true),
	fAlbumAll(true),
	fBrowserVisible(true),
	fSortKind(-1),
	fPlayingSerial(0),
	fPlayingKind(SOURCE_LIBRARY),
	fPlayingPlaylist(-1),
	fErrorsInARow(0),
	fOutputErrorShown(false),
	fRepeat(REPEAT_OFF),
	fShuffle(false),
	fVolume(80),
	fVolumeBeforeMute(80),
	fImportsPending(0),
	fNextImportRequest(1),
	fFilePanel(NULL),
	fTickRunner(NULL),
	fRefreshRunner(NULL),
	fSaveRunner(NULL),
	fNextShareId(1),
	fNextShareSerial(1),
	fShareFinder(NULL),
	fKeepAliveRunner(NULL),
	fSourceListStale(false),
	fScanRunner(NULL),
	fScanKey(0),
	fScanDirection(0),
	fScanTicks(0),
	fEqualizerPreset("Flat"),
	fEqualizerVisible(false),
	fEqualizerButton(NULL),
	fEqualizerItem(NULL)
{
	fLibrary.Load(settings_path("Library"));

	fPlayer = new Player(BMessenger(this));
	fImporter = new Importer(&fLibrary, BMessenger(this));
	fImporter->Run();
	fTagWriter = new TagWriter(BMessenger(this));
	fTagWriter->Run();

	_BuildLayout();
	AddCommonFilter(new SpaceFilter);
	AddCommonFilter(new ScanKeyFilter(this));
	AddCommonFilter(new MiniPlayerMenuFilter(this));

	fSortStates[SOURCE_LIBRARY] = sort_state(COLUMN_ARTIST, true);
	fSortStates[SOURCE_RECENTLY_ADDED] = sort_state(COLUMN_DATE_ADDED, false);
	fSortStates[SOURCE_RECENTLY_PLAYED] = sort_state(COLUMN_LAST_PLAYED,
		false);
	fSortStates[SOURCE_TOP_25] = sort_state(COLUMN_PLAYS, false);
	fSortStates[SOURCE_PLAYLIST] = sort_state(COLUMN_NUMBER, true);
	fSortStates[SOURCE_SHARE] = sort_state(COLUMN_ARTIST, true);
	fSortStates[SOURCE_SHARE_PLAYLIST] = sort_state(COLUMN_NUMBER, true);

	_ApplySettings(settings);
	_CreateEqualizerWindow();
	if (settings.GetBool("mini player", false))
		_SetMiniPlayer(true);

	BMessage tick(kMsgTick);
	fTickRunner = new BMessageRunner(BMessenger(this), &tick, kTickInterval);
	BMessage save(kMsgSaveLibrary);
	fSaveRunner = new BMessageRunner(BMessenger(this), &save, kSaveInterval);
	BMessage keepAlive(kMsgShareKeepAlive);
	fKeepAliveRunner = new BMessageRunner(BMessenger(this), &keepAlive,
		kKeepAliveInterval);

	fShareFinder = new ShareFinder(BMessenger(this));
	if (fShareFinder->Start() != B_OK)
		fprintf(stderr, "BeTunes: cannot look for shared libraries\n");
}


MainWindow::~MainWindow()
{
	delete fTickRunner;
	delete fRefreshRunner;
	delete fSaveRunner;
	delete fScanRunner;
	delete fKeepAliveRunner;
	delete fShareFinder;

	delete fPlayer;
	fImporter->Cancel();
	if (fImporter->Lock())
		fImporter->Quit();
	// waits for a file being written (a copy only replaces the original
	// when it is complete, so stopping would be safe, but slower is nicer)
	if (fTagWriter->Lock())
		fTagWriter->Quit();
	delete fFilePanel;
}


void
MainWindow::WindowActivated(bool active)
{
	// a key let go in another window would never stop it
	if (!active)
		StopKeyScan(-1);
	BWindow::WindowActivated(active);
}


void
MainWindow::StartKeyScan(int32 direction, int32 key)
{
	StopKeyScan(-1);
	BMessage scan(kMsgScan);
	scan.AddInt32("direction", direction);
	PostMessage(&scan);

	fScanKey = key;
	fScanDirection = direction;
	fScanTicks = 0;
	BMessage tick(kMsgScanTick);
	fScanRunner = new BMessageRunner(BMessenger(this), &tick,
		kScanTickInterval);
}


/*!	\a key went up: the scan it started ends (-1: any scan). */
void
MainWindow::StopKeyScan(int32 key)
{
	if (fScanRunner == NULL || (key >= 0 && key != fScanKey))
		return;
	delete fScanRunner;
	fScanRunner = NULL;
}


bool
MainWindow::QuitRequested()
{
	fImporter->Cancel();
	_SaveSettings();
	_SaveLibrary();
	_LogoutShares();
	return true;
}


// #pragma mark - building


void
MainWindow::_BuildLayout()
{
	BMenuBar* menuBar = _BuildMenuBar();
	fMenuBar = menuBar;

	// control bar
	BMessage* rewind = new BMessage(kMsgScan);
	rewind->AddInt32("direction", -1);
	BMessage* forward = new BMessage(kMsgScan);
	forward->AddInt32("direction", 1);
	fPreviousButton = new TransportButton("previous", GLYPH_PREVIOUS, 2.6f,
		new BMessage(kMsgPrevious), rewind);
	fPlayButton = new TransportButton("play", GLYPH_PLAY, 3.1f,
		new BMessage(kMsgPlayPause));
	fNextButton = new TransportButton("next", GLYPH_NEXT, 2.6f,
		new BMessage(kMsgNext), forward);
	fVolumeSlider = new VolumeSlider("volume", new BMessage(kMsgVolume));
	fDisplay = new StatusDisplay("display");
	fDisplay->SetTarget(BMessenger(this));
	fSearchField = new SearchField("search");
	fSearchField->SetTarget(BMessenger(this));
	fBrowseButton = new IconButton("browse", GLYPH_EYE,
		new BMessage(kMsgToggleBrowser));
	fBrowseButton->SetValue(B_CONTROL_ON);

	BarView* controlBar = new BarView("control bar", B_HORIZONTAL, true);
	BGroupLayout* searchAndBrowse;
	BLayoutBuilder::Group<>(controlBar, B_HORIZONTAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_SMALL_SPACING,
			B_USE_WINDOW_SPACING, B_USE_SMALL_SPACING)
		.AddGroup(B_HORIZONTAL, 3, 0)
			.Add(fPreviousButton)
			.Add(fPlayButton)
			.Add(fNextButton)
		.End()
		.AddStrut(be_control_look->DefaultItemSpacing())
		.Add(fVolumeSlider, 0)
		.AddGlue(1)
		.Add(fDisplay, 6)
		.AddGlue(1)
		// the search field well clear of the Browse button, as in iTunes
		.AddGroup(B_HORIZONTAL,
				ceilf(be_control_look->DefaultItemSpacing() * 1.3f), 0)
			.GetLayout(&searchAndBrowse)
			.AddGroup(B_VERTICAL, 2, 0)
				.AddGlue()
				.Add(fSearchField)
				.Add(new BarLabel(NULL, "Search", 0.85f))
				.AddGlue()
			.End()
			.AddGroup(B_VERTICAL, 2, 0)
				.AddGlue()
				.Add(fBrowseButton)
				.Add(new BarLabel(NULL, "Browse", 0.85f))
				.AddGlue()
			.End()
		.End();
	fSearchAndBrowse = searchAndBrowse;

	// source list
	fSourceList = new SourceListView("sources");
	fSourceList->SetSelectionMessage(new BMessage(kMsgSourceSelected));
	BScrollView* sourceScroll = new BScrollView("sources scroll", fSourceList,
		0, false, true, B_NO_BORDER);

	// browser
	fArtistList = new BrowserListView("artists");
	fArtistList->SetSelectionMessage(new BMessage(kMsgArtistSelected));
	fArtistList->SetInvocationMessage(new BMessage(kMsgPlaySongAt));
	fAlbumList = new BrowserListView("albums");
	fAlbumList->SetSelectionMessage(new BMessage(kMsgAlbumSelected));
	fAlbumList->SetInvocationMessage(new BMessage(kMsgPlaySongAt));
	BScrollView* artistScroll = new BScrollView("artists scroll", fArtistList,
		0, false, true, B_NO_BORDER);
	BScrollView* albumScroll = new BScrollView("albums scroll", fAlbumList,
		0, false, true, B_NO_BORDER);

	// songs
	fSongList = new SongListView("songs");
	fSongList->SetSelectionMessage(new BMessage(kMsgSongSelected));
	fSongList->SetInvocationMessage(new BMessage(kMsgSongInvoked));
	fSongList->SetTarget(this);

	fBrowserSplit = new BSplitView(B_HORIZONTAL, 4);
	BLayoutBuilder::Split<>(fBrowserSplit)
		.AddGroup(B_VERTICAL, 0, 1)
			.Add(new HeaderView("Artist"))
			.Add(artistScroll)
		.End()
		.AddGroup(B_VERTICAL, 0, 1)
			.Add(new HeaderView("Album"))
			.Add(albumScroll)
		.End();

	fRightSplit = new BSplitView(B_VERTICAL, 4);
	BLayoutBuilder::Split<>(fRightSplit)
		.Add(fBrowserSplit, 1)
		.Add(fSongList, 2);

	fMainSplit = new BSplitView(B_HORIZONTAL, 4);
	BLayoutBuilder::Split<>(fMainSplit)
		.AddGroup(B_VERTICAL, 0, 1)
			.Add(new HeaderView("Source"))
			.Add(sourceScroll)
		.End()
		.Add(fRightSplit, 5);
	fMainSplit->SetCollapsible(false);
	fRightSplit->SetCollapsible(false);
	fBrowserSplit->SetCollapsible(false);

	// bottom bar
	fAddButton = new IconButton("new playlist", GLYPH_PLUS,
		new BMessage(kMsgNewPlaylist));
	fShuffleButton = new IconButton("shuffle", GLYPH_SHUFFLE,
		new BMessage(kMsgToggleShuffle));
	fRepeatButton = new IconButton("repeat", GLYPH_REPEAT,
		new BMessage(kMsgSetRepeat));
	fStatusText = new BarLabel("status", "");
	fEqualizerButton = new IconButton("equalizer", GLYPH_EQUALIZER,
		new BMessage(kMsgShowEqualizer));
	float buttons = fAddButton->MinSize().width * 3 + 2 * 3;
		// the same on both sides keeps the totals centered
	float inset = BControlLook::ComposeSpacing(B_USE_WINDOW_SPACING);
	// the window's resize knob reaches into the corner: keep the width of a
	// scroll bar clear, as document windows do
	float rightInset = std::max(inset,
		be_control_look->GetScrollBarWidth(B_VERTICAL)
			+ BControlLook::ComposeSpacing(B_USE_SMALL_SPACING));

	BarView* bottomBar = new BarView("bottom bar", B_HORIZONTAL, false);
	fBottomBar = bottomBar;
	BLayoutBuilder::Group<>(bottomBar, B_HORIZONTAL, 3)
		.SetInsets(inset, 4, rightInset, 4)
		.Add(fAddButton)
		.Add(fShuffleButton)
		.Add(fRepeatButton)
		.AddGlue()
		.Add(fStatusText)
		.AddGlue()
		.AddStrut(buttons - fEqualizerButton->MinSize().width - 3
			- (rightInset - inset))
		.Add(fEqualizerButton);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(controlBar)
		.Add(fMainSplit)
		.Add(bottomBar);
}


BMenuBar*
MainWindow::_BuildMenuBar()
{
	BMenuBar* menuBar = new BMenuBar("menu bar");

	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("New Playlist", new BMessage(kMsgNewPlaylist),
		'N'));
	fFromSelectionItem = new BMenuItem("New Playlist from Selection",
		new BMessage(kMsgNewPlaylistFromSelection), 'N', B_SHIFT_KEY);
	menu->AddItem(fFromSelectionItem);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Add to Library" B_UTF8_ELLIPSIS,
		new BMessage(kMsgAddToLibrary), 'O'));
	menu->AddItem(new BMenuItem("Connect to Shared Library" B_UTF8_ELLIPSIS,
		new BMessage(kMsgConnectToShare), 'K'));
	fDisconnectItem = new BMenuItem("Disconnect Shared Library",
		new BMessage(kMsgDisconnectShare));
	menu->AddItem(fDisconnectItem);
	menu->AddSeparatorItem();
	fShowInTrackerItem = new BMenuItem("Show in Tracker",
		new BMessage(kMsgShowInTracker), 'R');
	menu->AddItem(fShowInTrackerItem);
	fGetInfoItem = new BMenuItem("Get Info", new BMessage(kMsgGetInfo), 'I');
	menu->AddItem(fGetInfoItem);
	menu->AddSeparatorItem();
	BMenuItem* about = new BMenuItem("About " APP_NAME,
		new BMessage(B_ABOUT_REQUESTED));
	about->SetTarget(be_app);
	menu->AddItem(about);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(menu);

	menu = new BMenu("Edit");
	menu->AddItem(new BMenuItem("Select All", new BMessage(kMsgSelectAll),
		'A'));
	menu->AddItem(new BMenuItem("Select None", new BMessage(kMsgSelectNone),
		'A', B_SHIFT_KEY));
	menu->AddSeparatorItem();
	fDeleteItem = new BMenuItem("Delete from Library" B_UTF8_ELLIPSIS,
		new BMessage(kMsgDeleteSongs));
	menu->AddItem(fDeleteItem);
	menu->AddSeparatorItem();
	fRenameItem = new BMenuItem("Rename Playlist",
		new BMessage(kMsgStartRename));
	menu->AddItem(fRenameItem);
	fDeletePlaylistItem = new BMenuItem("Delete Playlist" B_UTF8_ELLIPSIS,
		new BMessage(kMsgDeletePlaylist));
	menu->AddItem(fDeletePlaylistItem);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Find", new BMessage(kMsgFocusSearch), 'F'));
	menuBar->AddItem(menu);

	menu = new BMenu("Controls");
	fPlayItem = new BMenuItem("Play", new BMessage(kMsgPlayPause));
	menu->AddItem(fPlayItem);
	menu->AddItem(new BMenuItem("Stop", new BMessage(kMsgStop), '.'));
	menu->AddItem(new BMenuItem("Next Song", new BMessage(kMsgNext),
		B_RIGHT_ARROW));
	menu->AddItem(new BMenuItem("Previous Song", new BMessage(kMsgPrevious),
		B_LEFT_ARROW));
	// the keys themselves go through ScanKeyFilter, to scan while held
	BMessage* forward = new BMessage(kMsgScan);
	forward->AddInt32("direction", 1);
	menu->AddItem(new BMenuItem("Fast Forward", forward, '>'));
	BMessage* rewind = new BMessage(kMsgScan);
	rewind->AddInt32("direction", -1);
	menu->AddItem(new BMenuItem("Rewind", rewind, '<'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Volume Up", new BMessage(kMsgVolumeUp),
		B_UP_ARROW));
	menu->AddItem(new BMenuItem("Volume Down", new BMessage(kMsgVolumeDown),
		B_DOWN_ARROW));
	menu->AddItem(new BMenuItem("Mute", new BMessage(kMsgMute), 'M'));
	menu->AddSeparatorItem();
	fShuffleItem = new BMenuItem("Shuffle", new BMessage(kMsgToggleShuffle));
	menu->AddItem(fShuffleItem);
	BMenu* repeat = new BMenu("Repeat");
	repeat->SetRadioMode(true);
	const char* repeatLabels[] = { "Off", "All", "One" };
	for (int32 i = 0; i < 3; i++) {
		BMessage* message = new BMessage(kMsgSetRepeat);
		message->AddInt32("mode", i);
		fRepeatItems[i] = new BMenuItem(repeatLabels[i], message);
		repeat->AddItem(fRepeatItems[i]);
	}
	menu->AddItem(repeat);
	menu->AddSeparatorItem();
	fShowCurrentItem = new BMenuItem("Show Current Song",
		new BMessage(kMsgShowCurrentSong), 'L');
	menu->AddItem(fShowCurrentItem);
	menuBar->AddItem(menu);

	menu = new BMenu("View");
	fBrowserItem = new BMenuItem("Hide Browser",
		new BMessage(kMsgToggleBrowser), 'B');
	menu->AddItem(fBrowserItem);
	fEqualizerItem = new BMenuItem("Show Equalizer",
		new BMessage(kMsgShowEqualizer), 'E');
	menu->AddItem(fEqualizerItem);
	menu->AddSeparatorItem();
	fMiniPlayerItem = new BMenuItem("Switch to Mini Player",
		new BMessage(kMsgMiniPlayer), 'M', B_SHIFT_KEY);
	menu->AddItem(fMiniPlayerItem);
	fMiniOnTopItem = new BMenuItem("Keep Mini Player on Top",
		new BMessage(kMsgMiniOnTop));
	menu->AddItem(fMiniOnTopItem);
	menuBar->AddItem(menu);

	return menuBar;
}


void
MainWindow::_ApplySettings(const BMessage& settings)
{
	BRect frame;
	if (settings.FindRect("frame", &frame) == B_OK) {
		BRect screen = BScreen(this).Frame();
		if (screen.Intersects(frame.InsetByCopy(40, 40))) {
			MoveTo(frame.LeftTop());
			ResizeTo(frame.Width(), frame.Height());
		}
	} else
		CenterOnScreen();

	BRect miniFrame;
	if (settings.FindRect("mini frame", &miniFrame) == B_OK
		&& BScreen(this).Frame().Intersects(miniFrame.InsetByCopy(20, 10)))
		fMiniFrame = miniFrame;
	_SetMiniOnTop(settings.GetBool("mini on top", false));

	fVolumeBeforeMute = settings.GetInt32("volume before mute", 80);
	_SetVolume(settings.GetInt32("volume", 80));
	_SetShuffle(settings.GetBool("shuffle", false));
	_SetRepeat(settings.GetInt32("repeat", REPEAT_OFF));
	fSearchScope = settings.GetInt32("search scope", SEARCH_ALL);
	fSearchField->SetScope(fSearchScope);

	float weight;
	for (int32 i = 0; i < 2; i++) {
		if (settings.FindFloat("main split", i, &weight) == B_OK)
			fMainSplit->SetItemWeight(i, weight, i == 1);
		if (settings.FindFloat("right split", i, &weight) == B_OK)
			fRightSplit->SetItemWeight(i, weight, i == 1);
		if (settings.FindFloat("browser split", i, &weight) == B_OK)
			fBrowserSplit->SetItemWeight(i, weight, i == 1);
	}

	BMessage columns;
	if (settings.FindMessage("columns", &columns) == B_OK)
		fSongList->LoadState(&columns);
	BMessage state;
	for (int32 kind = SOURCE_LIBRARY; kind < SOURCE_KIND_COUNT; kind++) {
		if (settings.FindMessage("sort", kind, &state) == B_OK
			&& state.HasInt32("sortID"))
			fSortStates[kind] = state;
	}

	BMessage equalizer;
	if (settings.FindMessage("equalizer", &equalizer) == B_OK) {
		unarchive_equalizer(equalizer, fEqualizer);
		fEqualizerPreset = equalizer.GetString("preset", "Flat");
		fEqualizerVisible = equalizer.GetBool("visible", false);
		BRect frame;
		if (equalizer.FindRect("frame", &frame) == B_OK
			&& BScreen(this).Frame().Intersects(frame.InsetByCopy(20, 20)))
			fEqualizerFrame = frame;
		BMessage preset;
		for (int32 i = 0; equalizer.FindMessage("user preset", i, &preset)
				== B_OK; i++) {
			UserPreset user;
			user.name = preset.GetString("name", "");
			unarchive_equalizer(preset, user.settings);
			if (!user.name.IsEmpty())
				fUserPresets.push_back(user);
		}
	}

	BMessage added;
	for (int32 i = 0; settings.FindMessage("shared library", i, &added)
			== B_OK; i++) {
		const char* host = added.GetString("host", NULL);
		int32 port = added.GetInt32("port", 3689);
		if (host == NULL || port <= 0 || port > 65535
			|| _ShareByAddress(host, port) != NULL)
			continue;
		Share share;
		share.id = fNextShareId++;
		share.name = added.GetString("name", host);
		share.host = host;
		share.port = port;
		share.added = true;
		share.found = false;
		share.missed = 0;
		share.state = kShareIdle;
		share.serial = 0;
		share.session = 0;
		share.revision = 0;
		share.failures = 0;
		fShares.push_back(share);
	}

	_SetBrowserVisible(settings.GetBool("browser", true));
	_RebuildSourceList();
	_SelectSource((source_kind)settings.GetInt32("source kind",
		SOURCE_LIBRARY), settings.GetInt32("source playlist", -1));
}


void
MainWindow::_SaveSettings()
{
	BMessage settings;
	// the full player's frame, and the mini player's
	settings.AddRect("frame", fMiniPlayer ? fFullFrame : Frame());
	BRect miniFrame = fMiniPlayer ? Frame() : fMiniFrame;
	if (miniFrame.IsValid())
		settings.AddRect("mini frame", miniFrame);
	settings.AddBool("mini player", fMiniPlayer);
	settings.AddBool("mini on top", fMiniOnTop);
	settings.AddInt32("volume", fVolume);
	settings.AddInt32("volume before mute", fVolumeBeforeMute);
	settings.AddBool("shuffle", fShuffle);
	settings.AddInt32("repeat", fRepeat);
	settings.AddInt32("search scope", fSearchScope);
	settings.AddBool("browser", fBrowserVisible);
	for (int32 i = 0; i < 2; i++) {
		settings.AddFloat("main split", fMainSplit->ItemWeight(i));
		settings.AddFloat("right split", fRightSplit->ItemWeight(i));
		settings.AddFloat("browser split", fBrowserSplit->ItemWeight(i));
	}

	if (fSortKind >= 0)
		fSongList->GetSortState(fSortStates[fSortKind]);
	for (int32 kind = SOURCE_LIBRARY; kind < SOURCE_KIND_COUNT; kind++)
		settings.AddMessage("sort", &fSortStates[kind]);
	BMessage columns;
	fSongList->SaveState(&columns);
	settings.AddMessage("columns", &columns);
	// a shared library may be gone next time
	settings.AddInt32("source kind", _IsShareSource() ? SOURCE_LIBRARY
		: fSourceKind);
	settings.AddInt32("source playlist", _IsShareSource() ? -1
		: fSourcePlaylist);
	for (size_t i = 0; i < fShares.size(); i++) {
		if (!fShares[i].added)
			continue;
		BMessage added;
		added.AddString("name", fShares[i].name);
		added.AddString("host", fShares[i].host);
		added.AddInt32("port", fShares[i].port);
		settings.AddMessage("shared library", &added);
	}

	BMessage equalizer;
	archive_equalizer(fEqualizer, equalizer);
	equalizer.AddString("preset", fEqualizerPreset);
	equalizer.AddBool("visible", fEqualizerVisible);
	if (fEqualizerFrame.IsValid())
		equalizer.AddRect("frame", fEqualizerFrame);
	for (size_t i = 0; i < fUserPresets.size(); i++) {
		BMessage preset;
		preset.AddString("name", fUserPresets[i].name);
		archive_equalizer(fUserPresets[i].settings, preset);
		equalizer.AddMessage("user preset", &preset);
	}
	settings.AddMessage("equalizer", &equalizer);

	status_t status = save_message("Settings", settings);
	if (status != B_OK)
		fprintf(stderr, "BeTunes: cannot save settings: %s\n", strerror(status));
}


// #pragma mark - messages


void
MainWindow::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && message->HasRef("refs")) {
		_Import(*message, -1, false);
		return;
	}

	bool handled = true;
	switch (message->what) {
		// playback
		case kMsgPlayPause:
			_PlayPause();
			break;
		case kMsgStop:
			_Stop();
			break;
		case kMsgNext:
			_Next();
			break;
		case kMsgPrevious:
			_Previous();
			break;
		case kMsgScan:
			if (fPlayer->CurrentState() != Player::STOPPED) {
				bigtime_t time = fPlayer->Position()
					+ message->GetInt32("direction", 1) * kScanStep;
				fPlayer->Seek(std::max((bigtime_t)0, time));
			}
			break;
		case kMsgScanTick:
			if (fScanRunner != NULL && ++fScanTicks >= kScanTicksBeforeHold) {
				BMessage scan(kMsgScan);
				scan.AddInt32("direction", fScanDirection);
				PostMessage(&scan);
			}
			break;
		case kMsgSeek:
		{
			if (fPlayer->CurrentState() == Player::STOPPED)
				break;
			bigtime_t duration = fPlayer->Duration();
			if (duration <= 0) {
				const Song* song = fLibrary.SongById(fPlayer->CurrentSong());
				duration = song != NULL ? song->duration : 0;
			}
			float position;
			bigtime_t time;
			if (message->FindFloat("position", &position) == B_OK)
				fPlayer->Seek((bigtime_t)(position * duration));
			else if (message->FindInt64("time", &time) == B_OK)
				fPlayer->Seek(time);
			break;
		}
		case kMsgVolume:
			_SetVolume(message->GetInt32("volume", fVolumeSlider->Value()));
			break;
		case kMsgVolumeUp:
			_SetVolume(fVolume + kVolumeStep);
			break;
		case kMsgVolumeDown:
			_SetVolume(fVolume - kVolumeStep);
			break;
		case kMsgMute:
			if (fVolume > 0) {
				fVolumeBeforeMute = fVolume;
				_SetVolume(0);
			} else
				_SetVolume(fVolumeBeforeMute > 0 ? fVolumeBeforeMute : 80);
			break;
		case kMsgToggleShuffle:
			_SetShuffle(message->GetBool("shuffle", !fShuffle));
			break;
		case kMsgSetRepeat:
			_SetRepeat(message->GetInt32("mode", (fRepeat + 1) % 3));
			break;
		case kMsgShowCurrentSong:
			_ShowCurrentSong();
			break;
		case kMsgPlaySongAt:
			// "row" from scripting and menus; the browser lists send this
			// when an artist or album is double-clicked: play from the top
			_PlayFromView(message->GetInt32("row", -1));
			break;
		case kMsgSongInvoked:
			_PlayFromView(fSongList->SelectedIndex());
			break;
		case kMsgTick:
			if (fPlayer->CurrentState() != Player::STOPPED) {
				bigtime_t duration = fPlayer->Duration();
				if (duration <= 0) {
					const Song* song
						= fLibrary.SongById(fPlayer->CurrentSong());
					duration = song != NULL ? song->duration : 0;
				}
				fDisplay->SetTime(fPlayer->Position(), duration);
			}
			break;

		// player notifications
		case kMsgPlayerStarted:
			if (message->GetInt32("serial", -1) == fPlayingSerial) {
				fErrorsInARow = 0;
				fSongList->SetFailed(message->GetInt32("song", -1), false);
			}
			break;
		case kMsgPlayerEnded:
			if (message->GetInt32("serial", -1) == fPlayingSerial)
				_SongEnded(message->GetInt32("song", -1));
			break;
		case kMsgPlayerError:
			if (message->GetInt32("serial", -1) == fPlayingSerial)
				_PlayerError(message);
			break;

		// library
		case kMsgAddToLibrary:
			if (fFilePanel == NULL) {
				fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
					NULL, B_FILE_NODE | B_DIRECTORY_NODE, true);
				fFilePanel->Window()->SetTitle(
					APP_NAME ": Add to Library");
				fFilePanel->SetButtonLabel(B_DEFAULT_BUTTON, "Add");
			}
			fFilePanel->Show();
			break;
		case B_REFS_RECEIVED:
			_Import(*message, -1, false);
			break;
		case kMsgImportPaths:
		{
			int32 playlist = message->GetInt32("playlist", -1);
			if (message->GetBool("current playlist", false)
				&& _IsUserPlaylist())
				playlist = fSourcePlaylist;
			if (playlist == -2) {
				Playlist* created = _NewPlaylist(NULL,
					std::vector<int32>(), false);
				playlist = created->id;
			}
			_Import(*message, playlist, message->GetBool("play", false));
			break;
		}
		case kMsgImportSongs:
			_ImportedSongs(message);
			break;
		case kMsgImportProgress:
			if (fImportsPending > 0) {
				fDisplay->SetImportStatus(message->GetString("name", ""),
					message->GetInt32("count", 0));
			}
			break;
		case kMsgImportDone:
			_ImportDone(message);
			break;
		case kMsgRefreshViews:
			delete fRefreshRunner;
			fRefreshRunner = NULL;
			_RefreshAll(true);
			break;
		case kMsgSaveLibrary:
			if (fLibrary.IsDirty())
				_SaveLibrary();
			break;

		// playlists
		case kMsgNewPlaylist:
		{
			const char* name = message->GetString("name", NULL);
			_NewPlaylist(name, std::vector<int32>(), name == NULL);
			break;
		}
		case kMsgNewPlaylistFromSelection:
		{
			std::vector<int32> songs;
			fSongList->GetSelectedSongs(songs);
			_KeepLocalSongs(songs);
			if (songs.empty())
				break;
			const char* name = message->GetString("name", NULL);
			_NewPlaylist(name, songs, name == NULL);
			break;
		}
		case kMsgStartRename:
			if (_IsUserPlaylist())
				fSourceList->StartRename(fSourceList->CurrentSelection());
			break;
		case kMsgRenamePlaylist:
			_RenamePlaylist(message->GetInt32("playlist",
				_IsUserPlaylist() ? fSourcePlaylist : -1),
				message->GetString("name", ""));
			if (fSourceListStale && !fSourceList->IsRenaming())
				_RebuildSourceList();
			break;
		case kMsgDeletePlaylist:
		{
			int32 which;
			if (message->FindInt32("which", &which) == B_OK && which != 1)
				break;
			_DeletePlaylist(message->GetInt32("playlist", -1),
				message->HasInt32("which")
					|| message->GetBool("confirmed", false));
			break;
		}
		case kMsgDeleteSongs:
			_DeleteSongs(message->GetBool("confirmed", false));
			break;
		case kMsgReallyDelete:
		{
			if (message->GetInt32("which", 1) != 1) {
				fPendingDelete.clear();
				break;
			}
			std::set<int32> songs = fPendingDelete;
			if (songs.empty()) {
				std::vector<int32> selected;
				fSongList->GetSelectedSongs(selected);
				songs.insert(selected.begin(), selected.end());
			}
			fPendingDelete.clear();
			_RemoveFromLibrary(songs);
			break;
		}
		case kMsgAddToPlaylist:
		{
			std::vector<int32> songs;
			int32 song;
			for (int32 i = 0; message->FindInt32("song", i, &song) == B_OK;
					i++)
				songs.push_back(song);
			if (!message->HasInt32("song"))
				fSongList->GetSelectedSongs(songs);
			_AddToPlaylist(message->GetInt32("playlist", -1), songs, true);
			_SaveLibrary();
			break;
		}
		case kMsgSongsDropped:
		{
			std::vector<int32> songs;
			int32 song;
			for (int32 i = 0; message->FindInt32("song", i, &song) == B_OK;
					i++)
				songs.push_back(song);
			int32 playlist = message->GetInt32("playlist", -1);
			_KeepLocalSongs(songs);
			if (songs.empty())
				break;
			if (playlist == -2)
				_NewPlaylist(NULL, songs, true);
			else {
				_AddToPlaylist(playlist, songs, true);
				_SaveLibrary();
			}
			break;
		}
		case kMsgMoveSongs:
			_MoveSongs(message);
			break;
		case kMsgShowInTracker:
			_ShowInTracker(message->GetInt32("song", -1));
			break;
		case kMsgGetInfo:
			_GetInfo();
			break;
		case kMsgApplyInfo:
			_ApplyInfo(message);
			break;
		case kMsgTagsWritten:
			_TagsWritten(message);
			break;

		// shared libraries
		case kMsgConnectToShare:
		{
			const char* address;
			int32 id;
			if (message->FindInt32("share", &id) == B_OK) {
				Share* share = _ShareById(id);
				if (share == NULL)
					break;
				_SelectSource(SOURCE_SHARE, id);
				if (share->state == kShareIdle)
					_ConnectShare(id);
			} else if (message->FindString("address", &address) == B_OK)
				_ConnectToAddress(address, message->GetString("password", NULL));
			else
				_AskShareAddress();
			break;
		}
		case kMsgShareAddress:
			if (!message->GetBool("canceled", false)) {
				_ConnectToAddress(message->GetString("address", ""),
					NULL);
			}
			break;
		case kMsgSharePassword:
		{
			Share* share = _ShareById(message->GetInt32("share", -1));
			if (share == NULL || share->state != kShareIdle)
				break;
			if (message->GetBool("canceled", false)) {
				if (_SourceShare() == share->id)
					_SelectSource(SOURCE_LIBRARY, -1);
				break;
			}
			share->user = message->GetString("user", "");
			share->password = message->GetString("password", "");
			_ConnectShare(share->id);
			break;
		}
		case kMsgDisconnectShare:
			_DisconnectShare(message->GetInt32("share", _SourceShare()));
			break;
		case kMsgRemoveShare:
			_ForgetShare(message->GetInt32("share", _SourceShare()));
			break;
		case kMsgRefreshShares:
			fShareFinder->Refresh();
			break;
		case kMsgSharesFound:
			_SharesFound(message);
			break;
		case kMsgShareConnected:
			_ShareConnected(message);
			break;
		case kMsgShareKeepAlive:
			_KeepSharesAlive();
			break;
		case kMsgShareTouched:
		{
			Share* share = _ShareById(message->GetInt32("share", -1));
			if (share == NULL || share->state != kShareConnected
				|| share->serial != message->GetInt32("serial", -1))
				break;
			int32 error = message->GetInt32("error", DAAP_OK);
			if (error == DAAP_OK) {
				share->failures = 0;
				break;
			}
			// the network may be back next time; a refusal is final
			if (error == DAAP_NETWORK && ++share->failures < 2)
				break;
			_DisconnectShare(share->id, message->GetString("text", ""));
			break;
		}

		// view
		case kMsgSelectAll:
		{
			BTextView* text = dynamic_cast<BTextView*>(CurrentFocus());
			if (text != NULL)
				text->SelectAll();
			else
				fSongList->SelectAllRows();
			break;
		}
		case kMsgSelectNone:
			fSongList->DeselectAll();
			break;
		case kMsgToggleBrowser:
			_SetBrowserVisible(message->GetBool("visible", !fBrowserVisible));
			break;
		case kMsgSearchChanged:
		{
			const char* text = message->GetString("text", "");
			if (strcmp(text, fSearchField->Text()) != 0)
				fSearchField->SetText(text);
			std::vector<std::string> words = search_words(text);
			if (words != fSearchWords) {
				fSearchWords = words;
				_RefreshAll(true);
			}
			break;
		}
		case kMsgSearchScope:
		{
			int32 scope = message->GetInt32("scope", SEARCH_ALL);
			if (scope != fSearchField->Scope())
				fSearchField->SetScope(scope);
			if (scope != fSearchScope) {
				fSearchScope = fSearchField->Scope();
				if (!fSearchWords.empty())
					_RefreshAll(true);
			}
			break;
		}
		case kMsgFocusSearch:
			_SetMiniPlayer(false);
			fSearchField->MakeFocus(true);
			break;
		case kMsgMiniPlayer:
			_SetMiniPlayer(message->GetBool("mini", !fMiniPlayer));
			break;
		case kMsgMiniOnTop:
			_SetMiniOnTop(message->GetBool("on top", !fMiniOnTop));
			break;
		case kMsgMiniContextMenu:
		{
			BPoint where;
			if (message->FindPoint("where", &where) == B_OK)
				_ShowMiniContextMenu(where);
			break;
		}

		// equalizer
		case kMsgShowEqualizer:
		{
			// the window decides (it knows whether it is shown)
			BMessage show(kMsgShowEqualizer);
			bool visible;
			if (message->FindBool("show", &visible) == B_OK)
				show.AddBool("show", visible);
			fEqualizerWindow.SendMessage(&show);
			break;
		}
		case kMsgEqualizerWindowState:
			fEqualizerVisible = message->GetBool("visible", fEqualizerVisible);
			message->FindRect("frame", &fEqualizerFrame);
			break;
		case kMsgEqualizerChanged:
			_EqualizerChanged(message);
			break;
		case kMsgEqualizerMakePreset:
			_MakeEqualizerPreset(message->GetString("name", ""));
			break;
		case kMsgEqualizerDeletePreset:
			_DeleteEqualizerPreset(message->GetString("name", ""));
			break;
		case kMsgSortBy:
		{
			// scripting: like clicking a column title
			int32 column = message->GetInt32("column", COLUMN_ARTIST);
			if (column < COLUMN_NUMBER || column >= COLUMN_COUNT)
				break;
			fSongList->SetSortState(sort_state((song_column)column,
				message->GetBool("ascending", true)));
			break;
		}
		case kMsgSourceSelected:
		{
			// scripting: "select" selects a source (BListView adds its own
			// "index" field to selection messages)
			int32 index;
			if (message->FindInt32("select", &index) == B_OK
				&& index != fSourceList->CurrentSelection())
				fSourceList->Select(index);
			_SourceSelectionChanged();
			break;
		}
		case kMsgArtistSelected:
		{
			if (fArtistList->CurrentSelection() < 0) {
				if (fArtistList->CountItems() > 0)
					fArtistList->Select(0);
				break;
			}
			BString artist;
			bool all = !fArtistList->GetSelection(artist);
			if (all == fArtistAll && (all || artist == fArtist))
				break;
			fArtistAll = all;
			fArtist = artist;
			fAlbumAll = true;
			_UpdateAlbums();
			_UpdateSongs(false);
			break;
		}
		case kMsgAlbumSelected:
		{
			if (fAlbumList->CurrentSelection() < 0) {
				if (fAlbumList->CountItems() > 0)
					fAlbumList->Select(0);
				break;
			}
			BString album;
			bool all = !fAlbumList->GetSelection(album);
			if (all == fAlbumAll && (all || album == fAlbum))
				break;
			fAlbumAll = all;
			fAlbum = album;
			_UpdateSongs(false);
			break;
		}
		case kMsgSongSelected:
		{
			// scripting: "select" selects rows
			int32 index;
			if (message->FindInt32("select", &index) != B_OK)
				break;
			fSongList->DeselectAll();
			for (int32 i = 0; message->FindInt32("select", i, &index) == B_OK;
					i++) {
				BRow* row = fSongList->RowAt(index);
				if (row != NULL)
					fSongList->AddToSelection(row);
			}
			break;
		}
		case kMsgSongContextMenu:
		{
			BPoint where;
			if (message->FindPoint("where", &where) == B_OK)
				_ShowSongContextMenu(where);
			break;
		}
		case kMsgSourceContextMenu:
		{
			BPoint where;
			if (message->FindPoint("where", &where) == B_OK)
				_ShowSourceContextMenu(message->GetInt32("index", -1), where);
			break;
		}

		// scripting
		case kMsgGetState:
			_ReplyState(message);
			break;
		case kMsgGetSongs:
			_ReplySongs(message);
			break;
		case kMsgGetBrowser:
			_ReplyBrowser(message);
			break;
		case kMsgGetSources:
			_ReplySources(message);
			break;
		case kMsgGetShares:
			_ReplyShares(message);
			break;

		default:
			handled = false;
			BWindow::MessageReceived(message);
			break;
	}

	if (handled && message->IsSourceWaiting()) {
		BMessage reply(B_REPLY);
		reply.AddInt32("error", B_OK);
		message->SendReply(&reply);
	}
}


void
MainWindow::MenusBeginning()
{
	bool hasSelection = fSongList->CurrentSelection() != NULL;
	// the songs of a shared library can only be played
	bool shared = _IsShareSource();
	Player::State state = fPlayer->CurrentState();
	const Song* playing = state != Player::STOPPED
		? fLibrary.SongById(fPlayer->CurrentSong()) : NULL;
	fPlayItem->SetLabel(state == Player::PLAYING ? "Pause" : "Play");
	fDeleteItem->SetLabel(_IsUserPlaylist() ? "Remove from Playlist"
		: "Delete from Library" B_UTF8_ELLIPSIS);
	fDeleteItem->SetEnabled(hasSelection && !shared);
	fRenameItem->SetEnabled(_IsUserPlaylist());
	fDeletePlaylistItem->SetEnabled(_IsUserPlaylist());
	fFromSelectionItem->SetEnabled(hasSelection && !shared);
	fShowInTrackerItem->SetEnabled(hasSelection ? !shared
		: playing != NULL && playing->share == 0);
	fShowCurrentItem->SetEnabled(state != Player::STOPPED);
	fGetInfoItem->SetEnabled(hasSelection && !shared);
	const Share* share = NULL;
	for (size_t i = 0; i < fShares.size(); i++) {
		if (fShares[i].id == _SourceShare())
			share = &fShares[i];
	}
	fDisconnectItem->SetEnabled(share != NULL
		&& share->state != kShareIdle);
	fMiniPlayerItem->SetLabel(fMiniPlayer ? "Switch to Full Player"
		: "Switch to Mini Player");
	fBrowserItem->SetLabel(fBrowserVisible ? "Hide Browser" : "Show Browser");
	fEqualizerItem->SetLabel(fEqualizerVisible ? "Hide Equalizer"
		: "Show Equalizer");
	BWindow::MenusBeginning();
}


// #pragma mark - sources and filtering


void
MainWindow::_RebuildSourceList()
{
	for (int32 i = fSourceList->CountItems() - 1; i >= 0; i--)
		delete fSourceList->RemoveItem(i);

	BList items;
	items.AddItem(new SourceItem(SOURCE_LIBRARY, -1,
		Library::SourceName(SOURCE_LIBRARY)));
	// the shared libraries, as in iTunes, each with its playlists
	for (size_t i = 0; i < fShares.size(); i++) {
		const Share& share = fShares[i];
		SourceItem* item = new SourceItem(SOURCE_SHARE, share.id, share.name);
		item->SetConnected(share.state == kShareConnected);
		item->SetBusy(share.state == kShareConnecting);
		items.AddItem(item);
		std::vector<const Playlist*> playlists;
		fLibrary.GetSharedPlaylists(share.id, playlists);
		for (size_t j = 0; j < playlists.size(); j++) {
			item = new SourceItem(SOURCE_SHARE_PLAYLIST, playlists[j]->id,
				playlists[j]->name);
			item->SetIndented(true);
			items.AddItem(item);
		}
	}
	items.AddItem(new SourceItem(SOURCE_RECENTLY_ADDED, -1,
		Library::SourceName(SOURCE_RECENTLY_ADDED)));
	items.AddItem(new SourceItem(SOURCE_RECENTLY_PLAYED, -1,
		Library::SourceName(SOURCE_RECENTLY_PLAYED)));
	items.AddItem(new SourceItem(SOURCE_TOP_25, -1,
		Library::SourceName(SOURCE_TOP_25)));
	for (int32 i = 0; i < fLibrary.CountPlaylists(); i++) {
		const Playlist* playlist = fLibrary.PlaylistAt(i);
		items.AddItem(new SourceItem(SOURCE_PLAYLIST, playlist->id,
			playlist->name));
	}
	fSourceList->AddList(&items);
	fSourceListStale = false;
	_UpdatePlayingSource();

	int32 index = fSourceList->IndexOf(fSourceKind, fSourcePlaylist);
	fSourceList->Select(index >= 0 ? index : 0);
}


void
MainWindow::_SelectSource(source_kind kind, int32 playlist)
{
	int32 index = fSourceList->IndexOf(kind, playlist);
	if (index < 0)
		index = 0;
	if (fSourceList->CurrentSelection() != index)
		fSourceList->Select(index);
	fSourceList->ScrollToSelection();
	_SourceSelectionChanged();
}


void
MainWindow::_SourceSelectionChanged()
{
	SourceItem* item = fSourceList->SourceAt(fSourceList->CurrentSelection());
	if (item == NULL) {
		// keep something selected
		int32 index = fSourceList->IndexOf(fSourceKind, fSourcePlaylist);
		if (fSourceList->CountItems() > 0)
			fSourceList->Select(index >= 0 ? index : 0);
		return;
	}

	source_kind kind = item->Kind();
	int32 playlist = item->PlaylistId();
	if (fSortKind >= 0 && kind == fSourceKind && playlist == fSourcePlaylist)
		return;

	// each kind of source keeps its own sort order
	if (fSortKind >= 0)
		fSongList->GetSortState(fSortStates[fSortKind]);
	if (kind != fSortKind) {
		fSongList->SetSortState(fSortStates[kind]);
		fSortKind = kind;
	}
	fSongList->SetPlaylistMode(kind == SOURCE_PLAYLIST
		|| kind == SOURCE_SHARE_PLAYLIST);
	fSongList->SetDragEnabled(kind != SOURCE_SHARE
		&& kind != SOURCE_SHARE_PLAYLIST);

	fSourceKind = kind;
	fSourcePlaylist = playlist;
	fArtistAll = true;
	fAlbumAll = true;
	_RefreshAll(false);

	// selecting a shared library connects to it
	Share* share = kind == SOURCE_SHARE ? _ShareById(playlist) : NULL;
	if (share != NULL && share->state == kShareIdle)
		_ConnectShare(share->id);
}


void
MainWindow::_RefreshAll(bool keepSelection)
{
	fLibrary.GetSongs(fSourceKind, fSourcePlaylist, fSourceSongs);
	_FilterSearch();
	_UpdateArtists();
	_UpdateAlbums();
	_UpdateSongs(keepSelection);
}


bool
MainWindow::_MatchesSearch(const Song* song) const
{
	switch (fSearchScope) {
		case SEARCH_ARTISTS:
			return contains_all_words(song->searchArtist, fSearchWords);
		case SEARCH_ALBUMS:
			return contains_all_words(song->searchAlbum, fSearchWords);
		case SEARCH_SONGS:
			return contains_all_words(song->searchTitle, fSearchWords);
		case SEARCH_GENRES:
			return contains_all_words(song->searchGenre, fSearchWords);
		default:
		{
			// every word must be in one of the fields
			std::string all = song->searchTitle;
			all += '\n';
			all += song->searchArtist;
			all += '\n';
			all += song->searchAlbum;
			all += '\n';
			all += song->searchGenre;
			return contains_all_words(all, fSearchWords);
		}
	}
}


void
MainWindow::_FilterSearch()
{
	fSearched.clear();
	fSearched.reserve(fSourceSongs.size());
	for (size_t i = 0; i < fSourceSongs.size(); i++) {
		const Song* song = fLibrary.SongById(fSourceSongs[i]);
		if (song != NULL && (fSearchWords.empty() || _MatchesSearch(song)))
			fSearched.push_back((int32)i);
	}
}


void
MainWindow::_UpdateArtists()
{
	std::map<BString, BString> artists;
		// name -> sort key
	for (size_t i = 0; i < fSearched.size(); i++) {
		const Song* song = fLibrary.SongById(fSourceSongs[fSearched[i]]);
		if (artists.find(song->artist) == artists.end())
			artists[song->artist] = song->artistKey;
	}

	std::vector<BrowserEntry> entries;
	bool unknown = false;
	for (std::map<BString, BString>::iterator it = artists.begin();
			it != artists.end(); it++) {
		if (it->first.IsEmpty()) {
			unknown = true;
			continue;
		}
		BrowserEntry entry;
		entry.key = it->second;
		entry.value = it->first;
		entries.push_back(entry);
	}
	std::sort(entries.begin(), entries.end());
	if (unknown)
		entries.push_back(BrowserEntry());

	BString allLabel;
	allLabel << "All (" << (int32)entries.size()
		<< (entries.size() == 1 ? " Artist)" : " Artists)");
	set_browser_items(fArtistList, entries, "Unknown Artist", allLabel);

	if (!fArtistAll && artists.find(fArtist) == artists.end())
		fArtistAll = true;
	fArtistList->SelectValue(fArtist, fArtistAll);
}


void
MainWindow::_UpdateAlbums()
{
	bool filterArtist = fBrowserVisible && !fArtistAll;
	std::map<BString, BString> albums;
	for (size_t i = 0; i < fSearched.size(); i++) {
		const Song* song = fLibrary.SongById(fSourceSongs[fSearched[i]]);
		if (filterArtist && song->artist != fArtist)
			continue;
		if (albums.find(song->album) == albums.end())
			albums[song->album] = song->albumKey;
	}

	std::vector<BrowserEntry> entries;
	bool unknown = false;
	for (std::map<BString, BString>::iterator it = albums.begin();
			it != albums.end(); it++) {
		if (it->first.IsEmpty()) {
			unknown = true;
			continue;
		}
		BrowserEntry entry;
		entry.key = it->second;
		entry.value = it->first;
		entries.push_back(entry);
	}
	std::sort(entries.begin(), entries.end());
	if (unknown)
		entries.push_back(BrowserEntry());

	BString allLabel;
	allLabel << "All (" << (int32)entries.size()
		<< (entries.size() == 1 ? " Album)" : " Albums)");
	set_browser_items(fAlbumList, entries, "Unknown Album", allLabel);

	if (!fAlbumAll && albums.find(fAlbum) == albums.end())
		fAlbumAll = true;
	fAlbumList->SelectValue(fAlbum, fAlbumAll);
}


void
MainWindow::_UpdateSongs(bool keepSelection)
{
	bool filterArtist = fBrowserVisible && !fArtistAll;
	bool filterAlbum = fBrowserVisible && !fAlbumAll;

	fVisible.clear();
	std::vector<int32> songs;
	for (size_t i = 0; i < fSearched.size(); i++) {
		int32 position = fSearched[i];
		const Song* song = fLibrary.SongById(fSourceSongs[position]);
		if (filterArtist && song->artist != fArtist)
			continue;
		if (filterAlbum && song->album != fAlbum)
			continue;
		fVisible.push_back(position);
		songs.push_back(song->id);
	}
	fSongList->SetSongs(songs, fVisible, fLibrary, keepSelection);
	fSongList->SetPlayingSong(fPlayer->CurrentState() != Player::STOPPED
		? fPlayer->CurrentSong() : -1);
	_UpdateStatusText();
}


void
MainWindow::_UpdateStatusText()
{
	std::vector<int32> songs;
	songs.reserve(fVisible.size());
	for (size_t i = 0; i < fVisible.size(); i++)
		songs.push_back(fSourceSongs[fVisible[i]]);
	Share* share = fSourceKind == SOURCE_SHARE
		? _ShareById(fSourcePlaylist) : NULL;
	if (share != NULL && share->state == kShareConnecting) {
		BString text;
		text << "Connecting to \xe2\x80\x9c" << share->name
			<< "\xe2\x80\x9d" B_UTF8_ELLIPSIS;
		fStatusText->SetText(text);
		return;
	}
	bigtime_t duration;
	off_t size;
	fLibrary.GetTotals(songs, duration, size);
	fStatusText->SetText(format_totals((int32)songs.size(), duration, size));
}


bool
MainWindow::_IsFiltered() const
{
	return !fSearchWords.empty()
		|| (fBrowserVisible && (!fArtistAll || !fAlbumAll));
}


void
MainWindow::_ScheduleRefresh()
{
	if (fRefreshRunner != NULL)
		return;
	BMessage refresh(kMsgRefreshViews);
	fRefreshRunner = new BMessageRunner(BMessenger(this), &refresh,
		kRefreshDelay, 1);
}


void
MainWindow::_SetBrowserVisible(bool visible)
{
	if (visible == fBrowserVisible && fRightSplit->IsItemCollapsed(0) != visible)
		return;
	fBrowserVisible = visible;
	fRightSplit->SetItemCollapsed(0, !visible);
	fBrowseButton->SetValue(visible ? B_CONTROL_ON : B_CONTROL_OFF);
	if (!visible) {
		fArtistAll = true;
		fAlbumAll = true;
	}
	if (fSortKind >= 0)
		_RefreshAll(true);
}


// #pragma mark - playback


void
MainWindow::_PlayFromView(int32 index)
{
	std::vector<int32> songs;
	fSongList->GetSongs(songs);
	if (songs.empty())
		return;
	if (index >= (int32)songs.size())
		index = -1;

	fQueue.SetSongs(songs, index, fShuffle);
	fPlayingKind = fSourceKind;
	fPlayingPlaylist = fSourcePlaylist;
	fErrorsInARow = 0;
	_PlaySong(fQueue.Current());
}


void
MainWindow::_PlaySong(int32 id, bool paused)
{
	const Song* song = fLibrary.SongById(id);
	if (song == NULL) {
		_Stop();
		return;
	}

	fPlayingSerial = fPlayer->Play(song->path, id);
	if (paused)
		fPlayer->Pause();
	fDisplay->SetSong(song->title, song->artist, song->album);
	fDisplay->SetTime(0, song->duration);
	fSongList->SetPlayingSong(id);
	_UpdatePlayState();
	_UpdatePlayingSource();
}


void
MainWindow::_PlayPause()
{
	switch (fPlayer->CurrentState()) {
		case Player::PLAYING:
			fPlayer->Pause();
			break;
		case Player::PAUSED:
			fPlayer->Resume();
			break;
		case Player::STOPPED:
			// the selected song, or the first (a random one when shuffling)
			_PlayFromView(fSongList->SelectedIndex());
			break;
	}
	_UpdatePlayState();
}


void
MainWindow::_Next()
{
	if (fPlayer->CurrentState() == Player::STOPPED || fQueue.IsEmpty())
		return;

	bool paused = fPlayer->CurrentState() == Player::PAUSED;
	int32 next = fQueue.Next(fRepeat != REPEAT_OFF);
	if (next < 0) {
		_Stop();
		return;
	}
	_PlaySong(next, paused);
}


void
MainWindow::_Previous()
{
	if (fPlayer->CurrentState() == Player::STOPPED || fQueue.IsEmpty())
		return;

	if (fPlayer->Position() > kRestartThreshold) {
		fPlayer->Seek(0);
		return;
	}
	bool paused = fPlayer->CurrentState() == Player::PAUSED;
	_PlaySong(fQueue.Previous(fRepeat != REPEAT_OFF), paused);
}


void
MainWindow::_Stop()
{
	fPlayer->Stop();
	fPlayingSerial = fPlayer->Serial();
	fDisplay->ClearSong();
	fSongList->SetPlayingSong(-1);
	_UpdatePlayState();
	_UpdatePlayingSource();
}


void
MainWindow::_SongEnded(int32 id)
{
	Song* song = fLibrary.SongById(id);
	if (song != NULL) {
		fLibrary.SongPlayed(song);
		fSongList->UpdateSong(song);
		if (fSourceKind == SOURCE_RECENTLY_PLAYED
			|| fSourceKind == SOURCE_TOP_25)
			_ScheduleRefresh();
	}
	fErrorsInARow = 0;
	_Advance(false);
}


void
MainWindow::_Advance(bool afterError)
{
	if (fRepeat == REPEAT_ONE && !afterError && fQueue.Current() >= 0) {
		_PlaySong(fQueue.Current());
		return;
	}

	int32 next = fQueue.Next(fRepeat != REPEAT_OFF);
	if (next < 0) {
		// the end of the list
		_Stop();
		return;
	}
	_PlaySong(next);
}


void
MainWindow::_PlayerError(BMessage* message)
{
	const char* error = message->GetString("error", "unknown error");
	if (message->GetBool("output", false)) {
		_Stop();
		BString text("No audio output: ");
		text << error;
		fDisplay->ShowMessage(text);
		if (!fOutputErrorShown) {
			fOutputErrorShown = true;
			BString alertText;
			alertText << APP_NAME " could not connect to the audio output ("
				<< error << ").\n\nCheck the Media preferences, or restart "
				"the media services there.";
			BAlert* alert = new BAlert(APP_NAME ": No sound", alertText, "OK",
				NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go(NULL);
		}
		return;
	}

	int32 id = message->GetInt32("song", -1);
	fSongList->SetFailed(id, true);
	const Song* song = fLibrary.SongById(id);
	BString text;
	text << "\xe2\x80\x9c" << (song != NULL ? song->title.String() : "?")
		<< "\xe2\x80\x9d could not be played: " << error;
	fDisplay->ShowMessage(text);

	if (++fErrorsInARow >= fQueue.CountSongs()) {
		// nothing in the list can be played
		_Stop();
		return;
	}
	_Advance(true);
}


void
MainWindow::_UpdatePlayState()
{
	Player::State state = fPlayer->CurrentState();
	fPlayButton->SetSymbol(state == Player::PLAYING ? GLYPH_PAUSE
		: GLYPH_PLAY);
}


void
MainWindow::_UpdatePlayingSource()
{
	bool playing = fPlayer->CurrentState() != Player::STOPPED;
	for (int32 i = 0; i < fSourceList->CountItems(); i++) {
		SourceItem* item = fSourceList->SourceAt(i);
		bool isPlaying = playing && item->Kind() == fPlayingKind
			&& (fPlayingKind < SOURCE_PLAYLIST
				|| item->PlaylistId() == fPlayingPlaylist);
		if (isPlaying != item->IsPlaying()) {
			item->SetPlaying(isPlaying);
			fSourceList->InvalidateItem(i);
		}
	}
}


void
MainWindow::_SetVolume(int32 volume)
{
	volume = std::max((int32)0, std::min((int32)100, volume));
	fVolume = volume;
	fVolumeSlider->SetValue(volume);
	// a quadratic curve sounds more even than a linear one
	float gain = volume / 100.0f;
	fPlayer->SetVolume(gain * gain);
}


void
MainWindow::_SetShuffle(bool shuffle)
{
	fShuffle = shuffle;
	fQueue.SetShuffle(shuffle);
	fShuffleButton->SetValue(shuffle ? B_CONTROL_ON : B_CONTROL_OFF);
	fShuffleItem->SetMarked(shuffle);
}


void
MainWindow::_SetRepeat(int32 mode)
{
	if (mode < REPEAT_OFF || mode > REPEAT_ONE)
		mode = REPEAT_OFF;
	fRepeat = mode;
	fRepeatButton->SetValue(mode != REPEAT_OFF ? B_CONTROL_ON : B_CONTROL_OFF);
	fRepeatButton->SetSymbol(mode == REPEAT_ONE ? GLYPH_REPEAT_ONE
		: GLYPH_REPEAT);
	for (int32 i = 0; i < 3; i++)
		fRepeatItems[i]->SetMarked(i == mode);
}


void
MainWindow::_ShowCurrentSong()
{
	if (fPlayer->CurrentState() == Player::STOPPED)
		return;
	_SetMiniPlayer(false);
	int32 song = fPlayer->CurrentSong();

	if (fSourceList->IndexOf(fPlayingKind, fPlayingPlaylist) >= 0)
		_SelectSource(fPlayingKind, fPlayingPlaylist);
	if (fSongList->IndexOfSong(song) < 0 && _IsFiltered()) {
		// show everything
		fSearchWords.clear();
		fSearchField->SetText("");
		fArtistAll = true;
		fAlbumAll = true;
		_RefreshAll(true);
	}
	if (fSongList->IndexOfSong(song) < 0) {
		const Song* playing = fLibrary.SongById(song);
		if (playing != NULL && playing->share != 0)
			_SelectSource(SOURCE_SHARE, playing->share);
		else
			_SelectSource(SOURCE_LIBRARY, -1);
	}
	fSongList->SelectSong(song);
	fSongList->MakeFocus(true);
}


// #pragma mark - mini player


void
MainWindow::Zoom(BPoint origin, float width, float height)
{
	_SetMiniPlayer(!fMiniPlayer);
}


void
MainWindow::_SetMiniPlayer(bool mini)
{
	if (mini == fMiniPlayer)
		return;

	// everything but the control bar, and in it the search and browse
	// controls (their whole group, which would leave its spacing otherwise)
	BView* views[] = { fMenuBar, fMainSplit, fBottomBar };
	const size_t count = sizeof(views) / sizeof(views[0]);

	if (mini) {
		fFullFrame = Frame();
		for (size_t i = 0; i < count; i++)
			views[i]->Hide();
		fSearchAndBrowse->SetVisible(false);
		fMiniPlayer = true;
		// no resize knob over the controls; the borders still resize it
		SetLook(B_TITLED_WINDOW_LOOK);
		SetFeel(fMiniOnTop ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL);
		UpdateSizeLimits();

		BRect frame = fMiniFrame;
		if (!frame.IsValid()) {
			// where the full player was, not too wide
			frame = fFullFrame;
			frame.right = frame.left
				+ std::min(frame.Width(), ceilf(be_plain_font->Size() * 52));
		}
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Width(), 0);
			// the height is what the controls need
	} else {
		fMiniFrame = Frame();
		for (size_t i = 0; i < count; i++)
			views[i]->Show();
		fSearchAndBrowse->SetVisible(true);
		fMiniPlayer = false;
		SetLook(B_DOCUMENT_WINDOW_LOOK);
		SetFeel(B_NORMAL_WINDOW_FEEL);
		UpdateSizeLimits();

		if (fFullFrame.IsValid()) {
			MoveTo(fFullFrame.LeftTop());
			ResizeTo(fFullFrame.Width(), fFullFrame.Height());
		}
	}
	fMiniPlayerItem->SetLabel(fMiniPlayer ? "Switch to Full Player"
		: "Switch to Mini Player");
}


void
MainWindow::_SetMiniOnTop(bool onTop)
{
	fMiniOnTop = onTop;
	fMiniOnTopItem->SetMarked(onTop);
	if (fMiniPlayer)
		SetFeel(onTop ? B_FLOATING_ALL_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL);
}


void
MainWindow::_ShowMiniContextMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("mini player", false, false);
	menu->AddItem(new BMenuItem(fPlayer->CurrentState() == Player::PLAYING
		? "Pause" : "Play", new BMessage(kMsgPlayPause)));
	menu->AddItem(new BMenuItem("Next Song", new BMessage(kMsgNext)));
	menu->AddItem(new BMenuItem("Previous Song", new BMessage(kMsgPrevious)));
	menu->AddItem(new BMenuItem("Stop", new BMessage(kMsgStop)));
	menu->AddSeparatorItem();

	BMenuItem* shuffle = new BMenuItem("Shuffle",
		new BMessage(kMsgToggleShuffle));
	shuffle->SetMarked(fShuffle);
	menu->AddItem(shuffle);
	BMenu* repeat = new BMenu("Repeat");
	const char* repeatLabels[] = { "Off", "All", "One" };
	for (int32 i = 0; i < 3; i++) {
		BMessage* message = new BMessage(kMsgSetRepeat);
		message->AddInt32("mode", i);
		BMenuItem* item = new BMenuItem(repeatLabels[i], message);
		item->SetMarked(i == fRepeat);
		item->SetTarget(this);
		repeat->AddItem(item);
	}
	menu->AddItem(repeat);
	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem(fEqualizerVisible ? "Hide Equalizer"
		: "Show Equalizer", new BMessage(kMsgShowEqualizer)));
	BMenuItem* onTop = new BMenuItem("Keep on Top",
		new BMessage(kMsgMiniOnTop));
	onTop->SetMarked(fMiniOnTop);
	menu->AddItem(onTop);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Switch to Full Player",
		new BMessage(kMsgMiniPlayer)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED)));

	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


// #pragma mark - equalizer


void
MainWindow::_CreateEqualizerWindow()
{
	BRect frame = fEqualizerFrame;
	if (!frame.IsValid()) {
		// near the bottom left of the main window, out of the song list's
		// way as far as possible
		frame = BRect(0, 0, 100, 100);
		frame.OffsetTo(Frame().left + 40, Frame().bottom - 340);
	}
	EqualizerWindow* window = new EqualizerWindow(frame, BMessenger(this));
	// running, but hidden until asked for
	window->Hide();
	window->Show();
	fEqualizerWindow = BMessenger(window);

	_ApplyEqualizer(true);
	if (fEqualizerVisible) {
		BMessage show(kMsgShowEqualizer);
		show.AddBool("show", true);
		fEqualizerWindow.SendMessage(&show);
	}
}


static float
clamp_gain(float gain)
{
	return std::max(-kEqualizerMaxGain, std::min(kEqualizerMaxGain, gain));
}


void
MainWindow::_EqualizerChanged(BMessage* message)
{
	const char* preset = message->GetString("preset", NULL);

	if (message->GetBool("from window", false)) {
		// the user moved a slider or clicked "On": the window shows that
		// already
		unarchive_equalizer(*message, fEqualizer);
		fEqualizerPreset = preset != NULL ? preset : "Manual";
		_ApplyEqualizer(false);
		return;
	}

	if (preset != NULL) {
		EqualizerSettings values;
		if (_FindEqualizerPreset(preset, values)) {
			fEqualizer.preamp = values.preamp;
			for (int i = 0; i < kEqualizerBands; i++)
				fEqualizer.bands[i] = values.bands[i];
			fEqualizer.enabled = true;
			fEqualizerPreset = preset;
		}
	}

	// scripting: single values make it a manual setting
	bool manual = false;
	float value;
	if (message->FindFloat("preamp", &value) == B_OK) {
		fEqualizer.preamp = clamp_gain(value);
		manual = true;
	}
	for (int32 i = 0; i < kEqualizerBands
			&& message->FindFloat("gains", i, &value) == B_OK; i++) {
		fEqualizer.bands[i] = clamp_gain(value);
		manual = true;
	}
	int32 band;
	if (message->FindInt32("band", &band) == B_OK && band >= 0
		&& band < kEqualizerBands
		&& message->FindFloat("gain", &value) == B_OK) {
		fEqualizer.bands[band] = clamp_gain(value);
		manual = true;
	}
	if (manual) {
		fEqualizer.enabled = true;
		fEqualizerPreset = "Manual";
	}
	bool enabled;
	if (message->FindBool("enabled", &enabled) == B_OK)
		fEqualizer.enabled = enabled;

	_ApplyEqualizer(true);
}


void
MainWindow::_ApplyEqualizer(bool updateWindow)
{
	fPlayer->SetEqualizer(fEqualizer);
	fEqualizerButton->SetValue(fEqualizer.enabled ? B_CONTROL_ON
		: B_CONTROL_OFF);
	if (!updateWindow)
		return;

	BMessage update(kMsgEqualizerUpdate);
	archive_equalizer(fEqualizer, update);
	update.AddString("preset", fEqualizerPreset);
	for (size_t i = 0; i < fUserPresets.size(); i++)
		update.AddString("user preset", fUserPresets[i].name);
	fEqualizerWindow.SendMessage(&update);
}


bool
MainWindow::_FindEqualizerPreset(const char* name,
	EqualizerSettings& settings) const
{
	// the user's presets first: they may replace a built-in one
	for (size_t i = 0; i < fUserPresets.size(); i++) {
		if (fUserPresets[i].name == name) {
			settings = fUserPresets[i].settings;
			return true;
		}
	}
	const EqualizerPreset* preset = find_equalizer_preset(name);
	if (preset == NULL)
		return false;
	settings.preamp = preset->preamp;
	for (int i = 0; i < kEqualizerBands; i++)
		settings.bands[i] = preset->bands[i];
	return true;
}


void
MainWindow::_MakeEqualizerPreset(const char* name)
{
	BString presetName(name);
	presetName.Trim();
	if (presetName.IsEmpty() || presetName == "Manual")
		return;

	bool replaced = false;
	for (size_t i = 0; i < fUserPresets.size(); i++) {
		if (fUserPresets[i].name == presetName) {
			fUserPresets[i].settings = fEqualizer;
			replaced = true;
		}
	}
	if (!replaced) {
		UserPreset preset;
		preset.name = presetName;
		preset.settings = fEqualizer;
		fUserPresets.push_back(preset);
	}
	fEqualizerPreset = presetName;
	_ApplyEqualizer(true);
}


void
MainWindow::_DeleteEqualizerPreset(const char* name)
{
	for (size_t i = 0; i < fUserPresets.size(); i++) {
		if (fUserPresets[i].name == name) {
			fUserPresets.erase(fUserPresets.begin() + i);
			break;
		}
	}
	if (fEqualizerPreset == name)
		fEqualizerPreset = "Manual";
	_ApplyEqualizer(true);
}


// #pragma mark - library and playlists


void
MainWindow::_Import(const BMessage& refs, int32 playlist, bool play)
{
	if (!refs.HasRef("refs") && !refs.HasString("path"))
		return;

	BMessage request;
	entry_ref ref;
	for (int32 i = 0; refs.FindRef("refs", i, &ref) == B_OK; i++)
		request.AddRef("refs", &ref);
	const char* path;
	for (int32 i = 0; refs.FindString("path", i, &path) == B_OK; i++)
		request.AddString("path", path);

	int32 id = fNextImportRequest++;
	request.AddInt32("request", id);
	request.AddInt32("playlist", playlist);
	request.AddBool("play", play);
	if (play)
		fImportedForPlay[id] = std::vector<int32>();

	fImportsPending++;
	fDisplay->SetImportStatus("", 0);
	fImporter->Import(&request);
}


void
MainWindow::_ImportedSongs(BMessage* message)
{
	int32 playlist = message->GetInt32("playlist", -1);
	int32 request = message->GetInt32("request", 0);

	std::vector<int32> ids;
	BMessage entry;
	for (int32 i = 0; message->FindMessage("song", i, &entry) == B_OK; i++) {
		int32 id;
		if (entry.FindInt32("existing", &id) == B_OK) {
			if (fLibrary.SongById(id) != NULL)
				ids.push_back(id);
			continue;
		}
		Song* song = unarchive_song(entry);
		if (song == NULL)
			continue;
		BString path = song->path;
		Song* added = fLibrary.AddSong(song);
		id = added != NULL ? added->id : fLibrary.SongIdForPath(path);
		if (id >= 0)
			ids.push_back(id);
	}

	if (playlist >= 0)
		_AddToPlaylist(playlist, ids, false);
	std::map<int32, std::vector<int32> >::iterator found
		= fImportedForPlay.find(request);
	if (found != fImportedForPlay.end())
		found->second.insert(found->second.end(), ids.begin(), ids.end());

	_ScheduleRefresh();
}


void
MainWindow::_ImportDone(BMessage* message)
{
	if (--fImportsPending <= 0) {
		fImportsPending = 0;
		fDisplay->ClearImportStatus();
	}
	if (message->GetInt32("files", 0) == 0)
		fDisplay->ShowMessage("No audio files found");

	_SaveLibrary();
	delete fRefreshRunner;
	fRefreshRunner = NULL;
	_RefreshAll(true);

	// files opened from Tracker play right away
	int32 request = message->GetInt32("request", 0);
	std::map<int32, std::vector<int32> >::iterator found
		= fImportedForPlay.find(request);
	if (found == fImportedForPlay.end())
		return;
	std::vector<int32> songs = found->second;
	fImportedForPlay.erase(found);
	if (songs.empty())
		return;

	fQueue.SetSongs(songs, 0, fShuffle);
	fPlayingKind = SOURCE_LIBRARY;
	fPlayingPlaylist = -1;
	fErrorsInARow = 0;
	_PlaySong(fQueue.Current());
}


Playlist*
MainWindow::_NewPlaylist(const char* name, const std::vector<int32>& songs,
	bool rename)
{
	BString playlistName;
	if (name != NULL && name[0] != '\0')
		playlistName = name;
	else
		playlistName = fLibrary.UniquePlaylistName("untitled playlist");

	Playlist* playlist = fLibrary.AddPlaylist(playlistName);
	playlist->songs = songs;
	_KeepLocalSongs(playlist->songs);
	_RebuildSourceList();
	_SelectSource(SOURCE_PLAYLIST, playlist->id);
	if (rename) {
		fSourceList->StartRename(fSourceList->IndexOf(SOURCE_PLAYLIST,
			playlist->id));
	}
	_SaveLibrary();
	return playlist;
}


void
MainWindow::_AddToPlaylist(int32 id, const std::vector<int32>& songs,
	bool refreshNow)
{
	Playlist* playlist = fLibrary.PlaylistById(id);
	std::vector<int32> local(songs);
	_KeepLocalSongs(local);
	if (playlist == NULL || local.empty())
		return;
	playlist->songs.insert(playlist->songs.end(), local.begin(), local.end());
	fLibrary.SetDirty();
	if (fSourceKind != SOURCE_PLAYLIST || fSourcePlaylist != id)
		return;
	if (refreshNow)
		_RefreshAll(true);
	else
		_ScheduleRefresh();
}


void
MainWindow::_RenamePlaylist(int32 id, const char* name)
{
	Playlist* playlist = fLibrary.PlaylistById(id);
	BString newName(name);
	newName.Trim();
	if (playlist == NULL || newName.IsEmpty() || newName == playlist->name)
		return;

	playlist->name = newName;
	fLibrary.SetDirty();
	int32 index = fSourceList->IndexOf(SOURCE_PLAYLIST, id);
	SourceItem* item = fSourceList->SourceAt(index);
	if (item != NULL) {
		item->SetName(newName);
		fSourceList->InvalidateItem(index);
	}
	_SaveLibrary();
}


void
MainWindow::_DeletePlaylist(int32 id, bool confirmed)
{
	if (id < 0) {
		if (!_IsUserPlaylist())
			return;
		id = fSourcePlaylist;
	}
	Playlist* playlist = fLibrary.PlaylistById(id);
	if (playlist == NULL)
		return;

	if (!confirmed && !playlist->songs.empty()) {
		BString text;
		text << "Are you sure you want to delete the playlist \xe2\x80\x9c"
			<< playlist->name << "\xe2\x80\x9d?\n\n"
			"The songs stay in the library.";
		BAlert* alert = new BAlert(APP_NAME ": Delete playlist", text,
			"Cancel", "Delete", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		BMessage* reply = new BMessage(kMsgDeletePlaylist);
		reply->AddInt32("playlist", id);
		alert->Go(new BInvoker(reply, this));
		return;
	}

	int32 index = fSourceList->IndexOf(SOURCE_PLAYLIST, id);
	bool shown = fSourceKind == SOURCE_PLAYLIST && fSourcePlaylist == id;
	if (fPlayingKind == SOURCE_PLAYLIST && fPlayingPlaylist == id)
		fPlayingPlaylist = -1;
	fLibrary.RemovePlaylist(id);
	_SaveLibrary();

	if (shown) {
		// show the playlist that took its place, or the one above it
		_RebuildSourceList();
		int32 next = std::min(index, (int32)fSourceList->CountItems() - 1);
		SourceItem* item = fSourceList->SourceAt(next);
		if (item != NULL)
			_SelectSource(item->Kind(), item->PlaylistId());
	} else
		_RebuildSourceList();
}


void
MainWindow::_DeleteSongs(bool confirmed)
{
	std::vector<int32> selected;
	fSongList->GetSelectedSongs(selected);
	if (selected.empty() || _IsShareSource())
		return;

	if (_IsUserPlaylist()) {
		Playlist* playlist = fLibrary.PlaylistById(fSourcePlaylist);
		if (playlist == NULL)
			return;
		std::vector<int32> positions;
		fSongList->GetSelectedPositions(positions);
		std::set<int32> removed(positions.begin(), positions.end());
		std::vector<int32> songs;
		for (size_t i = 0; i < playlist->songs.size(); i++) {
			if (removed.count((int32)i) == 0)
				songs.push_back(playlist->songs[i]);
		}
		playlist->songs.swap(songs);
		fLibrary.SetDirty();
		_SaveLibrary();
		_RefreshAll(true);
		return;
	}

	if (!confirmed) {
		fPendingDelete.clear();
		fPendingDelete.insert(selected.begin(), selected.end());
		BString text;
		if (selected.size() == 1) {
			const Song* song = fLibrary.SongById(selected[0]);
			text << "Are you sure you want to remove \xe2\x80\x9c"
				<< (song != NULL ? song->title.String() : "")
				<< "\xe2\x80\x9d from the library?";
		} else {
			text << "Are you sure you want to remove the " << (int32)selected.size()
				<< " selected songs from the library?";
		}
		text << "\n\nThe files themselves are not deleted.";
		BAlert* alert = new BAlert(APP_NAME ": Remove songs", text, "Cancel",
			"Remove", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->Go(new BInvoker(new BMessage(kMsgReallyDelete), this));
		return;
	}

	_RemoveFromLibrary(std::set<int32>(selected.begin(), selected.end()));
}


void
MainWindow::_RemoveFromLibrary(const std::set<int32>& songs)
{
	if (songs.empty())
		return;
	// a song that is playing plays to its end
	for (std::set<int32>::const_iterator it = songs.begin(); it != songs.end();
			it++)
		fQueue.Remove(*it);
	fLibrary.RemoveSongs(songs);
	_SaveLibrary();
	_RefreshAll(true);
}


void
MainWindow::_MoveSongs(BMessage* message)
{
	Playlist* playlist = fLibrary.PlaylistById(fSourcePlaylist);
	if (!_IsUserPlaylist() || playlist == NULL || _IsFiltered()
		|| !fSongList->IsInPlaylistOrder()) {
		fDisplay->ShowMessage("Sort the playlist by \"#\" to change its order");
		return;
	}

	std::set<int32> from;
	int32 position;
	for (int32 i = 0; message->FindInt32("from", i, &position) == B_OK; i++)
		from.insert(position);
	int32 to = message->GetInt32("to", (int32)playlist->songs.size());

	std::vector<int32> moving;
	std::vector<int32> staying;
	int32 insertAt = to;
	for (int32 i = 0; i < (int32)playlist->songs.size(); i++) {
		if (from.count(i) > 0) {
			moving.push_back(playlist->songs[i]);
			if (i < to)
				insertAt--;
		} else
			staying.push_back(playlist->songs[i]);
	}
	if (moving.empty())
		return;
	insertAt = std::max((int32)0, std::min(insertAt, (int32)staying.size()));
	staying.insert(staying.begin() + insertAt, moving.begin(), moving.end());
	playlist->songs.swap(staying);
	fLibrary.SetDirty();
	_SaveLibrary();
	_RefreshAll(false);

	// keep the moved songs selected
	for (int32 i = insertAt; i < insertAt + (int32)moving.size(); i++) {
		BRow* row = fSongList->RowAt(i);
		if (row != NULL)
			fSongList->AddToSelection(row);
	}
}


void
MainWindow::_ShowInTracker(int32 id)
{
	std::vector<int32> songs;
	if (id >= 0)
		songs.push_back(id);
	else
		fSongList->GetSelectedSongs(songs);
	if (songs.empty() && fPlayer->CurrentState() != Player::STOPPED)
		songs.push_back(fPlayer->CurrentSong());
	if (songs.empty())
		return;

	const Song* song = fLibrary.SongById(songs[0]);
	if (song == NULL)
		return;
	if (song->share != 0) {
		fDisplay->ShowMessage("The song is on a shared library");
		return;
	}
	BEntry entry(song->path);
	BEntry parent;
	entry_ref parentRef;
	node_ref node;
	if (!entry.Exists() || entry.GetParent(&parent) != B_OK
		|| parent.GetRef(&parentRef) != B_OK
		|| entry.GetNodeRef(&node) != B_OK) {
		fDisplay->ShowMessage("The file is missing");
		return;
	}

	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &parentRef);
	message.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node_ref));
	BMessenger("application/x-vnd.Be-TRAK").SendMessage(&message);
}


void
MainWindow::_GetInfo()
{
	std::vector<int32> ids;
	fSongList->GetSelectedSongs(ids);
	std::vector<const Song*> songs;
	std::set<int32> seen;
	for (size_t i = 0; i < ids.size(); i++) {
		const Song* song = fLibrary.SongById(ids[i]);
		if (song != NULL && song->share == 0 && seen.insert(ids[i]).second)
			songs.push_back(song);
	}
	if (songs.empty())
		return;

	// genres for the menu: the library's and some common ones
	std::set<BString> genreSet;
	const char* common[] = { "Alternative", "Blues", "Classical", "Country",
		"Dance", "Electronic", "Folk", "Hip-Hop", "Jazz", "Metal", "Pop",
		"R&B", "Reggae", "Rock", "Soundtrack", "Spoken Word", "World" };
	for (size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++)
		genreSet.insert(common[i]);
	for (int32 i = 0; i < fLibrary.CountSongs(); i++) {
		const Song* song = fLibrary.SongAt(i);
		if (!song->genre.IsEmpty())
			genreSet.insert(song->genre);
	}
	std::vector<BString> genres(genreSet.begin(), genreSet.end());

	(new InfoWindow(this, BMessenger(this), songs, genres))->Show();
}


void
MainWindow::_ApplyInfo(BMessage* message)
{
	TagChanges changes = unarchive_tag_changes(*message);
	if (changes.IsEmpty())
		return;

	BMessage request;
	archive_tag_changes(changes, request);
	int32 playing = fPlayer->CurrentState() != Player::STOPPED
		? fPlayer->CurrentSong() : -1;
	bool playingChanged = false;
	int32 id;
	for (int32 i = 0; message->FindInt32("song", i, &id) == B_OK; i++) {
		Song* song = fLibrary.SongById(id);
		if (song == NULL || song->share != 0)
			continue;

		for (int field = 0; field < TAG_FIELD_COUNT; field++) {
			if (!changes.set[field])
				continue;
			BString value(changes.value[field].c_str());
			switch (field) {
				case TAG_TITLE:
					if (value.IsEmpty()) {
						// like importing: the file name
						BPath path(song->path);
						value = path.Leaf();
						int32 dot = value.FindLast('.');
						if (dot > 0)
							value.Truncate(dot);
					}
					song->title = value;
					break;
				case TAG_ARTIST:
					song->artist = value;
					break;
				case TAG_ALBUM:
					song->album = value;
					break;
				case TAG_GENRE:
					song->genre = value;
					break;
				case TAG_YEAR:
					song->year = parse_leading_int(value);
					break;
				case TAG_TRACK:
					song->track = parse_leading_int(value);
					break;
				case TAG_DISC:
					song->disc = parse_leading_int(value);
					break;
			}
		}
		fLibrary.UpdateKeys(song);
		if (can_write_tags(song->path))
			request.AddString("path", song->path);
		if (id == playing)
			playingChanged = true;
	}
	fLibrary.SetDirty();
	_SaveLibrary();
	_RefreshAll(true);

	if (playingChanged) {
		const Song* song = fLibrary.SongById(playing);
		bigtime_t position = fPlayer->Position();
		fDisplay->SetSong(song->title, song->artist, song->album);
		fDisplay->SetTime(position, fPlayer->Duration());
	}

	if (request.HasString("path")) {
		fTagWritesPending++;
		fTagWriter->Write(request);
	}
}


void
MainWindow::_TagsWritten(BMessage* message)
{
	if (fTagWritesPending > 0)
		fTagWritesPending--;

	// the files' sizes changed
	const char* path;
	int64 size;
	for (int32 i = 0; message->FindString("path", i, &path) == B_OK
			&& message->FindInt64("size", i, &size) == B_OK; i++) {
		Song* song = fLibrary.SongById(fLibrary.SongIdForPath(path));
		if (song != NULL) {
			song->size = size;
			if (song->duration > 0)
				song->bitRate = (int32)(size * 8 * 1000 / song->duration);
			fLibrary.SetDirty();
		}
	}

	int32 failed = message->GetInt32("failed count", 0);
	if (failed == 0)
		return;
	fTagWriteFailures += failed;

	BString text;
	const char* error = message->GetString("error", "");
	if (failed == 1) {
		BPath failedPath(message->GetString("failed", ""));
		text << "The tags of \xe2\x80\x9c" << failedPath.Leaf()
			<< "\xe2\x80\x9d could not be changed:\n" << error;
	} else {
		text << "The tags of " << failed << " files could not be changed. "
			"The first problem:\n" << error;
	}
	text << "\n\nThe changes are kept in the BeTunes library.";
	BAlert* alert = new BAlert(APP_NAME ": Get Info", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go(NULL);
}


void
MainWindow::_SaveLibrary()
{
	fLibrary.Save();
}


// #pragma mark - menus


BMenu*
MainWindow::_BuildAddToPlaylistMenu(const char* label)
{
	BMenu* menu = new BMenu(label);
	for (int32 i = 0; i < fLibrary.CountPlaylists(); i++) {
		const Playlist* playlist = fLibrary.PlaylistAt(i);
		BMessage* message = new BMessage(kMsgAddToPlaylist);
		message->AddInt32("playlist", playlist->id);
		BMenuItem* item = new BMenuItem(playlist->name, message);
		item->SetTarget(this);
		menu->AddItem(item);
	}
	if (menu->CountItems() == 0) {
		BMenuItem* item = new BMenuItem("No playlists", NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	}
	return menu;
}


void
MainWindow::_ShowSongContextMenu(BPoint where)
{
	if (fSongList->CurrentSelection() == NULL)
		return;

	BPopUpMenu* menu = new BPopUpMenu("songs", false, false);
	menu->AddItem(new BMenuItem("Play", new BMessage(kMsgSongInvoked)));
	if (_IsShareSource()) {
		// a shared library's songs can only be played
		menu->SetTargetForItems(this);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(where, true, true, true);
		return;
	}
	menu->AddItem(new BMenuItem("Get Info", new BMessage(kMsgGetInfo)));
	menu->AddSeparatorItem();
	menu->AddItem(_BuildAddToPlaylistMenu("Add to Playlist"));
	menu->AddItem(new BMenuItem("New Playlist from Selection",
		new BMessage(kMsgNewPlaylistFromSelection)));
	menu->AddItem(new BMenuItem("Show in Tracker",
		new BMessage(kMsgShowInTracker)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(_IsUserPlaylist() ? "Remove from Playlist"
		: "Delete from Library" B_UTF8_ELLIPSIS,
		new BMessage(kMsgDeleteSongs)));
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


void
MainWindow::_ShowSourceContextMenu(int32 index, BPoint where)
{
	SourceItem* item = fSourceList->SourceAt(index);
	BPopUpMenu* menu = new BPopUpMenu("source", false, false);

	if (item != NULL) {
		BMessage* play = new BMessage(kMsgPlaySongAt);
		play->AddInt32("row", -1);
		menu->AddItem(new BMenuItem("Play", play));
	}
	if (item != NULL && item->Kind() == SOURCE_PLAYLIST) {
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Rename Playlist",
			new BMessage(kMsgStartRename)));
		BMessage* remove = new BMessage(kMsgDeletePlaylist);
		remove->AddInt32("playlist", item->PlaylistId());
		menu->AddItem(new BMenuItem("Delete Playlist" B_UTF8_ELLIPSIS, remove));
	} else if (item != NULL && item->Kind() == SOURCE_SHARE) {
		const Share* share = _ShareById(item->PlaylistId());
		menu->AddSeparatorItem();
		BMessage* message = new BMessage(share != NULL
			&& share->state != kShareIdle
				? kMsgDisconnectShare : kMsgConnectToShare);
		message->AddInt32("share", item->PlaylistId());
		menu->AddItem(new BMenuItem(share != NULL && share->state != kShareIdle
			? "Disconnect" : "Connect", message));
		if (share != NULL && share->added) {
			BMessage* forget = new BMessage(kMsgRemoveShare);
			forget->AddInt32("share", share->id);
			menu->AddItem(new BMenuItem("Remove from List", forget));
		}
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Connect to Shared Library"
			B_UTF8_ELLIPSIS, new BMessage(kMsgConnectToShare)));
	} else if (item == NULL || item->Kind() == SOURCE_LIBRARY) {
		if (item != NULL)
			menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("New Playlist",
			new BMessage(kMsgNewPlaylist)));
		menu->AddItem(new BMenuItem("Add to Library" B_UTF8_ELLIPSIS,
			new BMessage(kMsgAddToLibrary)));
		menu->AddItem(new BMenuItem("Connect to Shared Library"
			B_UTF8_ELLIPSIS, new BMessage(kMsgConnectToShare)));
	}
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


// #pragma mark - shared libraries


namespace {


// What a thread needs to talk to a share; the thread deletes it.
struct ShareJob {
	BMessenger	target;
	int32		share;
	int32		serial;
	BString		host;
	uint16		port;
	BString		user;
	BString		password;
	uint32		session;
	uint32		revision;
};


status_t
connect_share(void* data)
{
	ShareJob* job = static_cast<ShareJob*>(data);
	DaapClient client(job->host, job->port);
	client.SetPassword(job->user, job->password);
	DaapLibrary* library = new DaapLibrary;
	daap_error error = client.Connect(*library);

	BMessage reply(kMsgShareConnected);
	reply.AddInt32("share", job->share);
	reply.AddInt32("serial", job->serial);
	reply.AddInt32("error", error);
	reply.AddString("text", client.ErrorText());
	reply.AddString("host", job->host);
	reply.AddInt32("port", job->port);
	reply.AddString("name", library->server.name);
	if (error == DAAP_OK) {
		reply.AddPointer("library", library);
		if (job->target.SendMessage(&reply) != B_OK) {
			// the window is gone
			client.Logout(library->session);
			delete library;
		}
	} else {
		job->target.SendMessage(&reply);
		delete library;
	}
	delete job;
	return B_OK;
}


status_t
touch_share(void* data)
{
	ShareJob* job = static_cast<ShareJob*>(data);
	DaapClient client(job->host, job->port);
	client.SetPassword(job->user, job->password);
	daap_error error = client.Touch(job->session, job->revision);

	BMessage reply(kMsgShareTouched);
	reply.AddInt32("share", job->share);
	reply.AddInt32("serial", job->serial);
	reply.AddInt32("error", error);
	reply.AddString("text", client.ErrorText());
	job->target.SendMessage(&reply);
	delete job;
	return B_OK;
}


status_t
logout_share(void* data)
{
	ShareJob* job = static_cast<ShareJob*>(data);
	DaapClient client(job->host, job->port);
	client.SetPassword(job->user, job->password);
	client.Logout(job->session);
	delete job;
	return B_OK;
}


thread_id
start_job(thread_func function, const char* name, ShareJob* job)
{
	thread_id thread = spawn_thread(function, name, B_NORMAL_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		delete job;
		return -1;
	}
	return thread;
}


void
show_alert(const char* title, const char* text)
{
	BAlert* alert = new BAlert(title, text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go(NULL);
}


}	// namespace


MainWindow::Share*
MainWindow::_ShareById(int32 id)
{
	for (size_t i = 0; i < fShares.size(); i++) {
		if (fShares[i].id == id)
			return &fShares[i];
	}
	return NULL;
}


MainWindow::Share*
MainWindow::_ShareByAddress(const char* host, uint16 port)
{
	for (size_t i = 0; i < fShares.size(); i++) {
		if (fShares[i].port == port && fShares[i].host.ICompare(host) == 0)
			return &fShares[i];
	}
	return NULL;
}


BString
MainWindow::_HostAndPort(const Share& share) const
{
	return daap_host_and_port(share.host, share.port);
}


bool
MainWindow::_IsShareSource() const
{
	return fSourceKind == SOURCE_SHARE || fSourceKind == SOURCE_SHARE_PLAYLIST;
}


int32
MainWindow::_SourceShare() const
{
	if (fSourceKind == SOURCE_SHARE)
		return fSourcePlaylist;
	if (fSourceKind == SOURCE_SHARE_PLAYLIST) {
		const Playlist* playlist
			= fLibrary.SharedPlaylistById(fSourcePlaylist);
		return playlist != NULL ? playlist->share : -1;
	}
	return -1;
}


void
MainWindow::_KeepLocalSongs(std::vector<int32>& songs) const
{
	std::vector<int32> local;
	local.reserve(songs.size());
	for (size_t i = 0; i < songs.size(); i++) {
		const Song* song = fLibrary.SongById(songs[i]);
		if (song == NULL || song->share == 0)
			local.push_back(songs[i]);
	}
	songs.swap(local);
}


void
MainWindow::_SharesFound(BMessage* message)
{
	std::set<int32> seen;
	bool changed = false;
	const char* name;
	for (int32 i = 0; message->FindString("name", i, &name) == B_OK; i++) {
		const char* host = message->GetString("host", i, "");
		int32 port = message->GetInt32("port", i, 3689);
		Share* share = _ShareByAddress(host, port);
		if (share == NULL) {
			Share found;
			found.id = fNextShareId++;
			found.name = name;
			found.host = host;
			found.port = port;
			found.added = false;
			found.found = true;
			found.missed = 0;
			found.state = kShareIdle;
			found.serial = 0;
			found.session = 0;
			found.revision = 0;
			found.failures = 0;
			fShares.push_back(found);
			seen.insert(found.id);
			changed = true;
			continue;
		}
		seen.insert(share->id);
		share->missed = 0;
		if (!share->found || share->name != name) {
			share->found = true;
			share->name = name;
			changed = true;
		}
	}

	for (size_t i = fShares.size(); i-- > 0;) {
		Share& share = fShares[i];
		if (seen.count(share.id) == 0 && share.found
			&& ++share.missed >= kMissedRoundsBeforeGone) {
			share.found = false;
			changed = true;
		}
		// gone from the network, not added by hand and not in use
		if (!share.found && !share.added && share.state == kShareIdle) {
			fShares.erase(fShares.begin() + i);
			changed = true;
		}
	}

	if (changed || fSourceListStale)
		_SharesChanged();
}


void
MainWindow::_SharesChanged()
{
	std::stable_sort(fShares.begin(), fShares.end(),
		[](const Share& a, const Share& b) {
			return a.name.ICompare(b.name) < 0;
		});
	if (fSourceList->IsRenaming()) {
		// the rename field would end up over another item
		fSourceListStale = true;
		return;
	}
	_RebuildSourceList();
}


void
MainWindow::_UpdateShareItems()
{
	for (int32 i = 0; i < fSourceList->CountItems(); i++) {
		SourceItem* item = fSourceList->SourceAt(i);
		if (item->Kind() != SOURCE_SHARE)
			continue;
		const Share* share = _ShareById(item->PlaylistId());
		if (share == NULL)
			continue;
		item->SetConnected(share->state == kShareConnected);
		item->SetBusy(share->state == kShareConnecting);
		fSourceList->InvalidateItem(i);
	}
	_UpdateStatusText();
}


void
MainWindow::_ConnectShare(int32 id)
{
	Share* share = _ShareById(id);
	if (share == NULL || share->state != kShareIdle)
		return;

	ShareJob* job = new ShareJob;
	job->target = BMessenger(this);
	job->share = id;
	job->serial = fNextShareSerial++;
	job->host = share->host;
	job->port = share->port;
	job->user = share->user;
	job->password = share->password;
	job->session = 0;
	job->revision = 0;
	share->serial = job->serial;
	share->state = kShareConnecting;
	if (start_job(&connect_share, "connect share", job) < 0) {
		share->state = kShareIdle;
		fDisplay->ShowMessage("Cannot connect: no more threads");
	}
	_UpdateShareItems();
}


void
MainWindow::_ShareConnected(BMessage* message)
{
	int32 id = message->GetInt32("share", -1);
	int32 serial = message->GetInt32("serial", -1);
	daap_error error = (daap_error)message->GetInt32("error", DAAP_PROTOCOL);
	DaapLibrary* library = NULL;
	message->FindPointer("library", (void**)&library);

	Share* share = _ShareById(id);
	if (share == NULL || share->serial != serial
		|| share->state != kShareConnecting) {
		// disconnected while connecting: log out of what came anyway
		if (library != NULL) {
			ShareJob* job = new ShareJob;
			job->share = id;
			job->serial = serial;
			job->host = message->GetString("host", "");
			job->port = message->GetInt32("port", 3689);
			job->session = library->session;
			job->revision = 0;
			if (share != NULL) {
				job->user = share->user;
				job->password = share->password;
			}
			start_job(&logout_share, "logout share", job);
			delete library;
		}
		return;
	}

	if (error == DAAP_OK && library != NULL) {
		share->state = kShareConnected;
		share->session = library->session;
		share->revision = library->revision;
		share->failures = 0;
		if (share->added && !share->found && !library->name.IsEmpty())
			share->name = library->name;
				// better than the address it was added by
		DaapSessions::Set(_HostAndPort(*share), share->session,
			daap_authorization(share->user, share->password));

		std::vector<int32> ids;
		fLibrary.AddSharedSongs(id, library->songs, ids);
		for (size_t i = 0; i < library->playlists.size(); i++) {
			const DaapPlaylist& playlist = library->playlists[i];
			std::vector<int32> songs;
			songs.reserve(playlist.songs.size());
			for (size_t j = 0; j < playlist.songs.size(); j++) {
				int32 index = playlist.songs[j];
				if (index >= 0 && index < (int32)ids.size())
					songs.push_back(ids[index]);
			}
			fLibrary.AddSharedPlaylist(id, playlist.name, songs);
		}
		delete library;

		BString text;
		text << "\xe2\x80\x9c" << share->name << "\xe2\x80\x9d: "
			<< (int32)ids.size() << (ids.size() == 1 ? " song" : " songs");
		_SharesChanged();
			// sorts fShares: share points elsewhere now
		if (_SourceShare() == id)
			_RefreshAll(false);
		fDisplay->ShowMessage(text);
		return;
	}

	delete library;
	share->state = kShareIdle;
	// a share added by its address is called by the name it gave, if it
	// got that far
	const char* serverName = message->GetString("name", "");
	if (share->added && !share->found && serverName[0] != '\0'
		&& share->name == share->host) {
		share->name = serverName;
		_SharesChanged();
			// sorts fShares
		share = _ShareById(id);
	}
	_UpdateShareItems();
	if (error == DAAP_NEEDS_PASSWORD || error == DAAP_WRONG_PASSWORD) {
		_AskSharePassword(*share, error == DAAP_WRONG_PASSWORD);
		return;
	}

	BString text;
	text << "Could not connect to \xe2\x80\x9c" << share->name
		<< "\xe2\x80\x9d.\n\n" << message->GetString("text", "");
	if (_SourceShare() == id)
		_SelectSource(SOURCE_LIBRARY, -1);
	show_alert(APP_NAME ": Shared library", text);
}


void
MainWindow::_DisconnectShare(int32 id, const char* lostText)
{
	Share* share = _ShareById(id);
	if (share == NULL)
		return;
	bool shown = _SourceShare() == id;

	if (share->state == kShareConnecting) {
		// its answer is ignored when it comes (and logged out of)
		share->state = kShareIdle;
		share->serial = 0;
		_UpdateShareItems();
		if (shown)
			_SelectSource(SOURCE_LIBRARY, -1);
		return;
	}
	if (share->state != kShareConnected)
		return;

	const Song* playing = fPlayer->CurrentState() != Player::STOPPED
		? fLibrary.SongById(fPlayer->CurrentSong()) : NULL;
	if (playing != NULL && playing->share == id)
		_Stop();
	const Song* queued = fLibrary.SongById(fQueue.Current());
	if (queued != NULL && queued->share == id)
		fQueue.Clear();
			// the queue is one source's songs

	if (lostText == NULL) {
		ShareJob* job = new ShareJob;
		job->share = id;
		job->serial = share->serial;
		job->host = share->host;
		job->port = share->port;
		job->user = share->user;
		job->password = share->password;
		job->session = share->session;
		job->revision = share->revision;
		start_job(&logout_share, "logout share", job);
	}
	DaapSessions::Remove(_HostAndPort(*share));
	fLibrary.RemoveShare(id);
	share->state = kShareIdle;
	share->session = 0;
	BString name(share->name);
	if (!share->found && !share->added)
		fShares.erase(fShares.begin() + (share - &fShares[0]));

	_RebuildSourceList();
	if (shown)
		_SelectSource(SOURCE_LIBRARY, -1);

	if (lostText != NULL) {
		BString text;
		text << "The connection to \xe2\x80\x9c" << name
			<< "\xe2\x80\x9d was lost.";
		if (lostText[0] != '\0')
			text << "\n\n" << lostText;
		show_alert(APP_NAME ": Shared library", text);
	}
}


void
MainWindow::_ForgetShare(int32 id)
{
	Share* share = _ShareById(id);
	if (share == NULL || !share->added)
		return;
	share->added = false;
	if (share->state != kShareIdle)
		_DisconnectShare(id);
	share = _ShareById(id);
	if (share != NULL && !share->found && share->state == kShareIdle)
		fShares.erase(fShares.begin() + (share - &fShares[0]));
	_SharesChanged();
}


void
MainWindow::_AskShareAddress()
{
	std::vector<PromptWindow::Field> fields;
	fields.push_back({ "address", "Address:", "", false });
	PromptWindow* window = new PromptWindow(this,
		"Connect to Shared Library",
		"The name or IP address of a computer that shares its music "
		"library, with \xe2\x80\x9c:port\xe2\x80\x9d when that is not 3689.",
		fields, "Connect", BMessenger(this), BMessage(kMsgShareAddress));
	window->Show();
}


void
MainWindow::_ConnectToAddress(const char* address, const char* password)
{
	std::string host;
	uint16_t port;
	std::string path;
	if (!parse_share_address(address, host, port, path)) {
		BString text;
		text << "\xe2\x80\x9c" << address << "\xe2\x80\x9d is not an address "
			"of a computer.";
		show_alert(APP_NAME ": Shared library", text);
		return;
	}

	Share* share = _ShareByAddress(host.c_str(), port);
	if (share == NULL) {
		Share added;
		added.id = fNextShareId++;
		added.name = host.c_str();
			// until it tells its own
		added.host = host.c_str();
		added.port = port;
		added.found = false;
		added.missed = 0;
		added.state = kShareIdle;
		added.serial = 0;
		added.session = 0;
		added.revision = 0;
		added.failures = 0;
		fShares.push_back(added);
		share = &fShares.back();
	}
	share->added = true;
		// remembered from now on
	if (password != NULL)
		share->password = password;
	int32 id = share->id;
	_SharesChanged();
	if (fSourceListStale)
		_RebuildSourceList();
	_SelectSource(SOURCE_SHARE, id);
		// connects it
}


void
MainWindow::_AskSharePassword(const Share& share, bool wrong)
{
	BString text;
	if (wrong)
		text << "That password is not right. ";
	text << "The shared library \xe2\x80\x9c" << share.name
		<< "\xe2\x80\x9d needs a password.";
	std::vector<PromptWindow::Field> fields;
	fields.push_back({ "password", "Password:", "", true });
	BMessage reply(kMsgSharePassword);
	reply.AddInt32("share", share.id);
	reply.AddString("user", share.user);
	PromptWindow* window = new PromptWindow(this, "Shared Library Password",
		text, fields, "OK", BMessenger(this), reply);
	window->Show();
}


void
MainWindow::_KeepSharesAlive()
{
	for (size_t i = 0; i < fShares.size(); i++) {
		const Share& share = fShares[i];
		if (share.state != kShareConnected)
			continue;
		ShareJob* job = new ShareJob;
		job->target = BMessenger(this);
		job->share = share.id;
		job->serial = share.serial;
		job->host = share.host;
		job->port = share.port;
		job->user = share.user;
		job->password = share.password;
		job->session = share.session;
		job->revision = share.revision;
		start_job(&touch_share, "touch share", job);
	}
}


void
MainWindow::_LogoutShares()
{
	// all at once, and not waiting long for a share that is gone
	std::vector<thread_id> threads;
	for (size_t i = 0; i < fShares.size(); i++) {
		Share& share = fShares[i];
		if (share.state != kShareConnected)
			continue;
		ShareJob* job = new ShareJob;
		job->share = share.id;
		job->serial = share.serial;
		job->host = share.host;
		job->port = share.port;
		job->user = share.user;
		job->password = share.password;
		job->session = share.session;
		job->revision = share.revision;
		thread_id thread = start_job(&logout_share, "logout share", job);
		if (thread >= 0)
			threads.push_back(thread);
		DaapSessions::Remove(_HostAndPort(share));
		share.state = kShareIdle;
	}
	bigtime_t end = system_time() + kLogoutTimeout;
	for (size_t i = 0; i < threads.size(); i++) {
		status_t result;
		wait_for_thread_etc(threads[i], B_ABSOLUTE_TIMEOUT, end, &result);
	}
}


// #pragma mark - scripting


void
MainWindow::_ReplyState(BMessage* message)
{
	BMessage reply(B_REPLY);
	Player::State state = fPlayer->CurrentState();
	reply.AddString("state", state == Player::PLAYING ? "playing"
		: state == Player::PAUSED ? "paused" : "stopped");
	int32 id = state != Player::STOPPED ? fPlayer->CurrentSong() : -1;
	reply.AddInt32("song", id);
	const Song* song = fLibrary.SongById(id);
	reply.AddString("title", song != NULL ? song->title.String() : "");
	reply.AddString("artist", song != NULL ? song->artist.String() : "");
	reply.AddString("album", song != NULL ? song->album.String() : "");
	reply.AddInt32("plays", song != NULL ? song->playCount : 0);
	reply.AddInt64("position", fPlayer->Position());
	reply.AddInt64("duration", fPlayer->Duration());
	reply.AddInt32("volume", fVolume);
	reply.AddBool("shuffle", fShuffle);
	reply.AddInt32("repeat", fRepeat);
	reply.AddInt32("queue", fQueue.CountSongs());
	reply.AddInt32("queue position", fQueue.Position());

	SourceItem* source = fSourceList->SourceAt(
		fSourceList->CurrentSelection());
	reply.AddString("source", source != NULL ? source->Name() : "");
	reply.AddInt32("source kind", fSourceKind);
	reply.AddInt32("source playlist", fSourcePlaylist);
	reply.AddInt32("rows", fSongList->CountRows());
	reply.AddInt32("selected", fSongList->SelectedIndex());
	reply.AddInt32("artists", fArtistList->CountItems());
	reply.AddInt32("albums", fAlbumList->CountItems());
	reply.AddString("artist selection", fArtistAll ? "" : fArtist.String());
	reply.AddString("album selection", fAlbumAll ? "" : fAlbum.String());
	reply.AddInt32("library", fLibrary.CountSongs());
	reply.AddInt32("playlists", fLibrary.CountPlaylists());
	reply.AddBool("importing", fImportsPending > 0);
	reply.AddString("search", fSearchField->Text());
	reply.AddInt32("scope", fSearchScope);
	reply.AddBool("browser", fBrowserVisible);
	reply.AddString("status", fStatusText->Text());
	reply.AddString("message", fDisplay->Message());
	reply.AddBool("renaming", fSourceList->IsRenaming());
	reply.AddBool("eq", fEqualizer.enabled);
	reply.AddString("eq preset", fEqualizerPreset);
	reply.AddFloat("eq preamp", fEqualizer.preamp);
	for (int i = 0; i < kEqualizerBands; i++)
		reply.AddFloat("eq gains", fEqualizer.bands[i]);
	reply.AddInt32("eq user presets", (int32)fUserPresets.size());
	reply.AddBool("eq window", fEqualizerVisible);
	reply.AddInt32("tag writes", fTagWritesPending);
	reply.AddBool("mini", fMiniPlayer);
	reply.AddBool("mini on top", fMiniOnTop);
	reply.AddInt32("tag failures", fTagWriteFailures);
	message->SendReply(&reply);
}


void
MainWindow::_ReplySongs(BMessage* message)
{
	BMessage reply(B_REPLY);
	for (int32 i = 0; i < fSongList->CountRows(); i++) {
		SongRow* row = static_cast<SongRow*>(fSongList->RowAt(i));
		const Song* song = fLibrary.SongById(row->SongId());
		if (song == NULL)
			continue;
		reply.AddInt32("id", song->id);
		reply.AddInt32("number", row->Number());
		reply.AddString("title", song->title);
		reply.AddString("artist", song->artist);
		reply.AddString("album", song->album);
		reply.AddString("genre", song->genre);
		reply.AddInt32("track", song->track);
		reply.AddInt32("year", song->year);
		reply.AddInt64("duration", song->duration);
		reply.AddInt32("plays", song->playCount);
		reply.AddBool("selected", row->IsSelected());
	}
	message->SendReply(&reply);
}


void
MainWindow::_ReplyBrowser(BMessage* message)
{
	BMessage reply(B_REPLY);
	for (int32 i = 0; i < fArtistList->CountItems(); i++)
		reply.AddString("artist", fArtistList->BrowserAt(i)->Text());
	for (int32 i = 0; i < fAlbumList->CountItems(); i++)
		reply.AddString("album", fAlbumList->BrowserAt(i)->Text());
	reply.AddInt32("artist selection", fArtistList->CurrentSelection());
	reply.AddInt32("album selection", fAlbumList->CurrentSelection());
	message->SendReply(&reply);
}


void
MainWindow::_ReplySources(BMessage* message)
{
	BMessage reply(B_REPLY);
	for (int32 i = 0; i < fSourceList->CountItems(); i++) {
		SourceItem* item = fSourceList->SourceAt(i);
		reply.AddString("name", item->Name());
		reply.AddInt32("kind", item->Kind());
		reply.AddInt32("playlist", item->PlaylistId());
		reply.AddBool("playing", item->IsPlaying());
		reply.AddBool("connected", item->IsConnected());
	}
	reply.AddInt32("selection", fSourceList->CurrentSelection());
	message->SendReply(&reply);
}


void
MainWindow::_ReplyShares(BMessage* message)
{
	BMessage reply(B_REPLY);
	for (size_t i = 0; i < fShares.size(); i++) {
		const Share& share = fShares[i];
		reply.AddInt32("id", share.id);
		reply.AddString("name", share.name);
		reply.AddString("host", share.host);
		reply.AddInt32("port", share.port);
		reply.AddString("state", share.state == kShareConnected ? "connected"
			: share.state == kShareConnecting ? "connecting" : "idle");
		reply.AddBool("added", share.added);
		reply.AddBool("found", share.found);
		reply.AddInt32("songs", fLibrary.CountSharedSongs(share.id));
	}
	message->SendReply(&reply);
}
