/*
 * Plays one song at a time. A decoder thread opens files and decodes them
 * into a ring buffer that the audio output's real time thread empties; all
 * slow work (opening files, connecting to the mixer) happens there, so the
 * window never waits. The player tells its target when a song started,
 * ended or failed; every notification carries the serial number returned
 * by Play(), so late notifications about an earlier song can be ignored.
 */
#ifndef PLAYER_H
#define PLAYER_H


#include <Locker.h>
#include <Messenger.h>
#include <String.h>

#include <deque>

#include "Equalizer.h"


class AudioOutput;
class BMediaFile;
class BMediaTrack;


class Player {
public:
	enum State {
		STOPPED,
		PLAYING,
		PAUSED
	};

								Player(const BMessenger& target);
								~Player();

	// Returns the serial number of this request.
			int32				Play(const char* path, int32 song);
			void				Stop();
			void				Pause();
			void				Resume();
			void				Seek(bigtime_t time);
	// Linear gain, 0..1
			void				SetVolume(float volume);
	// Applied as the audio goes out, so changes are heard at once.
			void				SetEqualizer(
									const EqualizerSettings& settings);

			State				CurrentState() const { return fState; }
			int32				CurrentSong() const { return fSong; }
			int32				Serial() const { return fSerial; }
			bigtime_t			Position() const;
			bigtime_t			Duration() const;

private:
			enum {
				kCommandPlay,
				kCommandStop,
				kCommandSeek,
				kCommandQuit
			};

			struct Command {
				int32			type;
				BString			path;
				int32			song;
				int32			serial;
				bigtime_t		time;
			};

			void				_PostCommand(const Command& command);
	static	status_t			_ThreadEntry(void* cookie);
			void				_Run();
			void				_Open(const Command& command);
			void				_Close();
			void				_DeleteOutput();
			void				_Seek(bigtime_t time);
			bool				_Decode();
			void				_Notify(uint32 what, int32 serial, int32 song,
									const char* error = NULL,
									bool output = false);

	static	void				_FillHook(void* cookie, float* buffer,
									int32 frames);
			void				_Fill(float* buffer, int32 frames);

			BMessenger			fTarget;

			// window thread
			State				fState;
			int32				fSong;
			int32				fSerial;

			// commands for the decoder thread
			BLocker				fCommandLock;
			std::deque<Command>	fCommands;
			sem_id				fWakeSem;
			thread_id			fThread;

			// decoder thread only
			BMediaFile*			fFile;
			BMediaTrack*		fTrack;
			AudioOutput*		fOutput;
			int32				fOpenSerial;
			int32				fOpenSong;
			uint8*				fDecodeBuffer;
			size_t				fDecodeBufferSize;
			float*				fDecoded;
			size_t				fDecodedCapacity;
			uint32				fDecodedFormat;
			int32				fPendingFrames;
			int32				fPendingOffset;
			int32				fEmptyReads;
			bool				fDecoderDone;
			bool				fEndNotified;

			// shared with the output thread, protected by fBufferLock
	mutable	BLocker				fBufferLock;
			float*				fRing;
			int32				fRingFrames;
			int32				fRingRead;
			int32				fRingFill;
			int32				fChannels;
			float				fFrameRate;
			int64				fFramesPlayed;
			bigtime_t			fDuration;
			float				fVolume;
			Equalizer			fEqualizer;
			bool				fActive;
			bool				fPaused;
};


#endif	// PLAYER_H
