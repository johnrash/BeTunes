/*
 * Where decoded audio goes: the system mixer through a BSoundPlayer, or a
 * silent output that only keeps time (for machines without sound hardware
 * and for tests; selected with the environment variable
 * BETUNES_AUDIO=silent).
 */
#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H


#include <SupportDefs.h>


class AudioOutput {
public:
	// Called from the output's real time thread: fill "frames" frames of
	// interleaved float samples with the output's channel count.
	typedef void (*fill_hook)(void* cookie, float* buffer, int32 frames);

	static	AudioOutput*		Create(float frameRate, int32 channels,
									fill_hook hook, void* cookie,
									status_t& error);
	virtual						~AudioOutput();

	virtual	status_t			Start() = 0;
	virtual	void				Stop() = 0;

			float				FrameRate() const { return fFrameRate; }
			int32				Channels() const { return fChannels; }

protected:
								AudioOutput(float frameRate, int32 channels,
									fill_hook hook, void* cookie);

			float				fFrameRate;
			int32				fChannels;
			fill_hook			fHook;
			void*				fCookie;
};


#endif	// AUDIO_OUTPUT_H
