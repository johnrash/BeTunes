/*
 * Writing ID3 tags. Follows the ID3v2.3.0 and ID3v2.4.0 specifications
 * (id3.org), with the usual allowances for real files: ID3v2.4 frame sizes
 * that are not syncsafe (old iTunes), unsynchronised 2.3 tags, junk after
 * the last frame.
 */
#include "Id3Tag.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "TextUtils.h"


namespace {


const size_t kHeaderSize = 10;
const size_t kPadding = 2048;
	// room for later changes, so they can be written in place
const size_t kMaxTagSize = 64 * 1024 * 1024;
const size_t kCopyBufferSize = 256 * 1024;

const uint8_t kFlagUnsynchronisation = 0x80;
const uint8_t kFlagExtendedHeader = 0x40;
const uint8_t kFlagFooter = 0x10;


struct Frame {
	std::string				id;
	uint16_t				flags;
	std::vector<uint8_t>	data;
};


struct Tag {
	Tag()
		:
		version(3),
		size(0)
	{
	}

	int						version;
		// 3 or 4 (what is written)
	std::vector<Frame>		frames;
	size_t					size;
		// bytes the tag takes in the file (0: there is none)
};


// #pragma mark - numbers


uint32_t
read_syncsafe(const uint8_t* p)
{
	return (uint32_t)(p[0] & 0x7f) << 21 | (uint32_t)(p[1] & 0x7f) << 14
		| (uint32_t)(p[2] & 0x7f) << 7 | (uint32_t)(p[3] & 0x7f);
}


uint32_t
read_be32(const uint8_t* p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8
		| (uint32_t)p[3];
}


void
append_syncsafe(std::vector<uint8_t>& out, uint32_t value)
{
	out.push_back((value >> 21) & 0x7f);
	out.push_back((value >> 14) & 0x7f);
	out.push_back((value >> 7) & 0x7f);
	out.push_back(value & 0x7f);
}


void
append_be32(std::vector<uint8_t>& out, uint32_t value)
{
	out.push_back(value >> 24);
	out.push_back((value >> 16) & 0xff);
	out.push_back((value >> 8) & 0xff);
	out.push_back(value & 0xff);
}


// #pragma mark - text


// Decodes one UTF-8 character; invalid bytes become U+FFFD.
uint32_t
next_code_point(const std::string& text, size_t& i)
{
	uint8_t c = text[i++];
	if (c < 0x80)
		return c;
	int extra;
	uint32_t point;
	if ((c & 0xe0) == 0xc0) {
		extra = 1;
		point = c & 0x1f;
	} else if ((c & 0xf0) == 0xe0) {
		extra = 2;
		point = c & 0x0f;
	} else if ((c & 0xf8) == 0xf0) {
		extra = 3;
		point = c & 0x07;
	} else
		return 0xfffd;
	for (; extra > 0; extra--) {
		if (i >= text.size() || (text[i] & 0xc0) != 0x80)
			return 0xfffd;
		point = point << 6 | (text[i++] & 0x3f);
	}
	return point;
}


void
append_utf8(std::string& out, uint32_t point)
{
	if (point < 0x80) {
		out += (char)point;
	} else if (point < 0x800) {
		out += (char)(0xc0 | point >> 6);
		out += (char)(0x80 | (point & 0x3f));
	} else if (point < 0x10000) {
		out += (char)(0xe0 | point >> 12);
		out += (char)(0x80 | ((point >> 6) & 0x3f));
		out += (char)(0x80 | (point & 0x3f));
	} else {
		out += (char)(0xf0 | point >> 18);
		out += (char)(0x80 | ((point >> 12) & 0x3f));
		out += (char)(0x80 | ((point >> 6) & 0x3f));
		out += (char)(0x80 | (point & 0x3f));
	}
}


bool
fits_latin1(const std::string& text)
{
	for (size_t i = 0; i < text.size();) {
		if (next_code_point(text, i) > 0xff)
			return false;
	}
	return true;
}


// Characters outside ISO-8859-1 become '?'.
std::string
to_latin1(const std::string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size();) {
		uint32_t point = next_code_point(text, i);
		out += point <= 0xff ? (char)point : '?';
	}
	return out;
}


