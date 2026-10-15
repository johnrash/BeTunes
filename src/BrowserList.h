/*
 * The artist and album lists of the browser. The first item is
 * "All (n Artists)". Typing selects the first matching entry.
 */
#ifndef BROWSER_LIST_H
#define BROWSER_LIST_H


#include <ListItem.h>
#include <ListView.h>
#include <String.h>


class BrowserItem : public BStringItem {
public:
								BrowserItem(const char* label,
									const char* value, bool all);

			const BString&		Value() const { return fValue; }
			bool				IsAll() const { return fAll; }

private:
			BString				fValue;
			bool				fAll;
};


class BrowserListView : public BListView {
public:
								BrowserListView(const char* name);

			BrowserItem*		BrowserAt(int32 index) const;
	// The selected entry; false for "All" (or nothing selected).
			bool				GetSelection(BString& value) const;
	// Selects the entry with that value, or "All".
			void				SelectValue(const char* value, bool all);

	virtual	void				KeyDown(const char* bytes, int32 count);

private:
			BString				fTyped;
			bigtime_t			fLastTyped;
};


#endif	// BROWSER_LIST_H
