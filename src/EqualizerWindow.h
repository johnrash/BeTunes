/*
 * The Equalizer window, like iTunes': an "On" check box, the preset menu,
 * the preamp and ten band sliders. It only shows and edits: every change
 * goes to the main window (kMsgEqualizerChanged), which owns the settings,
 * applies them to the player and sends them back (kMsgEqualizerUpdate).
 * Closing the window hides it.
 */
#ifndef EQUALIZER_WINDOW_H
#define EQUALIZER_WINDOW_H


#include <Control.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

#include "Equalizer.h"


class BCheckBox;
class BMenuField;
class BPopUpMenu;


// Equalizer settings in messages: "enabled" bool, "preamp" float, "gains"
// floats (one per band).
void archive_equalizer(const EqualizerSettings& settings, BMessage& message);
// Returns whether all bands were there.
bool unarchive_equalizer(const BMessage& message, EqualizerSettings& settings);


// A vertical slider from -12 to +12 dB (the value is in tenths of a dB).
// Double-click returns it to 0 dB.
class EqualizerSlider : public BControl {
public:
								EqualizerSlider(const char* name,
									BMessage* message);

	virtual	void				SetValue(int32 value);
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MessageReceived(BMessage* message);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

	// Distance from the top and bottom to the ends of the track (the
	// +12 dB and -12 dB positions).
	static	float				TrackInset();

private:
			BRect				_KnobRect() const;
			int32				_ValueAt(float y) const;
			void				_SetValueAndInvoke(int32 value);

			bool				fTracking;
			float				fGrabOffset;
};


class EqualizerWindow : public BWindow {
public:
								EqualizerWindow(BRect frame,
									const BMessenger& target);

	// Lets the window close for real (the application is quitting);
	// before, closing only hides it.
	static	void				AllowQuit();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				FrameMoved(BPoint position);

private:
			void				_Update(const BMessage* update);
			void				_BuildPresetMenu();
			void				_SetShown(bool show);
			void				_SendSettings(const char* preset);
			EqualizerSettings	_SettingsFromControls() const;

			BMessenger			fTarget;
			BCheckBox*			fOnBox;
			BPopUpMenu*			fPresetMenu;
			BMenuField*			fPresetField;
			EqualizerSlider*	fPreamp;
			EqualizerSlider*	fBands[kEqualizerBands];
			BString				fPreset;
			std::vector<BString> fUserPresets;

	static	bool				sQuitAllowed;
};


#endif	// EQUALIZER_WINDOW_H