// The data of a text frame: ISO-8859-1 or UTF-16 for ID3v2.3, UTF-8 for
// ID3v2.4.
std::vector<uint8_t>
text_frame_data(const std::string& text, int version)
{
	std::vector<uint8_t> data;
	if (version >= 4) {
		data.push_back(3);
		data.insert(data.end(), text.begin(), text.end());
		return data;
	}
	if (fits_latin1(text)) {
		data.push_back(0);
		std::string latin1 = to_latin1(text);
		data.insert(data.end(), latin1.begin(), latin1.end());
		return data;
	}
	// UTF-16 with a byte order mark (little endian)
	data.push_back(1);
	data.push_back(0xff);
	data.push_back(0xfe);
	for (size_t i = 0; i < text.size();) {
		uint32_t point = next_code_point(text, i);
		if (point >= 0x10000) {
			point -= 0x10000;
			uint16_t high = 0xd800 | (point >> 10);
			uint16_t low = 0xdc00 | (point & 0x3ff);
			data.push_back(high & 0xff);
			data.push_back(high >> 8);
			data.push_back(low & 0xff);
			data.push_back(low >> 8);
		} else {
			data.push_back(point & 0xff);
			data.push_back(point >> 8);
		}
	}
	return data;
}


std::string
decode_utf16(const uint8_t* p, size_t size, bool bigEndian)
{
	std::string out;
	for (size_t i = 0; i + 1 < size; i += 2) {
		uint32_t unit = bigEndian ? (p[i] << 8 | p[i + 1])
			: (p[i] | p[i + 1] << 8);
		if (unit == 0)
			break;
		if (unit >= 0xd800 && unit < 0xdc00 && i + 3 < size) {
			uint32_t low = bigEndian ? (p[i + 2] << 8 | p[i + 3])
				: (p[i + 2] | p[i + 3] << 8);
			if (low >= 0xdc00 && low < 0xe000) {
				unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
				i += 2;
			}
		}
		append_utf8(out, unit);
	}
	return out;
}


// The (first) text of a text frame, in UTF-8.
std::string
decode_text_frame(const std::vector<uint8_t>& data)
{
	if (data.empty())
		return std::string();
	const uint8_t* p = data.data() + 1;
	size_t size = data.size() - 1;
	switch (data[0]) {
		case 0:
		{
			std::string out;
			for (size_t i = 0; i < size && p[i] != 0; i++)
				append_utf8(out, p[i]);
			return out;
		}
		case 1:
			if (size >= 2 && p[0] == 0xfe && p[1] == 0xff)
				return decode_utf16(p + 2, size - 2, true);
			if (size >= 2 && p[0] == 0xff && p[1] == 0xfe)
				return decode_utf16(p + 2, size - 2, false);
			return decode_utf16(p, size, false);
		case 2:
			return decode_utf16(p, size, true);
		case 3:
		{
			size_t length = 0;
			while (length < size && p[length] != 0)
				length++;
			return std::string((const char*)p, length);
		}
	}
	return std::string();
}


// #pragma mark - reading tags


bool
valid_frame_id(const uint8_t* id, int length)
{
	for (int i = 0; i < length; i++) {
		if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9')))
			return false;
	}
	return true;
}


// Is there a frame (or padding, or the end) at "offset"?
bool
frame_follows(const uint8_t* body, size_t size, size_t offset, int idLength)
{
	if (offset == size)
		return true;
	if (offset > size)
		return false;
	if (body[offset] == 0)
		return true;
	return offset + idLength <= size && valid_frame_id(body + offset, idLength);
}


const char*
v22_frame_id(const std::string& id)
{
	static const char* const kIds[][2] = {
		{ "TT1", "TIT1" }, { "TT2", "TIT2" }, { "TT3", "TIT3" },
		{ "TP1", "TPE1" }, { "TP2", "TPE2" }, { "TP3", "TPE3" },
		{ "TP4", "TPE4" }, { "TAL", "TALB" }, { "TCO", "TCON" },
		{ "TYE", "TYER" }, { "TRK", "TRCK" }, { "TPA", "TPOS" },
		{ "TCM", "TCOM" }, { "TEN", "TENC" }, { "TSS", "TSSE" },
		{ "TCR", "TCOP" }, { "TBP", "TBPM" }, { "TXX", "TXXX" },
		{ "COM", "COMM" }, { "ULT", "USLT" }, { "PIC", "APIC" }
	};
	for (size_t i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
		if (id == kIds[i][0])
			return kIds[i][1];
	}
	return NULL;
}


