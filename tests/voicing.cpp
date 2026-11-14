// Measures how loud every key of both instruments is (K-weighted, like LUFS, over the first 0.6 s of a held
// note at velocity 0.8) and prints the correction table for Synth.cpp (kLuteNorm / kFluteNorm: dB every 6
// semitones from an octave below the lowest key). Run it after changing the voicing; paste the tables.
// Usage: voicing [check]   (check: fail if any of the 37 keys is more than 2 dB off the average)
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
		const int  low = k == 0 ? 36 : 60;
		std::vector<double> db;
		for (int i = 0; i < 37; ++i) {
			db.push_back(Loudness(inst, low + i));
		}
		double avg = 0.0;
		for (double d : db) {
			avg += d;
		}
		avg /= db.size();
		double worst = 0.0;
		for (int i = 0; i < 37; ++i) {
			worst = std::max(worst, std::abs(db[i] - avg));
		}
		std::printf("%s: average %.1f dB, worst key %.1f dB off\n", k == 0 ? "lute" : "flute", avg, worst);
		for (int i = 0; i < 37; i += 3) {
			std::printf("  key %2d (%d): %+.1f\n", i, low + i, db[i] - avg);
		}
		// the correction: minus the deviation, smoothed over +-3 semitones, every 6 semitones
		std::printf("  table:");
		for (int t = 0; t < 11; ++t) {
			const int at = t * 6 - 12;  // relative to the lowest key
			double    sum = 0.0;
			int       n = 0;
			for (int i = std::clamp(at - 3, 0, 36); i <= std::clamp(at + 3, 0, 36); ++i) {
				sum += db[i] - avg;
				++n;
			}
			std::printf(" %.1ff,", -sum / n);
		}
		std::printf("\n");
		bad += worst > 2.0 ? 1 : 0;
	}
	return check && bad ? 1 : 0;
}
