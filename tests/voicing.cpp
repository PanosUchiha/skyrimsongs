// Measures how loud every note of both instruments is, C2-A7 (every note a song can play; the 37 keys are part of
// it), K-weighted like LUFS over the first 0.6 s of a held note at velocity 0.8, and prints the corrections for
// Synth.cpp's kLuteNorm / kFluteNorm (dB at MIDI 24, 30 ... 108). Run it after changing the voicing; add them.
// Usage: voicing [check]   (check: fail if any note is more than 2.5 dB off the average)
#include "../src/music/Synth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
	struct Biquad
	{
		double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
		double Run(double x)
		{
			const double y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};

	double Loudness(music::Instrument a_inst, int a_pitch)
	{
		music::Synth       s(48000.0f);
		std::vector<float> b(28800);
		s.SetReverb(0.0f);
		s.NoteOn(a_inst, a_pitch, 0.8f, true);
		s.Render(b.data(), static_cast<int>(b.size()));
		Biquad shelf{ 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
		Biquad hp{ 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
		double e = 0.0;
		for (float x : b) {
			const double y = hp.Run(shelf.Run(x));
			e += y * y;
		}
		return 10.0 * std::log10(e / b.size() + 1e-15);
	}
}

int main(int argc, char** argv)
{
	const bool check = argc > 1 && std::strcmp(argv[1], "check") == 0;
	int        bad = 0;
	for (int k = 0; k < 2; ++k) {
		const auto inst = k == 0 ? music::Instrument::Lute : music::Instrument::Flute;
		// both play C2-A7; the flute below C4 is deliberately soft (a big low flute), so it is levelled from C4 up
		const int kLow = k == 0 ? 36 : 60, kHigh = 105, kN = kHigh - kLow + 1;
		std::vector<double> db;
		for (int i = 0; i < kN; ++i) {
			db.push_back(Loudness(inst, kLow + i));
		}
		double avg = 0.0;
		for (double d : db) {
			avg += d;
		}
		avg /= db.size();
		double worst = 0.0;
		for (int i = 0; i < kN; ++i) {
			worst = std::max(worst, std::abs(db[i] - avg));
		}
		std::printf("%s: average %.1f dB, worst key %.1f dB off\n", k == 0 ? "lute" : "flute", avg, worst);
		for (int i = 0; i < kN; i += 6) {
			std::printf("  note %d: %+.1f\n", kLow + i, db[i] - avg);
		}
		// the correction: minus the deviation, smoothed over +-3 semitones, every 6 semitones
		std::printf("  table:");
		for (int t = 0; t < 15; ++t) {
			const int at = 24 + t * 6 - kLow;  // relative to the lowest note
			double    sum = 0.0;
			int       n = 0;
			for (int i = std::clamp(at - 3, 0, kN - 1); i <= std::clamp(at + 3, 0, kN - 1); ++i) {
				sum += db[i] - avg;
				++n;
			}
			std::printf(" %.1ff,", -sum / n);
		}
		std::printf("\n");
		bad += worst > 2.5 ? 1 : 0;
	}
	return check && bad ? 1 : 0;
}
