/*
 * Get Info.
 */
#include "InfoWindow.h"

#include <Button.h>
#include <CheckBox.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>

#include "BeTunes.h"
#include "SongList.h"
#include "TagWriter.h"
#include "TextUtils.h"


static const uint32 kMsgOK = 'Iok ';
static const uint32 kMsgFieldChanged = 'Ifld';
static const uint32 kMsgChooseGenre = 'Igen';
static const uint32 kMsgSelectTab = 'Itab';
	// "index" int32 (for scripting)


namespace {


BString
field_value(const Song* song, tag_field field)
{
	BString value;
	switch (field) {
		case TAG_TITLE:
			value = song->title;
			break;
		case TAG_ARTIST:
			value = song->artist;
			break;
		case TAG_ALBUM:
			value = song->album;
			break;
		case TAG_GENRE:
			value = song->genre;
			break;
		case TAG_YEAR:
			if (song->year > 0)
				value << song->year;
			break;
		case TAG_TRACK:
			if (song->track > 0)
				value << song->track;
			break;
		case TAG_DISC:
			if (song->disc > 0)
				value << song->disc;
			break;
		default:
			break;
	}
	return value;
}


bool
is_numeric(tag_field field)
{
	return field == TAG_YEAR || field == TAG_TRACK || field == TAG_DISC;
}


BStringView*
summary_label(const char* text)
{
	BStringView* label = new BStringView(NULL, text);
	label->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);
	label->SetAlignment(B_ALIGN_RIGHT);
	return label;
}


BStringView*
summary_value(const char* text)
{
	BStringView* value = new BStringView(NULL, text);
	value->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	value->SetTruncation(B_TRUNCATE_MIDDLE);
	return value;
}


}	// namespace


InfoWindow::InfoWindow(BWindow* parent, const BMessenger& target,
	const std::vector<const Song*>& songs, const std::vector<BString>& genres)
	:
	BWindow(BRect(0, 0, 100, 100),
		songs.size() == 1 ? songs[0]->title.String()
			: "Multiple Song Information",
		B_TITLED_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fMultiple(songs.size() > 1),
	fGenres(genres),
	fNotWritable(0),
	fTabs(NULL)
{
	for (int i = 0; i < TAG_FIELD_COUNT; i++) {
		fFields[i] = NULL;
		fUse[i] = NULL;
	}
	for (size_t i = 0; i < songs.size(); i++) {
		fSongs.push_back(songs[i]->id);
		if (!can_write_tags(songs[i]->path))
			fNotWritable++;
	}

	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));
	BButton* ok = new BButton("ok", "OK", new BMessage(kMsgOK));

	BView* fields = _BuildFields(songs);
	BView* content = fields;
	if (!fMultiple) {
		BTabView* tabs = new BTabView("tabs", B_WIDTH_FROM_LABEL);
		fTabs = tabs;
		tabs->SetBorder(B_NO_BORDER);
		BView* summary = _BuildSummary(songs[0]);
		tabs->AddTab(summary);
		tabs->AddTab(fields);
		tabs->Select(1);
			// editing is what Get Info is opened for, mostly
		content = tabs;
	}

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(fMultiple ? B_USE_WINDOW_SPACING : 0,
			fMultiple ? B_USE_WINDOW_SPACING : B_USE_SMALL_SPACING,
			fMultiple ? B_USE_WINDOW_SPACING : 0, B_USE_WINDOW_SPACING)
		.Add(content)
		.AddGroup(B_HORIZONTAL)
			.SetInsets(fMultiple ? 0 : B_USE_WINDOW_SPACING, 0,
				fMultiple ? 0 : B_USE_WINDOW_SPACING, 0)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	ok->MakeDefault(true);

	AddToSubset(parent);
	CenterIn(parent->Frame());
	BTextControl* first = fFields[fMultiple ? TAG_ARTIST : TAG_TITLE];
	if (first != NULL)
		first->MakeFocus(true);
}


