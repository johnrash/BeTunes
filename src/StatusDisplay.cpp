/*
 * The status display.
 */
#include "StatusDisplay.h"

#include <Application.h>
#include <Bitmap.h>
#include <GradientLinear.h>
#include <IconUtils.h>
#include <MessageRunner.h>
#include <Resources.h>
#include <Shape.h>
#include <Window.h>

#include <math.h>

#include <algorithm>

#include "BeTunes.h"
#include "Controls.h"
#include "TextUtils.h"


static const bigtime_t kCycleInterval = 3000000;
static const bigtime_t kMessageDuration = 5000000;

static const rgb_color kScreenTop = { 244, 246, 231, 255 };
static const rgb_color kScreenBottom = { 214, 220, 194, 255 };
static const rgb_color kText = { 34, 38, 30, 255 };
static const rgb_color kSecondaryText = { 72, 78, 64, 255 };
static const rgb_color kBarFrame = { 70, 74, 62, 255 };
static const rgb_color kBarFill = { 105, 110, 96, 255 };
static const rgb_color kBarBackground = { 236, 239, 222, 255 };


// The ink of a string drawn at the origin, in pixel edges: it covers the
// columns left to right - 1 and the rows top to bottom - 1.
static BRect
ink_bounds(const BFont& font, const char* string)
{
	const char* strings[1] = { string };
	escapement_delta delta = { 0, 0 };
	BRect box;
	font.GetBoundingBoxesForStrings(strings, 1, B_SCREEN_METRIC, &delta, &box);
	return box;
}


StatusDisplay::StatusDisplay(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fHasSong(false),
	fPosition(0),
	fDuration(0),
	fShowAlbum(false),
	fShowRemaining(true),
	fImporting(false),
	fImportCount(0),
	fMessageUntil(0),
	fDragging(false),
	fDragFraction(0),
	fCycleRunner(NULL),
	fIcon(NULL)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


StatusDisplay::~StatusDisplay()
{
	delete fCycleRunner;
	delete fIcon;
}


void
StatusDisplay::AttachedToWindow()
{
	BView::AttachedToWindow();
	BMessage cycle(kMsgCycleDisplay);
	delete fCycleRunner;
	fCycleRunner = new BMessageRunner(BMessenger(this), &cycle,
		kCycleInterval);
	_LoadIcon();
}


void
StatusDisplay::_LoadIcon()
{
	if (fIcon != NULL)
		return;

	BResources* resources = be_app->AppResources();
	if (resources == NULL)
		return;
	size_t size;
	const void* data = resources->LoadResource(B_VECTOR_ICON_TYPE,
		"BEOS:ICON", &size);
	if (data == NULL)
		return;

	float iconSize = ceilf(_LineHeight() * 2.2f);
	fIcon = new BBitmap(BRect(0, 0, iconSize - 1, iconSize - 1), B_RGBA32);
	if (BIconUtils::GetVectorIcon((const uint8*)data, size, fIcon) != B_OK) {
		delete fIcon;
		fIcon = NULL;
	}
}


void
StatusDisplay::SetSong(const char* title, const char* artist,
	const char* album)
{
	fHasSong = true;
	fTitle = title;
	fArtist = artist;
	fAlbum = album;
	fShowAlbum = false;
	fPosition = 0;
	fDuration = 0;
	Invalidate();
}


void
StatusDisplay::ClearSong()
{
	fHasSong = false;
	fDragging = false;
	fPosition = 0;
	fDuration = 0;
	Invalidate();
}


void
StatusDisplay::SetTime(bigtime_t position, bigtime_t duration)
{
	if (duration > 0 && position > duration)
		position = duration;
	// redraw only when the shown seconds change
	if (position / 1000000 == fPosition / 1000000 && duration == fDuration) {
		fPosition = position;
		return;
	}
	fPosition = position;
	fDuration = duration;
	Invalidate();
}


void
StatusDisplay::SetImportStatus(const char* name, int32 count)
{
	fImporting = true;
	fImportName = name;
	fImportCount = count;
	Invalidate();
}


void
StatusDisplay::ClearImportStatus()
{
	fImporting = false;
	Invalidate();
}


void
StatusDisplay::ShowMessage(const char* message)
{
	fMessage = message;
	fMessageUntil = system_time() + kMessageDuration;
	Invalidate();
}


float
StatusDisplay::_LineHeight() const
{
	font_height height;
	be_plain_font->GetHeight(&height);
	return ceilf(height.ascent + height.descent + 2);
}


BFont
StatusDisplay::_TimeFont() const
{
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * 0.92f);
	return font;
}


BRect
StatusDisplay::_ScreenRect() const
{
	return Bounds().InsetByCopy(1, 1);
}


