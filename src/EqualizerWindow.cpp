/*
 * The Equalizer window.
 */
#include "EqualizerWindow.h"

#include <Button.h>
#include <CheckBox.h>
#include <GradientLinear.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>

#include <math.h>
#include <stdio.h>

#include <algorithm>

#include "BeTunes.h"


static const uint32 kMsgOn = 'Eon ';
static const uint32 kMsgSlider = 'Esld';
static const uint32 kMsgPreset = 'Epre';
static const uint32 kMsgMakePreset = 'Emak';
static const uint32 kMsgDeletePreset = 'Edel';
static const uint32 kMsgPresetName = 'Enam';
static const uint32 kMsgNameOK = 'Enok';

static const int32 kMaxValue = (int32)(kEqualizerMaxGain * 10);


bool EqualizerWindow::sQuitAllowed = false;


void
archive_equalizer(const EqualizerSettings& settings, BMessage& message)
{
	message.AddBool("enabled", settings.enabled);
	message.AddFloat("preamp", settings.preamp);
	for (int i = 0; i < kEqualizerBands; i++)
		message.AddFloat("gains", settings.bands[i]);
}


bool
unarchive_equalizer(const BMessage& message, EqualizerSettings& settings)
{
	settings.enabled = message.GetBool("enabled", settings.enabled);
	settings.preamp = message.GetFloat("preamp", settings.preamp);
	bool complete = true;
	for (int i = 0; i < kEqualizerBands; i++) {
		if (message.FindFloat("gains", i, &settings.bands[i]) != B_OK)
			complete = false;
	}
	return complete;
}


namespace {


float
font_size()
{
	return be_plain_font->Size();
}


BString
gain_text(int32 value)
{
	char text[32];
	snprintf(text, sizeof(text), "%+.1f dB", value / 10.0f);
	return text;
}


// The dB scale between the preamp and the bands.
class EqualizerScale : public BView {
public:
	EqualizerScale()
		:
		BView("scale", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		BFont font(be_plain_font);
		font.SetSize(be_plain_font->Size() * 0.85f);
		SetFont(&font);
	}

	virtual void Draw(BRect update)
	{
		BRect bounds = Bounds();
		float inset = EqualizerSlider::TrackInset();
		font_height height;
		GetFontHeight(&height);
		float shift = floorf((height.ascent - height.descent) / 2);
		SetHighUIColor(B_PANEL_TEXT_COLOR);
		const char* labels[3] = { "+12 dB", "0 dB", "\xe2\x88\x92" "12 dB" };
		float y[3] = { inset, floorf(bounds.Height() / 2), bounds.bottom - inset };
		for (int i = 0; i < 3; i++) {
			DrawString(labels[i], BPoint(bounds.right - StringWidth(labels[i]),
				y[i] + shift));
		}
	}

	virtual BSize MinSize()
	{
		return BSize(StringWidth("+12 dB") + 2, ceilf(font_size() * 11));
	}

	virtual BSize MaxSize()
	{
		return BSize(MinSize().width, B_SIZE_UNLIMITED);
	}

	virtual BSize PreferredSize()
	{
		return MinSize();
	}
};


BStringView*
small_label(const char* text)
{
	BStringView* label = new BStringView(NULL, text);
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * 0.85f);
	label->SetFont(&font);
	label->SetAlignment(B_ALIGN_CENTER);
	label->SetExplicitAlignment(BAlignment(B_ALIGN_HORIZONTAL_CENTER,
		B_ALIGN_TOP));
	return label;
}


