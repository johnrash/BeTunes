/*
 * The order songs are played in.
 */
#include "PlayQueue.h"

#include <algorithm>
#include <chrono>


PlayQueue::PlayQueue()
	:
	fPosition(-1),
	fShuffle(false),
	fRandom((uint32_t)std::chrono::steady_clock::now().time_since_epoch()
		.count())
{
}


void
PlayQueue::SetSeed(uint32_t seed)
{
	fRandom.seed(seed);
}


void
PlayQueue::SetSongs(const std::vector<int32_t>& songs, int32_t startIndex,
	bool shuffle)
{
	fSongs = songs;
	fShuffle = shuffle;
	fPosition = -1;
	if (fSongs.empty()) {
		fOrder.clear();
		return;
	}
	if (startIndex >= (int32_t)fSongs.size())
		startIndex = -1;

	if (shuffle) {
		if (startIndex < 0) {
			std::uniform_int_distribution<int32_t> pick(0,
				(int32_t)fSongs.size() - 1);
			startIndex = pick(fRandom);
		}
		fOrder = fSongs;
		std::swap(fOrder[0], fOrder[startIndex]);
		_Shuffle(1);
		fPosition = 0;
	} else {
		fOrder = fSongs;
		fPosition = startIndex < 0 ? 0 : startIndex;
	}
}


void
PlayQueue::Clear()
{
	fSongs.clear();
	fOrder.clear();
	fPosition = -1;
}


int32_t
PlayQueue::Current() const
{
	if (fPosition < 0 || fPosition >= (int32_t)fOrder.size())
		return -1;
	return fOrder[fPosition];
}


int32_t
PlayQueue::Next(bool wrap)
{
	if (fOrder.empty())
		return -1;

	if (fPosition + 1 < (int32_t)fOrder.size()) {
		fPosition++;
		return fOrder[fPosition];
	}

	if (fShuffle && fOrder.size() > 1) {
		// a new random order that does not repeat the song just played
		int32_t last = fOrder.back();
		_Shuffle(0);
		if (fOrder[0] == last)
			std::swap(fOrder[0], fOrder[fOrder.size() - 1]);
	}
	fPosition = 0;
	return wrap ? fOrder[0] : -1;
}


int32_t
PlayQueue::Previous(bool wrap)
{
	if (fOrder.empty())
		return -1;

	if (fPosition > 0)
		fPosition--;
	else
		fPosition = wrap ? (int32_t)fOrder.size() - 1 : 0;
	if (fPosition >= (int32_t)fOrder.size())
		fPosition = (int32_t)fOrder.size() - 1;
	return fOrder[fPosition];
}


void
PlayQueue::SetShuffle(bool shuffle)
{
	if (shuffle == fShuffle)
		return;
	fShuffle = shuffle;
	if (fOrder.empty())
		return;

	int32_t current = Current();
	if (shuffle) {
		fOrder = fSongs;
		if (current >= 0) {
			std::vector<int32_t>::iterator found
				= std::find(fOrder.begin(), fOrder.end(), current);
			if (found != fOrder.end())
				std::iter_swap(fOrder.begin(), found);
			_Shuffle(1);
			fPosition = 0;
		} else {
			_Shuffle(0);
			fPosition = -1;
		}
	} else {
		fOrder = fSongs;
		fPosition = -1;
		if (current >= 0) {
			std::vector<int32_t>::iterator found
				= std::find(fOrder.begin(), fOrder.end(), current);
			if (found != fOrder.end())
				fPosition = (int32_t)(found - fOrder.begin());
		}
	}
}


void
PlayQueue::Remove(int32_t song)
{
	fSongs.erase(std::remove(fSongs.begin(), fSongs.end(), song),
		fSongs.end());

	std::vector<int32_t> order;
	int32_t position = -1;
	for (int32_t i = 0; i < (int32_t)fOrder.size(); i++) {
		if (fOrder[i] != song)
			order.push_back(fOrder[i]);
		if (i == fPosition) {
			// the song before the next one stays "current"
			position = (int32_t)order.size() - 1;
		}
	}
	fOrder.swap(order);
	fPosition = position;
	if (fOrder.empty())
		fPosition = -1;
}


void
PlayQueue::_Shuffle(int32_t first)
{
	if ((int32_t)fOrder.size() - first > 1)
		std::shuffle(fOrder.begin() + first, fOrder.end(), fRandom);
}