BRect
StatusDisplay::_BarRect() const
{
	BRect screen = _ScreenRect();
	float line = _LineHeight();
	float timeWidth = ceilf(be_plain_font->StringWidth("-00:00:00")) + 6;
	float height = std::max(7.0f, floorf(line * 0.5f));
	// rows of the same parity as the digits' can center them exactly
	if (fmodf(height - ink_bounds(_TimeFont(), "0123456789").Height(), 2) != 0)
		height++;
	float top = floorf(screen.top + 6 + line * 2.5f) - ceilf(height / 2);
	return BRect(screen.left + timeWidth + 8, top,
		screen.right - timeWidth - 8, top + height - 1);
}


BRect
StatusDisplay::_RightTimeRect() const
{
	BRect bar = _BarRect();
	BRect screen = _ScreenRect();
	return BRect(bar.right + 2, bar.top - 6, screen.right - 2,
		bar.bottom + 6);
}


float
StatusDisplay::_FractionAt(BPoint where) const
{
	// the middle of the pixel on the track between the frame's inner edges,
	// as in Draw()
	BRect bar = _BarRect();
	float fraction = (where.x + 0.5f - (bar.left + 1))
		/ std::max(1.0f, bar.Width() - 1);
	return std::max(0.0f, std::min(1.0f, fraction));
}


void
StatusDisplay::Draw(BRect update)
{
	BarView::DrawBackground(this, update);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	BRect bounds = Bounds();
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);

	// bezel: dark edge, light lower lip
	SetHighColor(255, 255, 255, 110);
	FillRoundRect(bounds.OffsetByCopy(0, 1), 8, 8);
	SetHighColor(tint_color(panel, 1.6f));
	FillRoundRect(bounds, 8, 8);

	BRect screen = _ScreenRect();
	BGradientLinear gradient(screen.LeftTop(), screen.LeftBottom());
	gradient.AddColor(kScreenTop, 0);
	gradient.AddColor(kScreenBottom, 255);
	FillRoundRect(screen, 7, 7, gradient);
	// inner shadow along the top
	SetHighColor(0, 0, 0, 40);
	StrokeLine(BPoint(screen.left + 5, screen.top),
		BPoint(screen.right - 5, screen.top));

	BFont font(be_plain_font);
	font_height height;
	font.GetHeight(&height);
	float line = _LineHeight();
	float centerX = floorf((screen.left + screen.right) / 2);
	float textWidth = screen.Width() - 24;

	bool showMessage = !fMessage.IsEmpty() && system_time() < fMessageUntil;

	if (!fHasSong && !fImporting && !showMessage) {
		// idle: the application icon and name
		BFont bold(be_bold_font);
		bold.SetSize(be_bold_font->Size() * 1.5f);
		SetFont(&bold);
		font_height boldHeight;
		bold.GetHeight(&boldHeight);
		float nameWidth = StringWidth(APP_NAME);
		float iconWidth = fIcon != NULL ? fIcon->Bounds().Width() + 10 : 0;
		float left = centerX - (nameWidth + iconWidth) / 2;
		float middle = floorf((screen.top + screen.bottom) / 2);
		if (fIcon != NULL) {
			DrawBitmap(fIcon, BPoint(left,
				middle - floorf(fIcon->Bounds().Height() / 2)));
		}
		SetHighColor(kText);
		DrawString(APP_NAME, BPoint(left + iconWidth,
			middle + floorf((boldHeight.ascent - boldHeight.descent) / 2)));
		SetFont(be_plain_font);
		return;
	}

	SetFont(&font);
	float baseline1 = floorf(screen.top + 5 + height.ascent);
	float baseline2 = baseline1 + line;

	BString first;
	BString second;
	if (showMessage) {
		first = fMessage;
	} else if (fImporting) {
		first = "Adding to library";
		if (!fImportName.IsEmpty())
			first << ": " << fImportName;
		second << fImportCount << (fImportCount == 1 ? " file" : " files");
	} else {
		first = fTitle;
		second = fShowAlbum && !fAlbum.IsEmpty() ? fAlbum : fArtist;
		if (second.IsEmpty())
			second = fAlbum;
	}

	BFont titleFont(be_bold_font);
	SetFont(&titleFont);
	SetHighColor(kText);
	TruncateString(&first, B_TRUNCATE_END, textWidth);
	DrawString(first, BPoint(centerX - StringWidth(first) / 2, baseline1));
	SetFont(&font);
	SetHighColor(kSecondaryText);
	TruncateString(&second, B_TRUNCATE_END, textWidth);
	DrawString(second, BPoint(centerX - StringWidth(second) / 2, baseline2));

	if (!fHasSong)
		return;

	// times and the progress bar
	BRect bar = _BarRect();
	bigtime_t position = fDragging ? (bigtime_t)(fDragFraction * fDuration)
		: fPosition;
	float fraction = fDuration > 0 ? (float)position / fDuration : 0;
	fraction = std::max(0.0f, std::min(1.0f, fraction));

	BFont small(_TimeFont());
	SetFont(&small);
	SetHighColor(kText);
	BString elapsed(format_duration(position).c_str());
	BString right(format_duration(fShowRemaining ? fDuration - position
		: fDuration).c_str());
	// the digits centered on the bar, both times the same distance from its
	// frame (the bar covers the columns left to right, its right edge is
	// right + 1); by their advance, not their ink, so a ticking time stays
	// put
	BRect digits = ink_bounds(small, "0123456789");
	float middle = bar.top + (bar.Height() + 1) / 2;
	float timeBaseline = middle - (digits.top + digits.bottom) / 2;
	DrawString(elapsed, BPoint(bar.left - 8 - StringWidth(elapsed),
		timeBaseline));
	float rightX = bar.right + 1 + 8;
	if (fShowRemaining) {
		// the font's hyphen sits below the middle of the digits: the
		// remaining time's minus gets its own baseline that centers it
		BRect minus = ink_bounds(small, "-");
		float minusTop = roundf(middle - minus.Height() / 2);
		DrawString("-", BPoint(rightX, minusTop - minus.top));
		rightX += StringWidth("-");
	}
	DrawString(right, BPoint(rightX, timeBaseline));
	SetFont(be_plain_font);

	SetHighColor(kBarBackground);
	FillRect(bar);
	SetHighColor(kBarFrame);
	StrokeRect(bar);
	// the fill ends at x, which travels between the frame's inner edges
	float x = roundf(bar.left + 1 + fraction * (bar.Width() - 1));
	SetHighColor(kBarFill);
	if (x > bar.left + 1)
		FillRect(BRect(bar.left + 1, bar.top + 1, x - 1, bar.bottom - 1));

	// the diamond, centered on the end of the fill and the middle of the
	// bar: a shape keeps its corners where they are put, FillPolygon()
	// truncates them to pixel edges, which put it off the middle
	float size = floorf(bar.Height() / 2) + 3;
	BShape diamond;
	diamond.MoveTo(BPoint(x, middle - size));
	diamond.LineTo(BPoint(x + size, middle));
	diamond.LineTo(BPoint(x, middle + size));
	diamond.LineTo(BPoint(x - size, middle));
	diamond.Close();
	SetHighColor(kText);
	// shapes are drawn relative to the pen, which DrawString() moved
	MovePenTo(B_ORIGIN);
	FillShape(&diamond);
}


