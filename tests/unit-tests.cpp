/*
 * Unit tests for the parts of BeTunes that do not need the Haiku API
 * (text helpers, ID3v1 parsing, the play queue, the equalizer, the DAAP
 * protocol). Run them
 * anywhere:
 *   bash tests/run-unit-tests.sh
 */
#include "DaapProtocol.h"
#include "Equalizer.h"
#include "Id3Tag.h"
#include "PlayQueue.h"
#include "TextUtils.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>


static int sFailures = 0;
static int sChecks = 0;


#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

#define CHECK_EQ(expected, actual) \
	do { \
		sChecks++; \
		if (!((expected) == (actual))) { \
			sFailures++; \
			printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #expected, \
				#actual); \
		} \
	} while (0)


static void
test_folding()
{
	CHECK_EQ(std::string("jokulsarlon"), fold_for_search("Jökulsárlón"));
	CHECK_EQ(std::string("thingvellir"), fold_for_search("Þingvellir"));
	CHECK_EQ(std::string("sigrun olafsdottir"),
		fold_for_search("Sigrún Ólafsdóttir"));
	CHECK_EQ(std::string("strasse"), fold_for_search("Straße"));
	CHECK_EQ(std::string("lodz zolc"), fold_for_search("Łódź ŻÓŁĆ"));
	CHECK_EQ(std::string("oeuvre"), fold_for_search("Œuvre"));
	CHECK_EQ(std::string("the bit shifters"),
		fold_for_search("The Bit Shifters"));
	CHECK_EQ(std::string("don't"), fold_for_search("Don\xe2\x80\x99t"));
	// Greek and Cyrillic capitals
	CHECK_EQ(fold_for_search("αβγ"), fold_for_search("ΑΒΓ"));
	CHECK_EQ(fold_for_search("кино"), fold_for_search("КИНО"));
	// CJK passes through
	CHECK_EQ(std::string("東京"), fold_for_search("東京"));
	// broken UTF-8 does not crash
	CHECK(fold_for_search("\xc3").size() > 0);
	CHECK(fold_for_search(NULL).empty());
}


static void
test_search_words()
{
	std::vector<std::string> words = search_words("  Aurora   NORTH ");
	CHECK_EQ((size_t)2, words.size());
	CHECK_EQ(std::string("aurora"), words[0]);
	CHECK_EQ(std::string("north"), words[1]);

	std::string haystack = fold_for_search(
		"Magnetic North\nAurora Lane\nNorthern Lights\nAmbient");
	CHECK(contains_all_words(haystack, words));
	CHECK(contains_all_words(haystack, search_words("")));
	CHECK(!contains_all_words(haystack, search_words("aurora south")));
	CHECK(contains_all_words(fold_for_search("Jökulsárlón"),
		search_words("JOKUL")));
}


static void
test_numbers()
{
	CHECK_EQ(3, parse_leading_int("3/12"));
	CHECK_EQ(7, parse_leading_int(" 7"));
	CHECK_EQ(0, parse_leading_int(""));
	CHECK_EQ(0, parse_leading_int(NULL));
	CHECK_EQ(0, parse_leading_int("x"));

	CHECK_EQ(2004, parse_year("2004"));
	CHECK_EQ(2004, parse_year("2004-05-01"));
	CHECK_EQ(1999, parse_year("May 1999"));
	CHECK_EQ(0, parse_year("12345"));
	CHECK_EQ(0, parse_year(""));
	CHECK_EQ(0, parse_year(NULL));
}


static void
test_genres()
{
	CHECK_EQ(std::string("Rock"), genre_from_tag("(17)"));
	CHECK_EQ(std::string("Rock"), genre_from_tag("17"));
	CHECK_EQ(std::string("Classic"), genre_from_tag("(1)Classic"));
	CHECK_EQ(std::string("Ambient"), genre_from_tag("Ambient"));
	CHECK_EQ(std::string("(999)"), genre_from_tag("(999)"));
	CHECK_EQ(std::string("Synthpop"), std::string(id3v1_genre(147)));
	CHECK(id3v1_genre(148) == NULL);
	CHECK(id3v1_genre(-1) == NULL);
}


static void
test_id3v1()
{
	uint8_t tag[128];
	memset(tag, 0, sizeof(tag));
	memcpy(tag, "TAG", 3);
	memcpy(tag + 3, "Old Tagger", 10);
	memcpy(tag + 33, "Retro Tagger                  ", 30);
	memcpy(tag + 63, "Caf\xe9", 4);	// Latin-1
	memcpy(tag + 93, "1998", 4);
	tag[126] = 7;
	tag[127] = 17;

	Id3v1Tag parsed;
	CHECK(parse_id3v1(tag, sizeof(tag), parsed));
	CHECK_EQ(std::string("Old Tagger"), parsed.title);
	CHECK_EQ(std::string("Retro Tagger"), parsed.artist);
	CHECK_EQ(std::string("Caf\xc3\xa9"), parsed.album);
	CHECK_EQ(1998, parsed.year);
	CHECK_EQ(7, parsed.track);
	CHECK_EQ(std::string("Rock"), parsed.genre);

	// ID3v1.0: the comment uses all 30 bytes, no track number
	tag[125] = 'x';
	CHECK(parse_id3v1(tag, sizeof(tag), parsed));
	CHECK_EQ(0, parsed.track);

	// unknown genre
	tag[127] = 255;
	CHECK(parse_id3v1(tag, sizeof(tag), parsed));
	CHECK(parsed.genre.empty());

	tag[0] = 'X';
	CHECK(!parse_id3v1(tag, sizeof(tag), parsed));
	CHECK(!parse_id3v1(tag, 10, parsed));
}


