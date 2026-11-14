// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// USB MIDI keyboards (any MIDI input Windows knows), through the Windows multimedia API. Every input device is
// opened while you play and closed when you stop, so other programs can have them the rest of the time.
namespace MidiIn
{
	// a_onMessage runs on the driver's thread: status, data1, data2
	using Handler = void (*)(int a_status, int a_d1, int a_d2);

	int                      Open(Handler a_handler);  // how many devices opened
	void                     Close();
	std::vector<std::string> OpenNames();
}
