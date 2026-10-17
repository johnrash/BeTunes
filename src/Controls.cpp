/*
 * Custom controls.
 */
#include "Controls.h"

#include <ControlLook.h>
#include <GradientLinear.h>
#include <MessageRunner.h>
#include <Window.h>

#include <math.h>

#include <algorithm>


static const uint32 kHoldTick = 'Thld';
static const bigtime_t kHoldInterval = 150000;
static const int32 kHoldTicksBeforeScan = 3;


namespace {


float
font_size()
{
	return be_plain_font->Size();
}


rgb_color
glyph_color_on(rgb_color base)
{
	rgb_color blue = make_color(30, 110, 230, 255);
	return base.IsLight() ? blue : make_color(110, 170, 255, 255);
}


}	// namespace


// #pragma mark - BarView


BarView::BarView(const char* name, orientation direction, bool top)
	:
	BGroupView(name, direction),
	fTop(top)
{
	SetFlags(Flags() | B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
	SetViewColor(B_TRANSPARENT_COLOR);
}


void
BarView::Draw(BRect update)
{
	_FillGradient(this, 0, update);

	rgb_color line = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT);
	SetHighColor(line);
	BRect bounds = Bounds();
	if (fTop)
		StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
	else
		StrokeLine(bounds.LeftTop(), bounds.RightTop());
}


void
BarView::_FillGradient(BView* view, float top, BRect update)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	float height = Bounds().Height();
	BGradientLinear gradient(BPoint(0, top), BPoint(0, top + height));
	if (fTop) {
		gradient.AddColor(tint_color(base, B_LIGHTEN_1_TINT), 0);
		gradient.AddColor(base, 140);
		gradient.AddColor(tint_color(base, 1.1f), 255);
	} else {
		gradient.AddColor(tint_color(base, B_LIGHTEN_1_TINT), 0);
		gradient.AddColor(tint_color(base, 1.08f), 255);
	}
	view->FillRect(update, gradient);
}


void
BarView::DrawBackground(BView* view, BRect update)
{
	for (BView* parent = view->Parent(); parent != NULL;
			parent = parent->Parent()) {
		BarView* bar = dynamic_cast<BarView*>(parent);
		if (bar == NULL)
			continue;
		BPoint origin = view->ConvertFromScreen(
			bar->ConvertToScreen(bar->Bounds().LeftTop()));
		bar->_FillGradient(view, origin.y, update);
		return;
	}
	view->SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	view->FillRect(update);
}


// #pragma mark - TransportButton


TransportButton::TransportButton(const char* name, glyph symbol, float scale,
	BMessage* message, BMessage* holdMessage)
	:
	BControl(name, NULL, message, B_WILL_DRAW),
	fSymbol(symbol),
	fScale(scale),
	fPressed(false),
	fTracking(false),
	fHoldMessage(holdMessage),
	fHoldRunner(NULL),
	fHoldTicks(0)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


TransportButton::~TransportButton()
{
	delete fHoldRunner;
	delete fHoldMessage;
}


void
TransportButton::SetSymbol(glyph symbol)
{
	if (symbol == fSymbol)
		return;
	fSymbol = symbol;
	Invalidate();
}