// Asks for the name of a new preset.
class PresetNameWindow : public BWindow {
public:
	PresetNameWindow(BWindow* parent)
		:
		// modal for the application: a subset modal window of a floating
		// window is not shown at all
		BWindow(BRect(0, 0, 100, 50), "Make Preset", B_MODAL_WINDOW_LOOK,
			B_MODAL_APP_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
				| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
		fParent(parent)
	{
		fName = new BTextControl("name", "Name of the preset:", "", NULL);
		fName->SetExplicitMinSize(BSize(font_size() * 22, B_SIZE_UNSET));
		BButton* cancel = new BButton("cancel", "Cancel",
			new BMessage(B_QUIT_REQUESTED));
		BButton* ok = new BButton("ok", "OK", new BMessage(kMsgNameOK));

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(fName)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(ok)
			.End();
		ok->MakeDefault(true);

		CenterIn(parent->Frame());
		fName->MakeFocus(true);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgNameOK) {
			BString name(fName->Text());
			name.Trim();
			if (name.IsEmpty())
				return;
			BMessage named(kMsgPresetName);
			named.AddString("name", name);
			BMessenger(fParent).SendMessage(&named);
			Quit();
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BWindow*		fParent;
	BTextControl*	fName;
};


}	// namespace


// #pragma mark - EqualizerSlider


EqualizerSlider::EqualizerSlider(const char* name, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fTracking(false),
	fGrabOffset(0)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetToolTip(gain_text(0));
}


float
EqualizerSlider::TrackInset()
{
	return ceilf(font_size() * 0.4f) + 3;
}


void
EqualizerSlider::SetValue(int32 value)
{
	value = std::max(-kMaxValue, std::min(kMaxValue, value));
	if (value == Value())
		return;
	BControl::SetValue(value);
	SetToolTip(gain_text(value));
}


BRect
EqualizerSlider::_KnobRect() const
{
	BRect bounds = Bounds();
	float top = TrackInset();
	float bottom = bounds.bottom - TrackInset();
	float y = top + (kMaxValue - Value()) * (bottom - top) / (2 * kMaxValue);
	float width = ceilf(font_size() * 1.6f);
	float height = ceilf(font_size() * 0.8f);
	float x = floorf(bounds.Width() / 2);
	return BRect(x - floorf(width / 2), roundf(y - height / 2),
		x + floorf(width / 2), roundf(y - height / 2) + height);
}


int32
EqualizerSlider::_ValueAt(float y) const
{
	float top = TrackInset();
	float bottom = Bounds().bottom - TrackInset();
	float fraction = (y - top) / std::max(1.0f, bottom - top);
	int32 value = (int32)roundf(kMaxValue - fraction * 2 * kMaxValue);
	if (abs(value) <= 3)
		value = 0;
		// easy to hit 0 dB
	return std::max(-kMaxValue, std::min(kMaxValue, value));
}


void
EqualizerSlider::_SetValueAndInvoke(int32 value)
{
	if (value == Value())
		return;
	SetValue(value);
	Invoke();
}


void
EqualizerSlider::Draw(BRect update)
{
	BRect bounds = Bounds();
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(base);
	FillRect(update);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	float x = floorf(bounds.Width() / 2);
	float top = TrackInset();
	float bottom = bounds.bottom - TrackInset();

	// ticks at +12, 0 and -12 dB
	SetHighColor(tint_color(base, 1.45f));
	float tick = ceilf(font_size() * 0.45f);
	float middle = floorf((top + bottom) / 2);
	float ys[3] = { top, middle, bottom };
	for (int i = 0; i < 3; i++) {
		StrokeLine(BPoint(x - 4 - tick, ys[i]), BPoint(x - 4, ys[i]));
		StrokeLine(BPoint(x + 4, ys[i]), BPoint(x + 4 + tick, ys[i]));
	}

	// groove
	BRect groove(x - 2, top - 2, x + 2, bottom + 2);
	SetHighColor(tint_color(base, 1.5f));
	FillRoundRect(groove, 2, 2);
	SetHighColor(tint_color(base, 1.2f));
	FillRect(groove.InsetByCopy(1, 1));

	// knob
	BRect knob = _KnobRect();
	SetHighColor(0, 0, 0, 50);
	FillRoundRect(knob.OffsetByCopy(0, 1), 3, 3);
	SetHighColor(tint_color(base, IsEnabled() ? 1.6f : 1.3f));
	FillRoundRect(knob, 3, 3);
	BRect body = knob.InsetByCopy(1, 1);
	BGradientLinear gradient(body.LeftTop(), body.LeftBottom());
	gradient.AddColor(fTracking ? tint_color(base, 1.15f)
		: tint_color(base, B_LIGHTEN_2_TINT), 0);
	gradient.AddColor(tint_color(base, 1.12f), 255);
	FillRoundRect(body, 2, 2, gradient);
	float knobMiddle = floorf((knob.top + knob.bottom) / 2);
	SetHighColor(tint_color(base, 1.6f));
	StrokeLine(BPoint(body.left + 2, knobMiddle),
		BPoint(body.right - 2, knobMiddle));
}


void
EqualizerSlider::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;

	int32 clicks = 1;
	if (Window()->CurrentMessage() != NULL)
		clicks = Window()->CurrentMessage()->GetInt32("clicks", 1);
	if (clicks == 2) {
		_SetValueAndInvoke(0);
		return;
	}

