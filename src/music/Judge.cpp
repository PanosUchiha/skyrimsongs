// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Judge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace music
{
	namespace
	{
		constexpr double kWindow = 8.0;
		constexpr double kMemory = 16.0;  // how far back a repeated idea is remembered

		double Ramp(double x, double x0, double x1, double y0, double y1)
		{
			if (x <= x0) {
				return y0;
			}
			if (x >= x1) {
				return y1;
			}
			return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
		}
	}

	void FreeJudge::Reset()
	{
		m_events.clear();
		m_last = -1e9;
		m_first = -1e9;
	}

	void FreeJudge::Note(double a_time, int a_pitch)
	{
		m_events.push_back({ a_time, a_pitch });
		m_last = a_time;
		if (m_first < -1e8) {
			m_first = a_time;
		}
		while (!m_events.empty() && m_events.front().t < a_time - kMemory) {
			m_events.pop_front();
		}
	}

	namespace
	{
		// Krumhansl-Kessler probe-tone profiles: how well each scale degree fits a major / minor key
		constexpr double kMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
		constexpr double kMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

		// how much two notes clash, by the interval between them (semitones, mod 12)
		constexpr double kClash[12] = { 0.0, 1.0, 0.45, 0.12, 0.08, 0.05, 0.75, 0.0, 0.12, 0.08, 0.4, 0.9 };

		double Correlate(const double* a_x, const double* a_profile, int a_root)
		{
			double mx = 0.0, mp = 0.0;
			for (int i = 0; i < 12; ++i) {
				mx += a_x[i];
				mp += a_profile[i];
			}
			mx /= 12.0;
			mp /= 12.0;
			double sxy = 0.0, sxx = 0.0, syy = 0.0;
			for (int i = 0; i < 12; ++i) {
				const double x = a_x[(i + a_root) % 12] - mx, y = a_profile[i] - mp;
				sxy += x * y;
				sxx += x * x;
				syy += y * y;
			}
			return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt(sxx * syy) : 0.0;
		}
	}

	FreeJudge::Breakdown FreeJudge::Judge(double a_now) const
	{
		Breakdown b;
		std::vector<Ev> ev, mem;
		for (const auto& e : m_events) {
			if (e.t > a_now) {
				continue;
			}
			mem.push_back(e);
			if (e.t >= a_now - kWindow) {
				ev.push_back(e);
			}
		}
		if (ev.empty()) {
			b.quality = 0.12;
			return b;
		}
		// onsets: notes struck together are one onset (a chord), heard as its top note
		struct Onset
		{
			double           t;
			int              top;
			std::vector<int> notes;
		};
		auto onsetsOf = [](const std::vector<Ev>& a_ev) {
			std::vector<Onset> on;
			for (const auto& e : a_ev) {
				if (!on.empty() && e.t - on.back().t < 0.05) {
					on.back().top = (std::max)(on.back().top, e.pitch);
					on.back().notes.push_back(e.pitch);
				} else {
					on.push_back({ e.t, e.pitch, { e.pitch } });
				}
			}
			return on;
		};
		const auto on = onsetsOf(ev);
		const double span = std::clamp(a_now - (std::max)(m_first, a_now - kWindow), 1.0, kWindow);

		// pace: onsets a second (a lute player manages 6-8 a second in a fast run)
		const double rate = on.size() / span;
		b.pace = rate < 0.4 ? Ramp(rate, 0.0, 0.4, 0.0, 0.3) :
		         rate < 1.0 ? Ramp(rate, 0.4, 1.0, 0.3, 0.9) :
		         rate <= 7.0 ? 1.0 :
		                       Ramp(rate, 7.0, 12.0, 1.0, 0.2);

		// variety
		std::set<int> distinct;
		for (const auto& e : ev) {
			distinct.insert(e.pitch);
		}
		b.variety = distinct.size() <= 1 ? 0.15 : distinct.size() == 2 ? 0.5 : distinct.size() == 3 ? 0.8 : 1.0;

		// key: the strongest correlation of what was played (weighted by how long each note had before the
		// next) with any of the 24 major and minor key profiles
		int  keyRoot = 0;
		bool keyMinor = false;
		{
			double hist[12]{};
			for (std::size_t i = 0; i < on.size(); ++i) {
				const double w = std::clamp(i + 1 < on.size() ? on[i + 1].t - on[i].t : 0.5, 0.1, 1.0);
				for (int p : on[i].notes) {
					hist[((p % 12) + 12) % 12] += w;
				}
			}
			double best = -1.0;
			for (int root = 0; root < 12; ++root) {
				for (int m = 0; m < 2; ++m) {
					const double r = Correlate(hist, m ? kMinor : kMajor, root);
					if (r > best) {
						best = r;
						keyRoot = root;
						keyMinor = m == 1;
					}
				}
			}
			b.key = std::clamp((best - 0.45) / 0.4, 0.0, 1.0);
		}

		// stability: the notes you rest on (held longer than the notes around them, or before a breath) and the
		// last note, on the key's home notes
		{
			const int home[3] = { 0, keyMinor ? 3 : 4, 7 };
			double    sum = 0.0;
			int       n = 0;
			std::vector<double> gaps;
			for (std::size_t i = 0; i + 1 < on.size(); ++i) {
				gaps.push_back(on[i + 1].t - on[i].t);
			}
			std::vector<double> sorted = gaps;
			std::sort(sorted.begin(), sorted.end());
			const double median = sorted.empty() ? 0.5 : sorted[sorted.size() / 2];
			for (std::size_t i = 0; i < on.size(); ++i) {
				const bool last = i + 1 == on.size();
				const bool rests = !last && gaps[i] > median * 1.7 && gaps[i] > 0.25;
				if (!rests && !(last && a_now - on[i].t > 0.4)) {
					continue;
				}
				const int deg = ((on[i].top - keyRoot) % 12 + 12) % 12;
				sum += (deg == home[0] ? 1.0 : (deg == home[1] || deg == home[2]) ? 0.8 : 0.15);
				++n;
			}
			b.stability = n == 0 ? 0.6 : 0.3 + 0.7 * (sum / n);
		}

		// rhythm: how well the gaps between onsets fit a steady beat (its half, one and a half, double)
		{
			std::vector<double> ioi;
			for (std::size_t i = 1; i < on.size(); ++i) {
				const double d = on[i].t - on[i - 1].t;
				if (d > 0.06 && d < 2.0) {
					ioi.push_back(d);
				}
			}
			if (ioi.size() >= 3) {
				static constexpr double kGrid[4] = { 0.5, 1.0, 1.5, 2.0 };
				double                  bestFit = 0.0;
				for (double T = 0.18; T <= 1.0; T += 0.005) {
					double fit = 0.0;
					for (double d : ioi) {
						double err = 1e9;
						for (double g : kGrid) {
							err = (std::min)(err, std::abs(d / T - g) / g);
						}
						fit += std::exp(-(err / 0.09) * (err / 0.09));
					}
					bestFit = (std::max)(bestFit, fit / ioi.size());
				}
				b.rhythm = std::clamp((bestFit - 0.35) / 0.55, 0.0, 1.0);
			} else {
				b.rhythm = 0.6;
			}
		}

		// melody: steps and small leaps sing, jumps all over don't; a leap answered by a step back is good writing
		if (on.size() >= 3) {
			// each interval: steps are what melodies are mostly made of, then repeats and thirds; the tritone and
			// big leaps are rare
			static constexpr double kStep[13] = { 0.7, 1.0, 1.0, 0.75, 0.7, 0.55, 0.2, 0.45, 0.25, 0.25, 0.2, 0.15, 0.3 };
			double sum = 0.0;
			int    prev = 0, stepPairs = 0, pairs = 0;
			for (std::size_t i = 1; i < on.size(); ++i) {
				const int iv = on[i].top - on[i - 1].top, d = std::abs(iv);
				double    v = d <= 12 ? kStep[d] : 0.0;
				if (std::abs(prev) >= 5 && d >= 1 && d <= 2 && (iv > 0) != (prev > 0)) {
					v = (std::min)(1.0, v + 0.3);  // the gap filled
				}
				if (i >= 2) {
					++pairs;
					stepPairs += (d <= 2 && std::abs(prev) <= 2) ? 1 : 0;  // stepwise lines, the backbone of a tune
				}
				sum += v;
				prev = iv;
			}
			const double each = sum / (on.size() - 1);
			const double lines = pairs ? std::clamp(static_cast<double>(stepPairs) / pairs / 0.4, 0.0, 1.0) : 0.5;
			b.melody = 0.6 * each + 0.4 * lines;
		} else {
			b.melody = 0.6;
		}

		// harmony: every pair of notes in a chord, and each note against the one just before it if it's still
		// ringing (within a quarter second)
		{
			double clash = 0.0, weight = 0.0;
			for (std::size_t i = 0; i < on.size(); ++i) {
				const auto& ns = on[i].notes;
				for (std::size_t x = 0; x < ns.size(); ++x) {
					for (std::size_t y = x + 1; y < ns.size(); ++y) {
						clash += kClash[std::abs(ns[x] - ns[y]) % 12];
						weight += 1.0;
					}
				}
				if (i > 0 && on[i].t - on[i - 1].t < 0.25) {
					for (int a : on[i].notes) {
						for (int c : on[i - 1].notes) {
							clash += 0.5 * kClash[std::abs(a - c) % 12];
							weight += 0.5;
						}
					}
				}
			}
			b.harmony = weight > 0.0 ? std::clamp(1.0 - 1.4 * clash / weight, 0.0, 1.0) : 1.0;
		}

		// ideas: little figures (three intervals in a row, with their rhythm) heard before in the last 16 s
		{
			const auto all = onsetsOf(mem);
			struct Gram
			{
				int    iv[3];
				int    rh[3];
				double t;
			};
			std::vector<Gram> grams;
			for (std::size_t i = 3; i < all.size(); ++i) {
				Gram g{};
				g.t = all[i].t;
				for (int k = 0; k < 3; ++k) {
					const auto& a = all[i - 3 + k];
					const auto& c = all[i - 2 + k];
					g.iv[k] = std::clamp(c.top - a.top, -24, 24);
					// the rhythm, as each gap relative to the first (halves, equal, doubles...)
					const double base = (std::max)(0.05, all[i - 2].t - all[i - 3].t);
					g.rh[k] = static_cast<int>(std::lround(std::log2((std::max)(0.05, c.t - a.t) / base) * 2.0));
				}
				grams.push_back(g);
			}
			int recent = 0, repeated = 0;
			for (std::size_t i = 0; i < grams.size(); ++i) {
				if (grams[i].t < a_now - kWindow) {
					continue;
				}
				++recent;
				const bool flat = grams[i].iv[0] == 0 && grams[i].iv[1] == 0 && grams[i].iv[2] == 0;
				if (flat) {
					continue;  // the same note over and over isn't an idea
				}
				for (std::size_t j = 0; j + 2 < i; ++j) {  // an earlier one, not overlapping
					if (std::equal(std::begin(grams[i].iv), std::end(grams[i].iv), std::begin(grams[j].iv)) &&
						std::equal(std::begin(grams[i].rh), std::end(grams[i].rh), std::begin(grams[j].rh))) {
						++repeated;
						break;
					}
				}
			}
			b.ideas = recent == 0 ? 0.0 : std::clamp(static_cast<double>(repeated) / recent / 0.35, 0.0, 1.0);
		}

		// spam: hammering one key fast, or a blur of notes nobody can follow
		{
			int run = 1, worstRun = 1, blur = 0;
			for (std::size_t i = 1; i < on.size(); ++i) {
				const double d = on[i].t - on[i - 1].t;
				if (on[i].top == on[i - 1].top && d < 0.2) {
					worstRun = (std::max)(worstRun, ++run);
				} else {
					run = 1;
				}
				blur += d < 0.08 ? 1 : 0;
			}
			int widest = 0;
			for (const auto& o : on) {
				widest = (std::max)(widest, static_cast<int>(o.notes.size()));
			}
			b.spam = 1.0;
			if (worstRun >= 6) {
				b.spam *= 0.5;
			}
			if (on.size() > 4 && blur > static_cast<int>(on.size()) / 3) {
				b.spam *= 0.5;
			}
			if (widest >= 6) {
				b.spam *= 0.5;  // a fistful of keys (a real chord is three or four notes)
			}
		}

		// the music itself (melody counts most; repeated ideas are a bonus), scaled by whether there's enough of it,
		// by clashes, and by whether it's noise
		const double core = 0.25 * b.rhythm + 0.1 * b.key + 0.15 * b.stability + 0.4 * b.melody + 0.1 * b.ideas + 0.1 * b.ideas * b.melody;
		double       q = core * (0.65 + 0.2 * b.pace + 0.15 * b.variety) * (0.35 + 0.65 * b.harmony) * b.spam;
		// the first few notes: give the benefit of the doubt
		if (on.size() < 5) {
			q = 0.5 + (q - 0.5) * (on.size() / 5.0);
		}
		b.quality = std::clamp(q, 0.0, 1.0);
		return b;
	}

	// ------------------------------------------------------------------------------------------------ ChartJudge
	void ChartJudge::Load(std::vector<music::Note> a_chart)
	{
		std::sort(a_chart.begin(), a_chart.end(), [](const music::Note& a, const music::Note& b) {
			return a.start < b.start || (a.start == b.start && a.pitch < b.pitch);
		});
		m_chart = std::move(a_chart);
		m_state.assign(m_chart.size(), State::Waiting);
		m_next = 0;
		m_quality = 0.6;
		m_perfect = m_good = m_miss = m_extra = m_streak = 0;
	}

	void ChartJudge::Rate(double a_value) { m_quality += (a_value - m_quality) * 0.22; }

	void ChartJudge::Note(double a_songTime, int a_pitch)
	{
		std::size_t best = m_chart.size();
		double      bestErr = 1e9;
		for (std::size_t i = m_next; i < m_chart.size() && m_chart[i].start <= a_songTime + kGood; ++i) {
			if (m_state[i] != State::Waiting) {
				continue;
			}
			const double err = std::abs(m_chart[i].start - a_songTime);
			if (err <= kGood && ((m_chart[i].pitch - a_pitch) % 12 + 12) % 12 == 0 && err < bestErr) {
				best = i;
				bestErr = err;
			}
		}
		if (best < m_chart.size()) {
			if (bestErr <= kPerfect) {
				m_state[best] = State::Perfect;
				++m_perfect;
				Rate(1.0);
			} else {
				m_state[best] = State::Good;
				++m_good;
				Rate(0.8);
			}
			++m_streak;
			return;
		}
		// a note that isn't in the tune: a slip (an embellishment between notes is forgiven a little)
		++m_extra;
		m_quality += (0.3 - m_quality) * 0.1;
	}

	void ChartJudge::Advance(double a_songTime)
	{
		while (m_next < m_chart.size() && m_chart[m_next].start < a_songTime - kGood) {
			if (m_state[m_next] == State::Waiting) {
				m_state[m_next] = State::Missed;
				++m_miss;
				m_streak = 0;
				Rate(0.0);
			}
			++m_next;
		}
	}

	bool ChartJudge::Done(double a_songTime) const
	{
		if (m_chart.empty()) {
			return true;
		}
		return a_songTime > m_chart.back().start + m_chart.back().dur + 1.0;
	}

	double ChartJudge::Score() const
	{
		const int judged = m_perfect + m_good + m_miss;
		if (judged == 0) {
			return 0.0;
		}
		const double s = (m_perfect + 0.75 * m_good) / judged - 0.02 * m_extra * 20.0 / (std::max)(20, judged);
		return std::clamp(s, 0.0, 1.0);
	}
}
