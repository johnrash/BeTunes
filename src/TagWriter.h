/*
 * Writes changed song information into the files' tags, in the background
 * (a file may have to be copied). Only MP3 files (ID3 tags) can be written;
 * for other files the changes stay in the BeTunes library. Requests are
 * handled one after the other; the result goes to the target as
 * kMsgTagsWritten.
 */
#ifndef TAG_WRITER_H
#define TAG_WRITER_H


#include <Looper.h>
#include <Messenger.h>

#include "Id3Tag.h"


// Whether BeTunes can change the tags of this file.
bool can_write_tags(const char* path);

// Changes in a message: one string per changed field, named after
// kTagFieldNames.
void archive_tag_changes(const TagChanges& changes, BMessage& message);
TagChanges unarchive_tag_changes(const BMessage& message);


class TagWriter : public BLooper {
public:
								TagWriter(const BMessenger& target);

	// request: "path" strings and the changes (archive_tag_changes()).
	// Files that cannot be written are skipped.
			void				Write(const BMessage& request);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Write(BMessage* request);

			BMessenger			fTarget;
};


#endif	// TAG_WRITER_H
