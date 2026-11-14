// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

// The instruments, synthesised in real time (no samples): plain C++ with no game code, so the tests can render it.
//  - Lute (voiced for C2-C5): two Karplus-Strong strings per note, as a lute's courses are strung: the bass
//    courses in octaves, the middle ones in unison pairs very slightly apart, the top (the chanterelle) a single
//    string. Low notes are warm and ring long, high ones bright and short; plucked nearer the bridge as they go
//    up, with the click of the finger, through a small wooden body and a little room.
//  - Flute (voiced for C4-C7): low notes soft, breathy and rich in harmonics, high ones pure, bright and
//    stronger, as a real flute's registers are; a chiff at the start, a slight scoop up into the note and a
//    vibrato that blooms if you hold it. Played live it's monophonic and slurs from note to note.
//  Every key's loudness is evened out (measured, see kLuteNorm / kFluteNorm).
namespace music
{
	enum class Instrument : std::uint8_t
	{
		Lute,
		Flute,
		kCount
	};
	const char* InstrumentName(Instrument a_i);

	class Synth
	{
	public:
		explicit Synth(float a_rate = 48000.0f);

		// Thread-safe: called from the game's input, the MIDI keyboard's thread and the song player; the events are
		// applied at the start of the next block Render makes.
		void NoteOn(Instrument a_inst, int a_pitch, float a_velocity, bool a_live = true);  // velocity 0..1
		void NoteOff(Instrument a_inst, int a_pitch, bool a_live = true);
		void Sustain(bool a_on);  // a MIDI keyboard's pedal: notes ring on after their keys come up
		void AllOff();            // every note let go (rings out)
		void Silence();           // every note stopped at once
		void Coin(float a_volume);  // a coin landing: a patron's tip
		void SetVolume(float a_v);
		void SetReverb(float a_wet);

		// Mono, overwrites a_out. Call from one thread only (the audio thread).
		void Render(float* a_out, int a_frames);
		int  ActiveVoices() const { return m_active; }
		float Rate() const { return m_rate; }

	private:
		static constexpr int kVoices = 40;
		static constexpr int kMaxDelay = 4096;

		struct Event
		{
			enum class Kind : std::uint8_t { On, Off, Sustain, AllOff, Silence, Coin } kind;
			Instrument inst;
			bool       live;
			int        pitch;
			float      value;
		};

		struct String
		{
			std::array<float, kMaxDelay> buf{};
			int   len = 0, idx = 0;
			float apC = 0.0f, apX = 0.0f, apY = 0.0f, prev = 0.0f;
			float damp = 0.5f;  // the loop filter's weight on the previous sample: lighter for high notes, which would die at once
			float gain = 0.999f, amp = 1.0f;
		};

		struct Voice
		{
			bool       on = false;
			Instrument inst = Instrument::Lute;
			bool       coin = false;
			bool       live = true;
			bool       released = false;
			bool       held = false;     // the key is still down
			int        pitch = 60;
			float      vel = 1.0f;
			double     age = 0.0;
			double     relAge = 0.0;
			float      level = 1.0f;  // a slow follower of the output, to know when a voice has died away
			std::uint64_t serial = 0;
			// lute
			String s[2];
			float  t60 = 3.0f;
			float  norm = 1.0f;    // loudness, evened out across the 37 keys
			float  freq = 261.6f;
			// flute
			double phase = 0.0;
			float  curFreq = 261.6f, targetFreq = 261.6f;
			float  env = 0.0f, chiff = 0.0f;
			float  bpLow = 0.0f, bpBand = 0.0f;  // the breath noise's band-pass
			// coin
			float  coinVol = 0.0f;
			double phases[6]{};
		};

		void   Apply(const Event& a_e);
		Voice* Allocate();
		void   StartLute(Voice& a_v, int a_pitch, float a_vel);
		void   SetLuteDecay(Voice& a_v, float a_t60);
		float  LuteSample(Voice& a_v);
		float  FluteSample(Voice& a_v);
		float  CoinSample(Voice& a_v);
		float  Noise();
		float  Reverb(float a_in);

		float               m_rate;
		std::mutex          m_mtx;
		std::vector<Event>  m_queue, m_work;
		std::array<Voice, kVoices> m_voices;
		std::uint64_t       m_serial = 0;
		std::uint32_t       m_rng = 0x9E3779B9u;
		bool                m_sustain = false;
		float               m_volume = 0.8f;
		float               m_wet = 0.16f;
		int                 m_active = 0;
		std::vector<int>    m_fluteHeld;  // live flute keys down, oldest first (the newest sounds)

		// the lute's body: three band-pass resonances and a DC block
		struct Biquad
		{
			float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
			float Run(float x)
			{
				const float y = b0 * x + z1;
				z1 = b1 * x - a1 * y + z2;
				z2 = b2 * x - a2 * y;
				return y;
			}
		};
		Biquad m_body[3];
		Biquad m_tone;  // a gentle low-pass on the lute
		float  m_dcX = 0.0f, m_dcY = 0.0f;
		float  m_fluteHpX = 0.0f, m_fluteHpY = 0.0f;  // a high-pass on the flute: no boom under it

		// a small room (Freeverb, cut down)
		struct Comb
		{
			std::vector<float> buf;
			int   idx = 0;
			float store = 0.0f;
		};
		struct Allpass
		{
			std::vector<float> buf;
			int idx = 0;
		};
		Comb    m_comb[4];
		Allpass m_ap[2];
	};
}