static void
test_utf8()
{
	CHECK(is_valid_utf8("plain"));
	CHECK(is_valid_utf8("Caf\xc3\xa9"));
	CHECK(is_valid_utf8("\xe6\x9d\xb1\xe4\xba\xac"));
	CHECK(!is_valid_utf8("Caf\xe9 Classics"));
	CHECK(!is_valid_utf8("\xc3"));
	CHECK(!is_valid_utf8("\xc0\xaf"));
	CHECK_EQ(std::string("Caf\xc3\xa9 Classics"),
		ensure_utf8("Caf\xe9 Classics"));
	CHECK_EQ(std::string("Caf\xc3\xa9"), ensure_utf8("Caf\xc3\xa9"));
	CHECK(ensure_utf8(NULL).empty());
}


static void
test_format()
{
	CHECK_EQ(std::string("0:00"), format_duration(0));
	CHECK_EQ(std::string("0:06"), format_duration(6030000));
	CHECK_EQ(std::string("3:07"), format_duration(187000000));
	CHECK_EQ(std::string("1:02:03"), format_duration(3723000000LL));
	CHECK_EQ(std::string("0:00"), format_duration(-5));

	CHECK_EQ(std::string("Beatles"), std::string(skip_article("The Beatles")));
	CHECK_EQ(std::string("Theremin"), std::string(skip_article("Theremin")));
	CHECK_EQ(std::string("The "), std::string(skip_article("The ")));
}


static std::vector<int32_t>
range(int32_t count)
{
	std::vector<int32_t> songs;
	for (int32_t i = 0; i < count; i++)
		songs.push_back(100 + i);
	return songs;
}


static void
test_queue_in_order()
{
	PlayQueue queue;
	CHECK(queue.IsEmpty());
	CHECK_EQ(-1, queue.Current());
	CHECK_EQ(-1, queue.Next(true));

	queue.SetSongs(range(4), 1, false);
	CHECK_EQ(101, queue.Current());
	CHECK_EQ(102, queue.Next(false));
	CHECK_EQ(103, queue.Next(false));
	// end of the list without repeat: stop, back at the first song
	CHECK_EQ(-1, queue.Next(false));
	CHECK_EQ(100, queue.Current());

	// repeat all wraps
	queue.SetSongs(range(3), 2, false);
	CHECK_EQ(100, queue.Next(true));
	CHECK_EQ(102, queue.Previous(true));
	CHECK_EQ(101, queue.Previous(true));
	CHECK_EQ(100, queue.Previous(false));
	CHECK_EQ(100, queue.Previous(false));

	// start without a selection
	queue.SetSongs(range(3), -1, false);
	CHECK_EQ(100, queue.Current());

	// out of range start index
	queue.SetSongs(range(3), 7, false);
	CHECK_EQ(100, queue.Current());

	queue.SetSongs(std::vector<int32_t>(), 0, false);
	CHECK(queue.IsEmpty());
	CHECK_EQ(-1, queue.Current());
}


static void
test_queue_shuffle()
{
	PlayQueue queue;
	queue.SetSeed(42);
	std::vector<int32_t> songs = range(20);
	queue.SetSongs(songs, 5, true);
	CHECK_EQ(105, queue.Current());

	// every song exactly once per round
	std::set<int32_t> seen;
	seen.insert(queue.Current());
	for (int i = 1; i < 20; i++)
		seen.insert(queue.Next(false));
	CHECK_EQ((size_t)20, seen.size());
	CHECK(std::vector<int32_t>(queue.Order()) != songs);

	// the last song of a round is not the first of the next one
	for (int round = 0; round < 50; round++) {
		int32_t last = queue.Current();
		int32_t first = queue.Next(true);
		CHECK(first != last);
		for (int i = 1; i < 20; i++)
			queue.Next(true);
	}

	// random first song when nothing is selected
	std::set<int32_t> firsts;
	for (int i = 0; i < 30; i++) {
		queue.SetSongs(songs, -1, true);
		firsts.insert(queue.Current());
	}
	CHECK(firsts.size() > 3);

	// a single song repeats
	queue.SetSongs(range(1), -1, true);
	CHECK_EQ(100, queue.Current());
	CHECK_EQ(100, queue.Next(true));
	CHECK_EQ(-1, queue.Next(false));
}


static void
test_queue_toggle_shuffle()
{
	PlayQueue queue;
	queue.SetSeed(7);
	queue.SetSongs(range(10), 3, false);
	queue.SetShuffle(true);
	CHECK_EQ(103, queue.Current());
	CHECK_EQ(0, queue.Position());
	std::vector<int32_t> order = queue.Order();
	std::sort(order.begin(), order.end());
	CHECK(order == range(10));

	queue.Next(false);
	int32_t current = queue.Current();
	queue.SetShuffle(false);
	CHECK_EQ(current, queue.Current());
	CHECK_EQ(current - 100, queue.Position());
	CHECK(queue.Order() == range(10));
	if (current < 109)
		CHECK_EQ(current + 1, queue.Next(false));
}


static void
test_queue_remove()
{
	PlayQueue queue;
	queue.SetSongs(range(5), 2, false);
	// remove one before the current song
	queue.Remove(100);
	CHECK_EQ(102, queue.Current());
	// remove the current song: the next one follows
	queue.Remove(102);
	CHECK_EQ(101, queue.Current());
	CHECK_EQ(103, queue.Next(false));
	// remove one after
	queue.Remove(104);
	CHECK_EQ(-1, queue.Next(false));
	CHECK_EQ((int32_t)2, queue.CountSongs());

	// removing the first and current song
	queue.SetSongs(range(3), 0, false);
	queue.Remove(100);
	CHECK_EQ(-1, queue.Current());
	CHECK_EQ(101, queue.Next(false));

	// duplicates (a playlist can contain a song twice)
	std::vector<int32_t> songs;
	songs.push_back(1);
	songs.push_back(2);
	songs.push_back(1);
	queue.SetSongs(songs, 1, false);
	queue.Remove(1);
	CHECK_EQ(2, queue.Current());
	CHECK_EQ((int32_t)1, queue.CountSongs());

	queue.Remove(2);
	CHECK(queue.IsEmpty());
	CHECK_EQ(-1, queue.Current());
	CHECK_EQ(-1, queue.Next(true));
}


