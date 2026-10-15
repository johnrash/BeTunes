/*
 * The source list.
 */
#include "SourceList.h"

#include <GradientLinear.h>
#include <TextView.h>
#include <Window.h>

#include <math.h>

#include <algorithm>

#include "BeTunes.h"
#include "Icons.h"


static const uint32 kMsgFinishRename = 'Sfrn';


namespace {


float
icon_size(BRect frame)
{
	return floorf(frame.Height() - 4);
}


float
text_left(BRect frame)
{
	return frame.left + 6 + icon_size(frame) + 6;
}


class RenameView : public BTextView {
public:
	RenameView(BRect frame, BView* list)
		:
		BTextView(frame, "rename", BRect(0, 0, frame.Width(), frame.Height())
			.InsetByCopy(2, 1), B_FOLLOW_NONE, B_WILL_DRAW),
		fList(list)
	{
		SetWordWrap(false);
		DisallowChar('\n');
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
	}

	virtual void Draw(BRect update)
	{
		BTextView::Draw(update);
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(Bounds());
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		if (bytes[0] == B_ENTER || bytes[0] == B_ESCAPE) {
			_Finish(bytes[0] == B_ENTER);
			return;
		}
		BTextView::KeyDown(bytes, count);
	}

	virtual void MakeFocus(bool focus)
	{
		BTextView::MakeFocus(focus);
		if (!focus)
			_Finish(true);
	}

private:
	void _Finish(bool commit)
	{
		BMessage finish(kMsgFinishRename);
		finish.AddBool("commit", commit);
		BMessenger(fList).SendMessage(&finish);
	}

	BView*	fList;
};


}	// namespace


// #pragma mark - SourceItem


SourceItem::SourceItem(source_kind kind, int32 playlist, const char* name)
	:
	fKind(kind),
	fPlaylist(playlist),
	fName(name),
	fPlaying(false)
{
}


void
SourceItem::Update(BView* owner, const BFont* font)
{
	font_height height;
	font->GetHeight(&height);
	SetHeight(std::max(18.0f, ceilf((height.ascent + height.descent) * 1.6f)));
	SetWidth(owner->Bounds().Width());
}


void
SourceItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	SourceListView* list = dynamic_cast<SourceListView*>(owner);

	owner->PushState();
	rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_LIST_ITEM_TEXT_COLOR);
	if (IsSelected()) {
		rgb_color selected = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		BGradientLinear gradient(frame.LeftTop(), frame.LeftBottom());
		gradient.AddColor(tint_color(selected, 0.85f), 0);
		gradient.AddColor(tint_color(selected, 1.1f), 255);
		owner->FillRect(frame, gradient);
		text = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	} else {
		owner->SetHighColor(background);
		owner->FillRect(frame);
	}

	float size = icon_size(frame);
	BRect icon(frame.left + 6, frame.top + 2, frame.left + 6 + size,
		frame.top + 2 + size);
	switch (fKind) {
		case SOURCE_LIBRARY:
			draw_library_icon(owner, icon);
			break;
		case SOURCE_PLAYLIST:
			draw_playlist_icon(owner, icon);
			break;
		default:
			draw_smart_playlist_icon(owner, icon);
			break;
	}

	float right = frame.right - 4;
	if (fPlaying) {
		float speaker = floorf(frame.Height() * 0.6f);
		BRect rect(right - speaker, frame.top + (frame.Height() - speaker) / 2,
			right, frame.top + (frame.Height() + speaker) / 2);
		draw_glyph(owner, rect, GLYPH_SPEAKER_LOUD, text);
		right -= speaker + 4;
	}

	font_height height;
	owner->GetFontHeight(&height);
	float baseline = floorf(frame.top + (frame.Height() - (height.ascent
		+ height.descent)) / 2 + height.ascent);
	float left = text_left(frame);
	BString name(fName);
	owner->TruncateString(&name, B_TRUNCATE_END, right - left);
	owner->SetDrawingMode(B_OP_OVER);
	owner->SetHighColor(text);
	owner->DrawString(name, BPoint(left, baseline));

	if (list != NULL && list->DropIndex() >= 0
		&& list->DropIndex() == list->IndexOf(this)) {
		owner->SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		owner->SetPenSize(2);
		owner->StrokeRoundRect(frame.InsetByCopy(1, 1), 4, 4);
	}
	owner->PopState();
}


// #pragma mark - SourceListView


