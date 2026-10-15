/*
 * The song list.
 */
#include "SongList.h"

#include <Bitmap.h>
#include <DateTimeFormat.h>
#include <MessageFilter.h>
#include <Window.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "BeTunes.h"
#include "Icons.h"
#include "TextUtils.h"


namespace {


int
compare_keys(const BString& a, const BString& b)
{
	return strcmp(a.String(), b.String());
}


int
compare_numbers(int64 a, int64 b)
{
	if (a != b)
		return a < b ? -1 : 1;
	return 0;
}


int
compare_ties(const SortTie* a, const SortTie* b, bool trackFirst)
{
	if (a == NULL || b == NULL)
		return 0;
	int result;
	if (trackFirst) {
		// the album column: its songs in track order
		result = compare_numbers(a->track, b->track);
		if (result == 0)
			result = compare_keys(a->artist, b->artist);
		return result;
	}
	result = compare_keys(a->artist, b->artist);
	if (result == 0)
		result = compare_keys(a->album, b->album);
	if (result == 0)
		result = compare_numbers(a->track, b->track);
	return result;
}


// Text with a sort key (collation key or number) and the row's tie
// breaker.
class SongField : public BStringField {
public:
	SongField(const char* text, const BString& key, const SortTie* tie)
		:
		BStringField(text),
		fKey(key),
		fNumber(0),
		fNumeric(false),
		fTie(tie)
	{
	}

	SongField(const char* text, int64 number, const SortTie* tie)
		:
		BStringField(text),
		fNumber(number),
		fNumeric(true),
		fTie(tie)
	{
	}

	int Compare(const SongField* other, bool trackFirst) const
	{
		int result = fNumeric ? compare_numbers(fNumber, other->fNumber)
			: compare_keys(fKey, other->fKey);
		if (result != 0)
			return result;
		return compare_ties(fTie, other->fTie, trackFirst);
	}

private:
	BString			fKey;
	int64			fNumber;
	bool			fNumeric;
	const SortTie*	fTie;
};


class SongColumn : public BStringColumn {
public:
	SongColumn(const char* title, float width, float minWidth, float maxWidth,
			alignment align, bool trackFirst)
		:
		BStringColumn(title, width, minWidth, maxWidth, B_TRUNCATE_END, align),
		fTrackFirst(trackFirst)
	{
	}

	virtual int CompareFields(BField* field1, BField* field2)
	{
		return static_cast<SongField*>(field1)->Compare(
			static_cast<SongField*>(field2), fTrackFirst);
	}

private:
	bool	fTrackFirst;
};


class IndicatorField : public BField {
public:
	IndicatorField(int32 song)
		:
		fSong(song)
	{
	}

	int32 Song() const { return fSong; }

private:
	int32	fSong;
};


// The speaker in front of the playing song (and "!" for songs that could
// not be played).
class IndicatorColumn : public BColumn {
public:
	IndicatorColumn(SongListView* list, float width)
		:
		BColumn(width, width, width, B_ALIGN_CENTER),
		fList(list)
	{
	}

	virtual void DrawTitle(BRect rect, BView* parent)
	{
	}

	virtual void GetColumnName(BString* into) const
	{
		*into = "Now Playing";
	}

	virtual void DrawField(BField* field, BRect rect, BView* parent)
	{
		int32 song = static_cast<IndicatorField*>(field)->Song();
		float size = std::min(rect.Height() - 4, rect.Width() - 4);
		BRect glyphRect(0, 0, size, size);
		glyphRect.OffsetTo(rect.left + floorf((rect.Width() - size) / 2),
			rect.top + floorf((rect.Height() - size) / 2));
		if (song == fList->PlayingSong()) {
			draw_glyph(parent, glyphRect, GLYPH_SPEAKER_LOUD,
				parent->HighColor());
		} else if (fList->HasFailed(song)) {
			draw_glyph(parent, glyphRect.InsetByCopy(1, 1), GLYPH_WARNING,
				make_color(230, 130, 0, 255));
		}
	}

