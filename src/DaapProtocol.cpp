/*
 * DAAP without network: DMAP, request validation, multicast DNS packets,
 * HTTP and address helpers.
 *
 * The validation is MD5 with one of its constants changed, over the request
 * and a few fixed strings; how iTunes 4.5 to 6 compute it was published by
 * David Hammerton and Jon Lech Johansen (2004, 2005). iTunes 7 and later ask
 * for a different one that was never published, so they cannot be used.
 */
#include "DaapProtocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// #pragma mark - DMAP


DmapReader::DmapReader(const uint8_t* data, size_t size)
	:
	fStart(data),
	fCursor(data),
	fEnd(data + size)
{
}


DmapReader::DmapReader(const DmapElement& container)
	:
	fStart(container.data),
	fCursor(container.data),
	fEnd(container.data + container.length)
{
}


bool
DmapReader::Next(DmapElement& element)
{
	if (fCursor == NULL || fEnd - fCursor < 8)
		return false;
	uint32_t length = (uint32_t)fCursor[4] << 24 | (uint32_t)fCursor[5] << 16
		| (uint32_t)fCursor[6] << 8 | fCursor[7];
	if ((size_t)(fEnd - fCursor - 8) < length) {
		fCursor = fEnd;
		return false;
	}
	element.code = (uint32_t)fCursor[0] << 24 | (uint32_t)fCursor[1] << 16
		| (uint32_t)fCursor[2] << 8 | fCursor[3];
	element.data = fCursor + 8;
	element.length = length;
	fCursor += 8 + length;
	return true;
}


bool
DmapReader::Find(uint32_t code, DmapElement& element) const
{
	DmapReader reader(*this);
	reader.fCursor = fStart;
	while (reader.Next(element)) {
		if (element.code == code)
			return true;
	}
	return false;
}


uint64_t
dmap_int(const DmapElement& element)
{
	switch (element.length) {
		case 1:
		case 2:
		case 4:
		case 8:
		{
			uint64_t value = 0;
			for (uint32_t i = 0; i < element.length; i++)
				value = value << 8 | element.data[i];
			return value;
		}
		default:
			return 0;
	}
}


std::string
dmap_string(const DmapElement& element)
{
	return std::string((const char*)element.data, element.length);
}


uint64_t
dmap_child_int(const DmapElement& container, uint32_t code,
	uint64_t defaultValue)
{
	DmapElement child;
	if (!DmapReader(container).Find(code, child))
		return defaultValue;
	return dmap_int(child);
}


std::string
dmap_child_string(const DmapElement& container, uint32_t code)
{
	DmapElement child;
	if (!DmapReader(container).Find(code, child))
		return std::string();
	return dmap_string(child);
}


// #pragma mark - validation