// Gain in dB that the equalizer applies to a sine (measured after the
// filters settled), on the given channel of "channels".
static double
sine_gain(const EqualizerSettings& settings, float frequency,
	float frameRate = 44100, int channels = 1, int channel = 0)
{
	Equalizer equalizer;
	equalizer.SetFormat(frameRate, channels);
	equalizer.SetSettings(settings);

	int frames = (int)frameRate * 2;
	std::vector<float> samples((size_t)frames * channels, 0.0f);
	for (int i = 0; i < frames; i++) {
		samples[(size_t)i * channels + channel]
			= 0.25f * sinf(2 * M_PI * frequency * i / frameRate);
	}
	std::vector<float> input = samples;
	equalizer.Process(samples.data(), frames);

	double in = 0;
	double out = 0;
	for (int i = frames / 2; i < frames; i++) {
		size_t index = (size_t)i * channels + channel;
		in += input[index] * input[index];
		out += samples[index] * samples[index];
	}
	return 10 * log10(out / in);
}


static bool
near(double value, double expected, double tolerance)
{
	return fabs(value - expected) <= tolerance;
}


static void
test_equalizer()
{
	EqualizerSettings settings;
	CHECK(!settings.IsActive());
	settings.enabled = true;
	CHECK(!settings.IsActive());
		// all flat: nothing to do

	// flat leaves the samples alone, bit for bit
	float samples[4] = { 0.5f, -0.25f, 0.125f, 1.0f };
	Equalizer equalizer;
	equalizer.SetFormat(44100, 2);
	equalizer.SetSettings(settings);
	equalizer.Process(samples, 2);
	CHECK(samples[0] == 0.5f && samples[1] == -0.25f && samples[2] == 0.125f
		&& samples[3] == 1.0f);

	// off: no change even with gains set
	settings.enabled = false;
	settings.bands[5] = 12;
	CHECK(!settings.IsActive());
	CHECK(near(sine_gain(settings, 1000), 0, 0.001));

	// one band
	settings.enabled = true;
	CHECK(settings.IsActive());
	CHECK(near(sine_gain(settings, 1000), 12, 0.3));
	CHECK(fabs(sine_gain(settings, 125)) < 0.5);
		// three octaves away
	CHECK(fabs(sine_gain(settings, 8000)) < 0.5);
	settings.bands[5] = -12;
	CHECK(near(sine_gain(settings, 1000), -12, 0.3));
	settings.bands[5] = 0;

	// the lowest and highest bands, at other rates
	settings.bands[0] = -12;
	CHECK(near(sine_gain(settings, 32, 48000), -12, 0.5));
	settings.bands[0] = 0;
	settings.bands[9] = 6;
	CHECK(near(sine_gain(settings, 16000, 48000), 6, 0.5));
	// 16 kHz is above what 22050 Hz can hold: ignored, no NaNs
	CHECK(near(sine_gain(settings, 1000, 22050), 0, 0.1));
	settings.bands[9] = 0;

	// the preamp
	settings.preamp = -6;
	CHECK(near(sine_gain(settings, 440), -6, 0.05));
	settings.preamp = 0;

	// gains are limited to +-12 dB
	settings.bands[3] = 40;
	Equalizer limited;
	limited.SetSettings(settings);
	CHECK(limited.Settings().bands[3] == 12);
	settings.bands[3] = 0;

	// channels are filtered separately
	settings.bands[5] = 12;
	CHECK(near(sine_gain(settings, 1000, 44100, 2, 1), 12, 0.3));
	{
		Equalizer stereo;
		stereo.SetFormat(44100, 2);
		stereo.SetSettings(settings);
		std::vector<float> buffer(2 * 4410, 0.0f);
		for (int i = 0; i < 4410; i++)
			buffer[i * 2] = sinf(2 * M_PI * 1000 * i / 44100.0f);
		stereo.Process(buffer.data(), 4410);
		bool silent = true;
		for (int i = 0; i < 4410; i++) {
			if (buffer[i * 2 + 1] != 0)
				silent = false;
		}
		CHECK(silent);
	}

	// everything at +12 stays finite and bounded
	for (int i = 0; i < kEqualizerBands; i++)
		settings.bands[i] = 12;
	settings.preamp = 12;
	double gain = sine_gain(settings, 440);
	CHECK(gain == gain && gain < 40);

	// presets
	CHECK(count_equalizer_presets() >= 20);
	const EqualizerPreset* flat = find_equalizer_preset("Flat");
	CHECK(flat != NULL);
	bool flatIsFlat = flat != NULL && flat->preamp == 0;
	for (int i = 0; flat != NULL && i < kEqualizerBands; i++)
		flatIsFlat = flatIsFlat && flat->bands[i] == 0;
	CHECK(flatIsFlat);
	CHECK(find_equalizer_preset("Rock") != NULL);
	CHECK(find_equalizer_preset("Nonexistent") == NULL);
	bool inRange = true;
	bool sorted = true;
	for (int i = 0; i < count_equalizer_presets(); i++) {
		const EqualizerPreset& preset = equalizer_preset(i);
		for (int j = 0; j < kEqualizerBands; j++) {
			if (fabs(preset.bands[j]) > kEqualizerMaxGain)
				inRange = false;
		}
		if (i > 0 && strcmp(equalizer_preset(i - 1).name, preset.name) >= 0)
			sorted = false;
	}
	CHECK(inRange);
	CHECK(sorted);
}


// #pragma mark - ID3 writer


typedef std::vector<uint8_t> Bytes;
typedef std::map<std::string, std::string> TextFrames;


static Bytes
operator+(const Bytes& a, const Bytes& b)
{
	Bytes out(a);
	out.insert(out.end(), b.begin(), b.end());
	return out;
}

static std::string sTemporaryDirectory;