	virtual int CompareFields(BField* field1, BField* field2)
	{
		return 0;
	}

	virtual bool AcceptsField(const BField* field) const
	{
		return dynamic_cast<const IndicatorField*>(field) != NULL;
	}

private:
	SongListView*	fList;
};


// Right click: tell the window to show the context menu.
class ContextMenuFilter : public BMessageFilter {
public:
	ContextMenuFilter(SongListView* list)
		:
		BMessageFilter(B_MOUSE_DOWN),
		fList(list)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		int32 buttons = message->GetInt32("buttons", 0);
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0)
			return B_DISPATCH_MESSAGE;

		BPoint screenWhere;
		if (message->FindPoint("screen_where", &screenWhere) != B_OK)
			return B_DISPATCH_MESSAGE;

		BPoint where = fList->ScrollView()->ConvertFromScreen(screenWhere);
		BRow* row = fList->RowAt(where);
		fList->MakeFocus(true);
		if (row != NULL && !row->IsSelected()) {
			fList->DeselectAll();
			fList->AddToSelection(row);
		}

		BMessage menu(kMsgSongContextMenu);
		menu.AddPoint("where", screenWhere);
		fList->Window()->PostMessage(&menu);
		return B_SKIP_MESSAGE;
	}

private:
	SongListView*	fList;
};


BDateTimeFormat&
date_format()
{
	static BDateTimeFormat format;
	return format;
}


BString
number_text(int64 number)
{
	BString text;
	if (number > 0)
		text << number;
	return text;
}


}	// namespace


BString
format_date(time_t time)
{
	BString string;
	if (time == 0)
		return string;
	if (date_format().Format(string, time, B_SHORT_DATE_FORMAT,
			B_SHORT_TIME_FORMAT) != B_OK) {
		char buffer[64];
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", localtime(&time));
		string = buffer;
	}
	return string;
}


BString
format_size(off_t size)
{
	char buffer[32];
	if (size < 1024 * 1024)
		snprintf(buffer, sizeof(buffer), "%.0f KB", size / 1024.0);
	else if (size < 1024LL * 1024 * 1024)
		snprintf(buffer, sizeof(buffer), "%.1f MB", size / 1048576.0);
	else
		snprintf(buffer, sizeof(buffer), "%.2f GB", size / 1073741824.0);
	return buffer;
}


// #pragma mark - SongRow


SongRow::SongRow(const Song* song, int32 number)
	:
	fSong(song->id),
	fNumber(number)
{
	Update(song);
}


void
SongRow::Update(const Song* song)
{
	fTie.artist = song->artistKey;
	fTie.album = song->albumKey;
	fTie.track = song->disc * 10000 + song->track;
	const SortTie* tie = &fTie;

	SetField(new IndicatorField(song->id), COLUMN_INDICATOR);
	SetField(new SongField(number_text(fNumber), fNumber, NULL),
		COLUMN_NUMBER);
	SetField(new SongField(song->title, song->titleKey, tie), COLUMN_NAME);
	SetField(new SongField(format_duration(song->duration).c_str(),
		song->duration, tie), COLUMN_TIME);
	SetField(new SongField(song->artist, song->artistKey, tie),
		COLUMN_ARTIST);
	SetField(new SongField(song->album, song->albumKey, tie), COLUMN_ALBUM);
	SetField(new SongField(song->genre, song->genreKey, tie), COLUMN_GENRE);
	SetField(new SongField(number_text(song->track), fTie.track, tie),
		COLUMN_TRACK);
	SetField(new SongField(number_text(song->year), song->year, tie),
		COLUMN_YEAR);
	SetField(new SongField(number_text(song->playCount), song->playCount, tie),
		COLUMN_PLAYS);
	SetField(new SongField(format_date(song->lastPlayed), song->lastPlayed,
		tie), COLUMN_LAST_PLAYED);
	SetField(new SongField(format_date(song->dateAdded), song->dateAdded,
		tie), COLUMN_DATE_ADDED);
	SetField(new SongField(song->kind, song->kindKey, tie), COLUMN_KIND);
	BString bitRate;
	if (song->bitRate > 0)
		bitRate << song->bitRate << " kbps";
	SetField(new SongField(bitRate, song->bitRate, tie), COLUMN_BIT_RATE);
	SetField(new SongField(format_size(song->size), song->size, tie),
		COLUMN_SIZE);
}


