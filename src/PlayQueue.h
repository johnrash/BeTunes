/*
 * The order songs are played in: the list the user started playing from,
 * in display order or shuffled. Does not depend on the Haiku API (unit
 * tested by tests/unit-tests.cpp).
 */
#ifndef PLAY_QUEUE_H
#define PLAY_QUEUE_H


#include <stdint.h>

#include <random>
#include <vector>


class PlayQueue {
public:
								PlayQueue();

	// Starts a new queue. startIndex is the song to play first (an index
	// into songs), or -1 for the first song (a random one when shuffled).
			void				SetSongs(const std::vector<int32_t>& songs,
									int32_t startIndex, bool shuffle);
			void				Clear();

			bool				IsEmpty() const { return fOrder.empty(); }
			int32_t				CountSongs() const
									{ return (int32_t)fOrder.size(); }
	// The song playing (or about to play), -1 if none.
			int32_t				Current() const;
			int32_t				Position() const { return fPosition; }

	// Moves to the next song and returns it. At the end of the queue it
	// wraps around when "wrap" is set (shuffling again), otherwise it goes
	// back to the first song and returns -1.
			int32_t				Next(bool wrap);
	// Moves to the previous song (the last one when wrapping).
			int32_t				Previous(bool wrap);

	// Keeps the current song and reorders the others.
			void				SetShuffle(bool shuffle);
			bool				Shuffle() const { return fShuffle; }

	// Removes a song that is no longer in the library. If it is the current
	// one, the next call of Next() returns the song that followed it.
			void				Remove(int32_t song);

			const std::vector<int32_t>& Order() const { return fOrder; }
			void				SetSeed(uint32_t seed);

private:
			void				_Shuffle(int32_t first);

			std::vector<int32_t> fSongs;
				// in display order
			std::vector<int32_t> fOrder;
				// in play order
			int32_t				fPosition;
				// index into fOrder; -1 before the first song
			bool				fShuffle;
			std::mt19937		fRandom;
};


#endif	// PLAY_QUEUE_H
