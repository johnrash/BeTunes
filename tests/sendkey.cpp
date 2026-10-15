/*
 * Test helper: types keys into a window of a running application.
 *   sendkey <app signature> <window index> <key> [<key> ...]
 * A key is a single character or one of: left right up down home end pageup
 * pagedown enter backspace escape space; it can have modifiers in front:
 * cmd+, shift+, ctrl+, option+ (as in "cmd+shift+m").
 *
 * hey cannot do this: a scripted B_KEY_DOWN goes to the scripting code of
 * the view. Like the input server, this sends the key to the window's
 * preferred handler, so the window routes it to its shortcuts or its focus
 * view.
 *
 * Build: g++ -I/boot/system/develop/headers/private/app -o sendkey \
 *            sendkey.cpp -lbe
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <MessengerPrivate.h>
#include <TokenSpace.h>


struct NamedKey {
	const char*	name;
	char		byte;
};

static const NamedKey kKeys[] = {
	{ "left", B_LEFT_ARROW }, { "right", B_RIGHT_ARROW },
	{ "up", B_UP_ARROW }, { "down", B_DOWN_ARROW }, { "home", B_HOME },
	{ "end", B_END }, { "pageup", B_PAGE_UP }, { "pagedown", B_PAGE_DOWN },
	{ "enter", B_ENTER }, { "backspace", B_BACKSPACE },
	{ "escape", B_ESCAPE }, { "space", B_SPACE },
};

struct NamedModifier {
	const char*	prefix;
	int32		modifiers;
};

static const NamedModifier kModifiers[] = {
	{ "cmd+", B_COMMAND_KEY | B_LEFT_COMMAND_KEY },
	{ "shift+", B_SHIFT_KEY | B_LEFT_SHIFT_KEY },
	{ "ctrl+", B_CONTROL_KEY | B_LEFT_CONTROL_KEY },
	{ "option+", B_OPTION_KEY | B_LEFT_OPTION_KEY },
};


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s <signature> <window index> <key>...\n",
			argv[0]);
		return 1;
	}

	BMessenger application(argv[1]);
	BMessage request(B_GET_PROPERTY);
	request.AddSpecifier("Messenger");
	request.AddSpecifier("Window", (int32)atoi(argv[2]));
	BMessage reply;
	BMessenger window;
	if (application.SendMessage(&request, &reply) != B_OK
		|| reply.FindMessenger("result", &window) != B_OK) {
		fprintf(stderr, "%s: no such window\n", argv[0]);
		return 1;
	}
	BMessenger::Private messenger(window);
	messenger.SetTo(messenger.Team(), messenger.Port(), B_PREFERRED_TOKEN);

	for (int i = 3; i < argc; i++) {
		const char* name = argv[i];
		int32 modifiers = 0;
		bool found = true;
		while (found) {
			found = false;
			for (size_t m = 0; m < sizeof(kModifiers) / sizeof(kModifiers[0]);
					m++) {
				size_t length = strlen(kModifiers[m].prefix);
				if (strncmp(name, kModifiers[m].prefix, length) == 0
					&& name[length] != '\0') {
					modifiers |= kModifiers[m].modifiers;
					name += length;
					found = true;
				}
			}
		}

		char bytes[2] = { name[0], '\0' };
		for (size_t k = 0; k < sizeof(kKeys) / sizeof(kKeys[0]); k++) {
			if (strcmp(name, kKeys[k].name) == 0)
				bytes[0] = kKeys[k].byte;
		}

		BMessage key(B_KEY_DOWN);
		key.AddInt64("when", system_time());
		key.AddInt32("modifiers", modifiers);
		key.AddInt32("key", 0);
		key.AddInt32("raw_char", bytes[0]);
		key.AddInt8("byte", bytes[0]);
		key.AddString("bytes", bytes);
		if (window.SendMessage(&key) != B_OK) {
			fprintf(stderr, "%s: could not send\n", argv[0]);
			return 1;
		}
	}
	return 0;
}
