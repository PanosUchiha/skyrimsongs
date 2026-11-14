// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include "Midi.h"

#include <deque>
#include <vector>

// How well you're playing, 0..1, for the crowd.
namespace music
{
	// Free play: no sheet music, so it listens the way an audience does - a music-theory "ear", not a note
	// counter. Over the last eight seconds (sixteen for repeated ideas):
	//  - rhythm: do the gaps between notes sit on a steady beat (and its halves, one and a halves and doubles)?
	//  - key: how strongly the notes imply one key (Krumhansl-Kessler key profiles, as listeners hear tonality)
	//  - stability: do the notes you land on - the long ones, the ones before a breath - rest on the key's
	//    home notes (1, 3, 5), the way phrases end, rather than anywhere?
	//  - melody: steps and small leaps, a leap answered by a step back, not random jumps across the keyboard
	//  - harmony: notes sounding together (and over the ringing of the last ones) - consonant intervals
	//    (thirds, fourths, fifths, sixths) or clashes (seconds, sevenths, the tritone)?
	//  - ideas: real music repeats its little figures (the same intervals in the same rhythm); random notes don't
	//  - pace and variety: enough notes, not one note over and over
	//  - spam: hammering a key, or more notes a second than anyone can follow, is noise
	// A pause between phrases is fine; a long silence isn't playing (the room just drifts), see Silent().
	class FreeJudge
	{
	public:
		struct Breakdown
		{
			double rhythm = 0.5, key = 0.5, stability = 0.5, melody = 0.5, harmony = 1.0, ideas = 0.0, pace = 0.0, variety = 0.0, spam = 1.0;
			double quality = 0.0;
		};

		void      Reset();
		void      Note(double a_time, int a_pitch);
		Breakdown Judge(double a_now) const;
		double    LastNote() const { return m_last; }
		bool      Silent(double a_now) const { return a_now - m_last > 5.0; }  // not playing: the room drifts, no judging

	private:
		struct Ev
		{
			double t;
			int    pitch;
		};
		std::deque<Ev> m_events;
		double         m_last = -1e9;
		double         m_first = -1e9;
	};

	// Playing along with a song: every note of the tune must be played (any octave counts) close to its time.
	class ChartJudge
	{
	public:
		enum class State : std::uint8_t
		{
			Waiting,
			Perfect,
			Good,
			Missed
		};
		static constexpr double kPerfect = 0.07;
		static constexpr double kGood = 0.16;

		void Load(std::vector<music::Note> a_chart);
		void Note(double a_songTime, int a_pitch);  // a note you played
		void Advance(double a_songTime);            // notes gone past unplayed are missed
		bool Done(double a_songTime) const;

		double Quality() const { return m_quality; }  // recent playing (for the crowd)
		double Score() const;                         // the whole song so far, 0..1
		const std::vector<music::Note>& Chart() const { return m_chart; }
		State StateOf(std::size_t a_i) const { return m_state[a_i]; }
		int   Perfects() const { return m_perfect; }
		int   Goods() const { return m_good; }
		int   Misses() const { return m_miss; }
		int   Extras() const { return m_extra; }
		int   Streak() const { return m_streak; }

	private:
		void Rate(double a_value);

		std::vector<music::Note> m_chart;
		std::vector<State>       m_state;
		std::size_t              m_next = 0;  // first note not yet judged
		double                   m_quality = 0.6;
		int m_perfect = 0, m_good = 0, m_miss = 0, m_extra = 0, m_streak = 0;
	};
}
