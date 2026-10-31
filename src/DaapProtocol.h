/*
 * The parts of DAAP (iTunes music sharing) that need no network and no
 * Haiku API, so the unit tests can check them anywhere: DMAP, the tagged
 * binary format of the replies; the request validation iTunes 4.5 to 6
 * ask for; the multicast DNS queries and answers that find shared
 * libraries; and HTTP and address helpers.
 */
#ifndef DAAP_PROTOCOL_H
#define DAAP_PROTOCOL_H


#include <stddef.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>


// DMAP: elements of a four character code, a 32 bit big-endian length and
// the data; a container's data is more elements.

constexpr uint32_t
dmap_code(const char code[5])
{
	return (uint32_t)(uint8_t)code[0] << 24 | (uint32_t)(uint8_t)code[1] << 16
		| (uint32_t)(uint8_t)code[2] << 8 | (uint32_t)(uint8_t)code[3];
}


struct DmapElement {
	uint32_t			code;
	const uint8_t*		data;
	uint32_t			length;
};


// Walks the elements of one level. A truncated element ends the walk.
class DmapReader {
public:
						DmapReader(const uint8_t* data, size_t size);
						DmapReader(const DmapElement& container);

	bool				Next(DmapElement& element);
	// The first element of this level with the code (from the start).
	bool				Find(uint32_t code, DmapElement& element) const;

private:
	const uint8_t*		fStart;
	const uint8_t*		fCursor;
	const uint8_t*		fEnd;
};


// Integers of 1, 2, 4 or 8 bytes, big-endian; anything else is 0.
uint64_t dmap_int(const DmapElement& element);
std::string dmap_string(const DmapElement& element);

// The integer or string of a child, or the default.
uint64_t dmap_child_int(const DmapElement& container, uint32_t code,
	uint64_t defaultValue = 0);
std::string dmap_child_string(const DmapElement& container, uint32_t code);


// The Client-DAAP-Validation header for a request to iTunes 4.5 to 6:
// pathAndQuery as sent, requestId 0 when the request has none.
std::string daap_validation(const std::string& pathAndQuery,
	int32_t requestId);

// MD5 (apple: with the one constant iTunes changes), for the tests.
void daap_md5(const uint8_t* data, size_t size, bool apple,
	uint8_t digest[16]);


// Multicast DNS (RFC 6762): a query from an ordinary port, which responders
// answer to that port directly ("legacy unicast").

enum {
	kDnsTypeA = 1,
	kDnsTypePtr = 12,
	kDnsTypeTxt = 16,
	kDnsTypeSrv = 33
};

struct DnsQuestion {
	std::string			name;
	uint16_t			type;
};

struct DnsRecord {
	std::string			name;
	uint16_t			type;
	uint32_t			ttl;
	std::string			target;
		// PTR and SRV
	uint16_t			port;
		// SRV
	std::vector<std::string> text;
		// TXT: "key=value" strings
	uint32_t			address;
		// A: in host order
};

std::string dns_query(uint16_t id, const std::vector<DnsQuestion>& questions);
// The records of all sections of a reply; false if it is not one.
bool dns_parse_reply(const uint8_t* packet, size_t size,
	std::vector<DnsRecord>& records);
// Names compare without case.
bool dns_same_name(const std::string& a, const std::string& b);
// The first label of a name ("My Library" of "My Library._daap._tcp.local").
std::string dns_first_label(const std::string& name);
// The value of "key=value" in a TXT record, keys without case.
bool dns_text_value(const std::vector<std::string>& text, const char* key,
	std::string& value);


// HTTP

std::string base64_encode(const std::string& data);

// The status and the headers (names in lower case) of a response head, up
// to but not including the blank line.
bool http_parse_head(const std::string& head, int& status,
	std::map<std::string, std::string>& headers);

// "host", "host:port", "[::1]:port" or a daap:// or http:// URL; false if
// there is no host or the port is not a number from 1 to 65535.
bool parse_share_address(const std::string& address, std::string& host,
	uint16_t& port, std::string& path);


#endif	// DAAP_PROTOCOL_H
