// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "MidiIn.h"

#include <mmsystem.h>

namespace MidiIn
{
	namespace
	{
		std::mutex               g_mtx;
		std::vector<HMIDIIN>     g_open;
		std::vector<std::string> g_names;
		std::atomic<Handler>     g_handler{ nullptr };

		void CALLBACK OnMidi(HMIDIIN, UINT a_msg, DWORD_PTR, DWORD_PTR a_p1, DWORD_PTR)
		{
			if (a_msg != MIM_DATA) {
				return;
			}
			const auto m = static_cast<std::uint32_t>(a_p1);
			if (auto h = g_handler.load()) {
				h(static_cast<int>(m & 0xFF), static_cast<int>((m >> 8) & 0x7F), static_cast<int>((m >> 16) & 0x7F));
			}
		}
	}

	int Open(Handler a_handler)
	{
		std::lock_guard lk(g_mtx);
		g_handler = a_handler;
		if (!g_open.empty()) {
			return static_cast<int>(g_open.size());
		}
		const UINT n = ::midiInGetNumDevs();
		for (UINT i = 0; i < n; ++i) {
			MIDIINCAPSA caps{};
			::midiInGetDevCapsA(i, &caps, sizeof(caps));
			HMIDIIN h = nullptr;
			if (::midiInOpen(&h, i, reinterpret_cast<DWORD_PTR>(&OnMidi), 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR || !h) {
				spdlog::warn("MIDI: couldn't open '{}' (another program may have it)", caps.szPname);
				continue;
			}
			::midiInStart(h);
			g_open.push_back(h);
			g_names.emplace_back(caps.szPname);
			spdlog::info("MIDI: listening to '{}'", caps.szPname);
		}
		return static_cast<int>(g_open.size());
	}

	void Close()
	{
		std::lock_guard lk(g_mtx);
		for (auto h : g_open) {
			::midiInStop(h);
			::midiInReset(h);
			::midiInClose(h);
		}
		g_open.clear();
		g_names.clear();
	}

	std::vector<std::string> OpenNames()
	{
		std::lock_guard lk(g_mtx);
		return g_names;
	}
}
