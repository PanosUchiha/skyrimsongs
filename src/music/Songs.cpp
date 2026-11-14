// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Songs.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace music
{
	namespace
	{
		struct Tune
		{
			const char* title;
			int         difficulty;
			double      bpm;
			const char* notes;
		};

		// Public-domain melodies (folk tunes, a hymn and Beethoven), one note at a time.
		constexpr Tune kTunes[] = {
			{ "Ode to Joy", 1, 108.0,
				"E4:1 E4:1 F4:1 G4:1 | G4:1 F4:1 E4:1 D4:1 | C4:1 C4:1 D4:1 E4:1 | E4:1.5 D4:.5 D4:2 |"
				"E4:1 E4:1 F4:1 G4:1 | G4:1 F4:1 E4:1 D4:1 | C4:1 C4:1 D4:1 E4:1 | D4:1.5 C4:.5 C4:2 |"
				"D4:1 D4:1 E4:1 C4:1 | D4:1 E4:.5 F4:.5 E4:1 C4:1 | D4:1 E4:.5 F4:.5 E4:1 D4:1 | C4:1 D4:1 G3:2 |"
				"E4:1 E4:1 F4:1 G4:1 | G4:1 F4:1 E4:1 D4:1 | C4:1 C4:1 D4:1 E4:1 | D4:1.5 C4:.5 C4:2" },
			{ "Amazing Grace", 2, 88.0,
				"D4:1 | G4:2 B4:.5 G4:.5 | B4:2 A4:1 | G4:2 E4:1 | D4:2 D4:1 | G4:2 B4:.5 G4:.5 | B4:2 A4:1 | D5:3 |"
				"D5:2 B4:1 | D5:1.5 B4:.5 G4:1 | B4:2 A4:1 | G4:2 E4:1 | D4:2 D4:1 | G4:2 B4:.5 G4:.5 | B4:2 A4:1 | G4:3" },
			{ "Scarborough Fair", 3, 100.0,
				"D4:2 D4:1 | A4:2 A4:1 | E4:1.5 F4:.5 E4:1 | D4:3 | r:1 A4:1 C5:1 | D5:2 C5:1 | A4:1 B4:1 G4:1 | A4:3 |"
				"r:1 D5:1 D5:1 | D5:2 C5:1 | A4:2 A4:1 | G4:1 F4:1 E4:1 | C4:3 | D4:2 A4:1 | G4:2 F4:1 | E4:1 D4:1 C4:1 | D4:3" },
			{ "Drunken Sailor", 4, 116.0,
				"A4:.5 A4:.25 A4:.25 A4:.5 A4:.25 A4:.25 | A4:.5 D4:.5 F4:.5 A4:.5 | G4:.5 G4:.25 G4:.25 G4:.5 G4:.25 G4:.25 |"
				"G4:.5 C4:.5 E4:.5 G4:.5 | A4:.5 A4:.25 A4:.25 A4:.5 A4:.25 A4:.25 | A4:.5 B4:.5 C5:.5 D5:.5 |"
				"C5:.5 A4:.5 G4:.5 E4:.5 | D4:1 D4:1 |"
				"A4:1.5 A4:.5 | A4:.5 D4:.5 F4:.5 A4:.5 | G4:1.5 G4:.5 | G4:.5 C4:.5 E4:.5 G4:.5 | A4:1.5 A4:.5 |"
				"A4:.5 B4:.5 C5:.5 D5:.5 | C5:.5 A4:.5 G4:.5 E4:.5 | D4:1 D4:1" },
			{ "Greensleeves", 5, 100.0,
				"A4:1 | C5:2 D5:1 | E5:1.5 F5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:2 A4:1 | A4:1.5 G#4:.5 A4:1 |"
				"B4:2 G#4:1 | E4:2 A4:1 | C5:2 D5:1 | E5:1.5 F5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:1.5 B4:.5 A4:1 |"
				"G#4:1.5 F#4:.5 G#4:1 | A4:3 | r:3 | G5:3 | G5:1.5 F#5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:2 A4:1 |"
				"A4:1.5 G#4:.5 A4:1 | B4:2 G#4:1 | E4:3 | G5:3 | G5:1.5 F#5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 |"
				"C5:1.5 B4:.5 A4:1 | G#4:1.5 F#4:.5 G#4:1 | A4:3" },
		};

		int ParsePitch(const std::string& a_s)
		{
			if (a_s.empty()) {
				return -1;
			}
			static constexpr int kBase[7] = { 9, 11, 0, 2, 4, 5, 7 };  // A B C D E F G
			const char           c = static_cast<char>(std::toupper(static_cast<unsigned char>(a_s[0])));
			if (c < 'A' || c > 'G') {
				return -1;
			}
			int         pc = kBase[c - 'A'];
			std::size_t i = 1;
			while (i < a_s.size() && (a_s[i] == '#' || a_s[i] == 'b')) {
				pc += a_s[i] == '#' ? 1 : -1;
				++i;
			}
			if (i >= a_s.size()) {
				return -1;
			}
			int oct = 0;
			try {
				oct = std::stoi(a_s.substr(i));
			} catch (...) {
				return -1;
			}
			return (oct + 1) * 12 + pc;
		}
	}

	std::string NoteName(int a_pitch)
	{
		static constexpr const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
		return std::string(kNames[((a_pitch % 12) + 12) % 12]) + std::to_string(a_pitch / 12 - 1);
	}

	bool ParseNotation(const std::string& a_text, double a_bpm, std::vector<Note>& a_out, std::string* a_error)
	{
		a_out.clear();
		const double       beat = 60.0 / a_bpm;
		double             t = 0.0;
		std::string text = a_text;
		std::replace(text.begin(), text.end(), '|', ' ');
		std::istringstream in(text);
		std::string        tok;
		while (in >> tok) {
			const auto colon = tok.find(':');
			if (colon == std::string::npos) {
				if (a_error) {
					*a_error = "no length: " + tok;
				}
				return false;
			}
			double len = 0.0;
			try {
				len = std::stod(tok.substr(colon + 1));
			} catch (...) {
				len = 0.0;
			}
			if (len <= 0.0) {
				if (a_error) {
					*a_error = "bad length: " + tok;
				}
				return false;
			}
			const std::string head = tok.substr(0, colon);
			if (head != "r") {
				const int p = ParsePitch(head);
				if (p < 0) {
					if (a_error) {
						*a_error = "bad note: " + tok;
					}
					return false;
				}
				Note n;
				n.start = t;
				n.dur = len * beat * 0.92;
				n.pitch = p;
				n.vel = 96;
				a_out.push_back(n);
			}
			t += len * beat;
		}
		return !a_out.empty();
	}

	std::vector<Song> BuiltInSongs()
	{
		std::vector<Song> out;
		for (const auto& tune : kTunes) {
			Song s;
			s.title = tune.title;
			s.difficulty = tune.difficulty;
			if (!ParseNotation(tune.notes, tune.bpm, s.melody)) {
				continue;
			}
			s.all = s.melody;
			s.length = s.melody.back().start + s.melody.back().dur;
			out.push_back(std::move(s));
		}
		return out;
	}

	bool SongFromMidi(const std::vector<std::uint8_t>& a_data, const std::string& a_title, Song& a_out, std::string* a_error)
	{
		MidiFile f;
		if (!ParseMidi(a_data, f, a_error)) {
			return false;
		}
		a_out = {};
		a_out.title = a_title;
		a_out.all = std::move(f.notes);
		a_out.length = f.length;
		MidiFile tmp;
		tmp.notes = a_out.all;
		a_out.melody = ExtractMelody(tmp);
		if (a_out.melody.empty()) {
			if (a_error) {
				*a_error = "no melody found";
			}
			return false;
		}
		return true;
	}

	std::vector<Note> FoldIntoRange(const std::vector<Note>& a_notes, int a_low, int a_high)
	{
		std::vector<Note> out;
		out.reserve(a_notes.size());
		for (auto n : a_notes) {
			while (n.pitch < a_low) {
				n.pitch += 12;
			}
			while (n.pitch > a_high) {
				n.pitch -= 12;
			}
			bool doubled = false;
			for (std::size_t i = out.size(); i-- > 0 && out.size() - i <= 32;) {  // the notes just before (sorted by start)
				if (out[i].pitch == n.pitch && std::abs(out[i].start - n.start) < 0.02) {
					doubled = true;
					break;
				}
			}
			if (!doubled) {
				out.push_back(n);
			}
		}
		return out;
	}

	int FitOctaves(const std::vector<Note>& a_melody, int a_low, int a_span)
	{
		int bestShift = 0, bestIn = -1;
		for (int oct = -4; oct <= 4; ++oct) {
			const int shift = oct * 12;
			int       in = 0;
			for (const auto& n : a_melody) {
				const int p = n.pitch + shift;
				in += (p >= a_low && p <= a_low + a_span) ? 1 : 0;
			}
			// ties: the smaller move
			if (in > bestIn || (in == bestIn && std::abs(shift) < std::abs(bestShift))) {
				bestIn = in;
				bestShift = shift;
			}
		}
		return bestShift;
	}
}