void
StatusDisplay::MouseDown(BPoint where)
{
	BRect bar = _BarRect();
	if (fHasSong && fDuration > 0
		&& bar.InsetByCopy(-4, -5).Contains(where)) {
		fDragging = true;
		fDragFraction = _FractionAt(where);
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		Invalidate();
		return;
	}
	if (fHasSong && _RightTimeRect().Contains(where)) {
		fShowRemaining = !fShowRemaining;
		Invalidate();
		return;
	}
	if (fHasSong && where.y < bar.top - 4) {
		fShowAlbum = !fShowAlbum;
		Invalidate();
	}
}


void
StatusDisplay::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (!fDragging)
		return;
	fDragFraction = _FractionAt(where);
	Invalidate();
}


void
StatusDisplay::MouseUp(BPoint where)
{
	if (!fDragging)
		return;
	fDragging = false;
	fDragFraction = _FractionAt(where);
	fPosition = (bigtime_t)(fDragFraction * fDuration);

	BMessage seek(kMsgSeek);
	seek.AddFloat("position", fDragFraction);
	fTarget.SendMessage(&seek);
	Invalidate();
}


void
StatusDisplay::MessageReceived(BMessage* message)
{
	if (message->what == kMsgCycleDisplay) {
		if (fHasSong && !fArtist.IsEmpty() && !fAlbum.IsEmpty()) {
			fShowAlbum = !fShowAlbum;
			Invalidate();
		}
		if (!fMessage.IsEmpty() && system_time() >= fMessageUntil) {
			fMessage = "";
			Invalidate();
		}
		return;
	}
	BView::MessageReceived(message);
}


BSize
StatusDisplay::MinSize()
{
	float line = _LineHeight();
	return BSize(ceilf(be_plain_font->Size() * 22), ceilf(line * 3 + 12));
}


BSize
StatusDisplay::MaxSize()
{
	return BSize(ceilf(be_plain_font->Size() * 40), MinSize().height);
}


BSize
StatusDisplay::PreferredSize()
{
	return BSize(ceilf(be_plain_font->Size() * 34), MinSize().height);
}
