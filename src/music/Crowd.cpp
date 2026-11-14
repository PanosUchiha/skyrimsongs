// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Crowd.h"

#include <algorithm>
#include <cmath>

namespace music
{
	namespace
	{
		const std::vector<std::string> kHeckles = {
			"By Ysmir, my ears!",
			"Did you learn that from a skeever?",
			"Play something we know!",
			"My horker sings better than that.",
			"Stop! You're curdling the mead!",
			"Was that a song or a cry for help?",
			"Sweet Mara, make it stop.",
			"I'd rather hear a dragon gargle.",
			"Go back to the College and ask for your coin back!",
			"Boo! Off with you!",
			"Even a draugr would walk out on this.",
			"Is that thing strung with sinew?",
		};
		const std::vector<std::string> kCheers = {
			"Now that's a tune!",
			"Another! Play another!",
			"By Talos, they can play!",
			"Ha! Fingers of gold, this one.",
			"Better than the house bard, I'd say.",
			"That takes me back...",
			"Shor's bones, that's lovely.",
			"Keep playing, friend!",
		};
		const std::vector<std::string> kTips = {
			"Here, for your trouble.",
			"Keep it coming.",
			"Buy yourself some new strings.",
			"For the song.",
			"Lovely playing.",
			"Have a drink on me.",
			"",
			"",
		};
		const std::vector<std::string> kRelief = {
			"Is it over? Thank the Divines.",
			"Finally.",
			"Don't play that one again.",
		};
	}

	Crowd::Crowd(std::uint32_t a_seed) :
		m_rng(a_seed)
	{}

	double Crowd::Rand() { return std::uniform_real_distribution<double>(0.0, 1.0)(m_rng); }

	const std::string& Crowd::Pick(const std::vector<std::string>& a_lines)
	{
		return a_lines[std::uniform_int_distribution<std::size_t>(0, a_lines.size() - 1)(m_rng)];
	}

	void Crowd::Sync(const std::vector<std::uint32_t>& a_present)
	{
		std::erase_if(m_people, [&](const Person& p) { return std::find(a_present.begin(), a_present.end(), p.id) == a_present.end(); });
		for (auto id : a_present) {
			const bool known = std::any_of(m_people.begin(), m_people.end(), [&](const Person& p) { return p.id == id; });
			if (!known) {
				Person p{};
				p.id = id;
				// newcomers take their cue from the room
				p.mood = std::clamp((m_people.empty() ? 0.5 : Mood()) * 0.5 + 0.25 + (Rand() - 0.5) * 0.1, 0.2, 0.8);
				p.taste = 0.7 + Rand() * 0.6;
				p.purse = 0.5 + Rand() * 1.0;
				p.sinceTip = 4.0 * Rand();
				p.sinceShout = 6.0 * Rand();
				m_people.push_back(p);
			}
		}
	}

	double Crowd::Mood() const
	{
		if (m_people.empty()) {
			return 0.5;
		}
		double s = 0.0;
		for (const auto& p : m_people) {
			s += p.mood;
		}
		return s / m_people.size();
	}

	const char* Crowd::MoodWord(double a_mood)
	{
		return a_mood < 0.18 ? "Hostile" : a_mood < 0.35 ? "Restless" : a_mood < 0.55 ? "Listening" : a_mood < 0.78 ? "Enjoying it" : "Spellbound";
	}

	void Crowd::Step(double a_dt, double a_quality, double a_skill, bool a_playing, std::vector<CrowdEvent>& a_out)
	{
		m_sinceAnyShout += a_dt;
		for (auto& p : m_people) {
			p.sinceTip += a_dt;
			p.sinceShout += a_dt;
			if (a_playing) {
				const double d = a_quality - 0.5;
				const double k = 0.06 * m_set.patience * (d < 0.0 ? p.taste : 1.0 / p.taste);
				p.mood += d * k * a_dt;
			} else {
				p.mood += (0.5 - p.mood) * 0.01 * a_dt;
			}
			p.mood = std::clamp(p.mood, 0.0, 1.0);

			if (a_playing && p.mood > 0.58 && p.given < m_set.maxPerListener && p.sinceTip > 6.0) {
				const double lam = 0.035 * m_set.tipRate * std::pow((p.mood - 0.58) / 0.42, 1.3) * p.purse * (1.0 + a_skill * 0.5);
				if (Rand() < 1.0 - std::exp(-lam * a_dt)) {
					int gold = static_cast<int>(std::lround((1.0 + Rand() * 4.0) * (0.6 + p.mood) * p.purse * m_set.tipSize * (1.0 + a_skill)));
					gold = std::clamp(gold, 1, m_set.maxPerListener - p.given);
					p.given += gold;
					p.sinceTip = 0.0;
					m_earned += gold;
					a_out.push_back({ CrowdEvent::Kind::Tip, p.id, gold, Pick(kTips) });
					continue;
				}
			}
			if (a_playing && p.mood < 0.35 && p.sinceShout > 10.0 && m_sinceAnyShout > 3.0) {
				const double lam = 0.08 * ((0.35 - p.mood) / 0.35) * p.taste;
				if (Rand() < 1.0 - std::exp(-lam * a_dt)) {
					p.sinceShout = 0.0;
					m_sinceAnyShout = 0.0;
					a_out.push_back({ CrowdEvent::Kind::Heckle, p.id, 0, Pick(kHeckles) });
					continue;
				}
			}
			if (a_playing && p.mood > 0.8 && p.sinceShout > 20.0 && m_sinceAnyShout > 4.0) {
				if (Rand() < 1.0 - std::exp(-0.03 * a_dt)) {
					p.sinceShout = 0.0;
					m_sinceAnyShout = 0.0;
					a_out.push_back({ CrowdEvent::Kind::Cheer, p.id, 0, Pick(kCheers) });
				}
			}
		}
	}

	void Crowd::SongEnded(double a_score, double a_skill, std::vector<CrowdEvent>& a_out)
	{
		if (m_people.empty()) {
			return;
		}
		if (a_score >= 0.6) {
			int collection = 0;
			for (auto& p : m_people) {
				p.mood = std::clamp(p.mood + (a_score - 0.5) * 0.2, 0.0, 1.0);
				if (p.mood > 0.5 && p.given < m_set.maxPerListener) {
					int g = static_cast<int>(std::lround((a_score - 0.5) * 10.0 * p.purse * m_set.tipSize * (1.0 + a_skill)));
					g = std::clamp(g, 0, m_set.maxPerListener - p.given);
					p.given += g;
					collection += g;
				}
			}
			m_earned += collection;
			a_out.push_back({ CrowdEvent::Kind::Applause, m_people[static_cast<std::size_t>(Rand() * m_people.size()) % m_people.size()].id, collection,
				Pick(kCheers) });
		} else if (a_score < 0.35) {
			for (auto& p : m_people) {
				p.mood = std::clamp(p.mood - 0.05, 0.0, 1.0);
			}
			m_sinceAnyShout = 0.0;
			a_out.push_back({ CrowdEvent::Kind::Heckle, m_people[static_cast<std::size_t>(Rand() * m_people.size()) % m_people.size()].id, 0, Pick(kRelief) });
		}
	}
}
