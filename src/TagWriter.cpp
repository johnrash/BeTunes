/*
 * Writing tags in the background.
 */
#include "TagWriter.h"

#include <Node.h>
#include <fs_attr.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <vector>

#include "BeTunes.h"


static const uint32 kWriteRequest = 'Twrq';


namespace {


// The copy with the new tag gets the original's attributes (its MIME type,
// Tracker's icon position, the audio attributes, ...) and creation time.
int
copy_attributes(const char* original, const char* copy, void* cookie)
{
	BNode source(original);
	BNode target(copy);
	status_t status = source.InitCheck();
	if (status == B_OK)
		status = target.InitCheck();
	if (status != B_OK)
		return status;

	char name[B_ATTR_NAME_LENGTH];
	source.RewindAttrs();
	while (source.GetNextAttrName(name) == B_OK) {
		attr_info info;
		if (source.GetAttrInfo(name, &info) != B_OK)
			continue;
		std::vector<char> data(info.size > 0 ? info.size : 1);
		ssize_t bytes = source.ReadAttr(name, info.type, 0, data.data(),
			info.size);
		if (bytes < 0)
			return bytes;
		bytes = target.WriteAttr(name, info.type, 0, data.data(), bytes);
		if (bytes < 0)
			return bytes;
	}

	time_t created;
	if (source.GetCreationTime(&created) == B_OK)
		target.SetCreationTime(created);
	return 0;
}


}	// namespace


bool
can_write_tags(const char* path)
{
	const char* dot = strrchr(path, '.');
	if (dot == NULL)
		return false;
	return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".mp2") == 0
		|| strcasecmp(dot, ".mpga") == 0;
}


void
archive_tag_changes(const TagChanges& changes, BMessage& message)
{
	for (int field = 0; field < TAG_FIELD_COUNT; field++) {
		if (changes.set[field]) {
			message.AddString(kTagFieldNames[field],
				changes.value[field].c_str());
		}
	}
}


TagChanges
unarchive_tag_changes(const BMessage& message)
{
	TagChanges changes;
	for (int field = 0; field < TAG_FIELD_COUNT; field++) {
		const char* value;
		if (message.FindString(kTagFieldNames[field], &value) == B_OK)
			changes.Set((tag_field)field, value);
	}
	return changes;
}


TagWriter::TagWriter(const BMessenger& target)
	:
	BLooper("tag writer", B_LOW_PRIORITY),
	fTarget(target)
{
}


void
TagWriter::Write(const BMessage& request)
{
	BMessage copy(request);
	copy.what = kWriteRequest;
	PostMessage(&copy);
}


void
TagWriter::MessageReceived(BMessage* message)
{
	if (message->what == kWriteRequest) {
		_Write(message);
		return;
	}
	BLooper::MessageReceived(message);
}


void
TagWriter::_Write(BMessage* request)
{
	TagChanges changes = unarchive_tag_changes(*request);

	BMessage result(kMsgTagsWritten);
	int32 written = 0;
	int32 failed = 0;
	const char* path;
	for (int32 i = 0; request->FindString("path", i, &path) == B_OK; i++) {
		if (!can_write_tags(path) || changes.IsEmpty())
			continue;
		std::string error;
		int status = write_mp3_tags(path, changes, &copy_attributes, NULL,
			error);
		if (status != 0) {
			fprintf(stderr, "BeTunes: %s: %s\n", path, error.c_str());
			if (failed == 0)
				result.AddString("error", error.c_str());
			result.AddString("failed", path);
			failed++;
			continue;
		}
		struct stat info;
		if (stat(path, &info) == 0) {
			result.AddString("path", path);
			result.AddInt64("size", info.st_size);
		}
		written++;
	}
	result.AddInt32("written", written);
	result.AddInt32("failed count", failed);
	// with a timeout: the window may be waiting for this looper to quit
	fTarget.SendMessage(&result, (BHandler*)NULL, 2000000);
}
