/*
 * The source list on the left: the library, the smart playlists and the
 * user's playlists. Songs and files can be dropped on playlists (or below
 * the last one for a new playlist); playlists are renamed in place.
 */
#ifndef SOURCE_LIST_H
#define SOURCE_LIST_H


#include <ListItem.h>
#include <ListView.h>
#include <String.h>

#include "Library.h"


class BTextView;


class SourceItem : public BListItem {
public:
								SourceItem(source_kind kind, int32 playlist,
									const char* name);

			source_kind			Kind() const { return fKind; }
			int32				PlaylistId() const { return fPlaylist; }
			const char*			Name() const { return fName.String(); }
			void				SetName(const char* name) { fName = name; }
			bool				IsPlaying() const { return fPlaying; }
			void				SetPlaying(bool playing)
									{ fPlaying = playing; }

	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete = false);
	virtual	void				Update(BView* owner, const BFont* font);

private:
			source_kind			fKind;
			int32				fPlaylist;
			BString				fName;
			bool				fPlaying;
};


class SourceListView : public BListView {
public:
								SourceListView(const char* name);

			SourceItem*			SourceAt(int32 index) const;
			int32				IndexOf(source_kind kind, int32 playlist) const;
			using BListView::IndexOf;

			void				StartRename(int32 index);
			bool				IsRenaming() const
									{ return fRenameView != NULL; }
			int32				DropIndex() const { return fDropIndex; }

	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 count);
	virtual	void				MessageReceived(BMessage* message);

private:
			bool				_AcceptsDrop(const BMessage* message,
									int32 index) const;
			void				_SetDropIndex(int32 index);
			void				_FinishRename(bool commit);

			int32				fDropIndex;
				// -1: none, CountItems(): the empty area (new playlist)
			BTextView*			fRenameView;
			int32				fRenamePlaylist;
};


#endif	// SOURCE_LIST_H
