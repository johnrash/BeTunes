/*
 * A song on a shared library as a BPositionIO for BMediaFile: a thread
 * downloads it from the start into memory while it plays, and ReadAt()
 * waits for the bytes it needs. Reads far ahead of the download (the tags
 * at the end, a seek) are fetched on their own with a Range request, so
 * opening and seeking do not wait for the whole file: small pieces at
 * first (a seek probes the file here and there), larger ones while the
 * reads go on from where the last piece ended.
 */
#ifndef DAAP_STREAM_H
#define DAAP_STREAM_H


#include <DataIO.h>
#include <OS.h>
#include <String.h>

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <string>


class HttpConnection;


class DaapStream : public BPositionIO {
public:
	// url: daap://host:port/databases/<db>/items/<id>.<format>
								DaapStream(const char* url);
	virtual						~DaapStream();

	// Starts the download; waits until the server answered. error: a text
	// for the user.
			status_t			Open(BString& error);
	// From any thread: reads under way and to come fail.
			void				Cancel();

	virtual	ssize_t				ReadAt(off_t position, void* buffer,
									size_t size);
	virtual	ssize_t				WriteAt(off_t position, const void* buffer,
									size_t size);
	virtual	off_t				Seek(off_t position, uint32 seekMode);
	virtual	off_t				Position() const;
	virtual	status_t			SetSize(off_t size);
	virtual	status_t			GetSize(off_t* size) const;

	static	bool				IsStreamURL(const char* path);

private:
			struct Piece {
				off_t			start;
				std::string		data;
			};

	static	status_t			_DownloadEntry(void* cookie);
			void				_Download();
			status_t			_Request(HttpConnection& connection,
									off_t start, off_t end, int& status,
									BString& error);
			bool				_CopyFromPieces(off_t position, void* buffer,
									size_t size);
			status_t			_FetchPiece(off_t position, size_t size);

			BString				fHost;
			uint16				fPort;
			BString				fHostAndPort;
			BString				fPath;

			std::mutex			fLock;
			std::condition_variable fChanged;
			uint8*				fData;
			off_t				fSize;
			off_t				fReceived;
				// fData[0, fReceived) is there
			bool				fDone;
			status_t			fStatus;
				// how the download ended
			std::list<Piece>	fPieces;
				// fetched ahead of the download, newest first
			bool				fNoRanges;
			// the reading thread's
			off_t				fLastPieceStart;
			off_t				fLastPieceEnd;
			size_t				fPieceSize;
			std::atomic<bool>	fCanceled;
			off_t				fPosition;
			thread_id			fThread;
			HttpConnection*		fConnection;
				// the download's, handed over by Open()
};


#endif	// DAAP_STREAM_H