void
TransportButton::Draw(BRect update)
{
	BarView::DrawBackground(this, update);

	BRect bounds = Bounds();
	float size = std::min(bounds.Width(), bounds.Height()) - 2;
	BRect circle(0, 0, size, size);
	circle.OffsetTo(floorf((bounds.Width() - size) / 2),
		floorf((bounds.Height() - size) / 2));

	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	bool pressed = fPressed && IsEnabled();

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// drop shadow, rim
	SetHighColor(0, 0, 0, 50);
	FillEllipse(circle.OffsetByCopy(0, 1));
	SetHighColor(tint_color(base, IsEnabled() ? 1.55f : 1.3f));
	FillEllipse(circle);

	// body
	BRect body = circle.InsetByCopy(1, 1);
	BGradientLinear gradient(body.LeftTop(), body.LeftBottom());
	rgb_color top = pressed ? tint_color(base, 1.25f)
		: tint_color(base, B_LIGHTEN_2_TINT);
	rgb_color bottom = pressed ? tint_color(base, 1.08f)
		: tint_color(base, 1.12f);
	gradient.AddColor(top, 0);
	gradient.AddColor(bottom, 255);
	FillEllipse(body, gradient);

	// gloss on the upper half
	if (!pressed) {
		BRect gloss(body.left + body.Width() * 0.18f, body.top + 1,
			body.right - body.Width() * 0.18f,
			body.top + body.Height() * 0.5f);
		BGradientLinear shine(gloss.LeftTop(), gloss.LeftBottom());
		shine.AddColor(make_color(255, 255, 255, 150), 0);
		shine.AddColor(make_color(255, 255, 255, 0), 255);
		FillEllipse(gloss, shine);
	}

	// symbol
	rgb_color middle = mix(top, bottom, 128);
	rgb_color color = middle.IsLight() ? make_color(45, 45, 50, 255)
		: make_color(235, 235, 235, 255);
	if (!IsEnabled())
		color = mix(color, middle, 150);
	float inset = body.Width() * (fSymbol == GLYPH_PLAY
		|| fSymbol == GLYPH_PAUSE ? 0.28f : 0.24f);
	BRect symbol = body.InsetByCopy(inset, inset);
	if (fSymbol == GLYPH_PLAY) {
		// puts the triangle's centroid in the middle, which looks centered
		symbol.OffsetBy(body.Width() * 0.02f, 0);
	}
	draw_glyph(this, symbol, fSymbol, color);
}


void
TransportButton::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;

	fPressed = true;
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	Invalidate();

	if (fHoldMessage != NULL) {
		fHoldTicks = 0;
		delete fHoldRunner;
		BMessage tick(kHoldTick);
		fHoldRunner = new BMessageRunner(BMessenger(this), &tick,
			kHoldInterval);
	}
}


void
TransportButton::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (!fTracking)
		return;
	bool pressed = Bounds().Contains(where);
	if (pressed != fPressed) {
		fPressed = pressed;
		Invalidate();
	}
}


void
TransportButton::MouseUp(BPoint where)
{
	if (!fTracking)
		return;

	bool scanned = fHoldTicks >= kHoldTicksBeforeScan;
	_StopHold();
	fTracking = false;
	if (fPressed && Bounds().Contains(where) && !scanned)
		Invoke();
	fPressed = false;
	Invalidate();
}


void
TransportButton::MessageReceived(BMessage* message)
{
	if (message->what == kHoldTick) {
		if (fTracking && fPressed && ++fHoldTicks >= kHoldTicksBeforeScan)
			Invoke(fHoldMessage);
		return;
	}
	BControl::MessageReceived(message);
}


void
TransportButton::_StopHold()
{
	delete fHoldRunner;
	fHoldRunner = NULL;
}


BSize
TransportButton::MinSize()
{
	float size = ceilf(font_size() * fScale);
	return BSize(size, size);
}


BSize
TransportButton::MaxSize()
{
	return MinSize();
}


BSize
TransportButton::PreferredSize()
{
	return MinSize();
}


// #pragma mark - VolumeSlider


