/*
 * Adds files and folders to the library in the background: walks folders,
 * reads the tags of new audio files and sends them to the window in
 * batches. Requests are handled one after the other.
 */
#ifndef IMPORTER_H
#define IMPORTER_H


#include <Entry.h>
#include <Looper.h>
#include <Messenger.h>
#include <String.h>


class Library;


class Importer : public BLooper {
public:
								Importer(const Library* library,
									const BMessenger& target);

	// request: "refs" and/or "path" entries, "playlist" int32 (songs are
	// also added to that playlist), "request" int32 (echoed in the replies),
	// "play" bool (echoed in kMsgImportDone).
			void				Import(BMessage* request);
	// Stops the current request (when quitting).
			void				Cancel();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Import(BMessage* request);
			void				_ImportEntry(const entry_ref& ref, int32 depth);
			void				_ImportFile(const entry_ref& ref,
									const char* path);
			void				_Progress(const char* name, bool force);
			void				_Flush(bool force);
			void				_Send(BMessage* message);

			const Library*		fLibrary;
			BMessenger			fTarget;
			BMessage			fBatch;
			int32				fBatchCount;
			bigtime_t			fLastFlush;
			bigtime_t			fLastProgress;
			int32				fFilesSeen;
			int32				fAdded;
			BString				fFirstPath;
			int32				fPlaylist;
			int32				fRequest;
			int32				fCancelled;
};


#endif	// IMPORTER_H
