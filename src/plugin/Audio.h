// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include "../music/Midi.h"
#include "../music/Synth.h"

// The instruments' sound: the synth streamed through our own XAudio2 voice (low latency, and it doesn't stop when
// the game pauses its own sound). A song can be played through it with sample-accurate timing, or just clocked
// (play along: you play the notes; the clock keeps the sheet music in step with what you hear).
namespace Audio
{
	bool          Start();
	music::Synth* Synth();  // null if there's no audio
	void          SetVolume(float a_volume);
	void          SetReverb(float a_wet);

	// a_leadIn: seconds before the song's first beat (the count-in); a_audible false: clock only
	void   PlaySong(const std::vector<music::Note>& a_notes, music::Instrument a_inst, float a_speed, double a_leadIn, bool a_audible);
	void   SetSongSpeed(float a_speed);  // change it mid-song: carries on from where it is
	void   StopSong();
	bool   SongActive();
	double SongTime();  // song seconds at what you're hearing now (negative in the count-in); 0 with no song
	void   Click();     // a soft tick (the count-in)
}
