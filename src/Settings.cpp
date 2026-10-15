/*
 * Settings files.
 */
#include "Settings.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include "BeTunes.h"


BString
settings_path(const char* leaf)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		path.SetTo("/boot/home/config/settings");
	path.Append(APP_NAME);
	create_directory(path.Path(), 0755);
	path.Append(leaf);
	return BString(path.Path());
}


status_t
load_message(const char* leaf, BMessage& message)
{
	BFile file(settings_path(leaf), B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	return message.Unflatten(&file);
}


status_t
save_message(const char* leaf, const BMessage& message)
{
	BString path = settings_path(leaf);
	BString temporary(path);
	temporary << ".new";
	BFile file(temporary, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status == B_OK)
		status = message.Flatten(&file);
	file.Unset();
	if (status == B_OK)
		status = BEntry(temporary).Rename(path, true);
	return status;
}
