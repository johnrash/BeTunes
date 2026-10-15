/*
 * The audio player.
 */
#include "Player.h"

#include <Autolock.h>
#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Message.h>

#include <stdio.h>
#include <string.h>

#include "AudioOutput.h"
#include "BeTunes.h"


static const float kRingSeconds = 1.0f;
static const int32 kMaxEmptyReads = 100;


Player::Player(const BMessenger& target)
	:
	fTarget(target),
	fState(STOPPED),
	fSong(-1),
	fSerial(0),
	fCommandLock("player commands"),
	fWakeSem(-1),
	fThread(-1),
	fFile(NULL),
	fTrack(NULL),
	fOutput(NULL),
	fOpenSerial(0),
	fOpenSong(-1),
	fDecodeBuffer(NULL),
	fDecodeBufferSize(0),
	fDecoded(NULL),
	fDecodedCapacity(0),
	fDecodedFormat(0),
	fPendingFrames(0),
	fPendingOffset(0),
	fEmptyReads(0),
	fDecoderDone(true),
	fEndNotified(true),
	fBufferLock("player buffer"),
	fRing(NULL),
	fRingFrames(0),
	fRingRead(0),
	fRingFill(0),
	fChannels(0),
	fFrameRate(0),
	fFramesPlayed(0),
	fDuration(0),
	fVolume(1.0f),
	fActive(false),
	fPaused(false)
{
	fWakeSem = create_sem(0, "BeTunes player wake");
	fThread = spawn_thread(&_ThreadEntry, "BeTunes decoder", B_DISPLAY_PRIORITY,
		this);
	resume_thread(fThread);
}


Player::~Player()
{
	Command command;
	command.type = kCommandQuit;
	_PostCommand(command);
	status_t result;
	wait_for_thread(fThread, &result);
	delete_sem(fWakeSem);

	delete[] fRing;
	delete[] fDecodeBuffer;
	delete[] fDecoded;
}


int32
Player::Play(const char* path, int32 song)
{
	fSerial++;
	fSong = song;
	fState = PLAYING;
	{
		BAutolock _(fBufferLock);
		fPaused = false;
		fFramesPlayed = 0;
		fDuration = 0;
	}

	Command command;
	command.type = kCommandPlay;
	command.path = path;
	command.song = song;
	command.serial = fSerial;
	command.time = 0;
	_PostCommand(command);
	return fSerial;
}


void
Player::Stop()
{
	fSerial++;
	fSong = -1;
	fState = STOPPED;
	{
		BAutolock _(fBufferLock);
		fActive = false;
		fPaused = false;
		fFramesPlayed = 0;
		fDuration = 0;
	}

	Command command;
	command.type = kCommandStop;
	_PostCommand(command);
}


void
Player::Pause()
{
	if (fState != PLAYING)
		return;
	fState = PAUSED;
	BAutolock _(fBufferLock);
	fPaused = true;
}


void
Player::Resume()
{
	if (fState != PAUSED)
		return;
	fState = PLAYING;
	BAutolock _(fBufferLock);
	fPaused = false;
}


void
Player::Seek(bigtime_t time)
{
	if (fState == STOPPED)
		return;

	{
		// show the new position right away
		BAutolock _(fBufferLock);
		if (fFrameRate > 0)
			fFramesPlayed = (int64)(time * (double)fFrameRate / 1000000.0);
	}

	Command command;
	command.type = kCommandSeek;
	command.time = time;
	command.serial = fSerial;
	command.song = fSong;
	_PostCommand(command);
}


void
Player::SetVolume(float volume)
{
	BAutolock _(fBufferLock);
	fVolume = volume;
}


void
Player::SetEqualizer(const EqualizerSettings& settings)
{
	BAutolock _(fBufferLock);
	fEqualizer.SetSettings(settings);
}


bigtime_t
Player::Position() const
{
	BAutolock _(fBufferLock);
	if (fFrameRate <= 0)
		return 0;
	return (bigtime_t)(fFramesPlayed * 1000000.0 / fFrameRate);
}


bigtime_t
Player::Duration() const
{
	BAutolock _(fBufferLock);
	return fDuration;
}


// #pragma mark - decoder thread


