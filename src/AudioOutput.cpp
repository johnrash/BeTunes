/*
 * Audio outputs.
 */
#include "AudioOutput.h"

#include <MediaDefs.h>
#include <OS.h>
#include <SoundPlayer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


namespace {


const int32 kFramesPerBuffer = 2048;
const int32 kScratchFrames = 4096;


float
clamp_sample(float value)
{
	if (value > 1.0f)
		return 1.0f;
	if (value < -1.0f)
		return -1.0f;
	return value;
}


// Plays through the system mixer. The mixer normally accepts our float
// format as is; if it insists on something else the samples are converted.
class SoundPlayerOutput : public AudioOutput {
public:
	SoundPlayerOutput(float frameRate, int32 channels, fill_hook hook,
			void* cookie)
		:
		AudioOutput(frameRate, channels, hook, cookie),
		fPlayer(NULL),
		fScratch(NULL)
	{
		fScratch = new float[kScratchFrames * channels];

		media_raw_audio_format format = media_raw_audio_format::wildcard;
		format.frame_rate = frameRate;
		format.channel_count = channels;
		format.format = media_raw_audio_format::B_AUDIO_FLOAT;
		format.byte_order = B_MEDIA_HOST_ENDIAN;
		format.buffer_size = kFramesPerBuffer * channels * sizeof(float);
		fPlayer = new BSoundPlayer(&format, "BeTunes", &_PlayBuffer, NULL,
			this);
	}

	virtual ~SoundPlayerOutput()
	{
		Stop();
		delete fPlayer;
		delete[] fScratch;
	}

	status_t InitCheck() const
	{
		return fPlayer->InitCheck();
	}

	virtual status_t Start()
	{
		status_t status = fPlayer->Start();
		if (status == B_OK)
			fPlayer->SetHasData(true);
		return status;
	}

	virtual void Stop()
	{
		if (fPlayer->InitCheck() == B_OK) {
			fPlayer->SetHasData(false);
			fPlayer->Stop();
		}
	}

private:
	static void _PlayBuffer(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& format)
	{
		((SoundPlayerOutput*)cookie)->_Fill(buffer, size, format);
	}

	void _Fill(void* buffer, size_t size, const media_raw_audio_format& format)
	{
		int32 outChannels = format.channel_count;
		size_t sampleSize = format.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK;
		if (outChannels <= 0 || sampleSize == 0)
			return;

		if (format.format == media_raw_audio_format::B_AUDIO_FLOAT
			&& outChannels == fChannels) {
			fHook(fCookie, (float*)buffer, size / (sizeof(float) * fChannels));
			return;
		}

		// convert
		int32 frames = size / (sampleSize * outChannels);
		uint8* out = (uint8*)buffer;
		while (frames > 0) {
			int32 count = frames < kScratchFrames ? frames : kScratchFrames;
			fHook(fCookie, fScratch, count);
			for (int32 i = 0; i < count; i++) {
				for (int32 channel = 0; channel < outChannels; channel++) {
					int32 source = channel < fChannels ? channel : fChannels - 1;
					float value = clamp_sample(fScratch[i * fChannels + source]);
					switch (format.format) {
						case media_raw_audio_format::B_AUDIO_FLOAT:
							*(float*)out = value;
							break;
						case media_raw_audio_format::B_AUDIO_INT:
							*(int32*)out = (int32)(value * 2147483647.0);
							break;
						case media_raw_audio_format::B_AUDIO_SHORT:
							*(int16*)out = (int16)(value * 32767.0f);
							break;
						case media_raw_audio_format::B_AUDIO_UCHAR:
							*(uint8*)out = (uint8)(128 + (int32)(value * 127.0f));
							break;
						case media_raw_audio_format::B_AUDIO_CHAR:
							*(int8*)out = (int8)(value * 127.0f);
							break;
						default:
							memset(out, 0, sampleSize);
							break;
					}
					out += sampleSize;
				}
			}
			frames -= count;
		}
	}

	BSoundPlayer*	fPlayer;
	float*			fScratch;
};


// Keeps time without making a sound. For tests, BETUNES_AUDIO_DUMP=<file>
// appends everything it "plays" to that file (raw float samples).
class SilentOutput : public AudioOutput {
public:
	SilentOutput(float frameRate, int32 channels, fill_hook hook,
			void* cookie)
		:
		AudioOutput(frameRate, channels, hook, cookie),
		fThread(-1),
		fQuit(0),
		fDump(NULL)
	{
		fBuffer = new float[kFramesPerBuffer * channels];
		const char* dump = getenv("BETUNES_AUDIO_DUMP");
		if (dump != NULL && dump[0] != '\0')
			fDump = fopen(dump, "ab");
	}

	virtual ~SilentOutput()
	{
		Stop();
		delete[] fBuffer;
		if (fDump != NULL)
			fclose(fDump);
	}

	virtual status_t Start()
	{
		if (fThread >= 0)
			return B_OK;
		atomic_set(&fQuit, 0);
		fThread = spawn_thread(&_ThreadEntry, "BeTunes silent output",
			B_REAL_TIME_PRIORITY, this);
		if (fThread < 0)
			return fThread;
		return resume_thread(fThread);
	}

	virtual void Stop()
	{
		if (fThread < 0)
			return;
		atomic_set(&fQuit, 1);
		status_t result;
		wait_for_thread(fThread, &result);
		fThread = -1;
	}

private:
	static status_t _ThreadEntry(void* cookie)
	{
		((SilentOutput*)cookie)->_Run();
		return B_OK;
	}

	void _Run()
	{
		bigtime_t start = system_time();
		int64 done = 0;
		while (atomic_get(&fQuit) == 0) {
			snooze(20000);
			int64 due = (int64)((system_time() - start) * (double)fFrameRate
				/ 1000000.0);
			while (done < due) {
				int32 count = due - done < kFramesPerBuffer
					? (int32)(due - done) : kFramesPerBuffer;
				fHook(fCookie, fBuffer, count);
				if (fDump != NULL) {
					fwrite(fBuffer, sizeof(float), count * fChannels, fDump);
					fflush(fDump);
				}
				done += count;
			}
		}
	}

	thread_id	fThread;
	int32		fQuit;
	float*		fBuffer;
	FILE*		fDump;
};


}	// namespace


AudioOutput::AudioOutput(float frameRate, int32 channels, fill_hook hook,
	void* cookie)
	:
	fFrameRate(frameRate),
	fChannels(channels),
	fHook(hook),
	fCookie(cookie)
{
}


AudioOutput::~AudioOutput()
{
}


AudioOutput*
AudioOutput::Create(float frameRate, int32 channels, fill_hook hook,
	void* cookie, status_t& error)
{
	const char* mode = getenv("BETUNES_AUDIO");
	if (mode != NULL && strcmp(mode, "silent") == 0) {
		error = B_OK;
		return new SilentOutput(frameRate, channels, hook, cookie);
	}

	SoundPlayerOutput* output = new SoundPlayerOutput(frameRate, channels,
		hook, cookie);
	error = output->InitCheck();
	if (error != B_OK) {
		delete output;
		return NULL;
	}
	return output;
}
