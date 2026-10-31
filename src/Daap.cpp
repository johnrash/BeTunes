/*
 * DAAP over the network.
 */
#include "Daap.h"

#include <Autolock.h>
#include <Locker.h>
#include <OS.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

#include "DaapProtocol.h"
#include "Library.h"


#ifndef MSG_NOSIGNAL
#	define MSG_NOSIGNAL 0
#endif


static const bigtime_t kConnectTimeout = 5000000;
static const bigtime_t kRequestTimeout = 30000000;
	// a big library takes a while to list
static const size_t kMaxListSize = 256 * 1024 * 1024;
static const size_t kMaxHeadSize = 64 * 1024;
static const int32 kMaxPlaylists = 200;


// #pragma mark - HttpConnection


HttpConnection::HttpConnection(const std::atomic<bool>* cancel)
	:
	fSocket(-1),
	fContentLength(-1),
	fBodyRead(0),
	fChunked(false),
	fChunkLeft(0),
	fCancel(cancel)
{
}


HttpConnection::~HttpConnection()
{
	if (fSocket >= 0)
		close(fSocket);
}


status_t
HttpConnection::_Wait(int events, bigtime_t timeout)
{
	bigtime_t end = system_time() + timeout;
	while (true) {
		if (fCancel != NULL && *fCancel)
			return B_CANCELED;
		bigtime_t left = end - system_time();
		if (left <= 0)
			return B_TIMED_OUT;
		struct pollfd poller = { fSocket, (short)events, 0 };
		int result = poll(&poller, 1,
			(int)std::min((bigtime_t)100, left / 1000 + 1));
			// short slices, to notice the cancel flag
		if (result > 0)
			return B_OK;
		if (result < 0 && errno != EINTR)
			return errno;
	}
}


status_t
HttpConnection::Connect(const char* host, uint16 port, bigtime_t timeout)
{
	char service[8];
	snprintf(service, sizeof(service), "%u", port);
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* addresses = NULL;
	if (getaddrinfo(host, service, &hints, &addresses) != 0
		|| addresses == NULL)
		return B_NAME_NOT_FOUND;

	status_t status = B_ERROR;
	for (struct addrinfo* address = addresses; address != NULL;
			address = address->ai_next) {
		fSocket = socket(address->ai_family, SOCK_STREAM, 0);
		if (fSocket < 0) {
			status = errno;
			continue;
		}
		fcntl(fSocket, F_SETFL, fcntl(fSocket, F_GETFL) | O_NONBLOCK);
		if (connect(fSocket, address->ai_addr, address->ai_addrlen) == 0)
			status = B_OK;
		else if (errno != EINPROGRESS)
			status = errno;
		else {
			status = _Wait(POLLOUT, timeout);
			if (status == B_OK) {
				int error = 0;
				socklen_t length = sizeof(error);
				if (getsockopt(fSocket, SOL_SOCKET, SO_ERROR, &error, &length)
						!= 0)
					status = errno;
				else
					status = error;
			}
		}
		if (status == B_OK)
			break;
		close(fSocket);
		fSocket = -1;
		if (status == B_CANCELED)
			break;
	}
	freeaddrinfo(addresses);
	return status;
}


status_t
HttpConnection::Send(const std::string& request, bigtime_t timeout)
{
	size_t sent = 0;
	while (sent < request.size()) {
		status_t status = _Wait(POLLOUT, timeout);
		if (status != B_OK)
			return status;
		ssize_t count = send(fSocket, request.data() + sent,
			request.size() - sent, MSG_NOSIGNAL);
		if (count < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return errno;
		}
		sent += count;
	}
	return B_OK;
}


