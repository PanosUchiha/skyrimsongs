// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Midi.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace music
{
	namespace
	{
		struct Reader
		{
			const std::uint8_t* p;
			const std::uint8_t* end;
			bool                ok = true;

			std::uint32_t U(int a_bytes)
			{
				std::uint32_t v = 0;
				for (int i = 0; i < a_bytes; ++i) {
					if (p >= end) {
						ok = false;
						return 0;
					}
					v = (v << 8) | *p++;
				}
				return v;
			}
			std::uint32_t Vlq()
			{
				std::uint32_t v = 0;
				for (int i = 0; i < 4; ++i) {
					if (p >= end) {
						ok = false;
						return 0;
					}
					const std::uint8_t b = *p++;
					v = (v << 7) | (b & 0x7F);
					if (!(b & 0x80)) {
						return v;
					}
				}
				ok = false;
				return v;
			}
			void Skip(std::uint32_t n)
			{
				if (static_cast<std::size_t>(end - p) < n) {
					ok = false;
					p = end;
					return;
				}
				p += n;
			}
		};

		struct RawOn
		{
			std::uint64_t tick;
			int           vel;
		};
		struct RawNote
		{
			std::uint64_t on, off;
			int           pitch, vel, channel, track;
		};
	}

	bool ParseMidi(const std::vector<std::uint8_t>& a_data, MidiFile& a_out, std::string* a_error)
	{
		auto fail = [&](const char* a_why) {
			if (a_error) {
				*a_error = a_why;
			}
			return false;
		};
		a_out = {};
		Reader r{ a_data.data(), a_data.data() + a_data.size() };
		// a RIFF-wrapped MIDI (.rmi): skip to the MThd inside
		if (a_data.size() > 20 && std::equal(a_data.begin(), a_data.begin() + 4, "RIFF")) {
			for (std::size_t i = 12; i + 4 < a_data.size(); ++i) {
				if (std::equal(a_data.begin() + i, a_data.begin() + i + 4, "MThd")) {
					r.p = a_data.data() + i;
					break;
				}
			}
		}
		if (r.end - r.p < 14 || !std::equal(r.p, r.p + 4, "MThd")) {
			return fail("not a MIDI file");
		}
		r.Skip(4);
		const std::uint32_t hlen = r.U(4);
		const int           format = static_cast<int>(r.U(2));
		const int           ntracks = static_cast<int>(r.U(2));
		const std::uint16_t division = static_cast<std::uint16_t>(r.U(2));
		if (hlen > 6) {
			r.Skip(hlen - 6);
		}
		if (!r.ok || format > 2 || ntracks == 0 || division == 0) {
			return fail("bad MIDI header");
		}
		// ticks to seconds: SMPTE (frames x ticks per frame) or ticks per quarter note with the tempo map
		const bool   smpte = (division & 0x8000) != 0;
		const double smpteTickSec = smpte ? 1.0 / (static_cast<double>(256 - (division >> 8)) * (division & 0xFF)) : 0.0;
		const double tpq = smpte ? 1.0 : static_cast<double>(division);

		std::map<std::uint64_t, double> tempos;  // tick -> microseconds per quarter
		std::vector<RawNote>            raw;
		a_out.trackNames.resize(ntracks);
		for (int t = 0; t < ntracks && r.ok && r.p < r.end; ++t) {
			// find the next MTrk (skipping anything unknown)
			while (r.ok && r.end - r.p >= 8 && !std::equal(r.p, r.p + 4, "MTrk")) {
				r.Skip(4);
				r.Skip(r.U(4));
			}
			if (!r.ok || r.end - r.p < 8) {
				break;
			}
			r.Skip(4);
			const std::uint32_t len = r.U(4);
			Reader tr{ r.p, r.p + (std::min<std::size_t>)(len, static_cast<std::size_t>(r.end - r.p)) };
			r.Skip((std::min<std::size_t>)(len, static_cast<std::size_t>(r.end - r.p)));
			r.ok = true;
			std::uint64_t tick = 0;
			std::uint8_t  status = 0;
			std::map<int, std::vector<RawOn>> open;  // channel*128+pitch -> starts not yet ended
			while (tr.ok && tr.p < tr.end) {
				tick += tr.Vlq();
				if (!tr.ok || tr.p >= tr.end) {
					break;
				}
				std::uint8_t b = *tr.p;
				if (b & 0x80) {
					++tr.p;
					status = b;
				} else if (status < 0x80 || status >= 0xF0) {
					break;  // running status with nothing to run on
				}
				if (status == 0xFF) {
					const int           type = static_cast<int>(tr.U(1));
					const std::uint32_t mlen = tr.Vlq();
					if (type == 0x51 && mlen == 3) {
						tempos[tick] = static_cast<double>(tr.U(3));
					} else if (type == 0x03 && a_out.trackNames[t].empty()) {
						const auto* s = tr.p;
						tr.Skip(mlen);
						if (tr.ok) {
							a_out.trackNames[t].assign(reinterpret_cast<const char*>(s), mlen);
						}
					} else if (type == 0x2F) {
						break;
					} else {
						tr.Skip(mlen);
					}
					status = 0;  // meta and sysex events cancel running status
					continue;
				}
				if (status == 0xF0 || status == 0xF7) {
					tr.Skip(tr.Vlq());
					status = 0;
					continue;
				}
				const int kind = status & 0xF0, ch = status & 0x0F;
				const int d1 = static_cast<int>(tr.U(1));
				const int d2 = (kind == 0xC0 || kind == 0xD0) ? 0 : static_cast<int>(tr.U(1));
				if (!tr.ok) {
					break;
				}
				const int key = ch * 128 + (d1 & 0x7F);
				if (kind == 0x90 && d2 > 0) {
					open[key].push_back({ tick, d2 });
				} else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
					auto it = open.find(key);
					if (it != open.end() && !it->second.empty()) {
						const auto on = it->second.front();  // first in, first out
						it->second.erase(it->second.begin());
						raw.push_back({ on.tick, tick, d1 & 0x7F, on.vel, ch, t });
					}
				}
			}
			// notes never ended: end them with the track
			for (auto& [key, ons] : open) {
				for (const auto& on : ons) {
					raw.push_back({ on.tick, (std::max)(tick, on.tick + static_cast<std::uint64_t>(tpq / 2)), key % 128, on.vel, key / 128, t });
				}
			}
		}
		if (raw.empty()) {
			return fail("no notes in this MIDI file");
		}
		if (!tempos.contains(0)) {
			tempos[0] = 500000.0;  // 120 bpm
		}
		// tick -> seconds through the tempo map
		std::vector<std::pair<std::uint64_t, double>> segs;  // start tick, start seconds
		std::vector<double>                          uspq;
		{
			double        sec = 0.0;
			std::uint64_t prevTick = 0;
			double        prevTempo = tempos.begin()->second;
			for (const auto& [tk, tempo] : tempos) {
				sec += static_cast<double>(tk - prevTick) * prevTempo / 1e6 / tpq;
				segs.push_back({ tk, sec });
				uspq.push_back(tempo);
				prevTick = tk;
				prevTempo = tempo;
			}
		}
		auto toSec = [&](std::uint64_t a_tick) {
			if (smpte) {
				return static_cast<double>(a_tick) * smpteTickSec;
			}
			std::size_t i = 0;
			while (i + 1 < segs.size() && segs[i + 1].first <= a_tick) {
				++i;
			}
			return segs[i].second + static_cast<double>(a_tick - segs[i].first) * uspq[i] / 1e6 / tpq;
		};
		for (const auto& n : raw) {
			if (n.channel == 9) {
				continue;  // General MIDI drums: not something a lute or flute can play
			}
			Note o;
			o.start = toSec(n.on);
			o.dur = (std::max)(0.03, toSec(n.off) - o.start);
			o.pitch = n.pitch;
			o.vel = n.vel;
			o.channel = n.channel;
			o.track = n.track;
			a_out.notes.push_back(o);
		}
		if (a_out.notes.empty()) {
			return fail("only drums in this MIDI file");
		}
		std::sort(a_out.notes.begin(), a_out.notes.end(), [](const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
		// start at the first note
		const double first = a_out.notes.front().start;
		for (auto& n : a_out.notes) {
			n.start -= first;
			a_out.length = (std::max)(a_out.length, n.start + n.dur);
		}
		return true;
	}

	std::vector<Note> ExtractMelody(const MidiFile& a_file, int* a_track)
	{
		// group by track and channel (a format-0 file keeps every part in one track, on its own channel)
		std::map<int, std::vector<Note>> parts;
		for (const auto& n : a_file.notes) {
			parts[n.track * 16 + n.channel].push_back(n);
		}
		int    best = -1;
		double bestScore = -1e9;
		for (const auto& [key, notes] : parts) {
			if (notes.size() < 8) {
				continue;
			}
			double sum = 0.0;
			int    overlapped = 0;
			for (std::size_t i = 0; i < notes.size(); ++i) {
				sum += notes[i].pitch;
				if (i > 0 && notes[i].start - notes[i - 1].start < 0.03) {
					++overlapped;
				}
			}
			const double mean = sum / notes.size();
			const double chordy = static_cast<double>(overlapped) / notes.size();
			const double score = mean * 1.0 + std::log(static_cast<double>(notes.size())) * 6.0 - chordy * 30.0 - (mean > 90 ? (mean - 90) * 3.0 : 0.0);
			if (score > bestScore) {
				bestScore = score;
				best = key;
			}
		}
		if (best < 0 && !parts.empty()) {
			// every part is tiny: take the biggest
			std::size_t most = 0;
			for (const auto& [key, notes] : parts) {
				if (notes.size() > most) {
					most = notes.size();
					best = key;
				}
			}
		}
		if (best < 0) {
			return {};
		}
		if (a_track) {
			*a_track = best / 16;
		}
		const auto& src = parts[best];
		// the skyline: of notes starting together, the highest
		std::vector<Note> mel;
		for (std::size_t i = 0; i < src.size();) {
			std::size_t j = i;
			Note        top = src[i];
			while (j < src.size() && src[j].start - src[i].start < 0.03) {
				if (src[j].pitch > top.pitch) {
					top = src[j];
				}
				++j;
			}
			mel.push_back(top);
			i = j;
		}
		// thin out runs too fast to play by hand (a note every 90 ms at most), and end each note before the next
		std::vector<Note> out;
		for (const auto& n : mel) {
			if (!out.empty() && n.start - out.back().start < 0.09) {
				continue;
			}
			out.push_back(n);
		}
		for (std::size_t i = 0; i + 1 < out.size(); ++i) {
			out[i].dur = (std::min)(out[i].dur, out[i + 1].start - out[i].start);
		}
		if (!out.empty()) {
			const double first = out.front().start;
			for (auto& n : out) {
				n.start -= first;
			}
		}
		return out;
	}
}
