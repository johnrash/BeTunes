/*
 * DAAP over the network: plain HTTP/1.1 over a socket, the sessions of the
 * connected shares (for the player's streams) and the client that logs in
 * to a share and reads its songs and playlists. Everything here blocks;
 * the window runs it in threads of its own.
 */
#ifndef DAAP_H
#define DAAP_H


#include <String.h>
#include <SupportDefs.h>

#include <atomic>
#include <map>
#include <string>
#include <vector>


struct Song;


// One request on one connection ("Connection: close"). Setting the cancel
// flag (from any thread) makes whatever waits return B_CANCELED soon after.
class HttpConnection {
public:
								HttpConnection(
									const std::atomic<bool>* cancel = NULL);
								~HttpConnection();

			status_t			Connect(const char* host, uint16 port,
									bigtime_t timeout);
			status_t			Send(const std::string& request,
									bigtime_t timeout);
	// Reads the status line and the headers.
			status_t			ReadHead(int& status,
									std::map<std::string, std::string>&
										headers,
									bigtime_t timeout);
	// Up to size bytes of the body: 0 at its end. Waits at most timeout
	// for the first byte.
			ssize_t				ReadBody(void* buffer, size_t size,
									bigtime_t timeout);
	// The whole body (the Content-Length, or up to the end), at most
	// maxSize bytes.
			status_t			ReadWholeBody(std::string& body,
									size_t maxSize, bigtime_t timeout);

	// Content-Length, -1 if none was given
			int64				ContentLength() const
									{ return fContentLength; }

private:
			status_t			_Wait(int events, bigtime_t timeout);

			int					fSocket;
			std::string			fBuffered;
				// body bytes read with the head
			int64				fContentLength;
			int64				fBodyRead;
			bool				fChunked;
			int64				fChunkLeft;
			const std::atomic<bool>* fCancel;
};


// A DAAP GET with the headers iTunes clients send: requestId 0 for none.
// rangeStart >= 0 asks for the bytes from there on (rangeEnd inclusive,
// -1 for all of them).
status_t daap_get(HttpConnection& connection, const char* host, uint16 port,
	const std::string& pathAndQuery, const BString& authorization,
	int32 requestId, bigtime_t timeout, int& status,
	std::map<std::string, std::string>& headers,
	off_t rangeStart = -1, off_t rangeEnd = -1);

// "Basic ..." for a share's password; empty without one
BString daap_authorization(const char* user, const char* password);
// "host:port", "[v6 address]:port": how shares and their songs are named
BString daap_host_and_port(const char* host, uint16 port);


// The sessions of the connected shares, by "host:port". Thread safe.
namespace DaapSessions {
	void				Set(const char* hostAndPort, uint32 session,
							const char* authorization);
	void				Remove(const char* hostAndPort);
	// Gives the next request id too.
	bool				Get(const char* hostAndPort, uint32& session,
							BString& authorization, int32& requestId);
}


enum daap_error {
	DAAP_OK = 0,
	DAAP_NEEDS_PASSWORD,
	DAAP_WRONG_PASSWORD,
	DAAP_NOT_ALLOWED,
		// 403: iTunes 7 and later turn away every client but their own
	DAAP_BUSY,
		// 503: too many listeners
	DAAP_NETWORK,
	DAAP_PROTOCOL,
	DAAP_CANCELED
};


struct DaapServerInfo {
								DaapServerInfo();

			BString				name;
			uint32				version;
				// DAAP version: major << 16 | minor << 8 | patch
			int32				authenticationMethod;
				// 0 none, 1 a name and a password, 2 a password
			bool				loginRequired;
			bool				newValidation;
				// iTunes 7 and later (com.apple.itunes.req-fplay)
};


struct DaapPlaylist {
			BString				name;
			std::vector<int32>	songs;
				// indexes in DaapLibrary::songs
};


// What a share has, read when connecting.
struct DaapLibrary {
								DaapLibrary();
								~DaapLibrary();

			DaapServerInfo		server;
			BString				name;
				// the database's name, usually the share's
			uint32				session;
			uint32				revision;
			uint32				database;
			std::vector<Song*>	songs;
				// owned until taken (set to NULL)
			std::vector<DaapPlaylist> playlists;
};


class DaapClient {
public:
								DaapClient(const char* host, uint16 port);

			void				SetPassword(const char* user,
									const char* password);

	// Logs in and reads everything. On failure the error and a text for
	// the user.
			daap_error			Connect(DaapLibrary& library);
	// Keeps the session alive (sessions time out after about 30 minutes).
			daap_error			Touch(uint32 session, uint32 revision);
			void				Logout(uint32 session);

			const BString&		ErrorText() const { return fErrorText; }
			BString				HostAndPort() const;

private:
			daap_error			_Get(const BString& pathAndQuery,
									std::string& body, bigtime_t timeout);
			daap_error			_Fail(daap_error error, const char* text);
			daap_error			_ServerInfo(DaapServerInfo& info);
			daap_error			_Songs(DaapLibrary& library,
									std::map<uint32, int32>& byItem);
			daap_error			_Playlists(DaapLibrary& library,
									const std::map<uint32, int32>& byItem);

			BString				fHost;
			uint16				fPort;
			BString				fAuthorization;
			BString				fErrorText;
			int					fStatus;
				// the last HTTP status
};


#endif	// DAAP_H
