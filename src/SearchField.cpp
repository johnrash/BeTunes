/*
 * The search field.
 */
#include "SearchField.h"

#include <MenuItem.h>
#include <PopUpMenu.h>
#include <TextView.h>
#include <Window.h>

#include <math.h>
#include <string.h>

#include <algorithm>

#include "BeTunes.h"
#include "Controls.h"
#include "Icons.h"


static const char* const kScopeNames[] = {
	"All", "Artists", "Albums", "Songs", "Genres"
};
static const char* const kPlaceholders[] = {
	"Search", "Search Artists", "Search Albums", "Search Songs",
	"Search Genres"
};


class SearchTextView : public BTextView {
public:
	SearchTextView(SearchField* field)
		:
		BTextView("search text"),
		fField(field)
	{
		SetWordWrap(false);
		SetStylable(false);
		DisallowChar('\n');
		DisallowChar('\t');
	}

	virtual void Draw(BRect update)
	{
		BTextView::Draw(update);
		if (TextLength() > 0 || IsFocus())
			return;
		// placeholder
		rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
		rgb_color background = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
		SetHighColor(mix(text, background, 150));
		font_height height;
		GetFontHeight(&height);
		BRect rect = TextRect();
		DrawString(fField->Placeholder(),
			BPoint(rect.left, rect.top + ceilf(height.ascent)));
	}

	virtual void MakeFocus(bool focus)
	{
		BTextView::MakeFocus(focus);
		Invalidate();
		fField->Invalidate();
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		if (bytes[0] == B_ESCAPE) {
			fField->SetText("");
			return;
		}
		if (bytes[0] == B_ENTER)
			return;
		BTextView::KeyDown(bytes, count);
	}

protected:
	virtual void InsertText(const char* text, int32 length, int32 offset,
		const text_run_array* runs)
	{
		BTextView::InsertText(text, length, offset, runs);
		fField->TextChanged();
	}

	virtual void DeleteText(int32 start, int32 finish)
	{
		BTextView::DeleteText(start, finish);
		fField->TextChanged();
	}

private:
	SearchField*	fField;
};