namespace {


const uint32_t kMd5Constants[64] = {
	0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
	0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
	0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
	0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
	0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
	0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
	0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
	0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
	0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
	0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
	0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
	0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
	0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
	0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
	0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
	0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

const uint32_t kAppleConstant27 = 0x445a14ed;
	// iTunes' MD5 differs from the real one only here

const int kMd5Shifts[64] = {
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};


void
md5_block(uint32_t state[4], const uint8_t block[64], bool apple)
{
	uint32_t m[16];
	for (int i = 0; i < 16; i++) {
		m[i] = (uint32_t)block[i * 4] | (uint32_t)block[i * 4 + 1] << 8
			| (uint32_t)block[i * 4 + 2] << 16
			| (uint32_t)block[i * 4 + 3] << 24;
	}

	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	for (int i = 0; i < 64; i++) {
		uint32_t f;
		int g;
		if (i < 16) {
			f = (b & c) | (~b & d);
			g = i;
		} else if (i < 32) {
			f = (d & b) | (~d & c);
			g = (5 * i + 1) % 16;
		} else if (i < 48) {
			f = b ^ c ^ d;
			g = (3 * i + 5) % 16;
		} else {
			f = c ^ (b | ~d);
			g = (7 * i) % 16;
		}
		uint32_t constant = apple && i == 27
			? kAppleConstant27 : kMd5Constants[i];
		f += a + constant + m[g];
		a = d;
		d = c;
		c = b;
		b += f << kMd5Shifts[i] | f >> (32 - kMd5Shifts[i]);
	}
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}


std::string
hex_upper(const uint8_t* data, size_t size)
{
	static const char kDigits[] = "0123456789ABCDEF";
	std::string hex;
	for (size_t i = 0; i < size; i++) {
		hex += kDigits[data[i] >> 4];
		hex += kDigits[data[i] & 0xf];
	}
	return hex;
}


std::string
apple_md5_hex(const std::string& data)
{
	uint8_t digest[16];
	daap_md5((const uint8_t*)data.data(), data.size(), true, digest);
	return hex_upper(digest, sizeof(digest));
}


// One of the 256 strings mixed into a validation; the bits of the index
// pick one of two words for each of eight positions.
std::string
validation_table_entry(int index)
{
	std::string text;
	text += index & 0x40 ? "eqwsdxcqwesdc" : "op[;lm,piojkmn";
	text += index & 0x20 ? "876trfvb 34rtgbvc" : "=-0ol.,m3ewrdfv";
	text += index & 0x10 ? "87654323e4rgbv " : "1535753690868867974342659792";
	text += index & 0x08 ? "Song Name" : "DAAP-CLIENT-ID:";
	text += index & 0x04 ? "111222333444555" : "4089961010";
	text += index & 0x02 ? "playlist-item-spec" : "revision-number";
	text += index & 0x01 ? "session-id" : "content-codes";
	text += index & 0x80 ? "IUYHGFDCXWEDFGHN" : "iuytgfdxwerfghjm";
	return apple_md5_hex(text);
}


const int kValidationSelect = 2;
	// the entry iTunes 4.5 clients use (the "access index")


}	// namespace


void
daap_md5(const uint8_t* data, size_t size, bool apple, uint8_t digest[16])
{
	uint32_t state[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
	size_t done = 0;
	for (; size - done >= 64; done += 64)
		md5_block(state, data + done, apple);

	uint8_t last[128];
	size_t rest = size - done;
	memcpy(last, data + done, rest);
	last[rest] = 0x80;
	size_t padded = rest + 1 + 8 <= 64 ? 64 : 128;
	memset(last + rest + 1, 0, padded - rest - 1);
	uint64_t bits = (uint64_t)size * 8;
	for (int i = 0; i < 8; i++)
		last[padded - 8 + i] = (uint8_t)(bits >> (8 * i));
	md5_block(state, last, apple);
	if (padded == 128)
		md5_block(state, last + 64, apple);

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++)
			digest[i * 4 + j] = (uint8_t)(state[i] >> (8 * j));
	}
}


std::string
daap_validation(const std::string& pathAndQuery, int32_t requestId)
{
	static const std::string sEntry
		= validation_table_entry(kValidationSelect);

	std::string text(pathAndQuery);
	text += "Copyright 2003 Apple Computer, Inc.";
	text += sEntry;
	if (requestId > 0) {
		char number[16];
		snprintf(number, sizeof(number), "%d", (int)requestId);
		text += number;
	}
	return apple_md5_hex(text);
}


// #pragma mark - multicast DNS


namespace {


// Labels are joined with dots; dots and backslashes inside a label are
// escaped with a backslash, as DNS names are written.
std::vector<std::string>
split_name(const std::string& name)
{
	std::vector<std::string> labels;
	std::string label;
	for (size_t i = 0; i < name.size(); i++) {
		if (name[i] == '\\' && i + 1 < name.size())
			label += name[++i];
		else if (name[i] == '.') {
			if (!label.empty())
				labels.push_back(label);
			label.clear();
		} else
			label += name[i];
	}
	if (!label.empty())
		labels.push_back(label);
	return labels;
}


class DnsParser {
public:
	DnsParser(const uint8_t* packet, size_t size)
		:
		fPacket(packet),
		fSize(size)
	{
	}

	// The name at offset; offset moves past it (past the first pointer).
	bool ReadName(size_t& offset, std::string& name) const
	{
		name.clear();
		size_t position = offset;
		bool jumped = false;
		int jumps = 0;
		size_t length = 0;
		while (true) {
			if (position >= fSize)
				return false;
			uint8_t count = fPacket[position];
			if ((count & 0xc0) == 0xc0) {
				if (position + 1 >= fSize || ++jumps > 32)
					return false;
				if (!jumped)
					offset = position + 2;
				jumped = true;
				position = (size_t)(count & 0x3f) << 8 | fPacket[position + 1];
				continue;
			}
			if ((count & 0xc0) != 0)
				return false;
			position++;
			if (count == 0)
				break;
			if (position + count > fSize || (length += count + 1) > 255)
				return false;
			if (!name.empty())
				name += '.';
			for (size_t i = 0; i < count; i++) {
				char c = (char)fPacket[position + i];
				if (c == '.' || c == '\\')
					name += '\\';
				name += c;
			}
			position += count;
		}
		if (!jumped)
			offset = position;
		return true;
	}