// ID3v2.2 "PIC" (3 letter image format) to ID3v2.3 "APIC" (MIME type).
std::vector<uint8_t>
convert_picture(const std::vector<uint8_t>& data)
{
	if (data.size() < 5)
		return data;
	std::string format((const char*)data.data() + 1, 3);
	std::string mime;
	if (strcasecmp(format.c_str(), "JPG") == 0)
		mime = "image/jpeg";
	else {
		mime = "image/";
		for (size_t i = 0; i < format.size(); i++)
			mime += (char)tolower(format[i]);
	}
	std::vector<uint8_t> out;
	out.push_back(data[0]);
	out.insert(out.end(), mime.begin(), mime.end());
	out.push_back(0);
	out.insert(out.end(), data.begin() + 4, data.end());
	return out;
}


bool
parse_header(const uint8_t* header, int& version, uint8_t& flags,
	uint32_t& size)
{
	if (memcmp(header, "ID3", 3) != 0)
		return false;
	version = header[3];
	if (version < 2 || version > 4 || header[4] == 0xff)
		return false;
	for (int i = 6; i < 10; i++) {
		if (header[i] & 0x80)
			return false;
	}
	flags = header[5];
	size = read_syncsafe(header + 6);
	return true;
}


bool
parse_frames(const uint8_t* body, size_t size, int version, uint8_t flags,
	std::vector<Frame>& frames, std::string& error)
{
	std::vector<uint8_t> plain;
	if ((flags & kFlagUnsynchronisation) != 0 && version < 4) {
		// the whole tag is unsynchronised: 0xff 0x00 stands for 0xff
		for (size_t i = 0; i < size; i++) {
			plain.push_back(body[i]);
			if (body[i] == 0xff && i + 1 < size && body[i + 1] == 0)
				i++;
		}
		body = plain.data();
		size = plain.size();
	}

	size_t position = 0;
	if ((flags & kFlagExtendedHeader) != 0) {
		if (version == 2) {
			error = "The ID3v2.2 tag is compressed";
			return false;
		}
		if (size < 4) {
			error = "The ID3 tag is damaged";
			return false;
		}
		position = version == 3 ? 4 + read_be32(body) : read_syncsafe(body);
	}

	int idLength = version == 2 ? 3 : 4;
	size_t headerSize = version == 2 ? 6 : 10;
	while (position + headerSize <= size) {
		const uint8_t* header = body + position;
		if (header[0] == 0 || !valid_frame_id(header, idLength))
			break;
			// padding (or junk some writers leave)

		size_t frameSize;
		if (version == 2)
			frameSize = header[3] << 16 | header[4] << 8 | header[5];
		else if (version == 3)
			frameSize = read_be32(header + 4);
		else {
			// ID3v2.4 sizes are syncsafe, but some writers ignored that
			frameSize = read_syncsafe(header + 4);
			size_t plainSize = read_be32(header + 4);
			bool syncsafe = (header[4] | header[5] | header[6] | header[7])
				< 0x80;
			if (!syncsafe)
				frameSize = plainSize;
			else if (plainSize != frameSize
				&& !frame_follows(body, size, position + headerSize + frameSize,
					idLength)
				&& frame_follows(body, size, position + headerSize + plainSize,
					idLength))
				frameSize = plainSize;
		}
		if (position + headerSize + frameSize > size) {
			error = "The ID3 tag is damaged";
			return false;
		}

		Frame frame;
		frame.id.assign((const char*)header, idLength);
		frame.flags = version == 2 ? 0 : (header[8] << 8 | header[9]);
		frame.data.assign(header + headerSize,
			header + headerSize + frameSize);
		position += headerSize + frameSize;

		if (version == 2) {
			const char* id = v22_frame_id(frame.id);
			if (id == NULL)
				continue;
				// nothing we know how to keep
			frame.id = id;
			if (frame.id == "APIC")
				frame.data = convert_picture(frame.data);
		}
		frames.push_back(frame);
	}
	return true;
}


// #pragma mark - changing tags


const Frame*
find_frame(const Tag& tag, const char* id)
{
	for (size_t i = 0; i < tag.frames.size(); i++) {
		if (tag.frames[i].id == id)
			return &tag.frames[i];
	}
	return NULL;
}


// Replaces the first frame with this ID (removing any others), adds one if
// there is none; an empty text removes them all.
void
set_text_frame(Tag& tag, const char* id, const std::string& text)
{
	std::vector<Frame> frames;
	bool done = false;
	for (size_t i = 0; i < tag.frames.size(); i++) {
		if (tag.frames[i].id != id) {
			frames.push_back(tag.frames[i]);
			continue;
		}
		if (done || text.empty())
			continue;
		Frame frame;
		frame.id = id;
		frame.flags = 0;
		frame.data = text_frame_data(text, tag.version);
		frames.push_back(frame);
		done = true;
	}
	if (!done && !text.empty()) {
		Frame frame;
		frame.id = id;
		frame.flags = 0;
		frame.data = text_frame_data(text, tag.version);
		frames.push_back(frame);
	}
	tag.frames.swap(frames);
}