BView*
InfoWindow::_BuildFields(const std::vector<const Song*>& songs)
{
	BGridLayout* grid;
	BView* view = new BView("Info", 0);
	BLayoutBuilder::Group<>(view, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.GetLayout(&grid)
		.End();

	// the values the songs share (empty where they differ)
	for (int field = 0; field < TAG_FIELD_COUNT; field++) {
		BString value = field_value(songs[0], (tag_field)field);
		for (size_t i = 1; i < songs.size(); i++) {
			if (field_value(songs[i], (tag_field)field) != value) {
				value = "";
				break;
			}
		}
		fOriginal[field] = value;
	}

	struct {
		tag_field	field;
		const char*	label;
	} rows[] = {
		{ TAG_TITLE, "Name:" },
		{ TAG_ARTIST, "Artist:" },
		{ TAG_ALBUM, "Album:" },
		{ TAG_GENRE, "Genre:" },
		{ TAG_YEAR, "Year:" },
		{ TAG_TRACK, "Track #:" },
		{ TAG_DISC, "Disc #:" }
	};
	int32 row = 0;
	for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		if (fMultiple && rows[i].field == TAG_TITLE)
			continue;
			// several songs do not share a name
		_AddField(grid, row++, rows[i].field, rows[i].label);
	}

	// a note for files whose tags BeTunes cannot write
	BString note;
	if (fNotWritable > 0 && !fMultiple) {
		note = "Only the tags of MP3 files can be changed: for this song, "
			"the changes are kept in the BeTunes library.";
	} else if (fNotWritable > 0) {
		note << fNotWritable << " of these songs are not MP3 files: their "
			"changes are kept in the BeTunes library only.";
	}
	if (!note.IsEmpty()) {
		BStringView* label = new BStringView("note", note);
		BFont font(be_plain_font);
		font.SetSize(be_plain_font->Size() * 0.9f);
		label->SetFont(&font);
		label->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);
		grid->AddView(label, 0, row, fMultiple ? 4 : 3);
	}
	return view;
}


void
InfoWindow::_AddField(BGridLayout* grid, int32 row, tag_field field,
	const char* label)
{
	BMessage* changed = new BMessage(kMsgFieldChanged);
	changed->AddInt32("field", field);
	BTextControl* control = new BTextControl(kTagFieldNames[field], label,
		fOriginal[field], NULL);
	control->SetModificationMessage(changed);
	fFields[field] = control;

	int32 column = 0;
	if (fMultiple) {
		BString name("use ");
		name << kTagFieldNames[field];
		fUse[field] = new BCheckBox(name, NULL, NULL);
		grid->AddView(fUse[field], column++, row);
	}
	grid->AddItem(control->CreateLabelLayoutItem(), column, row);
	BLayoutItem* text = control->CreateTextViewLayoutItem();
	grid->AddItem(text, column + 1, row);

	float fontSize = be_plain_font->Size();
	if (is_numeric(field)) {
		BTextView* textView = control->TextView();
		for (uint32 c = 32; c < 256; c++) {
			if (!isdigit(c))
				textView->DisallowChar(c);
		}
		textView->SetMaxBytes(field == TAG_YEAR ? 4 : 3);
		text->SetExplicitMaxSize(BSize(fontSize * 5, B_SIZE_UNSET));
		text->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
			B_ALIGN_VERTICAL_CENTER));
	} else
		text->SetExplicitMinSize(BSize(fontSize * 24, B_SIZE_UNSET));

	if (field == TAG_GENRE && !fGenres.empty()) {
		BPopUpMenu* menu = new BPopUpMenu("Choose", false, false);
		for (size_t i = 0; i < fGenres.size(); i++) {
			BMessage* message = new BMessage(kMsgChooseGenre);
			message->AddString("genre", fGenres[i]);
			menu->AddItem(new BMenuItem(fGenres[i], message));
		}
		menu->SetTargetForItems(this);
		BMenuField* genres = new BMenuField("genres", NULL, menu);
		grid->AddView(genres, column + 2, row);
	}
}


