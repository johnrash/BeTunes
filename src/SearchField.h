/*
 * The rounded search field of the control bar. The magnifier opens a menu
 * to search all fields or only artists, albums, songs or genres; the cross
 * clears the field. Every change is sent to the target at once.
 */
#ifndef SEARCH_FIELD_H
#define SEARCH_FIELD_H


#include <Messenger.h>
#include <View.h>


class SearchTextView;


class SearchField : public BView {
public:
								SearchField(const char* name);

			void				SetTarget(const BMessenger& target)
									{ fTarget = target; }
			const char*			Text() const;
	// Sets the text and notifies the target (like typing).
			void				SetText(const char* text);
			int32				Scope() const { return fScope; }
			void				SetScope(int32 scope);
			void				MakeFocus(bool focus = true);

	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				FrameResized(float width, float height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

	// for SearchTextView
			void				TextChanged();
			const char*			Placeholder() const;

private:
			BRect				_MagnifierRect() const;
			BRect				_ClearRect() const;
			void				_LayoutTextView();

			SearchTextView*		fTextView;
			BMessenger			fTarget;
			int32				fScope;
			bool				fSettingText;
};


#endif	// SEARCH_FIELD_H