// #pragma mark - SongListView


SongListView::SongListView(const char* name)
	:
	BColumnListView(name, B_NAVIGABLE, B_PLAIN_BORDER, true),
	fPlayingSong(-1),
	fFilterAdded(false)
{
	float size = be_plain_font->Size();
	struct {
		song_column	column;
		const char*	title;
		float		width;
		alignment	align;
		bool		visible;
	} columns[] = {
		{ COLUMN_NUMBER, "#", 3, B_ALIGN_RIGHT, false },
		{ COLUMN_NAME, "Name", 14, B_ALIGN_LEFT, true },
		{ COLUMN_TIME, "Time", 4, B_ALIGN_RIGHT, true },
		{ COLUMN_ARTIST, "Artist", 11, B_ALIGN_LEFT, true },
		{ COLUMN_ALBUM, "Album", 11, B_ALIGN_LEFT, true },
		{ COLUMN_GENRE, "Genre", 7, B_ALIGN_LEFT, true },
		{ COLUMN_TRACK, "Track #", 7.5, B_ALIGN_RIGHT, true },
		{ COLUMN_YEAR, "Year", 4, B_ALIGN_RIGHT, true },
		{ COLUMN_PLAYS, "Play Count", 6.5, B_ALIGN_RIGHT, true },
		{ COLUMN_LAST_PLAYED, "Last Played", 10, B_ALIGN_LEFT, false },
		{ COLUMN_DATE_ADDED, "Date Added", 10, B_ALIGN_LEFT, false },
		{ COLUMN_KIND, "Kind", 10, B_ALIGN_LEFT, false },
		{ COLUMN_BIT_RATE, "Bit Rate", 6, B_ALIGN_RIGHT, false },
		{ COLUMN_SIZE, "Size", 6, B_ALIGN_RIGHT, false }
	};

	float indicatorWidth = ceilf(size * 1.7f);
	fColumns[COLUMN_INDICATOR] = new IndicatorColumn(this, indicatorWidth);
	AddColumn(fColumns[COLUMN_INDICATOR], COLUMN_INDICATOR);

	for (size_t i = 0; i < sizeof(columns) / sizeof(columns[0]); i++) {
		float width = ceilf(columns[i].width * size);
		BColumn* column = new SongColumn(columns[i].title, width,
			ceilf(size * 2), ceilf(size * 60), columns[i].align,
			columns[i].column == COLUMN_ALBUM);
		fColumns[columns[i].column] = column;
		AddColumn(column, columns[i].column);
		column->SetVisible(columns[i].visible);
	}

	SetSortingEnabled(true);
	SetSortColumn(fColumns[COLUMN_ARTIST], false, true);
	SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
}


BColumn*
SongListView::ColumnFor(song_column which) const
{
	return fColumns[which];
}


void
SongListView::AttachedToWindow()
{
	BColumnListView::AttachedToWindow();
	if (!fFilterAdded) {
		ScrollView()->AddFilter(new ContextMenuFilter(this));
		fFilterAdded = true;
	}
}


