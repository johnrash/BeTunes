/*
 * Background import of files and folders.
 */
#include "Importer.h"

#include <Directory.h>
#include <NodeInfo.h>
#include <Path.h>

#include <algorithm>
#include <string.h>
#include <vector>

#include "BeTunes.h"
#include "Library.h"
#include "TagReader.h"


static const int32 kImportRequest = 'Ireq';
static const int32 kMaxDepth = 32;
static const int32 kBatchSize = 50;
static const bigtime_t kFlushInterval = 400000;
static const bigtime_t kProgressInterval = 100000;


Importer::Importer(const Library* library, const BMessenger& target)
	:
	BLooper("importer", B_LOW_PRIORITY),
	fLibrary(library),
	fTarget(target),
	fBatch(kMsgImportSongs),
	fBatchCount(0),
	fLastFlush(0),
	fLastProgress(0),
	fFilesSeen(0),
	fAdded(0),
	fPlaylist(-1),
	fRequest(0),
	fCancelled(0)
{
}


void
Importer::Import(BMessage* request)
{
	BMessage copy(*request);
	copy.what = kImportRequest;
	PostMessage(&copy);
}


void
Importer::Cancel()
{
	atomic_set(&fCancelled, 1);
}


void
Importer::MessageReceived(BMessage* message)
{
	if (message->what == kImportRequest) {
		_Import(message);
		return;
	}
	BLooper::MessageReceived(message);
}


void
Importer::_Import(BMessage* request)
{
	if (atomic_get(&fCancelled) != 0)
		return;

	fFilesSeen = 0;
	fAdded = 0;
	fFirstPath = "";
	fPlaylist = request->GetInt32("playlist", -1);
	fRequest = request->GetInt32("request", 0);
	fBatch.MakeEmpty();
	fBatch.AddInt32("playlist", fPlaylist);
	fBatch.AddInt32("request", fRequest);
	fBatchCount = 0;
	fLastFlush = system_time();

	_Progress("", true);

	entry_ref ref;
	for (int32 i = 0; request->FindRef("refs", i, &ref) == B_OK; i++)
		_ImportEntry(ref, 0);
	const char* path;
	for (int32 i = 0; request->FindString("path", i, &path) == B_OK; i++) {
		if (get_ref_for_path(path, &ref) == B_OK)
			_ImportEntry(ref, 0);
	}

	_Flush(true);

	BMessage done(kMsgImportDone);
	done.AddInt32("added", fAdded);
	done.AddInt32("files", fFilesSeen);
	done.AddInt32("playlist", fPlaylist);
	done.AddInt32("request", fRequest);
	done.AddBool("play", request->GetBool("play", false));
	done.AddString("first", fFirstPath);
	_Send(&done);
}


void
Importer::_ImportEntry(const entry_ref& ref, int32 depth)
{
	if (atomic_get(&fCancelled) != 0 || depth > kMaxDepth)
		return;

	BEntry entry(&ref, true);
	if (entry.InitCheck() != B_OK || !entry.Exists())
		return;

	BPath path;
	if (entry.GetPath(&path) != B_OK)
		return;

	if (entry.IsDirectory()) {
		BDirectory directory(&entry);
		std::vector<entry_ref> children;
		entry_ref child;
		while (directory.GetNextRef(&child) == B_OK) {
			if (child.name[0] != '.')
				children.push_back(child);
		}
		// in name order, so playlists made from folders are in album order
		std::sort(children.begin(), children.end(),
			[](const entry_ref& a, const entry_ref& b) {
				return strcasecmp(a.name, b.name) < 0;
			});
		for (size_t i = 0; i < children.size(); i++)
			_ImportEntry(children[i], depth + 1);
		return;
	}

	entry_ref resolved;
	if (entry.GetRef(&resolved) != B_OK)
		return;

	char mimeType[B_MIME_TYPE_LENGTH] = "";
	BNode node(&entry);
	BNodeInfo info(&node);
	if (info.InitCheck() == B_OK)
		info.GetType(mimeType);
	if (!is_audio_file(resolved.name, mimeType))
		return;

	_ImportFile(resolved, path.Path());
}


void
Importer::_ImportFile(const entry_ref& ref, const char* path)
{
	fFilesSeen++;
	_Progress(ref.name, false);

	// one entry per file, in order: new songs with their tags, songs that
	// are already in the library with their id
	BMessage entry;
	int32 existing = fLibrary->SongIdForPath(path);
	if (existing >= 0) {
		entry.AddInt32("existing", existing);
	} else {
		Song song;
		status_t status = read_song_info(ref, path, song);
		if (status != B_OK)
			return;
		archive_song(song, entry);
		fAdded++;
	}
	fBatch.AddMessage("song", &entry);
	fBatchCount++;
	if (fFirstPath.IsEmpty())
		fFirstPath = path;

	_Flush(false);
}


void
Importer::_Progress(const char* name, bool force)
{
	bigtime_t now = system_time();
	if (!force && now - fLastProgress < kProgressInterval)
		return;
	fLastProgress = now;

	BMessage progress(kMsgImportProgress);
	progress.AddString("name", name);
	progress.AddInt32("count", fFilesSeen);
	_Send(&progress);
}


void
Importer::_Flush(bool force)
{
	if (fBatchCount == 0)
		return;
	if (!force && fBatchCount < kBatchSize
		&& system_time() - fLastFlush < kFlushInterval)
		return;

	_Send(&fBatch);
	fBatch.MakeEmpty();
	fBatch.AddInt32("playlist", fPlaylist);
	fBatch.AddInt32("request", fRequest);
	fBatchCount = 0;
	fLastFlush = system_time();
}


void
Importer::_Send(BMessage* message)
{
	// with a timeout: the window may be waiting for this looper to quit
	fTarget.SendMessage(message, (BHandler*)NULL, 2000000);
}