	bool Read16(size_t& offset, uint16_t& value) const
	{
		if (offset + 2 > fSize)
			return false;
		value = (uint16_t)(fPacket[offset] << 8 | fPacket[offset + 1]);
		offset += 2;
		return true;
	}

	bool Read32(size_t& offset, uint32_t& value) const
	{
		if (offset + 4 > fSize)
			return false;
		value = (uint32_t)fPacket[offset] << 24
			| (uint32_t)fPacket[offset + 1] << 16
			| (uint32_t)fPacket[offset + 2] << 8 | fPacket[offset + 3];
		offset += 4;
		return true;
	}

	const uint8_t*	fPacket;
	size_t			fSize;
};


}	// namespace


std::string
dns_query(uint16_t id, const std::vector<DnsQuestion>& questions)
{
	std::string packet;
	const uint8_t header[12] = { (uint8_t)(id >> 8), (uint8_t)id, 0, 0,
		0, (uint8_t)questions.size(), 0, 0, 0, 0, 0, 0 };
	packet.append((const char*)header, sizeof(header));
	for (size_t i = 0; i < questions.size(); i++) {
		std::vector<std::string> labels = split_name(questions[i].name);
		for (size_t j = 0; j < labels.size(); j++) {
			std::string label = labels[j].substr(0, 63);
			packet += (char)label.size();
			packet += label;
		}
		packet += '\0';
		packet += (char)(questions[i].type >> 8);
		packet += (char)questions[i].type;
		packet += '\0';
		packet += '\1';
			// class IN, a multicast (not unicast) response is fine
	}
	return packet;
}


bool
dns_parse_reply(const uint8_t* packet, size_t size,
	std::vector<DnsRecord>& records)
{
	DnsParser parser(packet, size);
	size_t offset = 0;
	uint16_t id, flags, questions, answers, authorities, additionals;
	if (!parser.Read16(offset, id) || !parser.Read16(offset, flags)
		|| !parser.Read16(offset, questions) || !parser.Read16(offset, answers)
		|| !parser.Read16(offset, authorities)
		|| !parser.Read16(offset, additionals))
		return false;
	if ((flags & 0x8000) == 0)
		return false;
		// a query, not a response

	std::string name;
	for (int i = 0; i < questions; i++) {
		if (!parser.ReadName(offset, name) || offset + 4 > size)
			return false;
		offset += 4;
	}

	int count = answers + authorities + additionals;
	for (int i = 0; i < count; i++) {
		DnsRecord record;
		uint16_t type, recordClass, length;
		if (!parser.ReadName(offset, record.name)
			|| !parser.Read16(offset, type)
			|| !parser.Read16(offset, recordClass)
			|| !parser.Read32(offset, record.ttl)
			|| !parser.Read16(offset, length) || offset + length > size)
			return false;
		record.type = type;
		record.port = 0;
		record.address = 0;
		size_t data = offset;
		offset += length;

		bool valid = true;
		switch (type) {
			case kDnsTypePtr:
				valid = parser.ReadName(data, record.target);
				break;
			case kDnsTypeSrv:
			{
				uint16_t priority, weight;
				valid = length >= 7 && parser.Read16(data, priority)
					&& parser.Read16(data, weight)
					&& parser.Read16(data, record.port)
					&& parser.ReadName(data, record.target);
				break;
			}
			case kDnsTypeTxt:
				for (size_t position = data; position < offset;) {
					size_t textLength = packet[position++];
					if (position + textLength > offset) {
						valid = false;
						break;
					}
					if (textLength > 0) {
						record.text.push_back(std::string(
							(const char*)packet + position, textLength));
					}
					position += textLength;
				}
				break;
			case kDnsTypeA:
				valid = length == 4 && parser.Read32(data, record.address);
				break;
			default:
				continue;
		}
		if (valid)
			records.push_back(record);
	}
	return true;
}


bool
dns_same_name(const std::string& a, const std::string& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
			return false;
	}
	return true;
}


std::string
dns_first_label(const std::string& name)
{
	std::vector<std::string> labels = split_name(name);
	return labels.empty() ? std::string() : labels[0];
}