status_t
HttpConnection::ReadHead(int& status,
	std::map<std::string, std::string>& headers, bigtime_t timeout)
{
	std::string head;
	char buffer[4096];
	size_t end;
	while ((end = head.find("\r\n\r\n")) == std::string::npos) {
		if (head.size() > kMaxHeadSize)
			return B_BAD_DATA;
		status_t result = _Wait(POLLIN, timeout);
		if (result != B_OK)
			return result;
		ssize_t count = recv(fSocket, buffer, sizeof(buffer), 0);
		if (count < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return errno;
		}
		if (count == 0)
			return B_BAD_DATA;
				// closed before the end of the head
		head.append(buffer, count);
	}

	fBuffered = head.substr(end + 4);
	head.resize(end);
	if (!http_parse_head(head, status, headers))
		return B_BAD_DATA;

	fContentLength = -1;
	std::map<std::string, std::string>::const_iterator found
		= headers.find("content-length");
	if (found != headers.end())
		fContentLength = strtoll(found->second.c_str(), NULL, 10);
	found = headers.find("transfer-encoding");
	fChunked = false;
	if (found != headers.end()) {
		BString encoding(found->second.c_str());
		fChunked = encoding.IFindFirst("chunked") >= 0;
	}
	if (fChunked)
		fContentLength = -1;
	fBodyRead = 0;
	fChunkLeft = 0;
	return B_OK;
}


ssize_t
HttpConnection::ReadBody(void* buffer, size_t size, bigtime_t timeout)
{
	if (size == 0)
		return 0;
	if (fContentLength >= 0 && fBodyRead >= fContentLength)
		return 0;

	// raw reads: what came with the head first
	auto rawRead = [this, timeout](void* to, size_t length) -> ssize_t {
		if (!fBuffered.empty()) {
			size_t count = std::min(length, fBuffered.size());
			memcpy(to, fBuffered.data(), count);
			fBuffered.erase(0, count);
			return count;
		}
		while (true) {
			status_t status = _Wait(POLLIN, timeout);
			if (status != B_OK)
				return status;
			ssize_t count = recv(fSocket, to, length, 0);
			if (count >= 0)
				return count;
			if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
				return errno;
		}
	};
	auto readLine = [&rawRead](std::string& line) -> status_t {
		line.clear();
		char c;
		while (line.size() < 1024) {
			ssize_t count = rawRead(&c, 1);
			if (count < 0)
				return count;
			if (count == 0)
				return B_BAD_DATA;
			if (c == '\n')
				return B_OK;
			if (c != '\r')
				line += c;
		}
		return B_BAD_DATA;
	};

	if (fChunked) {
		if (fChunkLeft < 0)
			return 0;
				// the last chunk was read
		if (fChunkLeft == 0) {
			std::string line;
			status_t status = readLine(line);
			if (status != B_OK)
				return status;
			fChunkLeft = strtoll(line.c_str(), NULL, 16);
			if (fChunkLeft <= 0) {
				// the end, and the trailer up to a blank line
				fChunkLeft = -1;
				do {
					if (readLine(line) != B_OK)
						break;
				} while (!line.empty());
				return 0;
			}
		}
		ssize_t count = rawRead(buffer,
			(size_t)std::min((int64)size, fChunkLeft));
		if (count <= 0)
			return count == 0 ? B_BAD_DATA : count;
		fChunkLeft -= count;
		fBodyRead += count;
		if (fChunkLeft == 0) {
			std::string line;
			readLine(line);
				// the CRLF after the chunk's data
		}
		return count;
	}

	if (fContentLength >= 0)
		size = (size_t)std::min((int64)size, fContentLength - fBodyRead);
	ssize_t count = rawRead(buffer, size);
	if (count > 0)
		fBodyRead += count;
	else if (count == 0 && fContentLength >= 0)
		return B_BAD_DATA;
			// closed before the end
	return count;
}


status_t
HttpConnection::ReadWholeBody(std::string& body, size_t maxSize,
	bigtime_t timeout)
{
	body.clear();
	if (fContentLength > (int64)maxSize)
		return B_NO_MEMORY;
	if (fContentLength > 0)
		body.reserve((size_t)fContentLength);
	char buffer[65536];
	while (true) {
		ssize_t count = ReadBody(buffer, sizeof(buffer), timeout);
		if (count < 0)
			return count;
		if (count == 0)
			return B_OK;
		if (body.size() + count > maxSize)
			return B_NO_MEMORY;
		body.append(buffer, count);
	}
}


// #pragma mark - requests


BString
daap_host_and_port(const char* host, uint16 port)
{
	BString text;
	if (strchr(host, ':') != NULL)
		text << "[" << host << "]";
	else
		text << host;
	text << ":" << port;
	return text;
}


