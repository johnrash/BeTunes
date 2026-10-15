/*
 * Custom controls of the BeTunes window: the round transport buttons, the
 * volume slider, small icon buttons and list headers.
 */
#ifndef CONTROLS_H
#define CONTROLS_H


#include <Control.h>
#include <GroupView.h>
#include <String.h>
#include <View.h>

#include "Icons.h"


class BMessageRunner;


// The brushed metal look of the control bar and the bottom bar: a group
// view with a gradient. Controls inside a bar draw the bar's gradient
// under themselves with DrawBackground(), so they blend in.
class BarView : public BGroupView {
public:
								BarView(const char* name,
									orientation direction, bool top);

	virtual	void				Draw(BRect update);

	// Fills the update rect of "view" with the gradient of the bar it is
	// in, or with its view color if it is not in a bar.
	static	void				DrawBackground(BView* view, BRect update);

private:
			void				_FillGradient(BView* view, float top,
									BRect update);

			bool				fTop;
				// border at the bottom (top bar) or at the top
};


// Previous, play/pause and next. Previous and next send "holdMessage"
// repeatedly while held down (fast forward and rewind).
class TransportButton : public BControl {
public:
								TransportButton(const char* name,
									glyph symbol, float scale,
									BMessage* message,
									BMessage* holdMessage = NULL);
	virtual						~TransportButton();

			void				SetSymbol(glyph symbol);

	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MessageReceived(BMessage* message);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_StopHold();

			glyph				fSymbol;
			float				fScale;
			bool				fPressed;
			bool				fTracking;
			BMessage*			fHoldMessage;
			BMessageRunner*		fHoldRunner;
			int32				fHoldTicks;
};


// Volume 0..100. Clicking the small speaker mutes, the large one sets the
// maximum. The mouse wheel works too.
class VolumeSlider : public BControl {
public:
								VolumeSlider(const char* name,
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

private:
			BRect				_TrackRect() const;
			float				_IconSize() const;
			void				_SetFromPoint(BPoint where);

			bool				fTracking;
};


// A small square button with a glyph. Value B_CONTROL_ON draws it pressed
// with a highlighted glyph (shuffle and repeat when on).
class IconButton : public BControl {
public:
								IconButton(const char* name, glyph symbol,
									BMessage* message);

			void				SetSymbol(glyph symbol);
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			glyph				fSymbol;
			bool				fPressed;
			bool				fTracking;
};


// A centered text that blends into a bar (the totals, the small labels
// under the search field and the browse button).
class BarLabel : public BView {
public:
								BarLabel(const char* name, const char* text,
									float sizeFactor = 1.0f);

			void				SetText(const char* text);
			const char*			Text() const { return fText.String(); }

	virtual	void				Draw(BRect update);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			BString				fText;
			float				fSizeFactor;
};


// A title above a list, drawn like the column titles of the song list.
class HeaderView : public BView {
public:
								HeaderView(const char* label);

			void				SetLabel(const char* label);
	virtual	void				Draw(BRect update);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

	static	float				HeaderHeight();

private:
			BString				fLabel;
};


#endif	// CONTROLS_H
