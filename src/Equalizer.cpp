/*
 * The graphic equalizer.
 *
 * Every band is a peaking filter from Robert Bristow-Johnson's "Audio EQ
 * Cookbook", one octave wide, computed in double precision (the 32 Hz band
 * has its poles close to the unit circle).
 */
#include "Equalizer.h"

#include <math.h>
#include <string.h>


const float kEqualizerFrequencies[kEqualizerBands] = {
	32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};

const char* const kEqualizerBandLabels[kEqualizerBands] = {
	"32", "64", "125", "250", "500", "1K", "2K", "4K", "8K", "16K"
};


namespace {


// The quality factor of a peaking filter one octave wide.
const double kBandQ = 1.41421356;

// Presets in the spirit of the ones iTunes came with.
const EqualizerPreset kPresets[] = {
	{ "Acoustic", 0, { 5, 5, 4, 1, 1.5, 1.5, 3.5, 4, 3.5, 2 } },
	{ "Bass Booster", 0, { 5.5, 4.5, 4, 2.5, 1, 0, 0, 0, 0, 0 } },
	{ "Bass Reducer", 0, { -5.5, -4.5, -4, -2.5, -1, 0, 0, 0, 0, 0 } },
	{ "Classical", 0, { 5, 4, 3, 2.5, -1.5, -1.5, 0, 2, 3, 3.5 } },
	{ "Dance", 0, { 3.5, 6.5, 5, 0, 2, 3.5, 5, 4.5, 3.5, 0 } },
	{ "Deep", 0, { 5, 3.5, 1.5, 1, 3, 2.5, 1.5, -2, -3.5, -4.5 } },
	{ "Electronic", 0, { 4.5, 4, 1.5, 0, -2, 2, 1, 1.5, 4, 5 } },
	{ "Flat", 0, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
	{ "Hip-Hop", 0, { 5, 4, 1.5, 3, -1, -1, 1.5, -0.5, 2, 3 } },
	{ "Jazz", 0, { 4, 3, 1.5, 2, -1.5, -1.5, 0, 1.5, 3, 3.5 } },
	{ "Latin", 0, { 4.5, 3, 0, 0, -1.5, -1.5, -1.5, 0, 3, 4.5 } },
	{ "Loudness", 0, { 6, 4, 0, 0, -2, 0, -1, -5, 5, 1 } },
	{ "Lounge", 0, { -3, -1.5, -0.5, 1.5, 4, 2.5, 0, -1.5, 2, 1 } },
	{ "Piano", 0, { 3, 2, 0, 2.5, 3, 1.5, 3.5, 4.5, 3, 3.5 } },
	{ "Pop", 0, { -1.5, -1, 0, 2, 4, 4, 2, 0, -1, -1.5 } },
	{ "R&B", 0, { 2.5, 7, 5.5, 1.5, -2.5, -1.5, 2.5, 3, 3, 4 } },
	{ "Rock", 0, { 5, 4, 3, 1.5, -0.5, -1, 0.5, 2.5, 3.5, 4.5 } },
	{ "Small Speakers", 0, { 5.5, 4, 3.5, 2.5, 1, 0, -1, -2, -3.5, -4 } },
	{ "Spoken Word", 0, { -3.5, -0.5, 0, 0.5, 3.5, 4.5, 5, 4.5, 2.5, 0 } },
	{ "Treble Booster", 0, { 0, 0, 0, 0, 0, 1, 2.5, 3.5, 4.5, 5.5 } },
	{ "Treble Reducer", 0, { 0, 0, 0, 0, 0, -1, -2.5, -3.5, -4.5, -5.5 } },
	{ "Vocal Booster", 0, { -1.5, -3, -3, 1.5, 4, 4, 3, 1.5, 0, -1.5 } }
};


float
clamp_gain(float gain)
{
	if (gain != gain)
		return 0;
	if (gain > kEqualizerMaxGain)
		return kEqualizerMaxGain;
	if (gain < -kEqualizerMaxGain)
		return -kEqualizerMaxGain;
	return gain;
}


}	// namespace


EqualizerSettings::EqualizerSettings()
	:
	enabled(false),
	preamp(0)
{
	for (int i = 0; i < kEqualizerBands; i++)
		bands[i] = 0;
}


bool
EqualizerSettings::IsActive() const
{
	if (!enabled)
		return false;
	if (preamp != 0)
		return true;
	for (int i = 0; i < kEqualizerBands; i++) {
		if (bands[i] != 0)
			return true;
	}
	return false;
}


bool
EqualizerSettings::operator==(const EqualizerSettings& other) const
{
	if (enabled != other.enabled || preamp != other.preamp)
		return false;
	for (int i = 0; i < kEqualizerBands; i++) {
		if (bands[i] != other.bands[i])
			return false;
	}
	return true;
}


int
count_equalizer_presets()
{
	return (int)(sizeof(kPresets) / sizeof(kPresets[0]));
}


const EqualizerPreset&
equalizer_preset(int index)
{
	if (index < 0 || index >= count_equalizer_presets())
		index = 0;
	return kPresets[index];
}


const EqualizerPreset*
find_equalizer_preset(const char* name)
{
	if (name == NULL)
		return NULL;
	for (int i = 0; i < count_equalizer_presets(); i++) {
		if (strcmp(kPresets[i].name, name) == 0)
			return &kPresets[i];
	}
	return NULL;
}


Equalizer::Equalizer()
	:
	fFrameRate(44100),
	fChannels(2),
	fPreampGain(1.0),
	fActiveCount(0)
{
	_Update();
	Reset();
}


void
Equalizer::SetFormat(float frameRate, int channels)
{
	if (frameRate <= 0 || channels <= 0)
		return;
	fFrameRate = frameRate;
	fChannels = channels;
	_Update();
	Reset();
}


void
Equalizer::SetSettings(const EqualizerSettings& settings)
{
	bool wasActive = fSettings.IsActive();
	fSettings = settings;
	fSettings.preamp = clamp_gain(fSettings.preamp);
	for (int i = 0; i < kEqualizerBands; i++)
		fSettings.bands[i] = clamp_gain(fSettings.bands[i]);
	_Update();
	if (!wasActive)
		Reset();
		// no stale state from long ago when switching on
}


void
Equalizer::Reset()
{
	fState.assign((size_t)fChannels * kEqualizerBands * 4, 0.0);
}


void
Equalizer::_Update()
{
	bool wasActive[kEqualizerBands] = {};
	for (int i = 0; i < fActiveCount; i++)
		wasActive[fActiveBands[i]] = true;

	fPreampGain = pow(10.0, fSettings.preamp / 20.0);
	fActiveCount = 0;

	for (int band = 0; band < kEqualizerBands; band++) {
		double gain = fSettings.bands[band];
		double frequency = kEqualizerFrequencies[band];
		// a band at or above the Nyquist frequency cannot be filtered
		if (gain == 0 || frequency >= fFrameRate * 0.45)
			continue;

		double a = pow(10.0, gain / 40.0);
		double w0 = 2 * M_PI * frequency / fFrameRate;
		double alpha = sin(w0) / (2 * kBandQ);
		double cosW0 = cos(w0);
		double a0 = 1 + alpha / a;

		Coefficients& filter = fFilters[band];
		filter.b0 = (1 + alpha * a) / a0;
		filter.b1 = -2 * cosW0 / a0;
		filter.b2 = (1 - alpha * a) / a0;
		filter.a1 = -2 * cosW0 / a0;
		filter.a2 = (1 - alpha / a) / a0;
		fActiveBands[fActiveCount++] = band;

		// a band that was off starts from silence, not from old state
		if (!wasActive[band]
			&& fState.size() == (size_t)fChannels * kEqualizerBands * 4) {
			for (int channel = 0; channel < fChannels; channel++) {
				double* s = &fState[((size_t)channel * kEqualizerBands
					+ band) * 4];
				s[0] = s[1] = s[2] = s[3] = 0;
			}
		}
	}
}


void
Equalizer::Process(float* samples, int frames)
{
	if (!fSettings.IsActive())
		return;

	for (int frame = 0; frame < frames; frame++) {
		for (int channel = 0; channel < fChannels; channel++) {
			double x = samples[frame * fChannels + channel] * fPreampGain;
			double* state = &fState[(size_t)channel * kEqualizerBands * 4];
			for (int i = 0; i < fActiveCount; i++) {
				int band = fActiveBands[i];
				const Coefficients& f = fFilters[band];
				double* s = state + band * 4;
				double y = f.b0 * x + f.b1 * s[0] + f.b2 * s[1]
					- f.a1 * s[2] - f.a2 * s[3];
				if (fabs(y) < 1e-20)
					y = 0;
					// no denormals in the tail after a song
				s[1] = s[0];
				s[0] = x;
				s[3] = s[2];
				s[2] = y;
				x = y;
			}
			samples[frame * fChannels + channel] = (float)x;
		}
	}
}