SourceListView::SourceListView(const char* name)
	:
	BListView(name, B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fDropIndex(-1),
	fRenameView(NULL),
	fRenamePlaylist(-1)
{
}


SourceItem*
SourceListView::SourceAt(int32 index) const
{
	return dynamic_cast<SourceItem*>(ItemAt(index));
}


int32
SourceListView::IndexOf(source_kind kind, int32 playlist) const
{
	for (int32 i = 0; i < CountItems(); i++) {
		SourceItem* item = SourceAt(i);
		if (item->Kind() == kind
			&& (kind != SOURCE_PLAYLIST || item->PlaylistId() == playlist))
			return i;
	}
	return -1;
}


void
SourceListView::StartRename(int32 index)
{
	SourceItem* item = SourceAt(index);
	if (item == NULL || item->Kind() != SOURCE_PLAYLIST)
		return;
	if (fRenameView != NULL)
		_FinishRename(true);

	ScrollTo(index);
	BRect frame = ItemFrame(index);
	frame.left = text_left(frame) - 3;
	frame.right -= 4;
	frame.InsetBy(0, 1);

	fRenamePlaylist = item->PlaylistId();
	fRenameView = new RenameView(frame, this);
	AddChild(fRenameView);
	fRenameView->SetText(item->Name());
	fRenameView->SelectAll();
	fRenameView->MakeFocus(true);
}


void
SourceListView::_FinishRename(bool commit)
{
	if (fRenameView == NULL)
		return;

	BTextView* view = fRenameView;
	fRenameView = NULL;
	BString name(view->Text());
	view->RemoveSelf();
	delete view;
	MakeFocus(true);

	if (commit) {
		BMessage rename(kMsgRenamePlaylist);
		rename.AddInt32("playlist", fRenamePlaylist);
		rename.AddString("name", name);
		Window()->PostMessage(&rename);
	}
}


void
SourceListView::Draw(BRect update)
{
	BListView::Draw(update);

	if (fDropIndex >= 0 && fDropIndex == CountItems()) {
		// dropping below the last item makes a new playlist
		BRect rect = Bounds();
		if (CountItems() > 0)
			rect.top = ItemFrame(CountItems() - 1).bottom + 1;
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		SetPenSize(2);
		StrokeRoundRect(rect.InsetByCopy(2, 2), 4, 4);
		SetPenSize(1);
	}
}


void
SourceListView::MouseDown(BPoint where)
{
	BMessage* current = Window()->CurrentMessage();
	int32 buttons = current != NULL
		? current->GetInt32("buttons", B_PRIMARY_MOUSE_BUTTON)
		: B_PRIMARY_MOUSE_BUTTON;
	int32 clicks = current != NULL ? current->GetInt32("clicks", 1) : 1;
	int32 index = BListView::IndexOf(where);

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		MakeFocus(true);
		if (index >= 0)
			Select(index);
		BMessage menu(kMsgSourceContextMenu);
		menu.AddInt32("index", index);
		menu.AddPoint("where", ConvertToScreen(where));
		Window()->PostMessage(&menu);
		return;
	}

	if (clicks == 2 && index >= 0 && index == CurrentSelection()
		&& SourceAt(index)->Kind() == SOURCE_PLAYLIST) {
		StartRename(index);
		return;
	}

	BListView::MouseDown(where);
}


void
SourceListView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (dragMessage != NULL && transit != B_EXITED_VIEW
		&& transit != B_OUTSIDE_VIEW) {
		int32 index = BListView::IndexOf(where);
		if (index < 0)
			index = CountItems();
		_SetDropIndex(_AcceptsDrop(dragMessage, index) ? index : -1);
	} else if (fDropIndex >= 0)
		_SetDropIndex(-1);

	BListView::MouseMoved(where, transit, dragMessage);
}


void
SourceListView::KeyDown(const char* bytes, int32 count)
{
	if (bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE) {
		Window()->PostMessage(kMsgDeletePlaylist);
		return;
	}
	BListView::KeyDown(bytes, count);
}


void
SourceListView::MessageReceived(BMessage* message)
{
	if (message->what == kMsgFinishRename) {
		_FinishRename(message->GetBool("commit", true));
		return;
	}

	if (message->WasDropped()) {
		BPoint where = ConvertFromScreen(message->DropPoint());
		int32 index = BListView::IndexOf(where);
		if (index < 0)
			index = CountItems();
		_SetDropIndex(-1);
		if (!_AcceptsDrop(message, index))
			return;

		SourceItem* item = SourceAt(index);
		int32 playlist = -1;
		if (item == NULL)
			playlist = -2;
				// a new playlist
		else if (item->Kind() == SOURCE_PLAYLIST)
			playlist = item->PlaylistId();

		if (message->what == kMsgSongDrag) {
			BMessage drop(kMsgSongsDropped);
			drop.AddInt32("playlist", playlist);
			int32 song;
			for (int32 i = 0; message->FindInt32("song", i, &song) == B_OK;
					i++)
				drop.AddInt32("song", song);
			Window()->PostMessage(&drop);
		} else {
			BMessage import(kMsgImportPaths);
			entry_ref ref;
			for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
				import.AddRef("refs", &ref);
			import.AddInt32("playlist", playlist);
			Window()->PostMessage(&import);
		}
		return;
	}

	BListView::MessageReceived(message);
}


bool
SourceListView::_AcceptsDrop(const BMessage* message, int32 index) const
{
	SourceItem* item = SourceAt(index);
	if (message->what == kMsgSongDrag) {
		return item == NULL || item->Kind() == SOURCE_PLAYLIST;
	}
	return message->HasRef("refs");
}


void
SourceListView::_SetDropIndex(int32 index)
{
	if (index == fDropIndex)
		return;
	fDropIndex = index;
	Invalidate();
}