void
Player::_PostCommand(const Command& command)
{
	{
		BAutolock _(fCommandLock);
		fCommands.push_back(command);
	}
	release_sem(fWakeSem);
}


status_t
Player::_ThreadEntry(void* cookie)
{
	((Player*)cookie)->_Run();
	return B_OK;
}


void
Player::_Run()
{
	while (true) {
		std::deque<Command> commands;
		{
			BAutolock _(fCommandLock);
			commands.swap(fCommands);
		}

		for (size_t i = 0; i < commands.size(); i++) {
			const Command& command = commands[i];
			// only the last of several play/stop requests matters
			bool superseded = false;
			for (size_t j = i + 1; j < commands.size(); j++) {
				if (commands[j].type != kCommandSeek)
					superseded = true;
			}

			switch (command.type) {
				case kCommandQuit:
					_Close();
					_DeleteOutput();
					return;

				case kCommandPlay:
					if (superseded)
						break;
					_Close();
					_Open(command);
					break;

				case kCommandStop:
					if (superseded)
						break;
					_Close();
					_DeleteOutput();
					break;

				case kCommandSeek:
					if (!superseded && command.serial == fOpenSerial)
						_Seek(command.time);
					break;
			}
		}

		bool busy = false;
		if (fTrack != NULL && !fDecoderDone)
			busy = _Decode();

		if (fTrack != NULL && fDecoderDone && !fEndNotified
			&& fPendingFrames == 0) {
			bool drained;
			{
				BAutolock _(fBufferLock);
				drained = fRingFill == 0;
			}
			if (drained) {
				fEndNotified = true;
				_Notify(kMsgPlayerEnded, fOpenSerial, fOpenSong);
			}
		}

		if (!busy)
			acquire_sem_etc(fWakeSem, 1, B_RELATIVE_TIMEOUT, 50000);
	}
}


void
Player::_Open(const Command& command)
{
	fOpenSerial = command.serial;
	fOpenSong = command.song;

	entry_ref ref;
	status_t status = get_ref_for_path(command.path, &ref);
	BEntry entry(&ref);
	if (status == B_OK && !entry.Exists())
		status = B_ENTRY_NOT_FOUND;
	if (status != B_OK) {
		_Notify(kMsgPlayerError, command.serial, command.song,
			strerror(status));
		return;
	}

	fFile = new BMediaFile(&ref);
	status = fFile->InitCheck();
	media_format format;
	if (status == B_OK) {
		status = B_MEDIA_BAD_FORMAT;
		for (int32 i = 0; i < fFile->CountTracks(); i++) {
			BMediaTrack* track = fFile->TrackAt(i);
			if (track == NULL)
				continue;
			media_format encoded;
			if (fTrack == NULL && track->EncodedFormat(&encoded) == B_OK
				&& (encoded.type == B_MEDIA_ENCODED_AUDIO
					|| encoded.type == B_MEDIA_RAW_AUDIO)) {
				fTrack = track;
				continue;
			}
			fFile->ReleaseTrack(track);
		}
		if (fTrack != NULL) {
			format.type = B_MEDIA_RAW_AUDIO;
			format.u.raw_audio = media_raw_audio_format::wildcard;
			format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
			format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
			status = fTrack->DecodedFormat(&format);
			if (status == B_OK && (format.type != B_MEDIA_RAW_AUDIO
					|| format.u.raw_audio.frame_rate <= 0
					|| format.u.raw_audio.channel_count == 0
					|| (format.u.raw_audio.format
						& media_raw_audio_format::B_AUDIO_SIZE_MASK) == 0))
				status = B_MEDIA_BAD_FORMAT;
		}
	}
	if (status != B_OK) {
		_Close();
		_Notify(kMsgPlayerError, command.serial, command.song,
			strerror(status));
		return;
	}

	float frameRate = format.u.raw_audio.frame_rate;
	int32 channels = format.u.raw_audio.channel_count;
	fDecodedFormat = format.u.raw_audio.format;

	// room for whatever the decoder hands out at once
	size_t sampleSize = fDecodedFormat
		& media_raw_audio_format::B_AUDIO_SIZE_MASK;
	size_t bufferSize = format.u.raw_audio.buffer_size;
	if (bufferSize < 65536)
		bufferSize = 65536;
	bufferSize *= 4;
	size_t samples = bufferSize / sampleSize;
	if (bufferSize != fDecodeBufferSize || samples != fDecodedCapacity) {
		delete[] fDecodeBuffer;
		delete[] fDecoded;
		fDecodeBuffer = new uint8[bufferSize];
		fDecoded = new float[samples];
		fDecodeBufferSize = bufferSize;
		fDecodedCapacity = samples;
	}

	// The output is kept between songs of the same format: no reconnection
	// to the mixer, almost no gap.
	if (fOutput != NULL && (fOutput->FrameRate() != frameRate
			|| fOutput->Channels() != channels))
		_DeleteOutput();

	{
		BAutolock _(fBufferLock);
		int32 ringFrames = (int32)(frameRate * kRingSeconds);
		if (ringFrames != fRingFrames || channels != fChannels) {
			delete[] fRing;
			fRing = new float[ringFrames * channels];
			fRingFrames = ringFrames;
		}
		if (channels != fChannels || frameRate != fFrameRate)
			fEqualizer.SetFormat(frameRate, channels);
		fChannels = channels;
		fFrameRate = frameRate;
		fRingRead = 0;
		fRingFill = 0;
		fFramesPlayed = 0;
		fDuration = fTrack->Duration();
		fActive = true;
	}
	fPendingFrames = 0;
	fPendingOffset = 0;
	fEmptyReads = 0;
	fDecoderDone = false;
	fEndNotified = false;

	if (fOutput == NULL) {
		fOutput = AudioOutput::Create(frameRate, channels, &_FillHook, this,
			status);
		if (fOutput != NULL)
			status = fOutput->Start();
		if (status != B_OK) {
			_DeleteOutput();
			_Close();
			_Notify(kMsgPlayerError, command.serial, command.song,
				strerror(status), true);
			return;
		}
	}

	BMessage started(kMsgPlayerStarted);
	started.AddInt32("serial", command.serial);
	started.AddInt32("song", command.song);
	started.AddInt64("duration", fTrack->Duration());
	fTarget.SendMessage(&started, (BHandler*)NULL, 1000000);
}


