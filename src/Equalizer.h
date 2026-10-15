/*
 * The 10 band graphic equalizer, like the one in iTunes: a preamp and ten
 * peaking filters an octave apart, from 32 Hz to 16 kHz, each -12..+12 dB.
 * Does not depend on the Haiku API (unit tested by tests/unit-tests.cpp).
 */
#ifndef EQUALIZER_H
#define EQUALIZER_H


#include <vector>


const int kEqualizerBands = 10;
const float kEqualizerMaxGain = 12.0f;
	// dB, for the preamp and every band

extern const float kEqualizerFrequencies[kEqualizerBands];
extern const char* const kEqualizerBandLabels[kEqualizerBands];
	// "32", "64", ... "16K"


struct EqualizerSettings {
								EqualizerSettings();

	// Does it change the sound (enabled, and not everything at 0 dB)?
			bool				IsActive() const;
			bool				operator==(const EqualizerSettings& other)
									const;

			bool				enabled;
			float				preamp;
			float				bands[kEqualizerBands];
				// dB
};


struct EqualizerPreset {
			const char*			name;
			float				preamp;
			float				bands[kEqualizerBands];
};

// The built-in presets, in menu (alphabetical) order.
int count_equalizer_presets();
const EqualizerPreset& equalizer_preset(int index);
// NULL if there is no built-in preset of that name.
const EqualizerPreset* find_equalizer_preset(const char* name);


class Equalizer {
public:
								Equalizer();

	// Resets the filters.
			void				SetFormat(float frameRate, int channels);
			void				SetSettings(const EqualizerSettings& settings);
			const EqualizerSettings& Settings() const { return fSettings; }

	// Filters interleaved samples in place (nothing happens unless the
	// settings are active).
			void				Process(float* samples, int frames);
			void				Reset();

private:
			struct Coefficients {
				double			b0;
				double			b1;
				double			b2;
				double			a1;
				double			a2;
			};

			void				_Update();

			EqualizerSettings	fSettings;
			float				fFrameRate;
			int					fChannels;
			double				fPreampGain;
			Coefficients		fFilters[kEqualizerBands];
			int					fActiveBands[kEqualizerBands];
			int					fActiveCount;
			std::vector<double>	fState;
				// per channel and band: x1, x2, y1, y2
};


#endif	// EQUALIZER_H