VolumeSlider::VolumeSlider(const char* name, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW),
	fTracking(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


void
VolumeSlider::SetValue(int32 value)
{
	BControl::SetValue(std::max((int32)0, std::min((int32)100, value)));
}


float
VolumeSlider::_IconSize() const
{
	return ceilf(font_size() * 1.15f);
}


BRect
VolumeSlider::_TrackRect() const
{
	BRect bounds = Bounds();
	float icon = _IconSize();
	float knob = ceilf(font_size() * 0.5f);
	float middle = floorf(bounds.Height() / 2);
	return BRect(icon + 4 + knob, middle - 2,
		bounds.right - icon - 4 - knob, middle + 2);
}


void
VolumeSlider::Draw(BRect update)
{
	BarView::DrawBackground(this, update);

	BRect bounds = Bounds();
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	if (!IsEnabled())
		text = mix(text, base, 140);

	float icon = _IconSize();
	float middle = floorf(bounds.Height() / 2);
	draw_glyph(this, BRect(0, middle - icon / 2, icon, middle + icon / 2),
		GLYPH_SPEAKER, text);
	draw_glyph(this, BRect(bounds.right - icon, middle - icon / 2,
		bounds.right, middle + icon / 2), GLYPH_SPEAKER_LOUD, text);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	BRect track = _TrackRect();
	float x = track.left + track.Width() * Value() / 100.0f;

	// groove
	SetHighColor(tint_color(base, 1.5f));
	FillRoundRect(track, 2, 2);
	BRect inner = track.InsetByCopy(1, 1);
	SetHighColor(tint_color(base, 1.15f));
	FillRect(inner);
	SetHighColor(tint_color(base, 1.35f));
	FillRect(BRect(inner.left, inner.top, std::max(inner.left, x),
		inner.bottom));

	// knob
	float radius = ceilf(font_size() * 0.5f);
	BRect knob(x - radius, middle - radius, x + radius, middle + radius);
	SetHighColor(0, 0, 0, 50);
	FillEllipse(knob.OffsetByCopy(0, 1));
	SetHighColor(tint_color(base, 1.55f));
	FillEllipse(knob);
	BRect body = knob.InsetByCopy(1, 1);
	BGradientLinear gradient(body.LeftTop(), body.LeftBottom());
	bool pressed = fTracking;
	gradient.AddColor(pressed ? tint_color(base, 1.2f)
		: tint_color(base, B_LIGHTEN_2_TINT), 0);
	gradient.AddColor(tint_color(base, 1.1f), 255);
	FillEllipse(body, gradient);
}


void
VolumeSlider::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;

	BRect track = _TrackRect();
	float knob = ceilf(font_size() * 0.5f);
	if (where.x < track.left - knob) {
		SetValue(0);
		Invoke();
		return;
	}
	if (where.x > track.right + knob) {
		SetValue(100);
		Invoke();
		return;
	}

	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	_SetFromPoint(where);
	Invalidate();
}


void
VolumeSlider::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fTracking)
		_SetFromPoint(where);
}


void
VolumeSlider::MouseUp(BPoint where)
{
	if (!fTracking)
		return;
	fTracking = false;
	Invalidate();
}


void
VolumeSlider::MessageReceived(BMessage* message)
{
	if (message->what == B_MOUSE_WHEEL_CHANGED) {
		float delta;
		if (IsEnabled()
			&& message->FindFloat("be:wheel_delta_y", &delta) == B_OK) {
			SetValue(Value() - (int32)roundf(delta * 5));
			Invoke();
		}
		return;
	}
	BControl::MessageReceived(message);
}


void
VolumeSlider::_SetFromPoint(BPoint where)
{
	BRect track = _TrackRect();
	int32 value = (int32)roundf((where.x - track.left) * 100.0f
		/ std::max(1.0f, track.Width()));
	value = std::max((int32)0, std::min((int32)100, value));
	if (value != Value()) {
		SetValue(value);
		Invoke();
	}
}


BSize
VolumeSlider::MinSize()
{
	float height = ceilf(font_size() * 1.4f);
	return BSize(ceilf(font_size() * 9), height);
}


BSize
VolumeSlider::MaxSize()
{
	return BSize(ceilf(font_size() * 11), MinSize().height);
}


BSize
VolumeSlider::PreferredSize()
{
	return BSize(ceilf(font_size() * 10), MinSize().height);
}


// #pragma mark - IconButton


IconButton::IconButton(const char* name, glyph symbol, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW),
	fSymbol(symbol),
	fPressed(false),
	fTracking(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


void
IconButton::SetSymbol(glyph symbol)
{
	if (symbol == fSymbol)
		return;
	fSymbol = symbol;
	Invalidate();
}


void
IconButton::Draw(BRect update)
{
	BarView::DrawBackground(this, update);

	BRect rect = Bounds();
	rgb_color base = ui_color(B_CONTROL_BACKGROUND_COLOR);
	rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	uint32 flags = 0;
	if (!IsEnabled())
		flags |= BControlLook::B_DISABLED;
	if (fPressed || Value() == B_CONTROL_ON)
		flags |= BControlLook::B_ACTIVATED;
	be_control_look->DrawButtonFrame(this, rect, update, base, background,
		flags);
	be_control_look->DrawButtonBackground(this, rect, update, base, flags);

	rgb_color color = ui_color(B_CONTROL_TEXT_COLOR);
	if (Value() == B_CONTROL_ON)
		color = glyph_color_on(base);
	if (!IsEnabled())
		color = mix(color, base, 140);
	BRect bounds = Bounds();
	float size = floorf(bounds.Height() * 0.62f);
	BRect glyphRect(0, 0, size, size);
	glyphRect.OffsetTo((bounds.Width() - size) / 2,
		(bounds.Height() - size) / 2);
	draw_glyph(this, glyphRect, fSymbol, color);
}


void
IconButton::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;
	fPressed = true;
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	Invalidate();
}


