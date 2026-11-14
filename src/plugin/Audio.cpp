// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Audio.h"
#include "Config.h"

// CommonLib targets Windows 7; XAudio2 2.8+ needs 8 or later in its header. We load the DLL ourselves (see Start).
#pragma push_macro("_WIN32_WINNT")
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <xaudio2.h>
#pragma pop_macro("_WIN32_WINNT")

namespace Audio
{
	namespace
	{
		constexpr UINT32 kRate = 48000;
		constexpr int    kBlock = 384;  // 8 ms
		constexpr int    kBuffers = 4;

		struct SongEvent
		{
			double t;  // song seconds
			bool   on;
			int    pitch;
			float  vel;
		};

		struct Player
		{
			std::vector<SongEvent> events;
			std::size_t            next = 0;
			music::Instrument      inst = music::Instrument::Lute;
			bool                   audible = false;
			bool                   active = false;
			float                  speed = 1.0f;
			double                 leadIn = 0.0;
			std::uint64_t          startSample = 0;  // the render position at song time -leadIn
		};

		IXAudio2*               g_xa = nullptr;
		IXAudio2MasteringVoice* g_master = nullptr;
		IXAudio2SourceVoice*    g_voice = nullptr;
		std::unique_ptr<music::Synth> g_synth;
		bool                    g_tried = false, g_ok = false;
		std::mutex              g_initMtx;

		std::mutex                 g_songMtx;
		Player                     g_song;
		std::atomic<std::uint64_t> g_rendered{ 0 };
		std::array<std::array<float, kBlock>, kBuffers> g_bufs{};
		int                        g_nextBuf = 0;

		void FillAndSubmit();

		class Callback final : public IXAudio2VoiceCallback
		{
		public:
			void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
			void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
			void STDMETHODCALLTYPE OnStreamEnd() override {}
			void STDMETHODCALLTYPE OnBufferStart(void*) override {}
			void STDMETHODCALLTYPE OnBufferEnd(void*) override { FillAndSubmit(); }
			void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
			void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT a_err) override { spdlog::error("Audio: voice error {:08X}", static_cast<std::uint32_t>(a_err)); }
		};
		Callback g_callback;

		// the song's notes due in this block go to the synth before it renders
		void RunSong(std::uint64_t a_blockStart)
		{
			std::lock_guard lk(g_songMtx);
			auto&           s = g_song;
			if (!s.active || !s.audible) {
				return;
			}
			const double t = (static_cast<double>(a_blockStart + kBlock) - static_cast<double>(s.startSample)) / kRate * s.speed - s.leadIn;
			while (s.next < s.events.size() && s.events[s.next].t <= t) {
				const auto& e = s.events[s.next++];
				if (e.on) {
					g_synth->NoteOn(s.inst, e.pitch, e.vel, false);
				} else {
					g_synth->NoteOff(s.inst, e.pitch, false);
				}
			}
		}

		void FillAndSubmit()
		{
			auto&               buf = g_bufs[g_nextBuf];
			g_nextBuf = (g_nextBuf + 1) % kBuffers;
			const std::uint64_t at = g_rendered.load();
			RunSong(at);
			g_synth->Render(buf.data(), kBlock);
			g_rendered = at + kBlock;
			XAUDIO2_BUFFER xb{};
			xb.AudioBytes = kBlock * sizeof(float);
			xb.pAudioData = reinterpret_cast<const BYTE*>(buf.data());
			g_voice->SubmitSourceBuffer(&xb);
		}

