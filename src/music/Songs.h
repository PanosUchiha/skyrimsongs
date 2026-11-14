// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include "Midi.h"

#include <string>
#include <vector>

namespace music
{
	struct Song
	{
		std::string       title;
		std::string       file;        // the .mid it came from; empty for the built-in tunes
		int               difficulty = 0;  // 1 easy .. 5 hard (built-ins), 0 unknown
		std::vector<Note> all;         // everything, for "let the music play"
		std::vector<Note> melody;      // one note at a time, to play along with
		double            length = 0.0;
	};

	// The tunes every bard knows: old folk tunes and hymns, long out of copyright.
	std::vector<Song> BuiltInSongs();

	// The notation the built-in tunes are written in: "C4:1 D4:.5 r:1 F#4:1.5 ...": note (or r for a rest), then
	// its length in beats. "|" bar lines are ignored. false if a token can't be read.
	bool ParseNotation(const std::string& a_text, double a_bpm, std::vector<Note>& a_out, std::string* a_error = nullptr);

	// A song from a MIDI file's bytes
	bool SongFromMidi(const std::vector<std::uint8_t>& a_data, const std::string& a_title, Song& a_out, std::string* a_error = nullptr);

	// How many semitones (whole octaves) to move a melody by so that as much of it as possible falls in
	// [a_low, a_low + a_span]
	int FitOctaves(const std::vector<Note>& a_melody, int a_low, int a_span);

	std::string NoteName(int a_pitch);  // "C#4"
}