// "5" keeps the total of an existing "3/12": "5/12".
std::string
with_total(const Tag& tag, const char* id, const std::string& number)
{
	if (number.empty())
		return number;
	const Frame* frame = find_frame(tag, id);
	if (frame == NULL)
		return number;
	std::string old = decode_text_frame(frame->data);
	size_t slash = old.find('/');
	if (slash == std::string::npos || slash + 1 >= old.size())
		return number;
	return number + old.substr(slash);
}


void
apply_changes(Tag& tag, const TagChanges& changes)
{
	static const char* const kFrames[TAG_FIELD_COUNT] = {
		"TIT2", "TPE1", "TALB", "TCON", NULL, "TRCK", "TPOS"
	};
	for (int field = 0; field < TAG_FIELD_COUNT; field++) {
		if (!changes.set[field])
			continue;
		const std::string& value = changes.value[field];
		if (field == TAG_YEAR) {
			// ID3v2.3 has TYER, ID3v2.4 TDRC: keep only the right one
			if (tag.version >= 4) {
				set_text_frame(tag, "TDRC", value);
				set_text_frame(tag, "TYER", "");
			} else {
				set_text_frame(tag, "TYER", value);
				set_text_frame(tag, "TDRC", "");
			}
		} else if (field == TAG_TRACK || field == TAG_DISC) {
			set_text_frame(tag, kFrames[field],
				with_total(tag, kFrames[field], value));
		} else
			set_text_frame(tag, kFrames[field], value);
	}
}


std::vector<uint8_t>
build_frames(const Tag& tag)
{
	std::vector<uint8_t> out;
	for (size_t i = 0; i < tag.frames.size(); i++) {
		const Frame& frame = tag.frames[i];
		out.insert(out.end(), frame.id.begin(), frame.id.end());
		if (tag.version >= 4)
			append_syncsafe(out, frame.data.size());
		else
			append_be32(out, frame.data.size());
		out.push_back(frame.flags >> 8);
		out.push_back(frame.flags & 0xff);
		out.insert(out.end(), frame.data.begin(), frame.data.end());
	}
	return out;
}


// A complete tag of "totalSize" bytes (the rest is padding).
std::vector<uint8_t>
build_tag(int version, const std::vector<uint8_t>& frames, size_t totalSize)
{
	std::vector<uint8_t> out;
	out.push_back('I');
	out.push_back('D');
	out.push_back('3');
	out.push_back(version);
	out.push_back(0);
	out.push_back(0);
		// no unsynchronisation, extended header or footer
	append_syncsafe(out, totalSize - kHeaderSize);
	out.insert(out.end(), frames.begin(), frames.end());
	out.resize(totalSize, 0);
	return out;
}


void
set_v1_text(uint8_t* field, size_t length, const std::string& text)
{
	std::string latin1 = to_latin1(text);
	memset(field, 0, length);
	memcpy(field, latin1.data(), std::min(length, latin1.size()));
}


void
update_id3v1(uint8_t* tag, const TagChanges& changes)
{
	if (changes.set[TAG_TITLE])
		set_v1_text(tag + 3, 30, changes.value[TAG_TITLE]);
	if (changes.set[TAG_ARTIST])
		set_v1_text(tag + 33, 30, changes.value[TAG_ARTIST]);
	if (changes.set[TAG_ALBUM])
		set_v1_text(tag + 63, 30, changes.value[TAG_ALBUM]);
	if (changes.set[TAG_YEAR])
		set_v1_text(tag + 93, 4, changes.value[TAG_YEAR]);
	if (changes.set[TAG_TRACK]) {
		int track = parse_leading_int(changes.value[TAG_TRACK].c_str());
		if (track > 0 && track <= 255) {
			// ID3v1.1: a zero byte, then the track, ends the comment
			tag[125] = 0;
			tag[126] = track;
		} else if (tag[125] == 0)
			tag[126] = 0;
	}
	if (changes.set[TAG_GENRE]) {
		int genre = 255;
		for (int i = 0; id3v1_genre(i) != NULL; i++) {
			if (strcasecmp(id3v1_genre(i), changes.value[TAG_GENRE].c_str())
					== 0) {
				genre = i;
				break;
			}
		}
		tag[127] = genre;
	}
}