void
SongListView::SetSongs(const std::vector<int32>& songs,
	const std::vector<int32>& positions, const Library& library,
	bool keepSelection)
{
	std::set<int32> selected;
	float top = 0;
	if (keepSelection) {
		for (BRow* row = CurrentSelection(); row != NULL;
				row = CurrentSelection(row))
			selected.insert(static_cast<SongRow*>(row)->SongId());
		top = ScrollView()->Bounds().top;
	}

	Clear();

	BList rows;
	for (size_t i = 0; i < songs.size(); i++) {
		const Song* song = library.SongById(songs[i]);
		if (song != NULL)
			rows.AddItem(new SongRow(song, positions[i] + 1));
	}
	AddRows(&rows, -1);

	if (keepSelection) {
		if (!selected.empty()) {
			for (int32 i = 0; i < CountRows(); i++) {
				SongRow* row = static_cast<SongRow*>(RowAt(i));
				if (selected.count(row->SongId()) > 0)
					AddToSelection(row);
			}
		}
		ScrollTo(BPoint(0, top));
	} else
		ScrollTo(BPoint(0, 0));
}


void
SongListView::UpdateSong(const Song* song)
{
	for (int32 i = 0; i < CountRows(); i++) {
		SongRow* row = static_cast<SongRow*>(RowAt(i));
		if (row->SongId() == song->id) {
			row->Update(song);
			UpdateRow(row);
		}
	}
}


void
SongListView::SetPlayingSong(int32 song)
{
	if (song == fPlayingSong)
		return;
	fPlayingSong = song;
	ScrollView()->Invalidate();
}


void
SongListView::SetFailed(int32 song, bool failed)
{
	if (failed)
		fFailed.insert(song);
	else
		fFailed.erase(song);
	ScrollView()->Invalidate();
}


void
SongListView::GetSongs(std::vector<int32>& songs) const
{
	songs.clear();
	int32 count = CountRows();
	songs.reserve(count);
	for (int32 i = 0; i < count; i++) {
		songs.push_back(static_cast<SongRow*>(
			const_cast<SongListView*>(this)->RowAt(i))->SongId());
	}
}


void
SongListView::GetSelectedSongs(std::vector<int32>& songs) const
{
	songs.clear();
	for (BRow* row = CurrentSelection(); row != NULL;
			row = CurrentSelection(row))
		songs.push_back(static_cast<SongRow*>(row)->SongId());
}


void
SongListView::GetSelectedPositions(std::vector<int32>& positions) const
{
	positions.clear();
	for (BRow* row = CurrentSelection(); row != NULL;
			row = CurrentSelection(row))
		positions.push_back(static_cast<SongRow*>(row)->Number() - 1);
}


int32
SongListView::SelectedIndex() const
{
	BRow* row = CurrentSelection();
	if (row == NULL)
		return -1;
	return const_cast<SongListView*>(this)->IndexOf(row);
}


int32
SongListView::IndexOfSong(int32 song)
{
	for (int32 i = 0; i < CountRows(); i++) {
		if (static_cast<SongRow*>(RowAt(i))->SongId() == song)
			return i;
	}
	return -1;
}


void
SongListView::SelectSong(int32 song)
{
	int32 index = IndexOfSong(song);
	if (index < 0)
		return;
	BRow* row = RowAt(index);
	DeselectAll();
	AddToSelection(row);
	SetFocusRow(row, false);
	ScrollTo(row);
}


void
SongListView::SelectAllRows()
{
	for (int32 i = 0; i < CountRows(); i++)
		AddToSelection(RowAt(i));
}


void
SongListView::SetPlaylistMode(bool playlist)
{
	SetColumnVisible(fColumns[COLUMN_NUMBER], playlist);
}


void
SongListView::GetSortState(BMessage& state)
{
	BMessage full;
	SaveState(&full);
	state.MakeEmpty();
	int32 id;
	for (int32 i = 0; full.FindInt32("sortID", i, &id) == B_OK; i++) {
		state.AddInt32("sortID", id);
		state.AddBool("sortascending", full.GetBool("sortascending", i, true));
	}
}


void
SongListView::SetSortState(const BMessage& state)
{
	BMessage load;
	load.AddBool("sortingenabled", true);
	int32 id;
	for (int32 i = 0; state.FindInt32("sortID", i, &id) == B_OK; i++) {
		load.AddInt32("sortID", id);
		load.AddBool("sortascending", state.GetBool("sortascending", i, true));
	}
	LoadState(&load);
}


