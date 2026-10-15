/*
 * The browser lists.
 */
#include "BrowserList.h"

#include <OS.h>

#include "TextUtils.h"


static const bigtime_t kTypeAheadTimeout = 1000000;


BrowserItem::BrowserItem(const char* label, const char* value, bool all)
	:
	BStringItem(label),
	fValue(value),
	fAll(all)
{
}


BrowserListView::BrowserListView(const char* name)
	:
	BListView(name, B_SINGLE_SELECTION_LIST),
	fLastTyped(0)
{
}


BrowserItem*
BrowserListView::BrowserAt(int32 index) const
{
	return dynamic_cast<BrowserItem*>(ItemAt(index));
}


bool
BrowserListView::GetSelection(BString& value) const
{
	BrowserItem* item = BrowserAt(CurrentSelection());
	if (item == NULL || item->IsAll())
		return false;
	value = item->Value();
	return true;
}


void
BrowserListView::SelectValue(const char* value, bool all)
{
	for (int32 i = 0; i < CountItems(); i++) {
		BrowserItem* item = BrowserAt(i);
		if (all ? item->IsAll() : (!item->IsAll() && item->Value() == value)) {
			if (CurrentSelection() != i)
				Select(i);
			return;
		}
	}
	if (CountItems() > 0 && CurrentSelection() != 0)
		Select(0);
}


void
BrowserListView::KeyDown(const char* bytes, int32 count)
{
	unsigned char key = bytes[0];
	if (key < 0x20 || key == B_DELETE || (modifiers() & B_COMMAND_KEY) != 0) {
		fTyped = "";
		BListView::KeyDown(bytes, count);
		return;
	}

	bigtime_t now = system_time();
	if (now - fLastTyped > kTypeAheadTimeout)
		fTyped = "";
	fLastTyped = now;
	fTyped.Append(bytes, count);

	std::string prefix = fold_for_search(fTyped);
	for (int32 i = 0; i < CountItems(); i++) {
		BrowserItem* item = BrowserAt(i);
		if (item->IsAll())
			continue;
		std::string label = fold_for_search(skip_article(item->Text()));
		std::string full = fold_for_search(item->Text());
		if (label.compare(0, prefix.size(), prefix) == 0
			|| full.compare(0, prefix.size(), prefix) == 0) {
			Select(i);
			ScrollToSelection();
			return;
		}
	}
}