// #pragma mark - files


bool
read_all(int fd, void* buffer, size_t size, off_t offset)
{
	uint8_t* p = (uint8_t*)buffer;
	while (size > 0) {
		ssize_t bytes = pread(fd, p, size, offset);
		if (bytes <= 0)
			return false;
		p += bytes;
		size -= bytes;
		offset += bytes;
	}
	return true;
}


bool
write_all(int fd, const void* buffer, size_t size)
{
	const uint8_t* p = (const uint8_t*)buffer;
	while (size > 0) {
		ssize_t bytes = write(fd, p, size);
		if (bytes <= 0) {
			if (bytes < 0 && errno == EINTR)
				continue;
			return false;
		}
		p += bytes;
		size -= bytes;
	}
	return true;
}


bool
write_all_at(int fd, const void* buffer, size_t size, off_t offset)
{
	const uint8_t* p = (const uint8_t*)buffer;
	while (size > 0) {
		ssize_t bytes = pwrite(fd, p, size, offset);
		if (bytes <= 0) {
			if (bytes < 0 && errno == EINTR)
				continue;
			return false;
		}
		p += bytes;
		size -= bytes;
		offset += bytes;
	}
	return true;
}


int
fail(std::string& error, const std::string& message, int code)
{
	error = message;
	return code != 0 ? code : EIO;
}


int
fail_errno(std::string& error, const char* what)
{
	int code = errno != 0 ? errno : EIO;
	error = std::string(what) + ": " + strerror(code);
	return code;
}


// Reads the ID3v2 tag at the start of a file (if any) into "tag".
int
read_tag(int fd, off_t fileSize, Tag& tag, std::string& error)
{
	uint8_t header[kHeaderSize];
	int version;
	uint8_t flags;
	uint32_t size;
	if (fileSize < (off_t)kHeaderSize || !read_all(fd, header, kHeaderSize, 0)
		|| !parse_header(header, version, flags, size))
		return 0;

	size_t total = kHeaderSize + size
		+ (version == 4 && (flags & kFlagFooter) != 0 ? kHeaderSize : 0);
	if ((off_t)total > fileSize || total > kMaxTagSize)
		return fail(error, "The ID3 tag is damaged", EINVAL);

	std::vector<uint8_t> body(size);
	if (size > 0 && !read_all(fd, body.data(), size, kHeaderSize))
		return fail_errno(error, "Cannot read the file");
	if (!parse_frames(body.data(), size, version, flags, tag.frames, error))
		return EINVAL;
	tag.version = version == 2 ? 3 : version;
	tag.size = total;
	return 0;
}


}	// namespace


// #pragma mark - TagChanges


const char* const kTagFieldNames[TAG_FIELD_COUNT] = {
	"title", "artist", "album", "genre", "year", "track", "disc"
};



TagChanges::TagChanges()
{
	for (int i = 0; i < TAG_FIELD_COUNT; i++)
		set[i] = false;
}


void
TagChanges::Set(tag_field field, const std::string& text)
{
	set[field] = true;
	value[field] = text;
}


bool
TagChanges::IsEmpty() const
{
	for (int i = 0; i < TAG_FIELD_COUNT; i++) {
		if (set[i])
			return false;
	}
	return true;
}


// #pragma mark - public