	BRect knob = _KnobRect();
	if (knob.InsetByCopy(-2, -2).Contains(where))
		fGrabOffset = where.y - (knob.top + knob.bottom) / 2;
	else {
		fGrabOffset = 0;
		_SetValueAndInvoke(_ValueAt(where.y));
	}
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	Invalidate();
}


void
EqualizerSlider::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fTracking)
		_SetValueAndInvoke(_ValueAt(where.y - fGrabOffset));
}


void
EqualizerSlider::MouseUp(BPoint where)
{
	if (!fTracking)
		return;
	fTracking = false;
	Invalidate();
}


void
EqualizerSlider::MessageReceived(BMessage* message)
{
	if (message->what == B_MOUSE_WHEEL_CHANGED) {
		float delta;
		if (IsEnabled()
			&& message->FindFloat("be:wheel_delta_y", &delta) == B_OK)
			_SetValueAndInvoke(Value() - (int32)roundf(delta * 5));
		return;
	}
	BControl::MessageReceived(message);
}


BSize
EqualizerSlider::MinSize()
{
	return BSize(ceilf(font_size() * 2.2f), ceilf(font_size() * 11));
}


BSize
EqualizerSlider::MaxSize()
{
	return BSize(MinSize().width, B_SIZE_UNLIMITED);
}


BSize
EqualizerSlider::PreferredSize()
{
	return MinSize();
}


// #pragma mark - EqualizerWindow