status_t
daap_get(HttpConnection& connection, const char* host, uint16 port,
	const std::string& pathAndQuery, const BString& authorization,
	int32 requestId, bigtime_t timeout, int& status,
	std::map<std::string, std::string>& headers, off_t rangeStart,
	off_t rangeEnd)
{
	status_t result = connection.Connect(host, port, kConnectTimeout);
	if (result != B_OK)
		return result;

	// the headers of the iTunes 4.5 to 6 clients, which their servers
	// insist on (other servers do not mind them)
	BString request;
	request << "GET " << pathAndQuery.c_str() << " HTTP/1.1\r\n"
		<< "Host: " << daap_host_and_port(host, port) << "\r\n"
		<< "Accept: */*\r\n"
		<< "Cache-Control: no-cache\r\n"
		<< "User-Agent: iTunes/4.6 (Windows; N)\r\n"
		<< "Client-DAAP-Version: 3.0\r\n"
		<< "Client-DAAP-Access-Index: 2\r\n"
		<< "Client-DAAP-Validation: "
		<< daap_validation(pathAndQuery, requestId).c_str() << "\r\n"
		<< "Client-DAAP-Request-ID: " << requestId << "\r\n";
	if (!authorization.IsEmpty())
		request << "Authorization: " << authorization << "\r\n";
	if (rangeStart >= 0) {
		request << "Range: bytes=" << rangeStart << "-";
		if (rangeEnd >= rangeStart)
			request << rangeEnd;
		request << "\r\n";
	}
	request << "Connection: close\r\n\r\n";

	result = connection.Send(request.String(), timeout);
	if (result == B_OK)
		result = connection.ReadHead(status, headers, timeout);
	return result;
}


BString
daap_authorization(const char* user, const char* password)
{
	if (password == NULL || password[0] == '\0')
		return BString();
	// iTunes ignores the name; some servers want one
	std::string credentials(user != NULL && user[0] != '\0' ? user : "BeTunes");
	credentials += ":";
	credentials += password;
	BString authorization("Basic ");
	authorization << base64_encode(credentials).c_str();
	return authorization;
}


// #pragma mark - DaapSessions


namespace {

struct SessionEntry {
	uint32		session;
	BString		authorization;
	int32		requestId;
};

BLocker sSessionLock("daap sessions");
std::map<std::string, SessionEntry> sSessions;

}	// namespace


void
DaapSessions::Set(const char* hostAndPort, uint32 session,
	const char* authorization)
{
	BAutolock _(sSessionLock);
	SessionEntry& entry = sSessions[hostAndPort];
	entry.session = session;
	entry.authorization = authorization;
	entry.requestId = 10;
		// where iTunes' clients started
}


void
DaapSessions::Remove(const char* hostAndPort)
{
	BAutolock _(sSessionLock);
	sSessions.erase(hostAndPort);
}


bool
DaapSessions::Get(const char* hostAndPort, uint32& session,
	BString& authorization, int32& requestId)
{
	BAutolock _(sSessionLock);
	std::map<std::string, SessionEntry>::iterator found
		= sSessions.find(hostAndPort);
	if (found == sSessions.end())
		return false;
	session = found->second.session;
	authorization = found->second.authorization;
	requestId = ++found->second.requestId;
	return true;
}


// #pragma mark - DaapClient


DaapServerInfo::DaapServerInfo()
	:
	version(0),
	authenticationMethod(0),
	loginRequired(false),
	newValidation(false)
{
}


DaapLibrary::DaapLibrary()
	:
	session(0),
	revision(0),
	database(0)
{
}


DaapLibrary::~DaapLibrary()
{
	for (size_t i = 0; i < songs.size(); i++)
		delete songs[i];
}


DaapClient::DaapClient(const char* host, uint16 port)
	:
	fHost(host),
	fPort(port),
	fStatus(0)
{
}


void
DaapClient::SetPassword(const char* user, const char* password)
{
	fAuthorization = daap_authorization(user, password);
}


BString
DaapClient::HostAndPort() const
{
	return daap_host_and_port(fHost, fPort);
}


daap_error
DaapClient::_Fail(daap_error error, const char* text)
{
	fErrorText = text;
	return error;
}


