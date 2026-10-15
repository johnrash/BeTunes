/*
 * Get Info, like iTunes: for one song a Summary tab (kind, size, bit rate,
 * play count, where the file is) and an Info tab with the editable fields;
 * for several songs the fields they share, each with a check box: typing
 * in a field ticks it, and only ticked fields are changed (an empty ticked
 * field clears that field in all songs). The changes go to the main window
 * as kMsgApplyInfo, which updates the library and writes the tags.
 */
#ifndef INFO_WINDOW_H
#define INFO_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

#include "Id3Tag.h"
#include "Library.h"


class BCheckBox;
class BGridLayout;
class BTabView;
class BTextControl;


class InfoWindow : public BWindow {
public:
								InfoWindow(BWindow* parent,
									const BMessenger& target,
									const std::vector<const Song*>& songs,
									const std::vector<BString>& genres);

	virtual	void				MessageReceived(BMessage* message);

private:
			BView*				_BuildFields(
									const std::vector<const Song*>& songs);
			BView*				_BuildSummary(const Song* song);
			void				_AddField(BGridLayout* grid, int32 row,
									tag_field field, const char* label);
			void				_Apply();

			BMessenger			fTarget;
			std::vector<int32>	fSongs;
			bool				fMultiple;
			BTextControl*		fFields[TAG_FIELD_COUNT];
			BCheckBox*			fUse[TAG_FIELD_COUNT];
			BString				fOriginal[TAG_FIELD_COUNT];
			std::vector<BString> fGenres;
			int32				fNotWritable;
			BTabView*			fTabs;
};


#endif	// INFO_WINDOW_H