static std::string
temporary_path(const char* name)
{
	if (sTemporaryDirectory.empty()) {
		const char* base = getenv("TMPDIR");
		std::string pattern = std::string(base != NULL ? base : "/tmp")
			+ "/betunes-id3-XXXXXX";
		char* buffer = strdup(pattern.c_str());
		if (mkdtemp(buffer) != NULL)
			sTemporaryDirectory = buffer;
		free(buffer);
	}
	return sTemporaryDirectory + "/" + name;
}


static void
write_file(const std::string& path, const Bytes& data)
{
	FILE* file = fopen(path.c_str(), "wb");
	if (file == NULL)
		return;
	fwrite(data.data(), 1, data.size(), file);
	fclose(file);
}


static Bytes
read_file(const std::string& path)
{
	Bytes data;
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return data;
	uint8_t buffer[4096];
	size_t read;
	while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
		data.insert(data.end(), buffer, buffer + read);
	fclose(file);
	return data;
}


static Bytes
fake_audio(size_t size)
{
	Bytes audio;
	for (size_t i = 0; i < size; i++)
		audio.push_back(i % 417 == 0 ? 0xff : (uint8_t)(i * 7 + 3));
	return audio;
}


static void
append(Bytes& out, const std::string& text)
{
	out.insert(out.end(), text.begin(), text.end());
}


// A frame of an ID3v2.2 (3 byte sizes), 2.3 (plain) or 2.4 (syncsafe) tag.
static Bytes
frame(const std::string& id, const Bytes& data, int version,
	bool plainSizeIn24 = false)
{
	Bytes out;
	append(out, id);
	uint32_t size = data.size();
	if (version == 2) {
		out.push_back(size >> 16);
		out.push_back((size >> 8) & 0xff);
		out.push_back(size & 0xff);
	} else if (version == 4 && !plainSizeIn24) {
		out.push_back((size >> 21) & 0x7f);
		out.push_back((size >> 14) & 0x7f);
		out.push_back((size >> 7) & 0x7f);
		out.push_back(size & 0x7f);
	} else {
		out.push_back(size >> 24);
		out.push_back((size >> 16) & 0xff);
		out.push_back((size >> 8) & 0xff);
		out.push_back(size & 0xff);
	}
	if (version > 2) {
		out.push_back(0);
		out.push_back(0);
	}
	out.insert(out.end(), data.begin(), data.end());
	return out;
}


static Bytes
latin1_text(const std::string& text)
{
	Bytes data(1, 0);
	append(data, text);
	return data;
}


static Bytes
utf8_text(const std::string& text)
{
	Bytes data(1, 3);
	append(data, text);
	return data;
}


static Bytes
tag(int version, const Bytes& frames, size_t padding, uint8_t flags = 0)
{
	Bytes out;
	append(out, "ID3");
	out.push_back(version);
	out.push_back(0);
	out.push_back(flags);
	uint32_t size = frames.size() + padding;
	out.push_back((size >> 21) & 0x7f);
	out.push_back((size >> 14) & 0x7f);
	out.push_back((size >> 7) & 0x7f);
	out.push_back(size & 0x7f);
	out.insert(out.end(), frames.begin(), frames.end());
	out.resize(out.size() + padding, 0);
	return out;
}


static Bytes
id3v1(const char* title, const char* artist, int track, int genre)
{
	Bytes out(128, 0);
	memcpy(out.data(), "TAG", 3);
	memcpy(out.data() + 3, title, strlen(title));
	memcpy(out.data() + 33, artist, strlen(artist));
	memcpy(out.data() + 93, "1998", 4);
	out[126] = track;
	out[127] = genre;
	return out;
}


static size_t
tag_size(const Bytes& file)
{
	if (file.size() < 10 || memcmp(file.data(), "ID3", 3) != 0)
		return 0;
	return 10 + ((file[6] & 0x7f) << 21 | (file[7] & 0x7f) << 14
		| (file[8] & 0x7f) << 7 | (file[9] & 0x7f));
}


// The bytes after the ID3v2 tag (and before "tail" bytes).
static Bytes
after_tag(const Bytes& file, size_t tail = 0)
{
	size_t start = tag_size(file);
	if (start + tail > file.size())
		return Bytes();
	return Bytes(file.begin() + start, file.end() - tail);
}


static bool
contains(const Bytes& haystack, const Bytes& needle)
{
	return std::search(haystack.begin(), haystack.end(), needle.begin(),
		needle.end()) != haystack.end();
}


static int sHookCalls = 0;


static int
count_hook(const char*, const char*, void* cookie)
{
	sHookCalls++;
	return cookie != NULL ? *(int*)cookie : 0;
}


static void
test_id3_no_tag()
{
	std::string path = temporary_path("plain.mp3");
	Bytes audio = fake_audio(6000);
	write_file(path, audio);

	TagChanges changes;
	changes.Set(TAG_TITLE, "Hello");
	changes.Set(TAG_ARTIST, "W\xc3\xb6rld");
	changes.Set(TAG_ALBUM, "\xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\x8e\xb5");
	std::string error;
	sHookCalls = 0;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), changes, count_hook, NULL,
		error));
	CHECK_EQ(1, sHookCalls);

	TextFrames frames;
	int version = 0;
	CHECK(read_id3v2_text(path.c_str(), frames, &version));
	CHECK_EQ(3, version);
	CHECK_EQ(std::string("Hello"), frames["TIT2"]);
	CHECK_EQ(std::string("W\xc3\xb6rld"), frames["TPE1"]);
	CHECK_EQ(std::string("\xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\x8e\xb5"),
		frames["TALB"]);
	Bytes file = read_file(path);
	CHECK(after_tag(file) == audio);
	CHECK(access((temporary_path(".plain.mp3.betunes-tags")).c_str(), F_OK)
		!= 0);

	// the next change fits into the padding: written in place
	TagChanges more;
	more.Set(TAG_ARTIST, "Another Artist");
	sHookCalls = 0;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), more, count_hook, NULL, error));
	CHECK_EQ(0, sHookCalls);
	Bytes again = read_file(path);
	CHECK_EQ(file.size(), again.size());
	frames.clear();
	CHECK(read_id3v2_text(path.c_str(), frames));
	CHECK_EQ(std::string("Another Artist"), frames["TPE1"]);
	CHECK_EQ(std::string("Hello"), frames["TIT2"]);
	CHECK(after_tag(again) == audio);
}


