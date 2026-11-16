// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Songs.h"
#include "Keymap.h"

#include <algorithm>
#include <cmath>
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


	bool ParseVirtualPianoSheet(const std::string& a_text, const std::string& a_title, double a_bpm, Song& a_out, std::string* a_error)
	{
		a_out = {};
		a_out.title = a_title.empty() ? "Virtual Piano Sheet" : a_title;
		a_out.file.clear();
		if (a_text.empty()) {
			if (a_error) *a_error = "The sheet is empty.";
			return false;
		}
		if (!std::isfinite(a_bpm) || a_bpm < 30.0 || a_bpm > 300.0) {
			if (a_error) *a_error = "Tempo must be between 30 and 300 BPM.";
			return false;
		}

		const double beat = 60.0 / a_bpm;
		const double fastStep = beat * 0.18;       // adjacent characters: quick succession
		const double fastestStep = beat * 0.12;    // spaced notes inside [a s d f]: fastest succession
		const double normalGap = beat * 0.52;      // one ordinary space between note groups
		const double barGap = beat * 0.90;         // each vertical bar
		const double lineGap = beat * 1.10;        // a single line break
		const double paragraphGap = beat * 2.0;    // blank line / paragraph break

		struct Group
		{
			double time = 0.0;
			std::vector<int> pitches;
		};
		std::vector<Group> groups;
		groups.reserve((std::min)(a_text.size(), std::size_t{8192}));
		double time = 0.0;
		auto addGroup = [&](std::vector<int> pitches) -> bool {
			if (pitches.empty()) return true;
			std::sort(pitches.begin(), pitches.end());
			pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
			groups.push_back({ time, std::move(pitches) });
			return groups.size() <= 16384;
		};
		auto pitchOf = [&](char c, std::size_t at) -> int {
			const int pitch = keymap::PianoSheetPitch(c);
			if (pitch < 0 && a_error) {
				*a_error = std::string("Unsupported sheet character '") + c + "' at position " + std::to_string(at + 1) + ".";
			}
			return pitch;
		};

		std::size_t i = 0;
		while (i < a_text.size()) {
			const char c = a_text[i];
			if (c == '\r') {
				++i;
				continue;
			}
			if (c == '\n') {
				std::size_t breaks = 0;
				while (i < a_text.size() && (a_text[i] == '\n' || a_text[i] == '\r')) {
					if (a_text[i] == '\n') ++breaks;
					++i;
				}
				time += breaks >= 2 ? paragraphGap : lineGap;
				continue;
			}

			if (c == ' ' || c == '\t') {
				std::size_t spaces = 0;
				while (i < a_text.size() && (a_text[i] == ' ' || a_text[i] == '\t')) {
					++spaces;
					++i;
				}
				if (i < a_text.size() && a_text[i] == '|') {
					// Spaces on either side of bars lengthen the pause, as on Virtual Piano.
					std::size_t bars = 0, barSpaces = spaces;
					while (i < a_text.size() && a_text[i] == '|') {
						++bars;
						++i;
						std::size_t between = 0;
						while (i < a_text.size() && (a_text[i] == ' ' || a_text[i] == '\t')) {
							++between;
							++i;
						}
						barSpaces += between;
				}
					time += bars * barGap + barSpaces * normalGap * 0.5;
				} else if (i < a_text.size()) {
					time += normalGap * (1.0 + 0.35 * static_cast<double>((std::min)(spaces, std::size_t{3}) - 1));
				}
				continue;
			}

			if (c == '|') {
				std::size_t bars = 0, spaces = 0;
				while (i < a_text.size()) {
					if (a_text[i] == '|') {
						++bars;
						++i;
					} else if (a_text[i] == ' ' || a_text[i] == '\t') {
						++spaces;
						++i;
					} else {
						break;
					}
				}
				time += bars * barGap + spaces * normalGap * 0.5;
				continue;
			}

			if (c == '[') {
				const std::size_t openAt = i++;
				std::vector<int> pitches;
				bool hasInnerSpaces = false;
				bool closed = false;
				while (i < a_text.size()) {
					const char inner = a_text[i];
					if (inner == ']') {
						closed = true;
						++i;
						break;
					}
					if (inner == ' ' || inner == '\t') {
						hasInnerSpaces = true;
						++i;
						continue;
					}
					if (inner == '\n' || inner == '\r' || inner == '[' || inner == '|') {
						if (a_error) *a_error = "Unclosed or malformed chord beginning at position " + std::to_string(openAt + 1) + ".";
						return false;
					}
					const int pitch = pitchOf(inner, i);
					if (pitch < 0) return false;
					pitches.push_back(pitch);
					++i;
				}
				if (!closed || pitches.empty()) {
					if (a_error) *a_error = "Empty or unclosed chord beginning at position " + std::to_string(openAt + 1) + ".";
					return false;
				}
				if (hasInnerSpaces) {
					for (std::size_t n = 0; n < pitches.size(); ++n) {
						if (!addGroup({ pitches[n] })) {
							if (a_error) *a_error = "The sheet contains too many notes (limit 16,384).";
							return false;
						}
						if (n + 1 < pitches.size()) time += fastestStep;
					}
				} else if (!addGroup(std::move(pitches))) {
					if (a_error) *a_error = "The sheet contains too many notes (limit 16,384).";
					return false;
				}
				continue;
			}

			const int pitch = pitchOf(c, i);
			if (pitch < 0) return false;
			std::vector<int> run{ pitch };
			++i;
			while (i < a_text.size() && keymap::IsPianoSheetKey(a_text[i])) {
				const int next = pitchOf(a_text[i], i);
				if (next < 0) return false;
				run.push_back(next);
				++i;
			}
			for (std::size_t n = 0; n < run.size(); ++n) {
				if (!addGroup({ run[n] })) {
					if (a_error) *a_error = "The sheet contains too many notes (limit 16,384).";
					return false;
				}
				if (n + 1 < run.size()) time += fastStep;
			}
		}

		if (groups.empty()) {
			if (a_error) *a_error = "No playable piano keys were found in the sheet.";
			return false;
		}
		if (groups.size() > 16384 || time > 600.0) {
			if (a_error) *a_error = "The sheet is too long (maximum 10 minutes / 16,384 notes).";
			return false;
		}

		for (std::size_t n = 0; n < groups.size(); ++n) {
			const double nextTime = n + 1 < groups.size() ? groups[n + 1].time : groups[n].time + 0.36;
			const double duration = std::clamp((nextTime - groups[n].time) * 0.88, 0.10, 0.65);
			for (const int pitch : groups[n].pitches) {
				Note note;
				note.start = groups[n].time;
				note.dur = duration;
				note.pitch = pitch;
				note.vel = 92;
				a_out.all.push_back(note);
			}
		}
		std::stable_sort(a_out.all.begin(), a_out.all.end(), [](const Note& a, const Note& b) {
			return a.start < b.start || (a.start == b.start && a.pitch < b.pitch);
		});
		a_out.melody = a_out.all;
		a_out.length = 0.0;
		for (const auto& note : a_out.all) {
			a_out.length = (std::max)(a_out.length, note.start + note.dur);
		}
		return true;
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

	std::vector<Note> Simplify(const Song& a_song)
	{
		std::vector<Note> melody = a_song.melody;
		std::sort(melody.begin(), melody.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
		// the melody's pitch at a moment (the note sounding then, or the next one)
		auto melodyAt = [&](double t) {
			auto it = std::lower_bound(melody.begin(), melody.end(), t, [](const Note& n, double v) { return n.start + n.dur < v; });
			return it == melody.end() ? (melody.empty() ? 127 : melody.back().pitch) : it->pitch;
		};
		std::vector<Note> all = a_song.all;
		std::sort(all.begin(), all.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
		std::vector<Note> bass;
		for (std::size_t i = 0; i < all.size();) {
			// notes struck together (within 40 ms): the lowest is the bass
			std::size_t j = i;
			const Note* low = &all[i];
			while (j < all.size() && all[j].start - all[i].start < 0.04) {
				if (all[j].pitch < low->pitch) {
					low = &all[j];
				}
				++j;
			}
			i = j;
			if (low->pitch >= 64 || low->pitch > melodyAt(low->start) - 5) {
				continue;  // no room under the tune
			}
			if (!bass.empty()) {
				const double gap = low->start - bass.back().start;
				if (gap < 0.25 || (gap < 0.45 && low->pitch == bass.back().pitch)) {
					continue;
				}
			}
			Note n = *low;
			n.vel = static_cast<int>(n.vel * 0.85);
			bass.push_back(n);
		}
		for (std::size_t i = 0; i < bass.size(); ++i) {
			const double next = i + 1 < bass.size() ? bass[i + 1].start : bass[i].start + 2.0;
			bass[i].dur = std::clamp(bass[i].dur, 0.3, (std::min)(2.0, next - bass[i].start));
		}
		std::vector<Note> out = melody;
		out.insert(out.end(), bass.begin(), bass.end());
		std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
		return out;
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