bool
SongListView::IsInPlaylistOrder()
{
	BMessage state;
	GetSortState(state);
	int32 count = 0;
	type_code type;
	if (state.GetInfo("sortID", &type, &count) != B_OK)
		return true;
	return count == 1 && state.GetInt32("sortID", -1) == COLUMN_NUMBER
		&& state.GetBool("sortascending", true);
}


bool
SongListView::InitiateDrag(BPoint where, bool wasSelected)
{
	std::vector<int32> songs;
	GetSelectedSongs(songs);
	if (songs.empty())
		return false;

	BMessage drag(kMsgSongDrag);
	for (size_t i = 0; i < songs.size(); i++)
		drag.AddInt32("song", songs[i]);
	std::vector<int32> positions;
	GetSelectedPositions(positions);
	for (size_t i = 0; i < positions.size(); i++)
		drag.AddInt32("position", positions[i]);
	drag.AddPointer("list", this);

	BString label;
	SongRow* first = static_cast<SongRow*>(CurrentSelection());
	BStringField* name = first != NULL
		? static_cast<BStringField*>(first->GetField(COLUMN_NAME)) : NULL;
	if (songs.size() == 1 && name != NULL)
		label = name->String();
	else
		label << songs.size() << " songs";

	BFont font;
	GetFont(&font);
	font_height height;
	font.GetHeight(&height);
	float lineHeight = ceilf(height.ascent + height.descent);
	float iconSize = lineHeight;
	BRect bounds(0, 0, ceilf(font.StringWidth(label)) + iconSize + 16,
		lineHeight + 6);
	BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
	if (bitmap->Lock()) {
		memset(bitmap->Bits(), 0, bitmap->BitsLength());
		BView* view = new BView(bounds, "drag", 0, 0);
		bitmap->AddChild(view);
		view->SetFont(&font);
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
		rgb_color background = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		background.alpha = 200;
		view->SetHighColor(background);
		view->FillRoundRect(bounds, 4, 4);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		rgb_color text = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
		draw_glyph(view, BRect(4, 3, 4 + iconSize, 3 + iconSize),
			GLYPH_PLAY, text);
		view->SetHighColor(text);
		view->DrawString(label, BPoint(iconSize + 10, 3 + ceilf(height.ascent)));
		view->Sync();
		bitmap->RemoveChild(view);
		delete view;
		bitmap->Unlock();
	}
	DragMessage(&drag, bitmap, B_OP_ALPHA, BPoint(10, bounds.Height() / 2));
	return true;
}


void
SongListView::MessageDropped(BMessage* message, BPoint where)
{
	if (message->HasRef("refs")) {
		BMessage import(kMsgImportPaths);
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
			import.AddRef("refs", &ref);
		import.AddBool("current playlist", true);
		Window()->PostMessage(&import);
		return;
	}

	void* list;
	if (message->what != kMsgSongDrag
		|| message->FindPointer("list", &list) != B_OK || list != this)
		return;

	// moving songs inside a playlist
	int32 to = CountRows();
	BRow* row = RowAt(where);
	if (row != NULL) {
		BRect frame;
		GetRowRect(row, &frame);
		to = static_cast<SongRow*>(row)->Number() - 1;
		if (where.y > (frame.top + frame.bottom) / 2)
			to++;
	}
	BMessage move(kMsgMoveSongs);
	int32 position;
	for (int32 i = 0; message->FindInt32("position", i, &position) == B_OK;
			i++)
		move.AddInt32("from", position);
	move.AddInt32("to", to);
	Window()->PostMessage(&move);
}


void
SongListView::KeyDown(const char* bytes, int32 count)
{
	if (bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE) {
		Window()->PostMessage(kMsgDeleteSongs);
		return;
	}
	BColumnListView::KeyDown(bytes, count);
}