daap_error
DaapClient::_Get(const BString& pathAndQuery, std::string& body,
	bigtime_t timeout)
{
	HttpConnection connection;
	std::map<std::string, std::string> headers;
	fStatus = 0;
	status_t result = daap_get(connection, fHost, fPort, pathAndQuery.String(),
		fAuthorization, 0, timeout, fStatus, headers);
	if (result == B_OK && fStatus >= 200 && fStatus < 300)
		result = connection.ReadWholeBody(body, kMaxListSize, timeout);
	if (result != B_OK) {
		BString text;
		if (result == B_NAME_NOT_FOUND)
			text << "There is no computer called \xe2\x80\x9c" << fHost
				<< "\xe2\x80\x9d.";
		else {
			text << "Could not reach " << HostAndPort() << ": "
				<< strerror(result) << ".";
		}
		return _Fail(DAAP_NETWORK, text);
	}

	switch (fStatus) {
		case 200:
		case 204:
			return DAAP_OK;
		case 401:
			if (fAuthorization.IsEmpty()) {
				return _Fail(DAAP_NEEDS_PASSWORD,
					"The shared library needs a password.");
			}
			return _Fail(DAAP_WRONG_PASSWORD,
				"The password is not right.");
		case 403:
			return _Fail(DAAP_NOT_ALLOWED,
				"The shared library turned BeTunes away.");
		case 503:
			return _Fail(DAAP_BUSY, "The shared library has too many "
				"listeners right now. Try again later.");
		default:
		{
			BString text;
			text << "The shared library answered with HTTP status "
				<< fStatus << ".";
			return _Fail(DAAP_PROTOCOL, text);
		}
	}
}


daap_error
DaapClient::_ServerInfo(DaapServerInfo& info)
{
	std::string body;
	daap_error error = _Get("/server-info", body, kRequestTimeout);
	if (error != DAAP_OK)
		return error;

	DmapElement server;
	if (!DmapReader((const uint8_t*)body.data(), body.size())
			.Find(dmap_code("msrv"), server))
		return _Fail(DAAP_PROTOCOL, "This is not a shared music library.");
	info.name = dmap_child_string(server, dmap_code("minm")).c_str();
	info.version = (uint32)dmap_child_int(server, dmap_code("apro"));
	info.authenticationMethod
		= (int32)dmap_child_int(server, dmap_code("msau"));
	info.loginRequired = dmap_child_int(server, dmap_code("mslr")) != 0;
	DmapElement element;
	info.newValidation = DmapReader(server).Find(dmap_code("aeFP"), element);
	return DAAP_OK;
}


daap_error
DaapClient::Connect(DaapLibrary& library)
{
	daap_error error = _ServerInfo(library.server);
	if (error != DAAP_OK)
		return error;
	// a library that says it needs a password answers the login with 401
	if (library.server.authenticationMethod != 0 && fAuthorization.IsEmpty())
		return _Fail(DAAP_NEEDS_PASSWORD, "The shared library needs a "
			"password.");

	std::string body;
	error = _Get("/login", body, kRequestTimeout);
	if (error != DAAP_OK)
		return error;
	DmapElement element;
	DmapReader reply((const uint8_t*)body.data(), body.size());
	if (!reply.Find(dmap_code("mlog"), element))
		return _Fail(DAAP_PROTOCOL, "The shared library did not log BeTunes "
			"in.");
	library.session = (uint32)dmap_child_int(element, dmap_code("mlid"));

	BString path;
	path << "/update?session-id=" << library.session << "&revision-number=1";
	error = _Get(path, body, kRequestTimeout);
	if (error == DAAP_NOT_ALLOWED && library.server.newValidation) {
		Logout(library.session);
		BString text;
		text << "\xe2\x80\x9c" << library.server.name << "\xe2\x80\x9d is "
			"shared by iTunes 7 or later, which lets only Apple's own "
			"players connect to it.";
		return _Fail(DAAP_NOT_ALLOWED, text);
	}
	if (error == DAAP_OK) {
		DmapReader update((const uint8_t*)body.data(), body.size());
		if (update.Find(dmap_code("mupd"), element))
			library.revision = (uint32)dmap_child_int(element,
				dmap_code("musr"), 1);
		else
			error = _Fail(DAAP_PROTOCOL, "The shared library sent no "
				"revision.");
	}

	if (error == DAAP_OK) {
		path = "";
		path << "/databases?session-id=" << library.session
			<< "&revision-number=" << library.revision;
		error = _Get(path, body, kRequestTimeout);
	}
	if (error == DAAP_OK) {
		DmapElement databases;
		DmapElement list;
		DmapElement item;
		if (!DmapReader((const uint8_t*)body.data(), body.size())
				.Find(dmap_code("avdb"), databases)
			|| !DmapReader(databases).Find(dmap_code("mlcl"), list)
			|| !DmapReader(list).Find(dmap_code("mlit"), item)) {
			error = _Fail(DAAP_PROTOCOL, "The shared library has no "
				"database.");
		} else {
			library.database = (uint32)dmap_child_int(item,
				dmap_code("miid"));
			library.name = dmap_child_string(item, dmap_code("minm")).c_str();
		}
	}
	if (library.name.IsEmpty())
		library.name = library.server.name;

	std::map<uint32, int32> byItem;
	if (error == DAAP_OK)
		error = _Songs(library, byItem);
	if (error == DAAP_OK)
		error = _Playlists(library, byItem);
	if (error != DAAP_OK)
		Logout(library.session);
	return error;
}