static void
test_id3_v23()
{
	std::string path = temporary_path("v23.mp3");
	Bytes picture;
	append(picture, std::string("\0image/png\0\x03\0", 13));
	for (int i = 0; i < 300; i++)
		picture.push_back(i % 3 == 0 ? 0xff : i);
	Bytes pictureFrame = frame("APIC", picture, 3);
	Bytes frames = frame("TIT2", latin1_text("Song"), 3)
		+ frame("TSSE", latin1_text("Lavf60"), 3)
		+ frame("TRCK", latin1_text("3/12"), 3)
		+ frame("TYER", latin1_text("2001"), 3)
		+ frame("TDRC", latin1_text("2001"), 3)
		+ pictureFrame
		+ frame("TCON", latin1_text("Rock"), 3);
	Bytes audio = fake_audio(9000);
	write_file(path, tag(3, frames, 0) + audio);

	TagChanges changes;
	changes.Set(TAG_ALBUM, "New Album");
	changes.Set(TAG_TRACK, "5");
	changes.Set(TAG_YEAR, "1999");
	changes.Set(TAG_GENRE, "");
	std::string error;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), changes, NULL, NULL, error));

	TextFrames text;
	int version = 0;
	CHECK(read_id3v2_text(path.c_str(), text, &version));
	CHECK_EQ(3, version);
	CHECK_EQ(std::string("New Album"), text["TALB"]);
	CHECK_EQ(std::string("5/12"), text["TRCK"]);
		// the total stays
	CHECK_EQ(std::string("1999"), text["TYER"]);
	CHECK(text.find("TDRC") == text.end());
	CHECK(text.find("TCON") == text.end());
		// removed
	CHECK_EQ(std::string("Song"), text["TIT2"]);
	CHECK_EQ(std::string("Lavf60"), text["TSSE"]);
	Bytes file = read_file(path);
	CHECK(contains(Bytes(file.begin(), file.begin() + tag_size(file)),
		pictureFrame));
		// the picture, byte for byte
	CHECK(after_tag(file) == audio);
}


static void
test_id3_v24()
{
	std::string path = temporary_path("v24.mp3");
	// a frame with a size that is not syncsafe (as old iTunes wrote them)
	std::string longText(200, 'x');
	Bytes frames = frame("TIT2", utf8_text("Caf\xc3\xa9"), 4)
		+ frame("TDRC", utf8_text("2004-05-01"), 4)
		+ frame("TYER", utf8_text("2004"), 4)
		+ frame("TCOM", utf8_text(longText), 4, true)
		+ frame("TALB", utf8_text("Album"), 4);
	Bytes audio = fake_audio(5000);
	write_file(path, tag(4, frames, 500) + audio);

	TextFrames text;
	CHECK(read_id3v2_text(path.c_str(), text));
	CHECK_EQ(longText, text["TCOM"]);
	CHECK_EQ(std::string("Album"), text["TALB"]);

	TagChanges changes;
	changes.Set(TAG_TITLE, "\xc3\x9c" "n" "\xc3\xaf" "c" "\xc3\xb6" "d" "\xc3\xa9");
	changes.Set(TAG_YEAR, "2010");
	std::string error;
	sHookCalls = 0;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), changes, count_hook, NULL,
		error));
	CHECK_EQ(0, sHookCalls);
		// fits into the padding

	text.clear();
	int version = 0;
	CHECK(read_id3v2_text(path.c_str(), text, &version));
	CHECK_EQ(4, version);
	CHECK_EQ(std::string("\xc3\x9c" "n" "\xc3\xaf" "c" "\xc3\xb6" "d" "\xc3\xa9"),
		text["TIT2"]);
	CHECK_EQ(std::string("2010"), text["TDRC"]);
	CHECK(text.find("TYER") == text.end());
	CHECK_EQ(longText, text["TCOM"]);
	CHECK(after_tag(read_file(path)) == audio);
}


static void
test_id3_v22()
{
	std::string path = temporary_path("v22.mp3");
	Bytes picture;
	append(picture, std::string("\0JPG\x03\0", 6));
	picture.push_back(0xff);
	picture.push_back(0xd8);
	Bytes frames = frame("TT2", latin1_text("Old"), 2)
		+ frame("TP1", latin1_text("Someone"), 2)
		+ frame("XYZ", latin1_text("unknown"), 2)
		+ frame("PIC", picture, 2);
	Bytes audio = fake_audio(3000);
	write_file(path, tag(2, frames, 100) + audio);

	TagChanges changes;
	changes.Set(TAG_ALBUM, "Converted");
	std::string error;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), changes, NULL, NULL, error));

	TextFrames text;
	int version = 0;
	CHECK(read_id3v2_text(path.c_str(), text, &version));
	CHECK_EQ(3, version);
	CHECK_EQ(std::string("Old"), text["TIT2"]);
	CHECK_EQ(std::string("Someone"), text["TPE1"]);
	CHECK_EQ(std::string("Converted"), text["TALB"]);
	Bytes file = read_file(path);
	Bytes apic;
	append(apic, std::string("APIC", 4));
	CHECK(contains(file, apic));
	Bytes mime;
	append(mime, "image/jpeg");
	CHECK(contains(file, mime));
	CHECK(after_tag(file) == audio);
}