EqualizerWindow::EqualizerWindow(BRect frame, const BMessenger& target)
	:
	BWindow(frame, "Equalizer", B_FLOATING_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS
			| B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fPreset("Flat")
{
	fOnBox = new BCheckBox("on", "On", new BMessage(kMsgOn));
	fPresetMenu = new BPopUpMenu("Flat", false, false);
	fPresetField = new BMenuField("preset", NULL, fPresetMenu);
	_BuildPresetMenu();

	BMessage* message = new BMessage(kMsgSlider);
	message->AddInt32("band", -1);
	fPreamp = new EqualizerSlider("preamp", message);

	BLayoutBuilder::Group<> sliders(B_HORIZONTAL, B_USE_SMALL_SPACING);
	sliders
		.AddGroup(B_VERTICAL, 2)
			.Add(fPreamp)
			.Add(small_label("Preamp"))
		.End()
		.AddGroup(B_VERTICAL, 2)
			.Add(new EqualizerScale())
			.Add(small_label(" "))
		.End()
		.AddStrut(2);
	for (int i = 0; i < kEqualizerBands; i++) {
		message = new BMessage(kMsgSlider);
		message->AddInt32("band", i);
		// named "band 1K" and so on (hey reads a name starting with a digit
		// as an index)
		BString name("band ");
		name << kEqualizerBandLabels[i];
		fBands[i] = new EqualizerSlider(name, message);
		sliders
			.AddGroup(B_VERTICAL, 2)
				.Add(fBands[i])
				.Add(small_label(kEqualizerBandLabels[i]))
			.End();
	}

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(fOnBox)
			.AddGlue()
			.Add(fPresetField)
		.End()
		.Add(sliders.View());
}


void
EqualizerWindow::AllowQuit()
{
	sQuitAllowed = true;
}


bool
EqualizerWindow::QuitRequested()
{
	if (sQuitAllowed)
		return true;
	_SetShown(false);
	return false;
}


void
EqualizerWindow::FrameMoved(BPoint position)
{
	BMessage state(kMsgEqualizerWindowState);
	state.AddBool("visible", !IsHidden());
	state.AddRect("frame", Frame());
	fTarget.SendMessage(&state);
}


void
EqualizerWindow::_SetShown(bool show)
{
	if (show && IsHidden())
		Show();
	else if (!show && !IsHidden())
		Hide();
	if (show)
		Activate();

	BMessage state(kMsgEqualizerWindowState);
	state.AddBool("visible", show);
	state.AddRect("frame", Frame());
	fTarget.SendMessage(&state);
}


void
EqualizerWindow::MessageReceived(BMessage* message)
{
	bool handled = true;
	switch (message->what) {
		case kMsgOn:
			_SendSettings(fPreset);
			break;

		case kMsgSlider:
			// editing the sliders switches the equalizer on, like iTunes
			fOnBox->SetValue(B_CONTROL_ON);
			fPreset = "Manual";
			_BuildPresetMenu();
			_SendSettings(fPreset);
			break;

		case kMsgPreset:
		{
			// the main window applies it and sends the values back
			BMessage choose(kMsgEqualizerChanged);
			choose.AddString("preset", message->GetString("preset", "Flat"));
			fTarget.SendMessage(&choose);
			break;
		}

		case kMsgMakePreset:
			(new PresetNameWindow(this))->Show();
			break;

		case kMsgPresetName:
		{
			BMessage make(kMsgEqualizerMakePreset);
			make.AddString("name", message->GetString("name", ""));
			fTarget.SendMessage(&make);
			break;
		}

		case kMsgDeletePreset:
		{
			BMessage remove(kMsgEqualizerDeletePreset);
			remove.AddString("name", fPreset);
			fTarget.SendMessage(&remove);
			break;
		}

		case kMsgEqualizerUpdate:
			_Update(message);
			break;

		case kMsgShowEqualizer:
			_SetShown(message->GetBool("show", IsHidden()));
			break;

		default:
			handled = false;
			BWindow::MessageReceived(message);
			break;
	}

	// these can be sent with hey too, which waits for a reply
	if (handled && message->IsSourceWaiting()) {
		BMessage reply(B_REPLY);
		reply.AddInt32("error", B_OK);
		message->SendReply(&reply);
	}
}


void
EqualizerWindow::_Update(const BMessage* update)
{
	EqualizerSettings settings;
	unarchive_equalizer(*update, settings);
	fOnBox->SetValue(settings.enabled ? B_CONTROL_ON : B_CONTROL_OFF);
	fPreamp->SetValue((int32)roundf(settings.preamp * 10));
	for (int i = 0; i < kEqualizerBands; i++)
		fBands[i]->SetValue((int32)roundf(settings.bands[i] * 10));

	fPreset = update->GetString("preset", "Manual");
	fUserPresets.clear();
	const char* name;
	for (int32 i = 0; update->FindString("user preset", i, &name) == B_OK;
			i++)
		fUserPresets.push_back(name);
	_BuildPresetMenu();
}


void
EqualizerWindow::_BuildPresetMenu()
{
	while (fPresetMenu->CountItems() > 0)
		delete fPresetMenu->RemoveItem((int32)0);

	BMenuItem* manual = new BMenuItem("Manual", NULL);
	manual->SetMarked(fPreset == "Manual");
	fPresetMenu->AddItem(manual);
	fPresetMenu->AddSeparatorItem();

	for (int i = 0; i < count_equalizer_presets(); i++) {
		const char* name = equalizer_preset(i).name;
		BMessage* message = new BMessage(kMsgPreset);
		message->AddString("preset", name);
		BMenuItem* item = new BMenuItem(name, message);
		item->SetMarked(fPreset == name);
		fPresetMenu->AddItem(item);
	}

	bool userPreset = false;
	if (!fUserPresets.empty()) {
		fPresetMenu->AddSeparatorItem();
		for (size_t i = 0; i < fUserPresets.size(); i++) {
			BMessage* message = new BMessage(kMsgPreset);
			message->AddString("preset", fUserPresets[i]);
			BMenuItem* item = new BMenuItem(fUserPresets[i], message);
			if (fPreset == fUserPresets[i]) {
				item->SetMarked(true);
				userPreset = true;
			}
			fPresetMenu->AddItem(item);
		}
	}

	fPresetMenu->AddSeparatorItem();
	fPresetMenu->AddItem(new BMenuItem("Make Preset" B_UTF8_ELLIPSIS,
		new BMessage(kMsgMakePreset)));
	BMenuItem* remove = new BMenuItem("Delete Preset",
		new BMessage(kMsgDeletePreset));
	remove->SetEnabled(userPreset);
	fPresetMenu->AddItem(remove);
	fPresetMenu->SetTargetForItems(this);

	if (fPresetMenu->Superitem() != NULL)
		fPresetMenu->Superitem()->SetLabel(fPreset);
}


void
EqualizerWindow::_SendSettings(const char* preset)
{
	BMessage changed(kMsgEqualizerChanged);
	archive_equalizer(_SettingsFromControls(), changed);
	changed.AddString("preset", preset);
	changed.AddBool("from window", true);
	fTarget.SendMessage(&changed);
}


EqualizerSettings
EqualizerWindow::_SettingsFromControls() const
{
	EqualizerSettings settings;
	settings.enabled = fOnBox->Value() == B_CONTROL_ON;
	settings.preamp = fPreamp->Value() / 10.0f;
	for (int i = 0; i < kEqualizerBands; i++)
		settings.bands[i] = fBands[i]->Value() / 10.0f;
	return settings;
}