void
Player::_Close()
{
	{
		BAutolock _(fBufferLock);
		fActive = false;
		fRingFill = 0;
	}
	delete fFile;
		// also deletes the track
	fFile = NULL;
	fTrack = NULL;
	fDecoderDone = true;
	fEndNotified = true;
	fPendingFrames = 0;
}


void
Player::_DeleteOutput()
{
	// never while holding fBufferLock: stopping waits for the output thread,
	// which may be waiting for the lock
	delete fOutput;
	fOutput = NULL;
}


void
Player::_Seek(bigtime_t time)
{
	if (fTrack == NULL)
		return;

	bigtime_t duration = fTrack->Duration();
	if (time < 0)
		time = 0;
	if (duration > 0 && time > duration)
		time = duration;

	bigtime_t target = time;
	if (fTrack->SeekToTime(&target, B_MEDIA_SEEK_CLOSEST_BACKWARD) != B_OK) {
		int64 frame = (int64)(time * (double)fFrameRate / 1000000.0);
		if (fTrack->SeekToFrame(&frame, B_MEDIA_SEEK_CLOSEST_BACKWARD) == B_OK)
			target = (bigtime_t)(frame * 1000000.0 / fFrameRate);
		else
			target = fTrack->CurrentTime();
	}

	{
		BAutolock _(fBufferLock);
		fRingRead = 0;
		fRingFill = 0;
		fFramesPlayed = (int64)(target * (double)fFrameRate / 1000000.0);
	}
	fPendingFrames = 0;
	fEmptyReads = 0;
	fDecoderDone = false;
	fEndNotified = false;
}


