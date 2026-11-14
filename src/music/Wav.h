// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace music
{
	// A WAV file (PCM 8/16/24/32-bit or float, any channels, any rate) as mono floats at a_rate. false if it
	// isn't one.
	bool ReadWav(const std::vector<std::uint8_t>& a_data, int a_rate, std::vector<float>& a_out, std::string* a_error = nullptr);
}
