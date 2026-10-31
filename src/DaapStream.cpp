/*
 * A song on a shared library, read while it downloads.
 */
#include "DaapStream.h"

#include <OS.h>

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>

#include "Daap.h"
#include "DaapProtocol.h"


static const bigtime_t kTimeout = 30000000;
	// no data for this long: the share is gone
static const off_t kMaxSize = 1024LL * 1024 * 1024;
static const off_t kAheadDistance = 512 * 1024;
	// reads further ahead of the download fetch a piece of their own
static const size_t kMinPieceSize = 32 * 1024;
static const size_t kMaxPieceSize = 1024 * 1024;
static const size_t kMaxPieces = 16;
static const size_t kDownloadChunk = 64 * 1024;


DaapStream::DaapStream(const char* url)
	:
	fPort(0),
	fData(NULL),
	fSize(0),
	fReceived(0),
	fDone(false),
	fStatus(B_OK),
	fNoRanges(false),
	fLastPieceStart(-1),
	fLastPieceEnd(-1),
	fPieceSize(kMinPieceSize),
	fCanceled(false),
	fPosition(0),
	fThread(-1),
	fConnection(NULL)
{
	std::string host;
	std::string path;
	uint16_t port;
	if (parse_share_address(url, host, port, path)) {
		fHost = host.c_str();
		fPort = port;
		fPath = path.c_str();
		fHostAndPort = daap_host_and_port(fHost, fPort);
	}
}


DaapStream::~DaapStream()
{
	Cancel();
	if (fThread >= 0) {
		status_t result;
		wait_for_thread(fThread, &result);
	}
	delete fConnection;
	free(fData);
}


bool
DaapStream::IsStreamURL(const char* path)
{
	return strncmp(path, "daap://", 7) == 0;
}


status_t
DaapStream::_Request(HttpConnection& connection, off_t start, off_t end,
	int& status, BString& error)
{
	uint32 session;
	BString authorization;
	int32 requestId;
	if (!DaapSessions::Get(fHostAndPort, session, authorization, requestId)) {
		error = "the shared library is not connected";
		return B_NOT_ALLOWED;
	}

	BString pathAndQuery(fPath);
	pathAndQuery << "?session-id=" << session;
	std::map<std::string, std::string> headers;
	status_t result = daap_get(connection, fHost, fPort,
		pathAndQuery.String(), authorization, requestId, kTimeout, status,
		headers, start, end);
	if (result != B_OK) {
		error = strerror(result);
		return result;
	}
	if (status == 404) {
		error = "the song is no longer on the shared library";
		return B_ENTRY_NOT_FOUND;
	}
	if (status != 200 && status != 206) {
		error = "the shared library answered with HTTP status ";
		error << status;
		return B_ERROR;
	}
	return B_OK;
}


status_t
DaapStream::Open(BString& error)
{
	if (fHost.IsEmpty()) {
		error = "bad address";
		return B_BAD_VALUE;
	}

	fConnection = new HttpConnection(&fCanceled);
	int status;
	status_t result = _Request(*fConnection, -1, -1, status, error);
	if (result != B_OK)
		return result;

	fSize = fConnection->ContentLength();
	if (fSize <= 0 || fSize > kMaxSize) {
		error = fSize <= 0 ? "the shared library did not say how long the "
			"song is" : "the song is too large";
		return B_NOT_SUPPORTED;
	}
	fData = (uint8*)malloc(fSize);
	if (fData == NULL) {
		error = strerror(B_NO_MEMORY);
		return B_NO_MEMORY;
	}

	fThread = spawn_thread(&_DownloadEntry, "daap download",
		B_NORMAL_PRIORITY, this);
	if (fThread < 0) {
		error = strerror(fThread);
		return fThread;
	}
	resume_thread(fThread);
	return B_OK;
}


void
DaapStream::Cancel()
{
	fCanceled = true;
	fChanged.notify_all();
}


status_t
DaapStream::_DownloadEntry(void* cookie)
{
	static_cast<DaapStream*>(cookie)->_Download();
	return B_OK;
}


