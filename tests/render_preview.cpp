// Renders the instruments to WAV files, to hear the synth outside the game:
//   build/preview_lute.wav   Greensleeves on the lute
//   build/preview_flute.wav  Amazing Grace on the flute
//   build/preview_tip.wav    a patron's coin
// Usage: render_preview <out dir> [song.mid ...] [--flute-samples <dir>]   (each .mid also rendered on both
// instruments, as "Play" does; with sampled flute notes from <dir>/<midi note>.wav if given)
#include "../src/music/Songs.h"
#include "../src/music/Synth.h"
#include "../src/music/Wav.h"

#include <filesystem>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	void WriteWav(const std::string& a_path, const std::vector<float>& a_pcm, int a_rate)
	{
		std::ofstream f(a_path, std::ios::binary);
		auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
		auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
		const std::uint32_t bytes = static_cast<std::uint32_t>(a_pcm.size() * 2);
		f.write("RIFF", 4);
		u32(36 + bytes);
		f.write("WAVEfmt ", 8);
		u32(16);
		u16(1);
		u16(1);
		u32(static_cast<std::uint32_t>(a_rate));
		u32(static_cast<std::uint32_t>(a_rate * 2));
		u16(2);
		u16(16);
		f.write("data", 4);
		u32(bytes);
		for (float s : a_pcm) {
			const auto v = static_cast<std::int16_t>(std::max(-1.0f, std::min(1.0f, s)) * 32000.0f);
			u16(static_cast<std::uint16_t>(v));
		}
	}

	std::vector<music::Synth::Sample> g_fluteSamples;

	std::vector<float> Play(music::Instrument a_inst, const std::vector<music::Note>& a_notes, int a_shift, double a_tail, bool a_live = true)
	{
		constexpr int      kRate = 48000, kBlock = 256;
		music::Synth       s(static_cast<float>(kRate));
		s.SetSamples(music::Instrument::Flute, g_fluteSamples);
		std::vector<float> out;
		double             end = 0.0;
		for (const auto& n : a_notes) {
			end = std::max(end, n.start + n.dur);
		}
		struct Ev
		{
			double t;
			bool   on;
			int    pitch;
			float  vel;
		};
		std::vector<Ev> ev;
		for (const auto& n : a_notes) {
			ev.push_back({ n.start, true, n.pitch + a_shift, n.vel / 127.0f });
			ev.push_back({ n.start + n.dur, false, n.pitch + a_shift, 0.0f });
		}
		std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && !a.on && b.on); });
		std::size_t next = 0;
		float       buf[kBlock];
		for (double t = 0.0; t < end + a_tail; t += static_cast<double>(kBlock) / kRate) {
			while (next < ev.size() && ev[next].t <= t) {
				if (ev[next].on) {
					s.NoteOn(a_inst, ev[next].pitch, ev[next].vel, a_live && a_inst == music::Instrument::Flute);
				} else {
					s.NoteOff(a_inst, ev[next].pitch, a_live && a_inst == music::Instrument::Flute);
				}
				++next;
			}
			s.Render(buf, kBlock);
			out.insert(out.end(), buf, buf + kBlock);
		}
		return out;
	}
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";
	std::vector<std::string> mids;
	for (int a = 2; a < argc; ++a) {
		if (std::string(argv[a]) == "--flute-samples" && a + 1 < argc) {
			for (const auto& e : std::filesystem::directory_iterator(argv[++a])) {
				std::ifstream             f(e.path(), std::ios::binary);
				std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
				music::Synth::Sample      smp;
				try {
					smp.root = std::stoi(e.path().stem().string());
				} catch (...) {
					continue;
				}
				if (music::ReadWav(bytes, 48000, smp.pcm)) {
					g_fluteSamples.push_back(std::move(smp));
				}
			}
			std::printf("%zu flute samples\n", g_fluteSamples.size());
		} else {
			mids.push_back(argv[a]);
		}
	}
	const auto        songs = music::BuiltInSongs();
	for (const auto& s : songs) {
		if (s.title == "Greensleeves") {
			// with a bass note on each bar's first beat, as a lutenist would
			auto notes = s.melody;
			for (const auto& n : s.melody) {
				const double beat = 60.0 / 100.0;
				const double bar = n.start / (3.0 * beat);
				if (std::abs(bar - std::round(bar)) < 1e-6) {
					notes.push_back({ n.start, 3.0 * beat * 0.95, n.pitch % 12 + 45 - (n.pitch % 12 > 9 ? 12 : 0), 70, 0, 0 });
				}
			}
			WriteWav(dir + "/preview_lute.wav", Play(music::Instrument::Lute, notes, -12, 3.0), 48000);
		}
		if (s.title == "Amazing Grace") {
			WriteWav(dir + "/preview_flute.wav", Play(music::Instrument::Flute, s.melody, 12, 2.0), 48000);
		}
	}
	// every one of the 37 keys, low to high, on each instrument
	for (int k = 0; k < 2; ++k) {
		const auto        inst = k == 0 ? music::Instrument::Lute : music::Instrument::Flute;
		const int         low = k == 0 ? 36 : 60;
		std::vector<music::Note> keys;
		for (int i = 0; i < 37; ++i) {
			keys.push_back({ i * 0.32, 0.28, low + i, 100, 0, 0 });
		}
		WriteWav(dir + (k == 0 ? "/preview_lute_37keys.wav" : "/preview_flute_37keys.wav"), Play(inst, keys, 0, 2.0), 48000);
	}
	{
		music::Synth       s;
		std::vector<float> out(48000);
		s.Coin(1.0f);
		s.Render(out.data(), 48000);
		WriteWav(dir + "/preview_tip.wav", out, 48000);
	}
	for (std::size_t a = 0; a < mids.size(); ++a) {
		std::ifstream             f(mids[a], std::ios::binary);
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		music::Song               song;
		std::string               err;
		if (!music::SongFromMidi(bytes, "song", song, &err)) {
			std::printf("%s: %s\n", mids[a].c_str(), err.c_str());
			continue;
		}
		WriteWav(dir + "/song" + std::to_string(a + 1) + "_lute.wav", Play(music::Instrument::Lute, music::FoldIntoRange(song.all), 0, 2.0, false), 48000);
		WriteWav(dir + "/song" + std::to_string(a + 1) + "_flute.wav", Play(music::Instrument::Flute, music::FoldIntoRange(song.all), 0, 2.0, false),
			48000);
	}
	std::printf("wrote preview_lute.wav, preview_flute.wav and preview_tip.wav to %s\n", dir.c_str());
	return 0;
}
