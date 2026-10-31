/*
 * A dialog that asks for a line or two of text.
 */
#include "PromptWindow.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>


static const uint32 kMsgOK = 'Pok ';


PromptWindow::PromptWindow(BWindow* parent, const char* title,
	const char* text, const std::vector<Field>& fields, const char* okLabel,
	const BMessenger& target, const BMessage& reply)
	:
	BWindow(BRect(0, 0, 100, 100), title, B_TITLED_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fReply(reply),
	fSent(false)
{
	BTextView* message = new BTextView("text");
	message->SetText(text);
	message->MakeEditable(false);
	message->MakeSelectable(false);
	message->SetWordWrap(true);
	message->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	message->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	message->SetHighUIColor(B_PANEL_TEXT_COLOR);
	float width = message->StringWidth("M") * 28;
	message->SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
	message->SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));

	BGridLayout* grid = BLayoutBuilder::Grid<>(B_USE_DEFAULT_SPACING,
		B_USE_SMALL_SPACING);
	for (size_t i = 0; i < fields.size(); i++) {
		BTextControl* control = new BTextControl(fields[i].name,
			fields[i].label, fields[i].text, NULL);
		control->TextView()->HideTyping(fields[i].hidden);
		if (fields[i].hidden && fields[i].text != NULL)
			control->SetText(fields[i].text);
				// HideTyping() cleared it
		grid->AddItem(control->CreateLabelLayoutItem(), 0, i);
		grid->AddItem(control->CreateTextViewLayoutItem(), 1, i);
		fFields.push_back(control);
	}

	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));
	BButton* ok = new BButton("ok", okLabel, new BMessage(kMsgOK));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(message)
		.Add(grid)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	ok->MakeDefault(true);

	if (parent != NULL) {
		AddToSubset(parent);
		CenterIn(parent->Frame());
	} else
		CenterOnScreen();
	if (!fFields.empty()) {
		fFields[0]->MakeFocus(true);
		fFields[0]->TextView()->SelectAll();
	}
}


void
PromptWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgOK) {
		_Send(false);
		Quit();
		return;
	}
	BWindow::MessageReceived(message);
}


bool
PromptWindow::QuitRequested()
{
	_Send(true);
	return true;
}


void
PromptWindow::_Send(bool canceled)
{
	if (fSent)
		return;
	fSent = true;
	BMessage reply(fReply);
	reply.AddBool("canceled", canceled);
	for (size_t i = 0; i < fFields.size(); i++) {
		if (!canceled)
			reply.AddString(fFields[i]->Name(), fFields[i]->Text());
	}
	fTarget.SendMessage(&reply);
}