static BString
kind_for_format(const std::string& format)
{
	struct Kind {
		const char*	format;
		const char*	kind;
	};
	static const Kind kKinds[] = {
		{ "mp3", "MPEG audio file" },
		{ "m4a", "AAC audio file" },
		{ "m4p", "Protected AAC audio file" },
		{ "aac", "AAC audio file" },
		{ "ogg", "Ogg Vorbis audio file" },
		{ "opus", "Opus audio file" },
		{ "flac", "FLAC audio file" },
		{ "wav", "WAV audio file" },
		{ "aif", "AIFF audio file" },
		{ "aiff", "AIFF audio file" },
		{ "wma", "Windows Media audio file" }
	};
	for (size_t i = 0; i < sizeof(kKinds) / sizeof(kKinds[0]); i++) {
		if (strcasecmp(format.c_str(), kKinds[i].format) == 0)
			return kKinds[i].kind;
	}
	BString kind(format.c_str());
	kind.ToUpper();
	kind << " audio file";
	return kind;
}


daap_error
DaapClient::_Songs(DaapLibrary& library, std::map<uint32, int32>& byItem)
{
	BString path;
	path << "/databases/" << library.database << "/items?session-id="
		<< library.session << "&revision-number=" << library.revision
		<< "&meta=dmap.itemid,dmap.itemname,dmap.itemkind,daap.songalbum,"
			"daap.songartist,daap.songgenre,daap.songformat,daap.songtime,"
			"daap.songtracknumber,daap.songdiscnumber,daap.songyear,"
			"daap.songsize,daap.songbitrate,daap.songdateadded,"
			"daap.songdescription,daap.songdatakind"
		<< "&type=music";
	std::string body;
	daap_error error = _Get(path, body, kRequestTimeout);
	if (error != DAAP_OK)
		return error;

	DmapElement items;
	DmapElement list;
	if (!DmapReader((const uint8_t*)body.data(), body.size())
			.Find(dmap_code("adbs"), items)
		|| !DmapReader(items).Find(dmap_code("mlcl"), list))
		return _Fail(DAAP_PROTOCOL, "The shared library sent no songs.");

	BString base;
	base << "daap://" << HostAndPort() << "/databases/" << library.database
		<< "/items/";
	DmapReader reader(list);
	DmapElement item;
	while (reader.Next(item)) {
		if (item.code != dmap_code("mlit"))
			continue;
		uint64 kind = dmap_child_int(item, dmap_code("mikd"), 2);
		uint64 dataKind = dmap_child_int(item, dmap_code("asdk"), 0);
		if (kind != 2 || dataKind != 0)
			continue;
				// not a song, or a radio stream

		uint32 id = (uint32)dmap_child_int(item, dmap_code("miid"));
		std::string format = dmap_child_string(item, dmap_code("asfm"));
		if (format.empty() || format.find_first_of("/?#&") != std::string::npos)
			format = "mp3";

		Song* song = new Song;
		song->path << base << id << "." << format.c_str();
		song->title = dmap_child_string(item, dmap_code("minm")).c_str();
		if (song->title.IsEmpty())
			song->title = "Untitled";
		song->artist = dmap_child_string(item, dmap_code("asar")).c_str();
		song->album = dmap_child_string(item, dmap_code("asal")).c_str();
		song->genre = dmap_child_string(item, dmap_code("asgn")).c_str();
		song->kind = dmap_child_string(item, dmap_code("asdt")).c_str();
		if (song->kind.IsEmpty())
			song->kind = kind_for_format(format);
		song->duration = (bigtime_t)dmap_child_int(item, dmap_code("astm"))
			* 1000;
		song->track = (int32)dmap_child_int(item, dmap_code("astn"));
		song->disc = (int32)dmap_child_int(item, dmap_code("asdn"));
		song->year = (int32)dmap_child_int(item, dmap_code("asyr"));
		song->size = (off_t)dmap_child_int(item, dmap_code("assz"));
		song->bitRate = (int32)dmap_child_int(item, dmap_code("asbr"));
		song->dateAdded = (time_t)dmap_child_int(item, dmap_code("asda"));
		byItem[id] = (int32)library.songs.size();
		library.songs.push_back(song);
	}
	return DAAP_OK;
}


