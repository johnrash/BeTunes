/*
 * A small dialog with a line of text and one or two text fields: the
 * address of a shared library, its password. OK sends the reply message to
 * the target with each field as a string under its name; Cancel, Escape
 * and the close box send it with "canceled" true.
 */
#ifndef PROMPT_WINDOW_H
#define PROMPT_WINDOW_H


#include <Message.h>
#include <Messenger.h>
#include <Window.h>

#include <vector>


class BTextControl;


class PromptWindow : public BWindow {
public:
	struct Field {
		const char*		name;
		const char*		label;
		const char*		text;
		bool			hidden;
	};

								PromptWindow(BWindow* parent,
									const char* title, const char* text,
									const std::vector<Field>& fields,
									const char* okLabel,
									const BMessenger& target,
									const BMessage& reply);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Send(bool canceled);

			BMessenger			fTarget;
			BMessage			fReply;
			std::vector<BTextControl*> fFields;
			bool				fSent;
};


#endif	// PROMPT_WINDOW_H
