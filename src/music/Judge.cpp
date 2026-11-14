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
		while (!m_events.empty() && m_events.front().t < a_time - kWindow) {
			m_events.pop_front();
		}
	}

	FreeJudge::Breakdown FreeJudge::Judge(double a_now) const
	{
		Breakdown b;
		std::vector<Ev> ev;
		for (const auto& e : m_events) {
			if (e.t >= a_now - kWindow && e.t <= a_now) {
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
			double t;
			int    top;
			int    size;
		};
		std::vector<Onset> on;
		for (const auto& e : ev) {
			if (!on.empty() && e.t - on.back().t < 0.04) {
				on.back().top = (std::max)(on.back().top, e.pitch);
				++on.back().size;
			} else {
				on.push_back({ e.t, e.pitch, 1 });
			}
		}
		const double span = std::clamp(a_now - (std::max)(m_first, a_now - kWindow), 1.0, kWindow);

		// pace: onsets a second
		const double rate = on.size() / span;
		b.pace = rate < 0.4 ? Ramp(rate, 0.0, 0.4, 0.0, 0.3) :
		         rate < 1.0 ? Ramp(rate, 0.4, 1.0, 0.3, 0.9) :
		         rate <= 6.0 ? 1.0 :
		                       Ramp(rate, 6.0, 11.0, 1.0, 0.25);

		// variety
		std::set<int> distinct;
		for (const auto& e : ev) {
			distinct.insert(e.pitch);
		}
		b.variety = distinct.size() <= 1 ? 0.15 : distinct.size() == 2 ? 0.5 : distinct.size() == 3 ? 0.8 : 1.0;

		// key: the best share of notes inside one major (or minor, the same notes) or harmonic minor scale
		{
			std::array<int, 12> hist{};
			for (const auto& e : ev) {
				++hist[((e.pitch % 12) + 12) % 12];
			}
			static constexpr int kMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
			static constexpr int kHarm[7] = { 0, 2, 3, 5, 7, 8, 11 };
			int best = 0;
			for (int root = 0; root < 12; ++root) {
				for (const auto* scale : { kMajor, kHarm }) {
					int in = 0;
					for (int k = 0; k < 7; ++k) {
						in += hist[(root + scale[k]) % 12];
					}
					best = (std::max)(best, in);
				}
			}
			b.key = std::clamp((static_cast<double>(best) / ev.size() - 0.55) / 0.4, 0.0, 1.0);
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

		// melody: steps and small leaps sing; jumps all over the keyboard don't
		if (on.size() >= 2) {
			double sum = 0.0;
			for (std::size_t i = 1; i < on.size(); ++i) {
				const int d = std::abs(on[i].top - on[i - 1].top);
				sum += d == 0 ? 0.6 : d <= 2 ? 1.0 : d <= 5 ? 0.85 : d <= 7 ? 0.5 : d <= 12 ? 0.25 : 0.0;
			}
			b.melody = sum / (on.size() - 1);
		} else {
			b.melody = 0.6;
		}

		// a fistful of keys at once is mashing (a real chord is three or four notes)
		int widest = 0;
		for (const auto& o : on) {
			widest = (std::max)(widest, o.size);
		}
		b.mash = widest >= 6 ? 0.4 : widest == 5 ? 0.7 : 1.0;

		// the music itself, scaled by whether there's enough of it (pace, variety)
		const double core = 0.35 * b.rhythm + 0.3 * b.key + 0.35 * b.melody;
		double       q = core * (0.6 + 0.2 * b.pace + 0.2 * b.variety) * b.mash;
		// the first few notes: give the benefit of the doubt
		if (on.size() < 4) {
			q = 0.5 + (q - 0.5) * (on.size() / 4.0);
		}
		// silence: the room drifts off
		const double gap = a_now - m_last;
		if (gap > 3.0) {
			q *= std::exp(-(gap - 3.0) / 3.0);
		}
		b.quality = std::clamp(q, 0.0, 1.0);
		return b;
	}

	// ------------------------------------------------------------------------------------------------ ChartJudge
	void ChartJudge::Load(std::vector<music::Note> a_chart)
	{
		std::sort(a_chart.begin(), a_chart.end(), [](const music::Note& a, const music::Note& b) { return a.start < b.start; });
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