// Moves decoded audio into the ring buffer; returns whether it did any
// work (if not, the thread waits for the output to make room).
bool
Player::_Decode()
{
	if (fPendingFrames == 0) {
		{
			BAutolock _(fBufferLock);
			if (fRingFrames - fRingFill < fRingFrames / 4)
				return false;
		}

		int64 frames = 0;
		media_header header;
		status_t status = fTrack->ReadFrames(fDecodeBuffer, &frames, &header);
		if (status != B_OK) {
			// B_LAST_BUFFER_ERROR at the end, anything else is just as final
			fDecoderDone = true;
			return false;
		}
		if (frames <= 0) {
			if (++fEmptyReads > kMaxEmptyReads)
				fDecoderDone = true;
			return true;
		}
		fEmptyReads = 0;

		size_t samples = frames * fChannels;
		size_t sampleSize = fDecodedFormat
			& media_raw_audio_format::B_AUDIO_SIZE_MASK;
		if (samples * sampleSize > fDecodeBufferSize) {
			fprintf(stderr, "BeTunes: decoder returned too many frames\n");
			samples = fDecodeBufferSize / sampleSize;
			frames = samples / fChannels;
		}
		switch (fDecodedFormat) {
			case media_raw_audio_format::B_AUDIO_FLOAT:
				memcpy(fDecoded, fDecodeBuffer, samples * sizeof(float));
				break;
			case media_raw_audio_format::B_AUDIO_INT:
				for (size_t i = 0; i < samples; i++)
					fDecoded[i] = ((int32*)fDecodeBuffer)[i] / 2147483648.0f;
				break;
			case media_raw_audio_format::B_AUDIO_SHORT:
				for (size_t i = 0; i < samples; i++)
					fDecoded[i] = ((int16*)fDecodeBuffer)[i] / 32768.0f;
				break;
			case media_raw_audio_format::B_AUDIO_UCHAR:
				for (size_t i = 0; i < samples; i++)
					fDecoded[i] = (fDecodeBuffer[i] - 128) / 128.0f;
				break;
			case media_raw_audio_format::B_AUDIO_CHAR:
				for (size_t i = 0; i < samples; i++)
					fDecoded[i] = ((int8*)fDecodeBuffer)[i] / 128.0f;
				break;
			default:
				memset(fDecoded, 0, samples * sizeof(float));
				break;
		}
		fPendingFrames = (int32)frames;
		fPendingOffset = 0;
	}

	BAutolock _(fBufferLock);
	int32 count = fRingFrames - fRingFill;
	if (count > fPendingFrames)
		count = fPendingFrames;
	int32 write = (fRingRead + fRingFill) % fRingFrames;
	int32 first = fRingFrames - write;
	if (first > count)
		first = count;
	memcpy(fRing + write * fChannels, fDecoded + fPendingOffset * fChannels,
		first * fChannels * sizeof(float));
	if (count > first) {
		memcpy(fRing, fDecoded + (fPendingOffset + first) * fChannels,
			(count - first) * fChannels * sizeof(float));
	}
	fRingFill += count;
	fPendingOffset += count;
	fPendingFrames -= count;
	return count > 0;
}


void
Player::_Notify(uint32 what, int32 serial, int32 song, const char* error,
	bool output)
{
	BMessage message(what);
	message.AddInt32("serial", serial);
	message.AddInt32("song", song);
	if (error != NULL)
		message.AddString("error", error);
	message.AddBool("output", output);
	// with a timeout: the window may be waiting for this thread to quit
	fTarget.SendMessage(&message, (BHandler*)NULL, 1000000);
}


// #pragma mark - output thread


void
Player::_FillHook(void* cookie, float* buffer, int32 frames)
{
	((Player*)cookie)->_Fill(buffer, frames);
}


void
Player::_Fill(float* buffer, int32 frames)
{
	int32 channels = 0;
	{
		BAutolock _(fBufferLock);
		channels = fChannels;
		int32 count = 0;
		if (fActive && !fPaused && fRing != NULL) {
			count = fRingFill < frames ? fRingFill : frames;
			float volume = fVolume;
			int32 first = fRingFrames - fRingRead;
			if (first > count)
				first = count;
			const float* source = fRing + fRingRead * channels;
			for (int32 i = 0; i < first * channels; i++)
				buffer[i] = source[i] * volume;
			for (int32 i = 0; i < (count - first) * channels; i++)
				buffer[first * channels + i] = fRing[i] * volume;
			fRingRead = (fRingRead + count) % fRingFrames;
			fRingFill -= count;
			fFramesPlayed += count;
		}
		if (count < frames && channels > 0)
			memset(buffer + count * channels, 0,
				(frames - count) * channels * sizeof(float));
		if (fActive && !fPaused)
			fEqualizer.Process(buffer, frames);
	}
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}