static void
test_id3_v1()
{
	std::string path = temporary_path("v1.mp3");
	Bytes audio = fake_audio(4000);
	Bytes v1 = id3v1("Old Tagger", "Retro", 7, 17);
	write_file(path, audio + v1);

	// removing a field: no ID3v2 tag is added just for that
	TagChanges remove;
	remove.Set(TAG_ALBUM, "");
	std::string error;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), remove, NULL, NULL, error));
	CHECK_EQ(audio.size() + 128, read_file(path).size());

	TagChanges changes;
	changes.Set(TAG_ARTIST, "\xc3\x85ngstr\xc3\xb6m");
	changes.Set(TAG_GENRE, "jazz");
	changes.Set(TAG_TRACK, "9");
	CHECK_EQ(0, write_mp3_tags(path.c_str(), changes, NULL, NULL, error));

	Bytes file = read_file(path);
	TextFrames text;
	CHECK(read_id3v2_text(path.c_str(), text));
	CHECK_EQ(std::string("\xc3\x85ngstr\xc3\xb6m"), text["TPE1"]);
	CHECK_EQ(std::string("9"), text["TRCK"]);
	CHECK(after_tag(file, 128) == audio);
	Bytes tail(file.end() - 128, file.end());
	CHECK(memcmp(tail.data(), "TAG", 3) == 0);
	CHECK(memcmp(tail.data() + 3, "Old Tagger", 10) == 0);
	CHECK(memcmp(tail.data() + 33, "\xc5ngstr\xf6m\0", 10) == 0);
	CHECK_EQ(9, tail[126]);
	CHECK_EQ(8, tail[127]);
		// "Jazz"

	TagChanges clear;
	clear.Set(TAG_TRACK, "");
	CHECK_EQ(0, write_mp3_tags(path.c_str(), clear, NULL, NULL, error));
	file = read_file(path);
	CHECK_EQ(0, file[file.size() - 2]);
	text.clear();
	CHECK(read_id3v2_text(path.c_str(), text));
	CHECK(text.find("TRCK") == text.end());
}


static void
test_id3_unsynchronised()
{
	std::string path = temporary_path("unsync.mp3");
	// "\xff\xe0" in the text is stored as "\xff\x00\xe0"
	Bytes data = latin1_text("A\xff\xe0Z");
	Bytes plain = frame("TIT2", data, 3);
	Bytes stored;
	for (size_t i = 0; i < plain.size(); i++) {
		stored.push_back(plain[i]);
		if (plain[i] == 0xff)
			stored.push_back(0);
	}
	Bytes audio = fake_audio(2000);
	write_file(path, tag(3, stored, 20, 0x80) + audio);

	TagChanges changes;
	changes.Set(TAG_ARTIST, "B");
	std::string error;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), changes, NULL, NULL, error));
	Bytes file = read_file(path);
	CHECK_EQ(0, file[5]);
		// no flags any more
	TextFrames text;
	CHECK(read_id3v2_text(path.c_str(), text));
	CHECK_EQ(std::string("A\xc3\xbf\xc3\xa0Z"), text["TIT2"]);
	CHECK_EQ(std::string("B"), text["TPE1"]);
	CHECK(after_tag(file) == audio);
}


static void
test_id3_failures()
{
	// a frame longer than the tag: refuse, change nothing
	std::string path = temporary_path("damaged.mp3");
	Bytes bad = frame("TIT2", latin1_text("x"), 3);
	bad[7] = 200;
	Bytes original = tag(3, bad, 0) + fake_audio(1000);
	write_file(path, original);
	TagChanges changes;
	changes.Set(TAG_TITLE, "y");
	std::string error;
	CHECK(write_mp3_tags(path.c_str(), changes, NULL, NULL, error) != 0);
	CHECK(!error.empty());
	CHECK(read_file(path) == original);

	// the attribute hook fails: the original stays, the copy goes
	path = temporary_path("hook.mp3");
	original = fake_audio(1000);
	write_file(path, original);
	int failure = EPERM;
	sHookCalls = 0;
	CHECK_EQ(EPERM, write_mp3_tags(path.c_str(), changes, count_hook,
		&failure, error));
	CHECK_EQ(1, sHookCalls);
	CHECK(read_file(path) == original);
	CHECK(access(temporary_path(".hook.mp3.betunes-tags").c_str(), F_OK) != 0);

	// a file that does not exist
	CHECK_EQ(ENOENT, write_mp3_tags(temporary_path("missing.mp3").c_str(),
		changes, NULL, NULL, error));

	// a read-only file (root may write anyway)
	if (geteuid() != 0) {
		path = temporary_path("readonly.mp3");
		write_file(path, original);
		chmod(path.c_str(), 0444);
		CHECK_EQ(EACCES, write_mp3_tags(path.c_str(), changes, NULL, NULL,
			error));
		CHECK(read_file(path) == original);
	}
}


static void
test_id3_footer()
{
	// an ID3v2.4 tag with a footer: the footer's room becomes padding
	std::string path = temporary_path("footer.mp3");
	Bytes frames = frame("TIT2", utf8_text("Feet"), 4);
	Bytes withFooter = tag(4, frames, 30, 0x10);
	Bytes footer;
	append(footer, "3DI");
	footer.insert(footer.end(), withFooter.begin() + 3, withFooter.begin() + 10);
	Bytes audio = fake_audio(1500);
	write_file(path, withFooter + footer + audio);
	size_t size = read_file(path).size();

	TagChanges changes;
	changes.Set(TAG_ARTIST, "Hands");
	std::string error;
	CHECK_EQ(0, write_mp3_tags(path.c_str(), changes, NULL, NULL, error));
	Bytes file = read_file(path);
	CHECK_EQ(size, file.size());
	TextFrames text;
	CHECK(read_id3v2_text(path.c_str(), text));
	CHECK_EQ(std::string("Feet"), text["TIT2"]);
	CHECK_EQ(std::string("Hands"), text["TPE1"]);
	CHECK(after_tag(file) == audio);
}


static void
test_id3_writer()
{
	test_id3_no_tag();
	test_id3_v23();
	test_id3_v24();
	test_id3_v22();
	test_id3_v1();
	test_id3_unsynchronised();
	test_id3_failures();
	test_id3_footer();
	if (!sTemporaryDirectory.empty()) {
		std::string command = "rm -rf '" + sTemporaryDirectory + "'";
		if (system(command.c_str()) != 0)
			printf("could not remove %s\n", sTemporaryDirectory.c_str());
	}
}