void
DaapStream::_Download()
{
	off_t received = 0;
	status_t status = B_OK;
	while (received < fSize) {
		// only this thread writes past fReceived, readers stay below it
		ssize_t count = fConnection->ReadBody(fData + received,
			(size_t)std::min((off_t)kDownloadChunk, fSize - received),
			kTimeout);
		if (count <= 0) {
			status = count == 0 ? B_PARTIAL_READ : count;
			break;
		}
		received += count;
		std::lock_guard<std::mutex> _(fLock);
		fReceived = received;
		fChanged.notify_all();
	}

	std::lock_guard<std::mutex> _(fLock);
	fDone = true;
	fStatus = status;
	fChanged.notify_all();
}


bool
DaapStream::_CopyFromPieces(off_t position, void* buffer, size_t size)
{
	for (std::list<Piece>::iterator piece = fPieces.begin();
			piece != fPieces.end(); piece++) {
		if (position < piece->start
			|| position + (off_t)size > piece->start
				+ (off_t)piece->data.size())
			continue;
		memcpy(buffer, piece->data.data() + (position - piece->start), size);
		fPieces.splice(fPieces.begin(), fPieces, piece);
		return true;
	}
	return false;
}


status_t
DaapStream::_FetchPiece(off_t position, size_t size)
{
	// reading on from the last piece: a larger one; elsewhere: probing
	if (position >= fLastPieceStart && position <= fLastPieceEnd)
		fPieceSize = std::min(fPieceSize * 2, kMaxPieceSize);
	else
		fPieceSize = kMinPieceSize;
	size_t length = std::max(size, fPieceSize);
	off_t end = std::min(fSize, position + (off_t)length) - 1;
	length = (size_t)(end - position + 1);
	fLastPieceStart = position;
	fLastPieceEnd = end + 1;
	HttpConnection connection(&fCanceled);
	int status;
	BString error;
	status_t result = _Request(connection, position, end, status, error);
	if (result != B_OK)
		return result;
	if (status != 206)
		return B_NOT_SUPPORTED;
			// the server sent the whole song: it ignores ranges

	Piece piece;
	piece.start = position;
	result = connection.ReadWholeBody(piece.data, length, kTimeout);
	if (result != B_OK)
		return result;
	if (piece.data.size() < size)
		return B_PARTIAL_READ;

	std::lock_guard<std::mutex> _(fLock);
	fPieces.push_front(piece);
	if (fPieces.size() > kMaxPieces)
		fPieces.pop_back();
	return B_OK;
}


ssize_t
DaapStream::ReadAt(off_t position, void* buffer, size_t size)
{
	if (position < 0)
		return B_BAD_VALUE;

	std::unique_lock<std::mutex> lock(fLock);
	off_t lastReceived = fReceived;
	bigtime_t lastProgress = system_time();
	while (true) {
		if (fCanceled)
			return B_CANCELED;
		if (position >= fSize || size == 0)
			return 0;
		size = (size_t)std::min((off_t)size, fSize - position);
		if (position + (off_t)size <= fReceived) {
			memcpy(buffer, fData + position, size);
			return size;
		}
		if (_CopyFromPieces(position, buffer, size))
			return size;
		if (fDone && fStatus != B_OK)
			return fStatus;

		if (!fNoRanges && position > fReceived + kAheadDistance) {
			lock.unlock();
			status_t status = _FetchPiece(position, size);
			lock.lock();
			if (status == B_NOT_SUPPORTED)
				fNoRanges = true;
			else if (status != B_OK)
				return status;
			continue;
		}

		fChanged.wait_for(lock, std::chrono::milliseconds(100));
		if (fReceived != lastReceived) {
			lastReceived = fReceived;
			lastProgress = system_time();
		} else if (system_time() - lastProgress > kTimeout)
			return B_TIMED_OUT;
	}
}


ssize_t
DaapStream::WriteAt(off_t position, const void* buffer, size_t size)
{
	return B_NOT_SUPPORTED;
}


off_t
DaapStream::Seek(off_t position, uint32 seekMode)
{
	off_t base = 0;
	switch (seekMode) {
		case SEEK_SET:
			break;
		case SEEK_CUR:
			base = fPosition;
			break;
		case SEEK_END:
			base = fSize;
			break;
		default:
			return B_BAD_VALUE;
	}
	if (base + position < 0)
		return B_BAD_VALUE;
	fPosition = base + position;
	return fPosition;
}


off_t
DaapStream::Position() const
{
	return fPosition;
}


status_t
DaapStream::SetSize(off_t size)
{
	return B_NOT_SUPPORTED;
}


status_t
DaapStream::GetSize(off_t* size) const
{
	*size = fSize;
	return B_OK;
}