		std::uint64_t PlayedSamples()
		{
			if (!g_voice) {
				return 0;
			}
			XAUDIO2_VOICE_STATE st{};
			g_voice->GetState(&st, 0);
			return st.SamplesPlayed;
		}
	}

	bool Start()
	{
		std::lock_guard lk(g_initMtx);
		if (g_tried) {
			return g_ok;
		}
		g_tried = true;
		if (FAILED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
			// already initialised differently on this thread: XAudio2 copes
		}
		// XAudio2 2.9 ships with Windows 10 and 11 (2.8 with Windows 8); loaded by hand so either works
		using create_t = HRESULT(__stdcall*)(IXAudio2**, UINT32, UINT32);
		create_t create = nullptr;
		for (const char* dll : { "xaudio2_9.dll", "xaudio2_8.dll" }) {
			if (HMODULE h = ::LoadLibraryA(dll)) {
				create = reinterpret_cast<create_t>(::GetProcAddress(h, "XAudio2Create"));
				if (create) {
					spdlog::info("Audio: using {}", dll);
					break;
				}
			}
		}
		if (!create || FAILED(create(&g_xa, 0, 0x00000001 /* processor 1 */)) || !g_xa) {
			spdlog::error("Audio: XAudio2 couldn't start - the instruments will be silent");
			return false;
		}
		if (FAILED(g_xa->CreateMasteringVoice(&g_master)) || !g_master) {
			spdlog::error("Audio: no audio device - the instruments will be silent");
			return false;
		}
		WAVEFORMATEX wf{};
		wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		wf.nChannels = 1;
		wf.nSamplesPerSec = kRate;
		wf.wBitsPerSample = 32;
		wf.nBlockAlign = 4;
		wf.nAvgBytesPerSec = kRate * 4;
		g_synth = std::make_unique<music::Synth>(static_cast<float>(kRate));
		g_synth->SetVolume(Config::Get().volume);
		g_synth->SetReverb(Config::Get().reverb);
		if (FAILED(g_xa->CreateSourceVoice(&g_voice, &wf, 0, 2.0f, &g_callback)) || !g_voice) {
			spdlog::error("Audio: couldn't make a voice - the instruments will be silent");
			return false;
		}
		for (int i = 0; i < kBuffers; ++i) {
			FillAndSubmit();
		}
		g_voice->Start();
		g_ok = true;
		spdlog::info("Audio: streaming at {} Hz, {} x {} samples buffered", kRate, kBuffers, kBlock);
		return true;
	}

	music::Synth* Synth() { return g_ok ? g_synth.get() : nullptr; }

	void SetVolume(float a_volume)
	{
		if (g_synth) {
			g_synth->SetVolume(a_volume);
		}
	}
	void SetReverb(float a_wet)
	{
		if (g_synth) {
			g_synth->SetReverb(a_wet);
		}
	}

	void PlaySong(const std::vector<music::Note>& a_notes, music::Instrument a_inst, float a_speed, double a_leadIn, bool a_audible)
	{
		if (!Start()) {
			return;
		}
		Player p;
		p.inst = a_inst;
		p.audible = a_audible;
		p.active = true;
		p.speed = std::clamp(a_speed, 0.25f, 2.0f);
		p.leadIn = a_leadIn;
		if (a_audible) {
			for (const auto& n : a_notes) {
				const float vel = std::clamp(n.vel / 127.0f, 0.1f, 1.0f);
				p.events.push_back({ n.start, true, n.pitch, vel });
				p.events.push_back({ n.start + (std::max)(0.05, n.dur), false, n.pitch, 0.0f });
			}
			// at the same moment, ends before starts (a repeated note is re-struck)
			std::sort(p.events.begin(), p.events.end(), [](const SongEvent& a, const SongEvent& b) { return a.t < b.t || (a.t == b.t && !a.on && b.on); });
		}
		std::lock_guard lk(g_songMtx);
		// the song starts where what's being rendered now will be heard
		p.startSample = g_rendered.load();
		g_song = std::move(p);
	}

	void StopSong()
	{
		{
			std::lock_guard lk(g_songMtx);
			const bool audible = g_song.active && g_song.audible;
			g_song = Player{};
			if (!audible) {
				return;
			}
		}
		if (g_synth) {
			g_synth->AllOff();
		}
	}

	bool SongActive()
	{
		std::lock_guard lk(g_songMtx);
		return g_song.active;
	}

	double SongTime()
	{
		std::uint64_t start = 0;
		float         speed = 1.0f;
		double        lead = 0.0;
		{
			std::lock_guard lk(g_songMtx);
			if (!g_song.active) {
				return 0.0;
			}
			start = g_song.startSample;
			speed = g_song.speed;
			lead = g_song.leadIn;
		}
		const double played = static_cast<double>(PlayedSamples());
		return (played - static_cast<double>(start)) / kRate * speed - lead;
	}

	void Click()
	{
		if (g_synth) {
			g_synth->NoteOn(music::Instrument::Lute, 96, 0.12f, false);
			g_synth->NoteOff(music::Instrument::Lute, 96, false);
		}
	}
}