bool
dns_text_value(const std::vector<std::string>& text, const char* key,
	std::string& value)
{
	size_t keyLength = strlen(key);
	for (size_t i = 0; i < text.size(); i++) {
		const std::string& entry = text[i];
		if (entry.size() > keyLength && entry[keyLength] == '='
			&& dns_same_name(entry.substr(0, keyLength), key)) {
			value = entry.substr(keyLength + 1);
			return true;
		}
	}
	return false;
}


// #pragma mark - HTTP


std::string
base64_encode(const std::string& data)
{
	static const char kAlphabet[]
		= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string encoded;
	size_t i = 0;
	for (; i + 2 < data.size(); i += 3) {
		uint32_t bits = (uint8_t)data[i] << 16 | (uint8_t)data[i + 1] << 8
			| (uint8_t)data[i + 2];
		encoded += kAlphabet[bits >> 18];
		encoded += kAlphabet[(bits >> 12) & 0x3f];
		encoded += kAlphabet[(bits >> 6) & 0x3f];
		encoded += kAlphabet[bits & 0x3f];
	}
	if (i + 1 == data.size()) {
		uint32_t bits = (uint8_t)data[i] << 16;
		encoded += kAlphabet[bits >> 18];
		encoded += kAlphabet[(bits >> 12) & 0x3f];
		encoded += "==";
	} else if (i + 2 == data.size()) {
		uint32_t bits = (uint8_t)data[i] << 16 | (uint8_t)data[i + 1] << 8;
		encoded += kAlphabet[bits >> 18];
		encoded += kAlphabet[(bits >> 12) & 0x3f];
		encoded += kAlphabet[(bits >> 6) & 0x3f];
		encoded += '=';
	}
	return encoded;
}


static std::string
trimmed(const std::string& text)
{
	size_t start = text.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return std::string();
	size_t end = text.find_last_not_of(" \t\r\n");
	return text.substr(start, end - start + 1);
}


bool
http_parse_head(const std::string& head, int& status,
	std::map<std::string, std::string>& headers)
{
	size_t lineEnd = head.find('\n');
	std::string statusLine = trimmed(head.substr(0, lineEnd));
	if (statusLine.compare(0, 5, "HTTP/") != 0)
		return false;
	size_t space = statusLine.find(' ');
	if (space == std::string::npos)
		return false;
	char* end;
	long code = strtol(statusLine.c_str() + space + 1, &end, 10);
	if (end == statusLine.c_str() + space + 1 || code < 100 || code > 999)
		return false;
	status = (int)code;

	headers.clear();
	while (lineEnd != std::string::npos) {
		size_t start = lineEnd + 1;
		lineEnd = head.find('\n', start);
		std::string line = head.substr(start,
			lineEnd == std::string::npos ? std::string::npos : lineEnd - start);
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string name = trimmed(line.substr(0, colon));
		for (size_t i = 0; i < name.size(); i++)
			name[i] = (char)tolower((unsigned char)name[i]);
		headers[name] = trimmed(line.substr(colon + 1));
	}
	return true;
}


bool
parse_share_address(const std::string& address, std::string& host,
	uint16_t& port, std::string& path)
{
	std::string text = trimmed(address);
	const char* schemes[] = { "daap://", "http://" };
	for (size_t i = 0; i < sizeof(schemes) / sizeof(schemes[0]); i++) {
		size_t length = strlen(schemes[i]);
		if (text.size() >= length
			&& dns_same_name(text.substr(0, length), schemes[i])) {
			text = text.substr(length);
			break;
		}
	}

	size_t slash = text.find('/');
	path = slash == std::string::npos ? std::string("/") : text.substr(slash);
	text = text.substr(0, slash);

	std::string portText;
	if (!text.empty() && text[0] == '[') {
		size_t close = text.find(']');
		if (close == std::string::npos)
			return false;
		host = text.substr(1, close - 1);
		if (close + 1 < text.size()) {
			if (text[close + 1] != ':')
				return false;
			portText = text.substr(close + 2);
		}
	} else {
		size_t colon = text.find(':');
		host = text.substr(0, colon);
		if (colon != std::string::npos)
			portText = text.substr(colon + 1);
	}
	if (host.empty() || host.find_first_of(" \t/") != std::string::npos)
		return false;

	port = 3689;
	if (!portText.empty()) {
		char* end;
		long number = strtol(portText.c_str(), &end, 10);
		if (*end != '\0' || number < 1 || number > 65535)
			return false;
		port = (uint16_t)number;
	}
	return true;
}