void
IconButton::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (!fTracking)
		return;
	bool pressed = Bounds().Contains(where);
	if (pressed != fPressed) {
		fPressed = pressed;
		Invalidate();
	}
}


void
IconButton::MouseUp(BPoint where)
{
	if (!fTracking)
		return;
	fTracking = false;
	bool invoke = fPressed && Bounds().Contains(where);
	fPressed = false;
	Invalidate();
	if (invoke)
		Invoke();
}


BSize
IconButton::MinSize()
{
	return BSize(ceilf(font_size() * 2.6f), ceilf(font_size() * 1.85f));
}


BSize
IconButton::MaxSize()
{
	return MinSize();
}


BSize
IconButton::PreferredSize()
{
	return MinSize();
}


// #pragma mark - BarLabel


BarLabel::BarLabel(const char* name, const char* text, float sizeFactor)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fText(text),
	fSizeFactor(sizeFactor)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * sizeFactor);
	SetFont(&font);
}


void
BarLabel::SetText(const char* text)
{
	if (fText == text)
		return;
	fText = text;
	Invalidate();
}


void
BarLabel::Draw(BRect update)
{
	BarView::DrawBackground(this, update);

	BRect bounds = Bounds();
	BString text(fText);
	TruncateString(&text, B_TRUNCATE_END, bounds.Width());
	font_height height;
	GetFontHeight(&height);
	SetDrawingMode(B_OP_OVER);
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	DrawString(text, BPoint(floorf((bounds.Width() - StringWidth(text)) / 2),
		floorf((bounds.Height() + height.ascent - height.descent) / 2)));
}


BSize
BarLabel::MinSize()
{
	font_height height;
	GetFontHeight(&height);
	return BSize(StringWidth("Browse"), ceilf(height.ascent + height.descent));
}


BSize
BarLabel::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
BarLabel::PreferredSize()
{
	font_height height;
	GetFontHeight(&height);
	return BSize(StringWidth(fText) + 4, ceilf(height.ascent + height.descent));
}


// #pragma mark - HeaderView


HeaderView::HeaderView(const char* label)
	:
	BView(NULL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fLabel(label)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


void
HeaderView::SetLabel(const char* label)
{
	fLabel = label;
	Invalidate();
}


float
HeaderView::HeaderHeight()
{
	// the same height as the column titles of BColumnListView
	return std::max(16.0f, ceilf(font_size() * 1.4f));
}


void
HeaderView::Draw(BRect update)
{
	BRect rect = Bounds();
	rgb_color line = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT);
	SetHighColor(line);
	StrokeLine(rect.LeftBottom(), rect.RightBottom());

	BRect background = rect;
	background.bottom--;
	be_control_look->DrawButtonBackground(this, background, update,
		ui_color(B_CONTROL_BACKGROUND_COLOR), 0,
		BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER);

	font_height height;
	GetFontHeight(&height);
	float baseline = floorf(rect.top + height.ascent
		+ (rect.Height() + 1 - (height.ascent + height.descent)) / 2);
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	SetDrawingMode(B_OP_OVER);
	BString label(fLabel);
	TruncateString(&label, B_TRUNCATE_END, rect.Width() - 10);
	DrawString(label, BPoint(rect.left + 6, baseline));
}


BSize
HeaderView::MinSize()
{
	return BSize(20, HeaderHeight());
}


BSize
HeaderView::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, HeaderHeight());
}


BSize
HeaderView::PreferredSize()
{
	return BSize(100, HeaderHeight());
}