int
write_mp3_tags(const char* path, const TagChanges& changes,
	tag_copy_hook hook, void* cookie, std::string& error)
{
	int fd = open(path, O_RDWR);
	if (fd < 0)
		return fail_errno(error, "Cannot open the file");

	struct stat info;
	if (fstat(fd, &info) != 0) {
		int result = fail_errno(error, "Cannot read the file");
		close(fd);
		return result;
	}
	off_t fileSize = info.st_size;

	Tag tag;
	int result = read_tag(fd, fileSize, tag, error);
	if (result != 0) {
		close(fd);
		return result;
	}

	uint8_t v1[128];
	bool hasV1 = fileSize - (off_t)tag.size >= 128
		&& read_all(fd, v1, sizeof(v1), fileSize - 128)
		&& memcmp(v1, "TAG", 3) == 0;

	apply_changes(tag, changes);
	if (hasV1)
		update_id3v1(v1, changes);

	std::vector<uint8_t> frames = build_frames(tag);
	if (frames.size() + kHeaderSize + kPadding > kMaxTagSize) {
		close(fd);
		return fail(error, "The ID3 tag is too large", EFBIG);
	}

	if (tag.size == 0 && frames.empty()) {
		// nothing for an ID3v2 tag (fields removed from a file without
		// one): only the ID3v1 tag, if any, changes
		if (hasV1 && (!write_all_at(fd, v1, sizeof(v1), fileSize - 128)
				|| fsync(fd) != 0)) {
			result = fail_errno(error, "Cannot write the file");
			close(fd);
			return result;
		}
		close(fd);
		return 0;
	}

	if (tag.size >= frames.size() + kHeaderSize) {
		// it fits: write the tag (and the ID3v1 tag) in place
		std::vector<uint8_t> data = build_tag(tag.version, frames, tag.size);
		if (!write_all_at(fd, data.data(), data.size(), 0)
			|| (hasV1 && !write_all_at(fd, v1, sizeof(v1), fileSize - 128))
			|| fsync(fd) != 0) {
			result = fail_errno(error, "Cannot write the file");
			close(fd);
			return result;
		}
		close(fd);
		return 0;
	}

	// write a copy with the new tag, then let it replace the original
	std::string pathString(path);
	size_t slash = pathString.rfind('/');
	std::string copy = slash == std::string::npos
		? "." + pathString + ".betunes-tags"
		: pathString.substr(0, slash + 1) + "."
			+ pathString.substr(slash + 1) + ".betunes-tags";
	unlink(copy.c_str());
		// left over from a crash
	int out = open(copy.c_str(), O_WRONLY | O_CREAT | O_EXCL,
		info.st_mode & 07777);
	if (out < 0) {
		result = fail_errno(error, "Cannot write in the file's folder");
		close(fd);
		return result;
	}

	bool ok = true;
	std::vector<uint8_t> data = build_tag(tag.version, frames,
		frames.size() + kHeaderSize + kPadding);
	ok = write_all(out, data.data(), data.size());

	off_t audioEnd = fileSize - (hasV1 ? 128 : 0);
	std::vector<uint8_t> buffer(kCopyBufferSize);
	for (off_t offset = tag.size; ok && offset < audioEnd;) {
		size_t chunk = (size_t)std::min((off_t)buffer.size(), audioEnd - offset);
		ok = read_all(fd, buffer.data(), chunk, offset)
			&& write_all(out, buffer.data(), chunk);
		offset += chunk;
	}
	if (ok && hasV1)
		ok = write_all(out, v1, sizeof(v1));
	if (ok)
		ok = fsync(out) == 0;
	if (!ok)
		result = fail_errno(error, "Cannot write the file");
	close(out);
	close(fd);

	if (ok && hook != NULL) {
		result = hook(path, copy.c_str(), cookie);
		if (result != 0) {
			error = std::string("Cannot copy the file's attributes: ")
				+ strerror(result);
			ok = false;
		}
	}
	if (ok && rename(copy.c_str(), path) != 0) {
		result = fail_errno(error, "Cannot replace the file");
		ok = false;
	}
	if (!ok) {
		unlink(copy.c_str());
		return result != 0 ? result : EIO;
	}
	return 0;
}


bool
parse_id3v2_text(const uint8_t* data, size_t size,
	std::map<std::string, std::string>& textFrames, int* _version)
{
	int version;
	uint8_t flags;
	uint32_t tagSize;
	if (size < kHeaderSize || !parse_header(data, version, flags, tagSize)
		|| kHeaderSize + tagSize > size)
		return false;

	std::vector<Frame> frames;
	std::string error;
	if (!parse_frames(data + kHeaderSize, tagSize, version, flags, frames,
			error))
		return false;
	for (size_t i = 0; i < frames.size(); i++) {
		if (frames[i].id[0] == 'T' && frames[i].id != "TXXX"
			&& textFrames.find(frames[i].id) == textFrames.end())
			textFrames[frames[i].id] = decode_text_frame(frames[i].data);
	}
	if (_version != NULL)
		*_version = version;
	return true;
}


bool
read_id3v2_text(const char* path, std::map<std::string, std::string>& frames,
	int* version)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return false;
	uint8_t header[kHeaderSize];
	int tagVersion;
	uint8_t flags;
	uint32_t size;
	bool ok = read_all(fd, header, kHeaderSize, 0)
		&& parse_header(header, tagVersion, flags, size);
	std::vector<uint8_t> data;
	if (ok) {
		data.resize(kHeaderSize + size);
		ok = read_all(fd, data.data(), data.size(), 0);
	}
	close(fd);
	return ok && parse_id3v2_text(data.data(), data.size(), frames, version);
}
