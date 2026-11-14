// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Wav.h"

#include <algorithm>
#include <cstring>

namespace music
{
	bool ReadWav(const std::vector<std::uint8_t>& a_data, int a_rate, std::vector<float>& a_out, std::string* a_error)
	{
		auto fail = [&](const char* a_why) {
			if (a_error) {
				*a_error = a_why;
			}
			return false;
		};
		auto u16 = [&](std::size_t o) { return static_cast<std::uint32_t>(a_data[o] | (a_data[o + 1] << 8)); };
		auto u32 = [&](std::size_t o) { return u16(o) | (u16(o + 2) << 16); };
		if (a_data.size() < 12 || std::memcmp(a_data.data(), "RIFF", 4) != 0 || std::memcmp(a_data.data() + 8, "WAVE", 4) != 0) {
			return fail("not a WAV file");
		}
		int         format = 0, channels = 0, rate = 0, bits = 0;
		std::size_t dataAt = 0, dataLen = 0;
		for (std::size_t o = 12; o + 8 <= a_data.size();) {
			const std::uint32_t len = u32(o + 4);
			if (std::memcmp(a_data.data() + o, "fmt ", 4) == 0 && o + 24 <= a_data.size()) {
				format = static_cast<int>(u16(o + 8));
				channels = static_cast<int>(u16(o + 10));
				rate = static_cast<int>(u32(o + 12));
				bits = static_cast<int>(u16(o + 22));
				if (format == 0xFFFE && len >= 40 && o + 8 + 26 <= a_data.size()) {
					format = static_cast<int>(u16(o + 8 + 24));  // WAVE_FORMAT_EXTENSIBLE: the sub-format
				}
			} else if (std::memcmp(a_data.data() + o, "data", 4) == 0) {
				dataAt = o + 8;
				dataLen = (std::min<std::size_t>)(len, a_data.size() - dataAt);
				break;
			}
			o += 8 + len + (len & 1);
		}
		if (!dataAt || channels < 1 || rate < 1000 || (format != 1 && format != 3)) {
			return fail("unsupported WAV (PCM or float only)");
		}
		const int bytes = bits / 8;
		if (bytes < 1 || bytes > 4 || (format == 3 && bits != 32)) {
			return fail("unsupported sample size");
		}
		const std::size_t frames = dataLen / (bytes * channels);
		std::vector<float> mono(frames);
		for (std::size_t f = 0; f < frames; ++f) {
			float sum = 0.0f;
			for (int c = 0; c < channels; ++c) {
				const std::uint8_t* p = a_data.data() + dataAt + (f * channels + c) * bytes;
				float               v = 0.0f;
				if (format == 3) {
					std::memcpy(&v, p, 4);
				} else if (bytes == 1) {
					v = (p[0] - 128) / 128.0f;
				} else if (bytes == 2) {
					v = static_cast<std::int16_t>(p[0] | (p[1] << 8)) / 32768.0f;
				} else if (bytes == 3) {
					const std::int32_t s = (p[0] << 8 | p[1] << 16 | p[2] << 24) >> 8;
					v = s / 8388608.0f;
				} else {
					std::int32_t s;
					std::memcpy(&s, p, 4);
					v = s / 2147483648.0f;
				}
				sum += v;
			}
			mono[f] = sum / channels;
		}
		// to the synth's rate (linear interpolation is fine for this)
		if (rate == a_rate) {
			a_out = std::move(mono);
		} else {
			const double step = static_cast<double>(rate) / a_rate;
			a_out.clear();
			for (double pos = 0.0; pos + 1.0 < static_cast<double>(frames); pos += step) {
				const auto  i = static_cast<std::size_t>(pos);
				const float t = static_cast<float>(pos - i);
				a_out.push_back(mono[i] * (1.0f - t) + mono[i + 1] * t);
			}
		}
		return !a_out.empty() || fail("empty WAV");
	}
}
