// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Standard MIDI files (format 0 and 1, any tempo map, SMPTE time too), read into timed notes.
namespace music
{
	struct Note
	{
		double start = 0.0;  // seconds
		double dur = 0.0;
		int    pitch = 60;
		int    vel = 100;
		int    channel = 0;
		int    track = 0;
	};

	struct MidiFile
	{
		std::vector<Note>        notes;       // every note, drums (channel 10) left out, sorted by start
		std::vector<std::string> trackNames;  // per track ("" if unnamed)
		double                   length = 0.0;
	};

	// false (and a reason) if it isn't a MIDI file or has no notes
	bool ParseMidi(const std::vector<std::uint8_t>& a_data, MidiFile& a_out, std::string* a_error = nullptr);

	// The tune to play along with: the track that most looks like the melody (enough notes, high, mostly one at a
	// time), reduced to one note at a time (the highest of any chord) and thinned where notes come faster than a hand
	// can follow on a keyboard.
	std::vector<Note> ExtractMelody(const MidiFile& a_file, int* a_track = nullptr);
}
