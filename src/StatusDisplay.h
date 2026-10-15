/*
 * The "LCD" in the middle of the control bar: the playing song, its artist
 * and album (alternating), elapsed and remaining time and a progress bar
 * that can be dragged to seek. Also shows import progress and messages.
 */
#ifndef STATUS_DISPLAY_H
#define STATUS_DISPLAY_H


#include <Messenger.h>
#include <String.h>
#include <View.h>


class BBitmap;
class BMessageRunner;


class StatusDisplay : public BView {
public:
								StatusDisplay(const char* name);
	virtual						~StatusDisplay();

			void				SetTarget(const BMessenger& target)
									{ fTarget = target; }

			void				SetSong(const char* title, const char* artist,
									const char* album);
			void				ClearSong();
			void				SetTime(bigtime_t position,
									bigtime_t duration);

			void				SetImportStatus(const char* name, int32 count);
			void				ClearImportStatus();
	// Shown for a few seconds instead of the song.
			void				ShowMessage(const char* message);

			bool				HasSong() const { return fHasSong; }
			const BString&		Message() const { return fMessage; }

	virtual	void				AttachedToWindow();
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
			float				_LineHeight() const;
			BRect				_ScreenRect() const;
			BRect				_BarRect() const;
			BRect				_RightTimeRect() const;
			float				_FractionAt(BPoint where) const;
			void				_LoadIcon();

			BMessenger			fTarget;
			bool				fHasSong;
			BString				fTitle;
			BString				fArtist;
			BString				fAlbum;
			bigtime_t			fPosition;
			bigtime_t			fDuration;
			bool				fShowAlbum;
			bool				fShowRemaining;

			bool				fImporting;
			BString				fImportName;
			int32				fImportCount;

			BString				fMessage;
			bigtime_t			fMessageUntil;

			bool				fDragging;
			float				fDragFraction;

			BMessageRunner*		fCycleRunner;
			BBitmap*			fIcon;
};


#endif	// STATUS_DISPLAY_H