BView*
InfoWindow::_BuildSummary(const Song* song)
{
	BView* view = new BView("Summary", 0);

	BStringView* title = new BStringView(NULL, song->title);
	BFont bold(be_bold_font);
	bold.SetSize(be_bold_font->Size() * 1.3f);
	title->SetFont(&bold);
	title->SetTruncation(B_TRUNCATE_END);
	title->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BString time(format_duration(song->duration).c_str());
	BString bitRate;
	if (song->bitRate > 0)
		bitRate << song->bitRate << " kbps";
	BString plays;
	plays << song->playCount;
	BString lastPlayed = song->lastPlayed != 0
		? format_date(song->lastPlayed) : BString("Never");

	BLayoutBuilder::Group<>(view, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(title)
		.Add(summary_value(song->artist))
		.Add(summary_value(song->album))
		.AddStrut(be_plain_font->Size())
		.AddGrid(B_USE_SMALL_SPACING, 2)
			.Add(summary_label("Kind:"), 0, 0)
			.Add(summary_value(song->kind), 1, 0)
			.Add(summary_label("Time:"), 0, 1)
			.Add(summary_value(time), 1, 1)
			.Add(summary_label("Size:"), 0, 2)
			.Add(summary_value(format_size(song->size)), 1, 2)
			.Add(summary_label("Bit Rate:"), 0, 3)
			.Add(summary_value(bitRate), 1, 3)
			.Add(summary_label("Date Added:"), 0, 4)
			.Add(summary_value(format_date(song->dateAdded)), 1, 4)
			.Add(summary_label("Play Count:"), 0, 5)
			.Add(summary_value(plays), 1, 5)
			.Add(summary_label("Last Played:"), 0, 6)
			.Add(summary_value(lastPlayed), 1, 6)
			.Add(summary_label("Where:"), 0, 7)
			.Add(summary_value(song->path), 1, 7)
		.End()
		.AddGlue();
	return view;
}


void
InfoWindow::MessageReceived(BMessage* message)
{
	bool handled = true;
	switch (message->what) {
		case kMsgFieldChanged:
		{
			// typing in a field of several songs ticks its box
			int32 field = message->GetInt32("field", -1);
			if (fMultiple && field >= 0 && field < TAG_FIELD_COUNT
				&& fUse[field] != NULL)
				fUse[field]->SetValue(B_CONTROL_ON);
			break;
		}

		case kMsgSelectTab:
			if (fTabs != NULL)
				fTabs->Select(message->GetInt32("index", 0));
			break;

		case kMsgChooseGenre:
			if (fFields[TAG_GENRE] != NULL)
				fFields[TAG_GENRE]->SetText(message->GetString("genre", ""));
			break;

		case kMsgOK:
			_Apply();
			if (message->IsSourceWaiting()) {
				BMessage reply(B_REPLY);
				reply.AddInt32("error", B_OK);
				message->SendReply(&reply);
			}
			Quit();
			return;

		default:
			handled = false;
			BWindow::MessageReceived(message);
			break;
	}

	// hey waits for a reply
	if (handled && message->IsSourceWaiting()) {
		BMessage reply(B_REPLY);
		reply.AddInt32("error", B_OK);
		message->SendReply(&reply);
	}
}


void
InfoWindow::_Apply()
{
	BMessage apply(kMsgApplyInfo);
	bool changed = false;
	for (int field = 0; field < TAG_FIELD_COUNT; field++) {
		if (fFields[field] == NULL)
			continue;
		BString value(fFields[field]->Text());
		value.Trim();
		bool use = fMultiple ? fUse[field]->Value() == B_CONTROL_ON
			: value != fOriginal[field];
		if (!use)
			continue;
		if (is_numeric((tag_field)field) && !value.IsEmpty()) {
			int number = parse_leading_int(value);
			value = "";
			if (number > 0)
				value << number;
		}
		apply.AddString(kTagFieldNames[field], value);
		changed = true;
	}
	if (!changed)
		return;
	for (size_t i = 0; i < fSongs.size(); i++)
		apply.AddInt32("song", fSongs[i]);
	fTarget.SendMessage(&apply);
}