daap_error
DaapClient::_Playlists(DaapLibrary& library,
	const std::map<uint32, int32>& byItem)
{
	BString path;
	path << "/databases/" << library.database << "/containers?session-id="
		<< library.session << "&revision-number=" << library.revision
		<< "&meta=dmap.itemid,dmap.itemname,dmap.itemcount,"
			"daap.baseplaylist";
	std::string body;
	daap_error error = _Get(path, body, kRequestTimeout);
	if (error != DAAP_OK) {
		// the songs are what matters: a library without playlists
		return DAAP_OK;
	}

	DmapElement containers;
	DmapElement list;
	if (!DmapReader((const uint8_t*)body.data(), body.size())
			.Find(dmap_code("aply"), containers)
		|| !DmapReader(containers).Find(dmap_code("mlcl"), list))
		return DAAP_OK;

	struct Container {
		uint32		id;
		BString		name;
	};
	std::vector<Container> found;
	DmapReader reader(list);
	DmapElement item;
	while (reader.Next(item)) {
		if (item.code != dmap_code("mlit")
			|| dmap_child_int(item, dmap_code("abpl")) != 0)
			continue;
				// the base playlist is the library itself
		Container container;
		container.id = (uint32)dmap_child_int(item, dmap_code("miid"));
		container.name = dmap_child_string(item, dmap_code("minm")).c_str();
		if (dmap_child_int(item, dmap_code("mimc"), 1) == 0
			|| container.name.IsEmpty())
			continue;
		found.push_back(container);
		if ((int32)found.size() >= kMaxPlaylists)
			break;
	}

	for (size_t i = 0; i < found.size(); i++) {
		path = "";
		path << "/databases/" << library.database << "/containers/"
			<< found[i].id << "/items?session-id=" << library.session
			<< "&revision-number=" << library.revision
			<< "&meta=dmap.itemid,dmap.containeritemid";
		if (_Get(path, body, kRequestTimeout) != DAAP_OK)
			continue;
		DmapElement songs;
		if (!DmapReader((const uint8_t*)body.data(), body.size())
				.Find(dmap_code("apso"), songs)
			|| !DmapReader(songs).Find(dmap_code("mlcl"), list))
			continue;

		DaapPlaylist playlist;
		playlist.name = found[i].name;
		DmapReader songReader(list);
		while (songReader.Next(item)) {
			if (item.code != dmap_code("mlit"))
				continue;
			std::map<uint32, int32>::const_iterator index = byItem.find(
				(uint32)dmap_child_int(item, dmap_code("miid")));
			if (index != byItem.end())
				playlist.songs.push_back(index->second);
		}
		if (!playlist.songs.empty())
			library.playlists.push_back(playlist);
	}
	return DAAP_OK;
}


daap_error
DaapClient::Touch(uint32 session, uint32 revision)
{
	BString path;
	path << "/databases?session-id=" << session << "&revision-number="
		<< revision;
	std::string body;
	return _Get(path, body, kRequestTimeout);
}


void
DaapClient::Logout(uint32 session)
{
	BString path;
	path << "/logout?session-id=" << session;
	std::string body;
	_Get(path, body, 3000000);
}