SearchField::SearchField(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
	fTextView(NULL),
	fScope(SEARCH_ALL),
	fSettingText(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	fTextView = new SearchTextView(this);
	fTextView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fTextView->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fTextView->SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
	AddChild(fTextView);
}


const char*
SearchField::Text() const
{
	return fTextView->Text();
}


void
SearchField::SetText(const char* text)
{
	if (strcmp(text, fTextView->Text()) == 0)
		return;
	// SetText() deletes and inserts: notify once
	fSettingText = true;
	fTextView->SetText(text);
	fSettingText = false;
	TextChanged();
	fTextView->Invalidate();
}


void
SearchField::SetScope(int32 scope)
{
	if (scope < SEARCH_ALL || scope > SEARCH_GENRES)
		scope = SEARCH_ALL;
	fScope = scope;
	fTextView->Invalidate();
}


void
SearchField::MakeFocus(bool focus)
{
	fTextView->MakeFocus(focus);
	if (focus)
		fTextView->SelectAll();
}


const char*
SearchField::Placeholder() const
{
	return kPlaceholders[fScope];
}


void
SearchField::TextChanged()
{
	if (fSettingText)
		return;
	BMessage message(kMsgSearchChanged);
	message.AddString("text", fTextView->Text());
	fTarget.SendMessage(&message);
	Invalidate(_ClearRect());
}


BRect
SearchField::_MagnifierRect() const
{
	BRect bounds = Bounds();
	float size = bounds.Height();
	return BRect(bounds.left, bounds.top, bounds.left + size, bounds.bottom);
}


BRect
SearchField::_ClearRect() const
{
	BRect bounds = Bounds();
	float size = bounds.Height();
	return BRect(bounds.right - size, bounds.top, bounds.right, bounds.bottom);
}


void
SearchField::_LayoutTextView()
{
	BRect bounds = Bounds();
	BRect magnifier = _MagnifierRect();
	BRect clear = _ClearRect();
	font_height height;
	fTextView->GetFontHeight(&height);
	float textHeight = ceilf(height.ascent + height.descent);
	float top = floorf((bounds.Height() - textHeight) / 2);
	BRect frame(magnifier.right - 2, top, clear.left, top + textHeight);
	fTextView->MoveTo(frame.LeftTop());
	fTextView->ResizeTo(frame.Width(), frame.Height());
	fTextView->SetTextRect(fTextView->Bounds());
}


void
SearchField::FrameResized(float width, float height)
{
	_LayoutTextView();
	BView::FrameResized(width, height);
}


void
SearchField::Draw(BRect update)
{
	BarView::DrawBackground(this, update);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	BRect bounds = Bounds();
	float radius = bounds.Height() / 2;
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color background = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	SetHighColor(255, 255, 255, 90);
	FillRoundRect(bounds.OffsetByCopy(0, 1), radius, radius);
	bool focused = fTextView->IsFocus() && Window() != NULL
		&& Window()->IsActive();
	SetHighColor(focused ? ui_color(B_KEYBOARD_NAVIGATION_COLOR)
		: tint_color(panel, 1.45f));
	FillRoundRect(bounds, radius, radius);
	BRect inner = bounds.InsetByCopy(focused ? 1.5f : 1, focused ? 1.5f : 1);
	SetHighColor(background);
	FillRoundRect(inner, radius - 1, radius - 1);
	// a hint of an inset shadow
	SetHighColor(0, 0, 0, 30);
	StrokeArc(inner.InsetByCopy(0.5, 0.5), 30, 120);

	rgb_color glyphColor = mix(ui_color(B_DOCUMENT_TEXT_COLOR), background,
		90);
	BRect magnifier = _MagnifierRect();
	draw_glyph(this, magnifier.InsetByCopy(magnifier.Width() * 0.26f,
		magnifier.Height() * 0.26f).OffsetByCopy(2, 0), GLYPH_MAGNIFIER,
		glyphColor);

	if (fTextView->TextLength() > 0) {
		BRect clear = _ClearRect();
		draw_glyph(this, clear.InsetByCopy(clear.Width() * 0.24f,
			clear.Height() * 0.24f).OffsetByCopy(-2, 0), GLYPH_CLEAR,
			mix(glyphColor, background, 60));
	}
}


void
SearchField::MouseDown(BPoint where)
{
	if (_ClearRect().Contains(where) && fTextView->TextLength() > 0) {
		SetText("");
		fTextView->MakeFocus(true);
		return;
	}

	if (_MagnifierRect().Contains(where)) {
		BPopUpMenu* menu = new BPopUpMenu("scope", false, false);
		for (int32 i = 0; i <= SEARCH_GENRES; i++) {
			BMessage* message = new BMessage(kMsgSearchScope);
			message->AddInt32("scope", i);
			BMenuItem* item = new BMenuItem(kScopeNames[i], message);
			item->SetMarked(i == fScope);
			menu->AddItem(item);
		}
		menu->SetTargetForItems(this);
		menu->SetAsyncAutoDestruct(true);
		BRect magnifier = _MagnifierRect();
		menu->Go(ConvertToScreen(magnifier.LeftBottom()), true, true,
			ConvertToScreen(magnifier), true);
		return;
	}

	fTextView->MakeFocus(true);
}


void
SearchField::MessageReceived(BMessage* message)
{
	if (message->what == kMsgSearchScope) {
		SetScope(message->GetInt32("scope", SEARCH_ALL));
		BMessage notify(kMsgSearchScope);
		notify.AddInt32("scope", fScope);
		fTarget.SendMessage(&notify);
		return;
	}
	BView::MessageReceived(message);
}


BSize
SearchField::MinSize()
{
	font_height height;
	be_plain_font->GetHeight(&height);
	float fieldHeight = ceilf(height.ascent + height.descent + 8);
	return BSize(ceilf(be_plain_font->Size() * 11), fieldHeight);
}


BSize
SearchField::MaxSize()
{
	return BSize(ceilf(be_plain_font->Size() * 16), MinSize().height);
}


BSize
SearchField::PreferredSize()
{
	return BSize(ceilf(be_plain_font->Size() * 14), MinSize().height);
}