// #pragma mark - DAAP


static std::string
dmap(const char* code, const std::string& data)
{
	std::string element(code, 4);
	uint32_t length = (uint32_t)data.size();
	element += (char)(length >> 24);
	element += (char)(length >> 16);
	element += (char)(length >> 8);
	element += (char)length;
	return element + data;
}


static std::string
dmap_number(const char* code, uint64_t value, int bytes)
{
	std::string data;
	for (int i = bytes - 1; i >= 0; i--)
		data += (char)(value >> (8 * i));
	return dmap(code, data);
}


static std::string
hex(const uint8_t* data, size_t size)
{
	std::string text;
	char digits[3];
	for (size_t i = 0; i < size; i++) {
		snprintf(digits, sizeof(digits), "%02x", data[i]);
		text += digits;
	}
	return text;
}


static void
test_daap_md5()
{
	// the real MD5 (RFC 1321's test suite) and iTunes' variant
	uint8_t digest[16];
	daap_md5((const uint8_t*)"", 0, false, digest);
	CHECK_EQ(std::string("d41d8cd98f00b204e9800998ecf8427e"), hex(digest, 16));
	daap_md5((const uint8_t*)"abc", 3, false, digest);
	CHECK_EQ(std::string("900150983cd24fb0d6963f7d28e17f72"), hex(digest, 16));
	const char* digits = "1234567890123456789012345678901234567890"
		"1234567890123456789012345678901234567890";
	daap_md5((const uint8_t*)digits, strlen(digits), false, digest);
	CHECK_EQ(std::string("57edf4a22be3c955ac49da2e2107b67a"), hex(digest, 16));
	daap_md5((const uint8_t*)"", 0, true, digest);
	CHECK_EQ(std::string("7d2e48f5dc1e1346c8d16fb6273e2caa"), hex(digest, 16));
	std::string as(200, 'a');
	daap_md5((const uint8_t*)as.data(), as.size(), true, digest);
	CHECK_EQ(std::string("1b2cdefb54825fd45ce4ccc40eda62f2"), hex(digest, 16));

	CHECK_EQ(std::string("77BE0B44D0D6ED1268381859B133B4B9"),
		daap_validation("/server-info", 0));
	CHECK_EQ(std::string("E3B047FF11C1F845093E898495A585E2"),
		daap_validation("/login", 0));
	CHECK_EQ(std::string("6795220880DFDBBE24A0CD69C96E0205"),
		daap_validation("/databases/1/items/5.ogg?session-id=123", 11));
}


static void
test_dmap()
{
	std::string item1 = dmap("mlit", dmap_number("miid", 7, 4)
		+ dmap("minm", "Kaldalón") + dmap_number("astm", 215000, 4)
		+ dmap_number("astn", 3, 2) + dmap_number("asdk", 0, 1));
	std::string item2 = dmap("mlit", dmap_number("miid", 8, 4)
		+ dmap("minm", ""));
	std::string reply = dmap("adbs", dmap_number("mstt", 200, 4)
		+ dmap_number("mtco", 2, 4) + dmap("mlcl", item1 + item2));

	DmapReader top((const uint8_t*)reply.data(), reply.size());
	DmapElement adbs;
	CHECK(top.Find(dmap_code("adbs"), adbs));
	CHECK_EQ((uint64_t)200, dmap_child_int(adbs, dmap_code("mstt")));
	CHECK_EQ((uint64_t)99, dmap_child_int(adbs, dmap_code("none"), 99));
	DmapElement list;
	CHECK(DmapReader(adbs).Find(dmap_code("mlcl"), list));
	DmapReader items(list);
	DmapElement item;
	CHECK(items.Next(item));
	CHECK_EQ(dmap_code("mlit"), item.code);
	CHECK_EQ((uint64_t)7, dmap_child_int(item, dmap_code("miid")));
	CHECK_EQ(std::string("Kaldalón"), dmap_child_string(item,
		dmap_code("minm")));
	CHECK_EQ((uint64_t)215000, dmap_child_int(item, dmap_code("astm")));
	CHECK_EQ((uint64_t)3, dmap_child_int(item, dmap_code("astn")));
	CHECK(items.Next(item));
	CHECK_EQ((uint64_t)8, dmap_child_int(item, dmap_code("miid")));
	CHECK_EQ(std::string(), dmap_child_string(item, dmap_code("minm")));
	CHECK(!items.Next(item));

	// a truncated reply ends the walk instead of reading past it
	std::string cut = reply.substr(0, reply.size() - 3);
	DmapReader truncated((const uint8_t*)cut.data(), cut.size());
	CHECK(!truncated.Next(item));
	DmapReader empty(NULL, 0);
	CHECK(!empty.Next(item));
}


static std::string
dns_name(const char* dotted)
{
	std::string name;
	std::string label;
	for (const char* c = dotted;; c++) {
		if (*c == '.' || *c == '\0') {
			name += (char)label.size();
			name += label;
			label.clear();
			if (*c == '\0')
				break;
		} else
			label += *c;
	}
	return name + '\0';
}


static std::string
dns_record(const std::string& name, uint16_t type, const std::string& data)
{
	std::string record = name;
	record += (char)(type >> 8);
	record += (char)type;
	record += std::string("\x80\x01\0\0\x11\x94", 6);
		// class IN with the cache flush bit, TTL 4500
	record += (char)(data.size() >> 8);
	record += (char)data.size();
	return record + data;
}


static void
test_mdns()
{
	std::vector<DnsQuestion> questions;
	questions.push_back({ "_daap._tcp.local", kDnsTypePtr });
	std::string query = dns_query(0x1234, questions);
	CHECK_EQ(std::string("\x12\x34\0\0\0\x01\0\0\0\0\0\0", 12),
		query.substr(0, 12));
	CHECK_EQ(dns_name("_daap._tcp.local") + std::string("\0\x0c\0\x01", 4),
		query.substr(12));

	// a reply like iTunes': the PTR answer, the rest in the additional
	// section, names compressed
	std::string packet("\0\0\x84\0\0\0\0\x01\0\0\0\x03", 12);
	size_t service = packet.size();
	std::string instance = std::string("\x0fMy.Library \xe2\x80\x99s")
		+ std::string("\xc0", 1) + (char)service;
	packet += dns_record(dns_name("_daap._tcp.local"), kDnsTypePtr, instance);
	size_t instanceOffset = packet.size() - instance.size();
	std::string instanceName = std::string("\xc0", 1) + (char)instanceOffset;
	std::string srv("\0\0\0\0\x0e\x69", 6);
	srv += dns_name("box.local");
	packet += dns_record(instanceName, kDnsTypeSrv, srv);
	std::string text("\x09txtvers=1\x0dPassword=true", 24);
	packet += dns_record(instanceName, kDnsTypeTxt, text);
	packet += dns_record(dns_name("box.local"), kDnsTypeA,
		std::string("\xc0\xa8\x01\x9f", 4));

	std::vector<DnsRecord> records;
	CHECK(dns_parse_reply((const uint8_t*)packet.data(), packet.size(),
		records));
	CHECK_EQ((size_t)4, records.size());
	if (records.size() == 4) {
		CHECK_EQ((uint16_t)kDnsTypePtr, records[0].type);
		CHECK(dns_same_name("_DAAP._tcp.local", records[0].name));
		CHECK_EQ(std::string("My\\.Library \xe2\x80\x99s._daap._tcp.local"),
			records[0].target);
		CHECK_EQ(std::string("My.Library \xe2\x80\x99s"),
			dns_first_label(records[0].target));
		CHECK_EQ(records[0].target, records[1].name);
		CHECK_EQ((uint16_t)3689, records[1].port);
		CHECK_EQ(std::string("box.local"), records[1].target);
		std::string value;
		CHECK(dns_text_value(records[2].text, "password", value));
		CHECK_EQ(std::string("true"), value);
		CHECK(!dns_text_value(records[2].text, "Machine Name", value));
		CHECK_EQ((uint32_t)0xc0a8019f, records[3].address);
		CHECK_EQ((uint32_t)4500, records[3].ttl);
	}

	// the name escapes survive a query for the instance
	questions.clear();
	questions.push_back({ records.empty() ? std::string() : records[0].target,
		kDnsTypeSrv });
	query = dns_query(1, questions);
	CHECK_EQ(std::string("\x0fMy.Library \xe2\x80\x99s"),
		query.substr(12, 16));

	// pointer loops, truncation and queries are not replies
	std::string loop("\0\0\x84\0\0\0\0\x01\0\0\0\0\xc0\x0c\0\x01", 16);
	records.clear();
	CHECK(!dns_parse_reply((const uint8_t*)loop.data(), loop.size(),
		records));
	CHECK(!dns_parse_reply((const uint8_t*)packet.data(), packet.size() - 2,
		records));
	CHECK(!dns_parse_reply((const uint8_t*)query.data(), query.size(),
		records));
}


static void
test_http_helpers()
{
	CHECK_EQ(std::string("bm9uZTpzZWNyZXQ="), base64_encode("none:secret"));
	CHECK_EQ(std::string("YQ=="), base64_encode("a"));
	CHECK_EQ(std::string("YWI="), base64_encode("ab"));
	CHECK_EQ(std::string(""), base64_encode(""));

	int status = 0;
	std::map<std::string, std::string> headers;
	CHECK(http_parse_head("HTTP/1.1 206 Partial Content\r\n"
		"Content-Length:  1024\r\nContent-Range: bytes 0-1023/5000\r\n"
		"DAAP-Server: iTunes/4.6\r\n", status, headers));
	CHECK_EQ(206, status);
	CHECK_EQ(std::string("1024"), headers["content-length"]);
	CHECK_EQ(std::string("bytes 0-1023/5000"), headers["content-range"]);
	CHECK_EQ(std::string("iTunes/4.6"), headers["daap-server"]);
	CHECK(!http_parse_head("SSH-2.0-OpenSSH\r\n", status, headers));
	CHECK(!http_parse_head("HTTP/1.0 abc\r\n", status, headers));

	std::string host, path;
	uint16_t port = 0;
	CHECK(parse_share_address(" 192.168.1.162 ", host, port, path));
	CHECK_EQ(std::string("192.168.1.162"), host);
	CHECK_EQ((uint16_t)3689, port);
	CHECK(parse_share_address("music.local:3690", host, port, path));
	CHECK_EQ(std::string("music.local"), host);
	CHECK_EQ((uint16_t)3690, port);
	CHECK(parse_share_address("daap://10.0.0.2:3689/databases/1/items/5.mp3",
		host, port, path));
	CHECK_EQ(std::string("10.0.0.2"), host);
	CHECK_EQ(std::string("/databases/1/items/5.mp3"), path);
	CHECK(parse_share_address("[fe80::1]:4000", host, port, path));
	CHECK_EQ(std::string("fe80::1"), host);
	CHECK_EQ((uint16_t)4000, port);
	CHECK(!parse_share_address("", host, port, path));
	CHECK(!parse_share_address("host:0", host, port, path));
	CHECK(!parse_share_address("host:70000", host, port, path));
	CHECK(!parse_share_address("host:12ab", host, port, path));
	CHECK(!parse_share_address("my host", host, port, path));
}


int
main()
{
	test_folding();
	test_search_words();
	test_numbers();
	test_genres();
	test_id3v1();
	test_utf8();
	test_format();
	test_queue_in_order();
	test_queue_shuffle();
	test_queue_toggle_shuffle();
	test_queue_remove();
	test_equalizer();
	test_id3_writer();
	test_daap_md5();
	test_dmap();
	test_mdns();
	test_http_helpers();

	printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
