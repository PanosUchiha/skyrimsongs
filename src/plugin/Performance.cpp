// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Performance.h"

#include "Audio.h"
#include "InstrumentHud.h"
#include "Config.h"
#include "MidiIn.h"
#include "Overlay.h"

#include "../music/Crowd.h"
#include "../music/Judge.h"
#include "../music/Keymap.h"
#include "../music/Songs.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#	define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>

#undef GetObject  // Windows.h renames it to GetObjectA, and BSScript::Variable has a GetObject
#undef small      // and rpcndr.h makes "small" a type

namespace Performance
{
	namespace
	{
		using music::Instrument;

		// ------------------------------------------------------------------------------------------ game forms
		constexpr RE::FormID kGold = 0x0000000F;
		constexpr RE::FormID kInnkeeperFaction = 0x0005091B;  // JobInnkeeperFaction
		constexpr RE::FormID kLocTypeInn = 0x0001CB87;
		constexpr RE::FormID kActorTypeNPC = 0x00013794;
		// the vanilla instruments (and the quest ones, found by sharing their models)
		constexpr RE::FormID kLutes[] = { 0x000DABAB };
		constexpr RE::FormID kFlutes[] = { 0x000DABA7, 0x00105177, 0x00105109 };
		// the game's bard idles (Skyrim.esm): they put the instrument in the hands. Idles' editor IDs aren't kept
		// in memory, so these are looked up by form ID (the same ones Skyrim's Got Talent plays on the player).
		constexpr RE::FormID kIdleLute = 0x00096F8D;
		constexpr RE::FormID kIdleFlute = 0x00096F8C;
		constexpr RE::FormID kIdleStopLoose = 0x0010D9EE;
		// the audience's idles (Skyrim.esm), the ones the game's own bard audiences use (and Skyrim's Got Talent):
		// applause, dancing, drinking and cheering, and handing something over (a tip)
		constexpr RE::FormID kIdleApplaud[] = { 0x000D8730, 0x000D8731, 0x000D8732, 0x000D8733 };
		constexpr RE::FormID kIdleDance[] = { 0x000F7C8A, 0x000F7C8B };
		constexpr RE::FormID kIdleEnjoy[] = { 0x00103653, 0x00097273 };
		constexpr RE::FormID kIdleGive = 0x000B5E20;
		// and the unimpressed (animation events, as Skyrim's Got Talent sends them)
		constexpr const char* kBooEvents[] = { "idlesurrender", "idlescratchhead", "idlepointclose", "idledialoguedefensivehandgesture" };
		constexpr std::uint32_t kSaveVersion = 1;

		const char* kSongDir = "Data\\SKSE\\Plugins\\SkyrimSongs\\Songs";

		RE::TESBoundObject*            g_gold = nullptr;
		RE::TESFaction*                g_innkeepers = nullptr;
		std::set<RE::FormID>           g_instrumentIds[2];
		std::set<std::string>          g_instrumentModels[2];
		RE::TESIdleForm*               g_idles[2]{};
		RE::TESIdleForm*               g_stopIdle = nullptr;
		std::vector<RE::BGSSoundCategory*> g_musicCats;
		std::set<RE::FormID>           g_lookers;            // seated listeners turned to watch you (head and eyes; a chair won't let them turn)
		double                         g_nextBardHush = 0.0;  // when to ask the inn's bard to stop again
		RE::BGSMusicType*              g_silence = nullptr;  // a music type of nothing but silence, pushed while you play
		std::vector<RE::TESIdleForm*>  g_applaud, g_dance, g_enjoy;
		RE::TESIdleForm*               g_give = nullptr;
		bool                           g_silenced = false;

		// ------------------------------------------------------------------------------------------ state
		enum class Act
		{
			Free,   // play what you like
			Along,  // play a song from the songbook, judged note by note
			Auto    // the song plays itself (the cheat)
		};

		struct Feed
		{
			std::string text;
			ImU32       color;
			double      t;
		};

		std::recursive_mutex g_mtx;
		bool                 g_playing = false;
		std::atomic<bool>    g_wantStart{ false };
		Act                  g_act = Act::Free;
		Instrument           g_inst = Instrument::Lute;
		Instrument           g_lastInst = Instrument::Lute;
		int                  g_octave = 0;
		bool                 g_shiftL = false, g_shiftR = false;
		std::map<int, int>   g_keyPitch;  // a key that's down -> the pitch it started
		std::atomic<int>     g_instA{ 0 };  // g_inst, for the MIDI keyboard's thread
		std::atomic<bool>    g_midiLive{ false };
		bool                 g_owns[2]{};
		// notes from the MIDI keyboard: sounded at once on its thread, judged on the next frame
		struct MidiHit
		{
			int    pitch;
			double now;
			double songT;
		};
		std::mutex           g_midiMtx;
		std::vector<MidiHit> g_midiQ;
		double                          g_hudToggled = -10.0;  // when Backspace last hid or showed the interface
		std::unordered_map<int, double> g_lit;  // pitch -> when it last sounded

		music::FreeJudge  g_free;
		music::ChartJudge g_chart;
		music::Crowd      g_crowd;
		music::Song       g_song;  // the song in play (Along / Auto)
		int               g_songShift = 0;
		double            g_lastClick = -99.0;
		double            g_quality = 0.5;
		music::FreeJudge::Breakdown g_breakdown;

		std::deque<Feed>                          g_feed;
		std::unordered_map<std::uint32_t, std::string> g_names;
		std::uint32_t                             g_keeper = 0;
		bool                                      g_inInn = false;
		RE::TESObjectCELL*                        g_cell = nullptr;
		int                                       g_earned = 0;
		double                                    g_badFor = 0.0;
		bool                                      g_warned = false;
		bool                                      g_forcedThird = false;
		std::vector<std::pair<RE::BGSSoundCategory*, float>> g_muted;
		int                                       g_midiCount = 0;
		std::vector<std::string>                  g_midiNames;

		// the songbook
		struct Entry
		{
			music::Song           song;
			std::string           error;
			std::filesystem::path path;
			std::filesystem::file_time_type mtime{};
		};
		std::vector<Entry> g_files;

		// saved
		std::map<RE::FormID, float> g_bans;  // cell -> the day you were thrown out
		std::map<RE::FormID, float> g_tipped;  // listener -> the day they last tipped you (once a day each)
		int                         g_totalEarned = 0;
		int                         g_performances = 0;

		std::mt19937 g_rng{ std::random_device{}() };

		double Now()
		{
			static const auto t0 = std::chrono::steady_clock::now();
			return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		}

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		void Hud(const std::string& a_text) { RE::SendHUDMessage::ShowHUDMessage(a_text.c_str()); }

		void Say(std::string a_text, ImU32 a_color)
		{
			g_feed.push_back({ std::move(a_text), a_color, Now() });
			while (g_feed.size() > 7) {
				g_feed.pop_front();
			}
		}

		constexpr ImU32 kInk = IM_COL32(242, 236, 222, 255);
		constexpr ImU32 kGoldCol = IM_COL32(230, 190, 105, 255);
		constexpr ImU32 kGreen = IM_COL32(150, 215, 130, 255);
		constexpr ImU32 kRed = IM_COL32(235, 110, 95, 255);
		constexpr ImU32 kDim = IM_COL32(190, 180, 160, 255);

		template <class Self, class... Args>
		void Call(Self* a_self, const char* a_fn, Args... a_args)
		{
			if (!a_self) {
				return;
			}
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return;
			}
			RE::BSScript::Variable v;
			RE::BSScript::PackValue(&v, a_self);
			auto obj = v.GetObject();
			if (!obj) {
				spdlog::warn("Papyrus call {}: no script object for {:08X}", a_fn, a_self->GetFormID());
				return;
			}
			auto*                                                    args = RE::MakeFunctionArguments(std::move(a_args)...);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
			vm->DispatchMethodCall1(obj, a_fn, args, cb);
		}

		const char* InstName(Instrument a_i) { return a_i == Instrument::Lute ? "lute" : "flute"; }
		int         Base() { return (g_inst == Instrument::Lute ? 36 : 60) + g_octave * 12; }  // the bottom row's C: lute C2-C5, flute C4-C7

		// ------------------------------------------------------------------------------------------ instruments
		std::string Lower(const char* a_s)
		{
			std::string s = a_s ? a_s : "";
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// "lute" in a name or file name, but not inside "flute" (or "absolute", "resolute"...)
		bool HasWord(const std::string& a_s, const char* a_word)
		{
			for (auto at = a_s.find(a_word); at != std::string::npos; at = a_s.find(a_word, at + 1)) {
				const bool startOk = at == 0 || !std::isalpha(static_cast<unsigned char>(a_s[at - 1]));
				if (startOk) {
					return true;
				}
			}
			return false;
		}

		// 0 lute, 1 flute, -1 neither. The game's own (by form ID and model), and anything from a mod - misc item,
		// weapon, armour you wear on your back - with "lute" or "flute" in its name or its model's file name.
		int InstrumentKind(RE::TESBoundObject& a_obj)
		{
			for (int k = 0; k < 2; ++k) {
				if (g_instrumentIds[k].contains(a_obj.GetFormID())) {
					return k;
				}
			}
			std::string model;
			if (const auto* m = a_obj.As<RE::TESModel>(); m && m->GetModel()) {
				model = Lower(m->GetModel());
				for (int k = 0; k < 2; ++k) {
					if (g_instrumentModels[k].contains(model)) {
						return k;
					}
				}
				if (const auto slash = model.find_last_of("\\/"); slash != std::string::npos) {
					model = model.substr(slash + 1);
				}
			}
			const std::string name = Lower(a_obj.GetName());
			if (HasWord(name, "flute") || HasWord(model, "flute")) {
				return 1;
			}
			if (HasWord(name, "lute") || HasWord(model, "lute")) {
				return 0;
			}
			return -1;
		}

		bool IsInstrument(RE::TESBoundObject& a_obj, int a_k) { return InstrumentKind(a_obj) == a_k; }

		// what's in the pack that might be meant as an instrument, for the log when nothing is recognised
		void LogInstrumentLikeItems()
		{
			auto* p = Player();
			if (!p) {
				return;
			}
			int shown = 0;
			for (const auto& [obj, n] : p->GetInventoryCounts()) {
				if (!obj || n <= 0) {
					continue;
				}
				const std::string name = Lower(obj->GetName());
				std::string       model;
				if (const auto* m = obj->As<RE::TESModel>(); m && m->GetModel()) {
					model = Lower(m->GetModel());
				}
				if (name.find("lute") != std::string::npos || name.find("instrument") != std::string::npos || model.find("lute") != std::string::npos ||
					model.find("instrument") != std::string::npos || name.find("drum") != std::string::npos) {
					spdlog::info("  in your pack: {:08X} '{}' type {} model '{}' x{}", obj->GetFormID(), obj->GetName(), static_cast<int>(obj->GetFormType()), model, n);
					++shown;
				}
			}
			if (!shown) {
				spdlog::info("  nothing in your pack is named or modelled like a lute or flute");
			}
		}

		bool Owns(Instrument a_i)
		{
			if (!Config::Get().requireInstrument) {
				return true;
			}
			auto* p = Player();
			if (!p) {
				return false;
			}
			const int k = a_i == Instrument::Lute ? 0 : 1;
			for (const auto& [obj, n] : p->GetInventoryCounts([k](RE::TESBoundObject& a_obj) { return IsInstrument(a_obj, k); })) {
				if (n > 0) {
					return true;
				}
			}
			return false;
		}

		// The bard idle, played straight through the player's AI process (what Papyrus's PlayIdle does), a moment
		// after you take the instrument out (the camera has to settle into third person first), retried a few times
		// if the game refuses it, then through Papyrus as a last try.
		double g_idleAt = -1.0;
		int    g_idleTries = 0;

		bool NativeIdle(RE::TESIdleForm* a_idle)
		{
			auto* p = Player();
			if (!p || !a_idle) {
				return false;
			}
			auto* proc = p->GetActorRuntimeData().currentProcess;
			return proc && proc->PlayIdle(p, a_idle, nullptr);
		}

		void PlayIdleFor(Instrument a_i, double a_delay = 0.4)
		{
			(void)a_i;
			g_idleAt = Now() + a_delay;
			g_idleTries = 0;
		}

		void RunIdle()
		{
			auto* idle = g_idles[g_inst == Instrument::Lute ? 0 : 1];
			if (!idle) {
				spdlog::warn("No {} idle to play", InstName(g_inst));
				g_idleAt = -1.0;
				return;
			}
			if (NativeIdle(idle)) {
				spdlog::info("Playing the {} idle {:08X} (try {})", InstName(g_inst), idle->GetFormID(), g_idleTries + 1);
				g_idleAt = -1.0;
				return;
			}
			if (++g_idleTries < 12) {
				spdlog::info("The game isn't ready for the {} idle yet (try {})", InstName(g_inst), g_idleTries);
				g_idleAt = Now() + 0.25;
				return;
			}
			spdlog::warn("The game refused the {} idle {:08X} twelve times; asking through Papyrus", InstName(g_inst), idle->GetFormID());
			Call(Player(), "PlayIdle", idle);
			g_idleAt = -1.0;
		}

		void StopIdle()
		{
			g_idleAt = -1.0;
			auto* p = Player();
			if (g_stopIdle && NativeIdle(g_stopIdle)) {
				return;
			}
			if (g_stopIdle) {
				Call(p, "PlayIdle", g_stopIdle);
			} else if (p) {
				p->NotifyAnimationGraph("IdleForceDefaultState");
			}
		}

		// ------------------------------------------------------------------------------------------ gold
		int GoldOf(RE::Actor* a_actor)
		{
			// (not Actor::GetGoldAmount: it goes through the default-object table, which crashed on 1.5.97 in Yacht)
			if (!a_actor) {
				return 0;
			}
			for (const auto& [obj, n] : a_actor->GetInventoryCounts([](RE::TESBoundObject& a_obj) { return a_obj.GetFormID() == kGold; })) {
				return n;
			}
			return 0;
		}

		void GiveGold(int a_n)
		{
			auto* p = Player();
			if (!p || !g_gold || a_n <= 0) {
				return;
			}
			p->AddObjectToContainer(g_gold, nullptr, a_n, nullptr);
			g_earned += a_n;
			g_totalEarned += a_n;
			if (Config::Get().speechXp > 0.0f) {
				p->AddSkillExperience(RE::ActorValue::kSpeech, a_n * Config::Get().speechXp);
			}
			if (auto* s = Audio::Synth()) {
				s->Coin(std::clamp(0.4f + a_n * 0.05f, 0.4f, 1.0f));
			}
		}

		double Speech01()
		{
			auto* p = Player();
			if (!p) {
				return 0.0;
			}
			return std::clamp(p->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech) / 100.0, 0.0, 1.0);
		}

		// ------------------------------------------------------------------------------------------ the game's music
		void MuteMusic(bool a_mute)
		{
			// the music: a silent music type on top of the game's (the way Skyrim's Got Talent does it)
			if (a_mute && Config::Get().muteMusic && g_silence && !g_silenced) {
				Call(g_silence, "Add");
				g_silenced = true;
			} else if (!a_mute && g_silenced) {
				Call(g_silence, "Remove");
				g_silenced = false;
			}
			if (a_mute) {
				if (!Config::Get().muteMusic || !g_muted.empty()) {
					return;
				}
				for (auto* c : g_musicCats) {
					g_muted.push_back({ c, c->GetCategoryVolume() });
					c->SetCategoryVolume(0.0f);
				}
			} else {
				for (auto& [c, v] : g_muted) {
					c->SetCategoryVolume(v);
				}
				g_muted.clear();
			}
		}

		// ------------------------------------------------------------------------------------------ notes
		void SetInst(Instrument a_i)
		{
			g_inst = a_i;
			g_instA = static_cast<int>(a_i);
		}

		// a note you played: lit on the keyboard and judged
		void Register(int a_pitch, double a_now, double a_songT)
		{
			g_lit[a_pitch] = a_now;
			switch (g_act) {
			case Act::Free:
				g_free.Note(a_now, a_pitch);
				break;
			case Act::Along:
				g_chart.Note(a_songT - Config::Get().latencyMs / 1000.0, a_pitch);
				break;
			case Act::Auto:
				break;
			}
		}

		void Sound(int a_pitch, float a_vel)
		{
			if (auto* s = Audio::Synth()) {
				s->NoteOn(g_inst, a_pitch, a_vel, true);
			}
			Register(a_pitch, Now(), g_act == Act::Along ? Audio::SongTime() : 0.0);
		}

		void Unsound(int a_pitch)
		{
			if (auto* s = Audio::Synth()) {
				s->NoteOff(g_inst, a_pitch, true);
			}
		}

		// On the MIDI driver's thread: the note sounds straight away (the synth takes its own lock); the rest waits
		// for the frame. Never takes the performance's lock, so closing the device can't deadlock with it.
		void OnMidi(int a_status, int a_d1, int a_d2)
		{
			if (!g_midiLive.load()) {
				return;
			}
			auto* s = Audio::Synth();
			if (!s) {
				return;
			}
			const auto inst = static_cast<Instrument>(g_instA.load());
			const int  kind = a_status & 0xF0;
			if (kind == 0x90 && a_d2 > 0) {
				s->NoteOn(inst, a_d1, std::clamp(a_d2 / 127.0f, 0.1f, 1.0f), true);
				const MidiHit hit{ a_d1, Now(), Audio::SongActive() ? Audio::SongTime() : 0.0 };
				std::lock_guard lk(g_midiMtx);
				if (g_midiQ.size() < 512) {
					g_midiQ.push_back(hit);
				}
			} else if (kind == 0x80 || (kind == 0x90 && a_d2 == 0)) {
				s->NoteOff(inst, a_d1, true);
			} else if (kind == 0xB0 && a_d1 == 64) {
				s->Sustain(a_d2 >= 64);
			} else if (kind == 0xB0 && (a_d1 == 120 || a_d1 == 123)) {
				s->AllOff();
			}
		}

		void ReleaseAllKeys()
		{
			for (const auto& [scan, pitch] : g_keyPitch) {
				Unsound(pitch);
			}
			g_keyPitch.clear();
			if (auto* s = Audio::Synth()) {
				s->Sustain(false);
				s->AllOff();
			}
		}

		void RefreshOwned()
		{
			g_owns[0] = Owns(Instrument::Lute);
			g_owns[1] = Owns(Instrument::Flute);
		}

		// ------------------------------------------------------------------------------------------ the songbook
		void LoadSongs()
		{
			std::error_code ec;
			std::filesystem::create_directories(kSongDir, ec);
			std::vector<Entry> found;
			for (const auto& de : std::filesystem::directory_iterator(kSongDir, ec)) {
				auto ext = de.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext != ".mid" && ext != ".midi" && ext != ".rmi") {
					continue;
				}
				const auto mtime = de.last_write_time(ec);
				// already read and unchanged
				auto old = std::find_if(g_files.begin(), g_files.end(), [&](const Entry& a) { return a.path == de.path(); });
				if (old != g_files.end() && old->mtime == mtime) {
					found.push_back(std::move(*old));
					continue;
				}
				Entry e;
				e.path = de.path();
				e.mtime = mtime;
				const auto title = e.path.stem().u8string();
				std::ifstream f(e.path, std::ios::binary);
				std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
				if (bytes.size() > 8 * 1024 * 1024) {
					e.error = "too big";
					e.song.title = std::string(title.begin(), title.end());
				} else if (!music::SongFromMidi(bytes, std::string(title.begin(), title.end()), e.song, &e.error)) {
					e.song.title = std::string(title.begin(), title.end());
					spdlog::warn("Songbook: {}: {}", e.path.filename().string(), e.error);
				} else {
					spdlog::info("Songbook: {} ({} notes, melody {}, {:.0f} s)", e.path.filename().string(), e.song.all.size(), e.song.melody.size(),
						e.song.length);
				}
				found.push_back(std::move(e));
			}
			std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) { return _stricmp(a.song.title.c_str(), b.song.title.c_str()) < 0; });
			g_files = std::move(found);
		}

		void BackToFree()
		{
			Audio::StopSong();
			g_act = Act::Free;
			g_song = {};
			g_free.Reset();
		}

		void StartAlong(const music::Song& a_song)
		{
			BackToFree();
			g_song = a_song;
			g_songShift = music::FitOctaves(g_song.melody, Base(), 36);
			std::vector<music::Note> chart = g_song.melody;
			for (auto& n : chart) {
				n.pitch += g_songShift;
			}
			g_chart.Load(std::move(chart));
			g_act = Act::Along;
			g_lastClick = -99.0;
			Audio::PlaySong({}, g_inst, Config::Get().speed, 3.0, false);
			Say(fmt::format("Play along: {}", g_song.title), kGoldCol);
			spdlog::info("Play along: {} ({} notes, shifted {} semitones, speed {:.2f})", g_song.title, g_chart.Chart().size(), g_songShift, Config::Get().speed);
		}

		void StartAuto(const music::Song& a_song)
		{
			BackToFree();
			g_song = a_song;
			// the song as written, every note from C2 to A7 (like a sequencer's piano roll); anything beyond is
			// moved by octaves into it
			g_songShift = 0;
			const std::size_t before = g_song.all.size();
			auto notes = music::FoldIntoRange(g_song.all);
			spdlog::info("  {} notes, {} doubled ones dropped after folding into the {}'s range", before, before - notes.size(), InstName(g_inst));
			g_song.all = notes;
			g_act = Act::Auto;
			Audio::PlaySong(notes, g_inst, Config::Get().speed, 1.0, true);
			Say(fmt::format("You play {}.", g_song.title), kDim);
			spdlog::info("Let it play: {} ({} notes)", g_song.title, notes.size());
		}

		// ------------------------------------------------------------------------------------------ the audience moves
		// Listeners react with the game's own audience idles: a tipper hands the coin over, a delighted room claps,
		// dances and raises a mug, an unhappy one scratches its head and points. Happy listeners also come over to
		// stand in front of you while you play (KeepOffsetFromActor, which overrides their AI until you stop).
		constexpr int kMaxVisitors = 4;
		// A visitor walks (PathToReference) to an invisible marker on an arc in front of where you stood when you
		// took the instrument out, then stays put (SetDontMove), turned to face you and looking at you. (Not
		// KeepOffsetFromActor: that keeps them at an offset in your own frame, so every turn of the camera swung
		// them round you in circles.)
		constexpr RE::FormID kXMarker = 0x0000003B;
		struct Visitor
		{
			RE::FormID          id = 0;
			int                 slot = 0;
			double              leaveAt = 0.0;  // they have places to be: each stays a while, then wanders off mid-song
			double              giveUpAt = 0.0;  // couldn't get there
			RE::ObjectRefHandle marker;
			RE::NiPoint3        spot;
			bool                arrived = false;
		};
		RE::NiPoint3 g_anchor;
		float        g_anchorHeading = 0.0f;
		std::unordered_map<RE::FormID, double> g_awayUntil;  // wandered off: not coming back for a while
		std::vector<Visitor>                     g_visitors;
		std::unordered_map<RE::FormID, double>   g_nextMove;  // listener -> when they may move again
		std::vector<RE::FormID>                  g_staleVisitors;  // saved mid-performance: sent home after the load

		RE::Actor* ActorOf(RE::FormID a_id) { return RE::TESForm::LookupByID<RE::Actor>(a_id); }
		bool       IsVisitor(RE::FormID a_id)
		{
			return std::any_of(g_visitors.begin(), g_visitors.end(), [&](const Visitor& v) { return v.id == a_id; });
		}

		std::string NameOfActor(RE::Actor* a_actor)
		{
			const char* n = a_actor ? a_actor->GetDisplayFullName() : nullptr;
			return n && *n ? n : "Someone";
		}

		bool NpcIdle(RE::Actor* a_actor, RE::TESIdleForm* a_idle)
		{
			if (!a_actor || !a_idle || a_actor->IsDead() || !a_actor->Is3DLoaded()) {
				return false;
			}
			auto* proc = a_actor->GetActorRuntimeData().currentProcess;
			return proc && proc->PlayIdle(a_actor, a_idle, nullptr);
		}

		bool Seated(RE::Actor* a_actor)
		{
			auto* st = a_actor ? a_actor->AsActorState() : nullptr;
			return st && st->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal;
		}

		// turn to you first: an idle plays the way the actor faces, and many were facing away
		void FacePlayer(RE::Actor* a_actor)
		{
			auto* player = Player();
			if (!a_actor || !player) {
				return;
			}
			if (Seated(a_actor)) {
				// a chair holds them where they sit: they turn their head and watch you instead
				if (g_lookers.insert(a_actor->GetFormID()).second) {
					Call(a_actor, "SetLookAt", static_cast<RE::TESObjectREFR*>(player), false);
				}
				return;
			}
			const auto at = a_actor->GetPosition(), me = player->GetPosition();
			a_actor->SetHeading(std::atan2(me.x - at.x, me.y - at.y));  // Skyrim's heading: 0 = north (+y), clockwise
		}

		template <class T>
		const T& PickOf(const std::vector<T>& a_v)
		{
			return a_v[std::uniform_int_distribution<std::size_t>(0, a_v.size() - 1)(g_rng)];
		}

		void React(RE::FormID a_id, int a_kind)  // 0 tip, 1 cheer/applause, 2 boo, 3 enjoy (dance / drink)
		{
			auto* a = ActorOf(a_id);
			if (!a || !Config::Get().reactions) {
				return;
			}
			FacePlayer(a);
			switch (a_kind) {
			case 0:
				NpcIdle(a, g_give);
				break;
			case 1:
				if (!g_applaud.empty()) {
					NpcIdle(a, PickOf(g_applaud));
				}
				break;
			case 2:
				a->NotifyAnimationGraph(kBooEvents[std::uniform_int_distribution<int>(0, 3)(g_rng)]);
				break;
			default:
				{
					const bool dance = !g_dance.empty() && std::uniform_int_distribution<int>(0, 5)(g_rng) == 0;  // a dance now and then
					const auto& set = dance ? g_dance : (g_enjoy.empty() ? g_applaud : g_enjoy);
					if (!set.empty()) {
						NpcIdle(a, PickOf(set));
					}
				}
				break;
			}
			g_nextMove[a_id] = Now() + 15.0 + std::uniform_real_distribution<double>(0.0, 20.0)(g_rng);
		}

		void ReleaseActor(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return;
			}
			Call(a_actor, "SetDontMove", false);
			Call(a_actor, "ClearLookAt");
			Call(a_actor, "EvaluatePackage");  // back to their own business (ends the walk, if they were still on it)
		}

		void SendAway(const Visitor& a_v)
		{
			ReleaseActor(ActorOf(a_v.id));
			if (auto m = a_v.marker.get()) {
				Call(m.get(), "Delete");
			}
		}

		void SendAllAway()
		{
			for (const auto& v : g_visitors) {
				SendAway(v);
			}
			g_visitors.clear();
			g_nextMove.clear();
			g_awayUntil.clear();
		}

		// every half second: who comes over, who drifts off, and the room's own little movements
		void MoveAudience(const std::vector<std::uint32_t>& a_present, RE::FormID a_keeper)
		{
			const auto& cfg = Config::Get();
			auto*       player = Player();
			if (!player) {
				return;
			}
			const double now = Now();
			// visitors who've reached their spot stop there, turn to you and watch
			for (auto& v : g_visitors) {
				if (v.arrived) {
					continue;
				}
				auto* a = ActorOf(v.id);
				if (!a) {
					continue;
				}
				const auto at = a->GetPosition();
				if (std::hypot(at.x - v.spot.x, at.y - v.spot.y) < 70.0f) {
					v.arrived = true;
					Call(a, "SetDontMove", true);
					FacePlayer(a);
					Call(a, "SetLookAt", static_cast<RE::TESObjectREFR*>(player), false);
					g_nextMove[v.id] = now + 3.0;
				}
			}
			// visitors who've cooled off go back to what they were doing, and any of them may just wander off
			std::erase_if(g_visitors, [&](const Visitor& v) {
				const double mood = g_crowd.MoodOf(v.id);
				if (!v.arrived && now >= v.giveUpAt) {
					SendAway(v);  // couldn't find a way: never mind
					g_awayUntil[v.id] = now + 120.0;
					return true;
				}
				if (mood < 0.45 || now >= v.leaveAt) {
					SendAway(v);
					if (mood >= 0.45) {
						g_awayUntil[v.id] = now + std::uniform_real_distribution<double>(90.0, 240.0)(g_rng);
						if (auto* a = ActorOf(v.id)) {
							Say(fmt::format("{} wanders off.", NameOfActor(a)), kDim);
						}
					}
					return true;
				}
				return false;
			});
			// happy listeners come over (not the innkeeper: someone has to mind the bar)
			const int most = (std::min)(cfg.maxVisitors, kMaxVisitors);
			for (auto id : a_present) {
				if (static_cast<int>(g_visitors.size()) >= most) {
					break;
				}
				if (auto away = g_awayUntil.find(id); away != g_awayUntil.end() && now < away->second) {
					continue;
				}
				if (id == a_keeper || g_crowd.MoodOf(id) < 0.65 ||
					std::any_of(g_visitors.begin(), g_visitors.end(), [&](const Visitor& v) { return v.id == id; })) {
					continue;
				}
				auto* a = ActorOf(id);
				if (!a || a->IsPlayerTeammate()) {
					continue;  // followers are with you already
				}
				int slot = 0;
				while (std::any_of(g_visitors.begin(), g_visitors.end(), [&](const Visitor& v) { return v.slot == slot; })) {
					++slot;
				}
				// a little arc in front of where you stood (in the world, not your frame), each with a marker to walk to
				static constexpr float kAngles[kMaxVisitors] = { -20.0f, 25.0f, -55.0f, 60.0f };
				const float            ang = g_anchorHeading + kAngles[slot] * 3.14159265f / 180.0f, r = 210.0f;
				const RE::NiPoint3     spot{ g_anchor.x + r * std::sin(ang), g_anchor.y + r * std::cos(ang), g_anchor.z };
				auto*                  markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
				auto                   marker = markerBase ? player->PlaceObjectAtMe(markerBase, false) : RE::NiPointer<RE::TESObjectREFR>{};
				if (!marker) {
					continue;
				}
				marker->SetPosition(spot);
				Call(a, "PathToReference", marker.get(), 0.5f);  // a walk
				Visitor v;
				v.id = id;
				v.slot = slot;
				// how long they stay: anything from half a minute to a few minutes
				v.leaveAt = now + std::uniform_real_distribution<double>(30.0, 180.0)(g_rng);
				v.giveUpAt = now + 25.0;
				v.marker = marker->GetHandle();
				v.spot = spot;
				g_visitors.push_back(v);
				g_nextMove[id] = now + 1e9;  // no moves until they've got there
				spdlog::info("{} comes over to listen", NameOfActor(a));
			}
			// the room's movements
			if (!cfg.reactions) {
				return;
			}
			for (auto id : a_present) {
				auto it = g_nextMove.find(id);
				if (it != g_nextMove.end() && now < it->second) {
					continue;
				}
				const double mood = g_crowd.MoodOf(id);
				if (mood > 0.75 && std::uniform_int_distribution<int>(0, 2)(g_rng) == 0) {
					React(id, std::uniform_int_distribution<int>(0, 1)(g_rng) == 0 ? 1 : 3);
				} else if (mood >= 0.0 && mood < 0.3 && std::uniform_int_distribution<int>(0, 3)(g_rng) == 0) {
					React(id, 2);
				} else {
					g_nextMove[id] = now + 4.0 + std::uniform_real_distribution<double>(0.0, 6.0)(g_rng);
				}
			}
		}

		// ------------------------------------------------------------------------------------------ the crowd
		// a beggar: the Beggar class (Edda, Snilf, Brenuin...), or named as one
		bool IsBeggar(RE::Actor* a_actor)
		{
			auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
			auto  has = [](const char* a_s) {
				if (!a_s) {
					return false;
				}
				std::string l = a_s;
				std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return l.find("beggar") != std::string::npos;
			};
			return (base && base->npcClass && has(base->npcClass->GetFullName())) || has(a_actor->GetDisplayFullName());
		}

		// the inn's bard stops while you play: Skyrim's own "stop playing" (BardSongsScript.StopAllSongs), asked again
		// now and then, as a bard will start up again by themselves after a while
		void HushBards()
		{
			const double now = Now();
			if (now < g_nextBardHush) {
				return;
			}
			g_nextBardHush = now + 8.0;
			static RE::TESQuest* quest = [] {
				auto* q = RE::TESForm::LookupByEditorID<RE::TESQuest>("BardSongs");
				if (!q) {
					q = RE::TESForm::LookupByID<RE::TESQuest>(0x74A55);
				}
				spdlog::info("Bards: {}", q ? fmt::format("BardSongs quest {:08X}", q->GetFormID()) : std::string("BardSongs quest not found"));
				return q;
			}();
			if (quest && quest->IsRunning()) {
				Call(quest, "StopAllSongs");
			}
		}

		void ScanListeners()
		{
			auto* pl = RE::ProcessLists::GetSingleton();
			auto* player = Player();
			if (!pl || !player) {
				return;
			}
			auto*      cell = player->GetParentCell();
			const bool interior = cell && cell->IsInteriorCell();
			const auto here = player->GetPosition();
			const float range = Config::Get().listenRange;
			std::vector<std::uint32_t> ids, beggars;
			g_keeper = 0;
			for (auto& h : pl->highActorHandles) {
				auto ap = h.get();
				auto* a = ap.get();
				if (!a || a->IsPlayerRef() || a->IsDead() || a->IsChild() || !a->Is3DLoaded() || a->IsDisabled()) {
					continue;
				}
				if (a->IsInCombat() || a->IsHostileToActor(player)) {
					continue;
				}
				auto* race = a->GetRace();
				if (!race || !race->HasKeywordID(kActorTypeNPC)) {
					continue;  // people, not dogs and horses
				}
				if (interior && a->GetParentCell() != cell) {
					continue;
				}
				if (a->GetPosition().GetDistance(here) > range) {
					continue;
				}
				if (auto* st = a->AsActorState(); st && st->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping) {
					continue;
				}
				const auto id = a->GetFormID();
				ids.push_back(id);
				const char* name = a->GetDisplayFullName();
				g_names[id] = name && *name ? name : "Someone";
				if (g_innkeepers && a->IsInFaction(g_innkeepers)) {
					g_keeper = id;
				}
				// beggars listen, but have nothing to give
				if (IsBeggar(a)) {
					beggars.push_back(id);
				}
			}
			// who already tipped you today
			const float               today = RE::Calendar::GetSingleton() ? std::floor(RE::Calendar::GetSingleton()->GetDaysPassed()) : 0.0f;
			std::vector<std::uint32_t> noTips = beggars;
			for (auto id : ids) {
				if (auto it = g_tipped.find(id); it != g_tipped.end() && std::floor(it->second) >= today) {
					noTips.push_back(id);
				}
			}
			g_crowd.Sync(ids, &noTips);
			MoveAudience(ids, g_keeper);
			for (auto id : ids) {
				if (auto* a = ActorOf(id); a && Seated(a)) {
					FacePlayer(a);
				}
			}
			for (auto it = g_lookers.begin(); it != g_lookers.end();) {
				auto* a = ActorOf(*it);
				if (!a || std::find(ids.begin(), ids.end(), *it) == ids.end() || !Seated(a)) {
					if (a && !Seated(a)) {
						// stood up: let them be (a visitor's own look-at is set when they arrive)
						if (!IsVisitor(*it)) {
							Call(a, "ClearLookAt");
						}
					} else if (a) {
						Call(a, "ClearLookAt");
					}
					it = g_lookers.erase(it);
				} else {
					++it;
				}
			}
			HushBards();
			bool innLoc = false;
			if (auto* loc = player->GetCurrentLocation()) {
				innLoc = loc->HasKeywordID(kLocTypeInn);
			}
			g_inInn = interior && (g_keeper != 0 || innLoc);
		}

		std::string NameOf(std::uint32_t a_id)
		{
			auto it = g_names.find(a_id);
			return it == g_names.end() ? "Someone" : it->second;
		}

		void MarkTipped(RE::FormID a_id)
		{
			g_tipped[a_id] = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f;
		}

		void HandleCrowd(const std::vector<music::CrowdEvent>& a_events)
		{
			for (const auto& e : a_events) {
				switch (e.kind) {
				case music::CrowdEvent::Kind::Tip:
					GiveGold(e.gold);
					MarkTipped(e.id);
					React(e.id, 0);
					Say(e.line.empty() ? fmt::format("{} tosses you {} septim{}.", NameOf(e.id), e.gold, e.gold == 1 ? "" : "s") :
					                     fmt::format("{} tosses you {} septim{}. \"{}\"", NameOf(e.id), e.gold, e.gold == 1 ? "" : "s", e.line),
						kGoldCol);
					break;
				case music::CrowdEvent::Kind::Heckle:
					React(e.id, 2);
					Say(fmt::format("{}: \"{}\"", NameOf(e.id), e.line), kRed);
					break;
				case music::CrowdEvent::Kind::Cheer:
					React(e.id, 1);
					Say(fmt::format("{}: \"{}\"", NameOf(e.id), e.line), kGreen);
					break;
				case music::CrowdEvent::Kind::Applause:
					GiveGold(e.gold);
					for (auto id : e.payers) {
						MarkTipped(id);
					}
					for (auto& [id, name] : g_names) {
						if (g_crowd.MoodOf(id) >= 0.5) {
							React(id, 1);
						}
					}
					Say(e.gold > 0 ? fmt::format("Applause! The room puts together {} septims. {}: \"{}\"", e.gold, NameOf(e.id), e.line) :
					                 fmt::format("Applause! {}: \"{}\"", NameOf(e.id), e.line),
						kGreen);
					break;
				}
			}
		}

		// ------------------------------------------------------------------------------------------ start / stop
		bool BlockedByUi()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return true;
			}
			if (ui->GameIsPaused() || ui->IsApplicationMenuOpen() || ui->IsItemMenuOpen() || ui->IsModalMenuOpen() ||
				ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) {
				return true;
			}
			if (auto* cm = RE::ControlMap::GetSingleton(); cm && (cm->GetRuntimeData().textEntryCount > 0 || !cm->IsMovementControlsEnabled())) {
				return true;
			}
			return false;
		}

		// why you can't play right now ("" if you can)
		std::string CantPlay()
		{
			auto* p = Player();
			if (!p || !p->Is3DLoaded() || p->IsDead()) {
				return "-";
			}
			if (p->IsInCombat()) {
				return "Not in the middle of a fight.";
			}
			auto* st = p->AsActorState();
			if (st && st->IsWeaponDrawn()) {
				return "Put your weapon away first.";
			}
			if (p->IsOnMount()) {
				return "Get down from your horse first.";
			}
			if (st && st->IsSwimming()) {
				return "Not while swimming.";
			}
			if (st && st->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
				return "Stand up to play.";
			}
			if (auto* cell = p->GetParentCell(); cell && cell->IsInteriorCell()) {
				if (auto it = g_bans.find(cell->GetFormID()); it != g_bans.end()) {
					const float today = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f;
					if (today - it->second < 1.0f) {
						return "Not in here. You were thrown out earlier today.";
					}
				}
			}
			return "";
		}

		void Stop(const char* a_why);

		void Begin()
		{
			if (BlockedByUi()) {
				return;
			}
			const auto why = CantPlay();
			if (!why.empty()) {
				if (why != "-") {
					Hud(why);
				}
				return;
			}
			auto* p0 = Player();
			// your instrument: the last one you played, if you still have it
			RefreshOwned();
			const bool lute = g_owns[0], flute = g_owns[1];
			if (!lute && !flute) {
				Hud("You need a lute or a flute to play.");
				spdlog::info("No lute or flute recognised in your pack:");
				LogInstrumentLikeItems();
				return;
			}
			SetInst((g_lastInst == Instrument::Lute ? lute : !flute) ? Instrument::Lute : Instrument::Flute);
			if (!Audio::Start()) {
				Hud("SkyrimSongs: no sound device found.");
				return;
			}
			Audio::SetVolume(Config::Get().volume);
			Audio::SetReverb(Config::Get().reverb);
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && Config::Get().thirdPerson && cam->IsInFirstPerson()) {
				cam->ForceThirdPerson();
				g_forcedThird = true;
			}
			// both hands are on the instrument: a torch or shield in the left hand goes away (as in Skyrim's Got Talent)
			if (auto* left = p0->GetEquippedObject(true); left && (left->Is(RE::FormType::Light) || left->Is(RE::FormType::Armor))) {
				if (auto* bound = left->As<RE::TESBoundObject>()) {
					RE::ActorEquipManager::GetSingleton()->UnequipObject(p0, bound);
				}
			}
			PlayIdleFor(g_inst);
			MuteMusic(true);
			g_midiCount = MidiIn::Open(&OnMidi);
			g_midiNames = MidiIn::OpenNames();
			g_playing = true;
			g_act = Act::Free;
			g_octave = 0;
			g_shiftL = g_shiftR = false;
			g_keyPitch.clear();
			g_lit.clear();
			{
				std::lock_guard mk(g_midiMtx);
				g_midiQ.clear();
			}
			g_free.Reset();
			g_crowd = music::Crowd(static_cast<std::uint32_t>(g_rng()));
			music::Crowd::Settings cs;
			cs.tipRate = Config::Get().tipRate;
			cs.tipSize = Config::Get().tipSize;
			cs.maxPerListener = Config::Get().maxTipPerListener;
			cs.patience = Config::Get().patience;
			cs.oneTipEach = true;  // and once a day: see g_tipped
			g_crowd.SetSettings(cs);
			g_names.clear();
			g_feed.clear();
			g_earned = 0;
			g_badFor = 0.0;
			g_warned = false;
			g_cell = Player()->GetParentCell();
			g_anchor = Player()->GetPosition();
			g_anchorHeading = Player()->GetAngleZ();
			g_quality = 0.5;
			++g_performances;
			ScanListeners();
			Say(fmt::format("You take out your {}.{}", InstName(g_inst),
					g_midiCount ? fmt::format(" (MIDI: {})", g_midiNames.empty() ? "connected" : g_midiNames.front()) : ""),
				kInk);
			Overlay::SetMode(Overlay::Mode::Playing);
			spdlog::info("Playing the {}: {} listening{}, MIDI devices {}", InstName(g_inst), g_crowd.Size(), g_inInn ? " (an inn)" : "", g_midiCount);
		}

		void Stop(const char* a_why)
		{
			if (!g_playing) {
				return;
			}
			g_playing = false;
			g_midiLive = false;
			SendAllAway();
			for (auto id : g_lookers) {
				Call(ActorOf(id), "ClearLookAt");
			}
			g_lookers.clear();
			g_nextBardHush = 0.0;
			ReleaseAllKeys();
			Audio::StopSong();
			MidiIn::Close();
			g_act = Act::Free;
			StopIdle();
			if (g_forcedThird) {
				if (auto* cam = RE::PlayerCamera::GetSingleton()) {
					cam->ForceFirstPerson();
				}
				g_forcedThird = false;
			}
			MuteMusic(false);
			g_lastInst = g_inst;
			Overlay::SetMode(Overlay::Mode::Closed);
			if (g_earned > 0) {
				Hud(fmt::format("You earned {} septims playing the {}.", g_earned, InstName(g_inst)));
			}
			spdlog::info("Stopped playing ({}), earned {}", a_why, g_earned);
		}

		// The innkeeper has had enough: out the nearest door that leads outside, and not back today.
		void ThrowOut()
		{
			auto* p = Player();
			auto* cell = p ? p->GetParentCell() : nullptr;
			const std::string keeper = g_keeper ? NameOf(g_keeper) : "The innkeeper";
			Hud(fmt::format("{}: \"That's enough! Out, and take that racket with you!\"", keeper));
			if (cell) {
				g_bans[cell->GetFormID()] = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f;
			}
			RE::TESObjectREFR* dest = nullptr;
			RE::NiPoint3       destPos;
			float              best = std::numeric_limits<float>::max();
			if (cell) {
				const auto here = p->GetPosition();
				cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
					if (!a_ref || a_ref->IsDisabled()) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto* base = a_ref->GetBaseObject();
					if (!base || !base->Is(RE::FormType::Door)) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto* tp = a_ref->extraList.GetByType<RE::ExtraTeleport>();
					if (!tp || !tp->teleportData) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto linked = tp->teleportData->linkedDoor.get();
					if (!linked) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					auto*       lc = linked->GetParentCell();
					const float d = a_ref->GetPosition().GetDistance(here) + ((lc && lc->IsExteriorCell()) ? 0.0f : 1.0e6f);
					if (d < best) {
						best = d;
						dest = linked.get();
						destPos = tp->teleportData->position;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			Stop("thrown out");
			if (dest) {
				const auto at = dest->GetPosition();
				Call(p, "MoveTo", dest, destPos.x - at.x, destPos.y - at.y, destPos.z - at.z, false);
				spdlog::info("Thrown out through {:08X}", dest->GetFormID());
			} else {
				spdlog::info("Thrown out: no door out found, just stopped");
			}
		}

		// ------------------------------------------------------------------------------------------ the frame
		void Tick()
		{
			static double s_prev = Now();
			const double  now = Now();
			const double  dt = std::clamp(now - s_prev, 0.0, 0.1);
			s_prev = now;

			if (g_wantStart.exchange(false) && !g_playing) {
				Begin();
				return;
			}
			if (!g_playing) {
				return;
			}
			auto* p = Player();
			auto* ui = RE::UI::GetSingleton();
			if (!p || p->IsDead()) {
				Stop("dead");
				return;
			}
			if (p->IsInCombat()) {
				Stop("combat");
				Hud(fmt::format("You put your {} away to fight.", InstName(g_inst)));
				return;
			}
			if (auto* st = p->AsActorState(); st && st->IsWeaponDrawn()) {
				Stop("weapon drawn");
				return;
			}
			if (p->GetParentCell() != g_cell) {
				Stop("left the area");
				return;
			}
			if (ui && (ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))) {
				Stop("a menu opened");
				return;
			}
			if (g_idleAt > 0.0 && now >= g_idleAt) {
				RunIdle();
			}
			g_midiLive = Overlay::GetMode() == Overlay::Mode::Playing && g_act != Act::Auto;
			{
				std::vector<MidiHit> hits;
				{
					std::lock_guard mk(g_midiMtx);
					hits.swap(g_midiQ);
				}
				for (const auto& h : hits) {
					Register(h.pitch, h.now, h.songT);
				}
			}
			static double s_scan = 0.0;
			if (now - s_scan > 0.5) {
				s_scan = now;
				ScanListeners();
			}

			const bool book = Overlay::GetMode() == Overlay::Mode::Book;
			bool       performing = !book;
			switch (g_act) {
			case Act::Free:
				g_breakdown = g_free.Judge(now);
				g_quality = g_breakdown.quality;
				// a long silence isn't a bad performance, it's no performance: the room drifts back to neutral
				performing = performing && !g_free.Silent(now);
				break;
			case Act::Along:
				{
					const double t = Audio::SongTime();
					// the count-in: three soft ticks
					for (double beat = -3.0; beat < 0.0; beat += 1.0) {
						if (g_lastClick < beat && t >= beat) {
							g_lastClick = beat;
							Audio::Click();
						}
					}
					performing = performing && t >= 0.0;
					g_chart.Advance(t - Config::Get().latencyMs / 1000.0);
					g_quality = g_chart.Quality();
					if (g_chart.Done(t)) {
						const double score = g_chart.Score();
						Say(fmt::format("{}: {:.0f}%  ({} perfect, {} good, {} missed)", g_song.title, score * 100.0, g_chart.Perfects(), g_chart.Goods(),
								g_chart.Misses()),
							score >= 0.6 ? kGreen : kRed);
						std::vector<music::CrowdEvent> ev;
						g_crowd.SongEnded(score, Speech01(), ev);
						HandleCrowd(ev);
						if (Config::Get().speechXp > 0.0f) {
							p->AddSkillExperience(RE::ActorValue::kSpeech, static_cast<float>(score * 20.0 * Config::Get().speechXp));
						}
						spdlog::info("Song over: {} {:.2f}", g_song.title, score);
						BackToFree();
					}
				}
				break;
			case Act::Auto:
				{
					g_quality = 0.92;
					if (Audio::SongTime() > g_song.length + 1.5) {
						std::vector<music::CrowdEvent> ev;
						g_crowd.SongEnded(0.9, Speech01(), ev);
						HandleCrowd(ev);
						BackToFree();
					}
				}
				break;
			}
			std::vector<music::CrowdEvent> ev;
			g_crowd.Step(dt, g_quality, Speech01(), performing, ev);
			HandleCrowd(ev);

			// an inn that's turned on you
			if (g_inInn && Config::Get().throwOut && g_crowd.Size() > 0 && performing) {
				const double mood = g_crowd.Mood();
				if (mood < 0.18) {
					g_badFor += dt;
				} else if (mood > 0.25) {
					g_badFor = 0.0;
					g_warned = false;
				}
				if (g_badFor > 4.0 && !g_warned) {
					g_warned = true;
					const std::string keeper = g_keeper ? NameOf(g_keeper) : "The innkeeper";
					Say(fmt::format("{}: \"Play something decent or get out!\"", keeper), kRed);
					Hud(fmt::format("{}: \"Play something decent or get out!\"", keeper));
				}
				if (g_badFor > 11.0) {
					ThrowOut();
				}
			}
		}

		void SafeTick()
		{
			__try {
				Tick();
			} __except (Overlay::SehFilter(GetExceptionInformation(), "Performance tick")) {
			}
		}

		// ------------------------------------------------------------------------------------------ the look
		float g_ui = 1.0f;
		struct Fonts
		{
			ImFont* body = nullptr;
			ImFont* sm = nullptr;
			ImFont* key = nullptr;
			ImFont* title = nullptr;
			ImFont* book = nullptr;
			ImFont* bookIt = nullptr;
			ImFont* big = nullptr;
		} F;

		void Text(ImDrawList* a_dl, ImFont* a_font, ImVec2 a_pos, ImU32 a_col, const std::string& a_text, float a_alignX = 0.0f)
		{
			const auto size = a_font->CalcTextSizeA(a_font->FontSize, FLT_MAX, 0.0f, a_text.c_str());
			a_pos.x -= size.x * a_alignX;
			a_dl->AddText(a_font, a_font->FontSize, a_pos + ImVec2(1, 1), IM_COL32(0, 0, 0, 160), a_text.c_str());
			a_dl->AddText(a_font, a_font->FontSize, a_pos, a_col, a_text.c_str());
		}

		ImU32 Fade(ImU32 a_col, float a_alpha)
		{
			const auto a = static_cast<ImU32>(((a_col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a_alpha, 0.0f, 1.0f));
			return (a_col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
		}

		void DrawHud()
		{
			float sw = 0.0f, sh = 0.0f;
			Overlay::ScreenSize(sw, sh);
			const float s = g_ui;
			auto*       dl = ImGui::GetBackgroundDrawList();
			const double now = Now();
			const double songT = g_act == Act::Free ? 0.0 : Audio::SongTime();
			const auto&  cfg = Config::Get();

			const float W = (std::min)(980.0f * s, sw * 0.92f);
			const bool  along = g_act == Act::Along;
			const float headH = 34.0f * s, laneH = 30.0f * s, hwH = along ? laneH * 3.0f + 14.0f * s : 26.0f * s;
			const float kbH = !cfg.showKeyboard ? 0.0f : (g_inst == music::Instrument::Flute ? 170.0f : 190.0f) * s;  // the instrument
			const float hintH = 24.0f * s;
			const float H = headH + hwH + kbH + hintH + 18.0f * s;
			const ImVec2 p0((sw - W) * 0.5f, sh - H - 26.0f * s), p1(p0.x + W, p0.y + H);
			dl->AddRectFilled(p0, p1, IM_COL32(16, 12, 9, 200), 8.0f * s);
			dl->AddRect(p0, p1, IM_COL32(200, 160, 90, 110), 8.0f * s, 0, 1.5f * s);

			// the header: instrument and what you're doing, the room, your earnings
			float y = p0.y + 8.0f * s;
			std::string what = g_act == Act::Free ? "Free play" : g_act == Act::Along ? "Playing along: " + g_song.title : "Playing: " + g_song.title;
			Text(dl, F.book, { p0.x + 16.0f * s, y }, kGoldCol, fmt::format("{}  ·  {}", music::InstrumentName(g_inst), what));
			{
				const double mood = g_crowd.Mood();
				const float  bw = 170.0f * s, bh = 8.0f * s;
				const ImVec2 b0(p0.x + W * 0.60f, y + 11.0f * s);
				std::string  label = g_crowd.Size() == 0 ? "Nobody's listening" :
				                                           fmt::format("{} · {} listening", music::Crowd::MoodWord(mood), g_crowd.Size());
				Text(dl, F.sm, { b0.x + bw * 0.5f, y - 8.0f * s }, kDim, label, 0.5f);
				dl->AddRectFilled(b0, b0 + ImVec2(bw, bh), IM_COL32(60, 50, 40, 220), 3.0f * s);
				if (g_crowd.Size() > 0) {
					const ImU32 c = mood < 0.3 ? kRed : mood < 0.55 ? IM_COL32(220, 200, 140, 255) : kGreen;
					dl->AddRectFilled(b0, b0 + ImVec2(bw * static_cast<float>(mood), bh), c, 3.0f * s);
				}
			}
			Text(dl, F.body, { p1.x - 16.0f * s, y + 2.0f * s }, kGoldCol, fmt::format("{} septims", g_earned), 1.0f);
			y += headH;

			// the sheet music: the notes slide to the line; the lanes are the keyboard's rows (top row highest)
			if (along) {
				const float  x0 = p0.x + 16.0f * s, x1 = p1.x - 16.0f * s, hitX = x0 + 90.0f * s;
				const double ahead = 3.2 / (std::max)(0.5f, cfg.speed);
				const float  pps = static_cast<float>((x1 - hitX) / ahead);
				dl->AddRectFilled({ x0, y }, { x1, y + laneH * 3.0f + 4.0f * s }, IM_COL32(0, 0, 0, 90), 4.0f * s);
				for (int r = 1; r < 3; ++r) {
					dl->AddLine({ x0, y + laneH * r }, { x1, y + laneH * r }, IM_COL32(255, 255, 255, 18));
				}
				dl->PushClipRect({ x0, y }, { x1, y + laneH * 3.0f + 4.0f * s }, true);
				const auto& chart = g_chart.Chart();
				const int   base = Base();
				for (std::size_t i = 0; i < chart.size(); ++i) {
					const auto& n = chart[i];
					const float xs = hitX + static_cast<float>((n.start - songT) * pps);
					if (xs > x1 + 10.0f) {
						break;
					}
					const float xe = (std::max)(xs + 26.0f * s, hitX + static_cast<float>((n.start + n.dur - songT) * pps));
					if (xe < x0) {
						continue;
					}
					music::keymap::Label l;
					const bool           ok = music::keymap::LabelFor(n.pitch, base, l);
					const int            lane = ok ? 2 - l.row : 1;
					const float          ly = y + lane * laneH + 3.0f * s;
					const auto           st = g_chart.StateOf(i);
					ImU32 fill = IM_COL32(120, 95, 60, 230), text = kInk;
					if (st == music::ChartJudge::State::Perfect) {
						fill = IM_COL32(230, 190, 105, 120);
					} else if (st == music::ChartJudge::State::Good) {
						fill = IM_COL32(120, 190, 110, 120);
					} else if (st == music::ChartJudge::State::Missed) {
						fill = IM_COL32(170, 70, 60, 110);
						text = kDim;
					}
					dl->AddRectFilled({ xs, ly }, { xe, ly + laneH - 6.0f * s }, fill, 5.0f * s);
					const std::string lab = ok ? fmt::format("{}{}", l.letter, l.shift ? "+" : "") : music::NoteName(n.pitch);
					Text(dl, F.key, { xs + 6.0f * s, ly + 1.0f * s }, text, lab);
				}
				dl->PopClipRect();
				dl->AddLine({ hitX, y - 2.0f * s }, { hitX, y + laneH * 3.0f + 6.0f * s }, kGoldCol, 2.5f * s);
				if (songT < 0.0) {
					Text(dl, F.big, { (x0 + x1) * 0.5f, y + laneH * 0.5f }, kGoldCol, fmt::format("{}", static_cast<int>(std::ceil(-songT))), 0.5f);
				}
				Text(dl, F.sm, { x1 - 6.0f * s, y + 2.0f * s }, kDim,
					fmt::format("{:.0f}%   streak {}", g_chart.Score() * 100.0, g_chart.Streak()), 1.0f);
				y += hwH;
			} else if (g_act == Act::Free) {
				// what the room hears in your playing
				const auto& b = g_breakdown;
				const bool  silent = g_free.Silent(now);
				const std::pair<const char*, double> parts[] = { { "Rhythm", b.rhythm }, { "Melody", b.melody }, { "Key", b.key },
					{ "Phrasing", b.stability }, { "Harmony", b.harmony }, { "Ideas", b.ideas } };
				const float colW = (W - 32.0f * s) / 6.0f;
				for (int i = 0; i < 6; ++i) {
					const float  cx = p0.x + 16.0f * s + colW * i;
					const double v = silent ? 0.0 : std::clamp(parts[i].second, 0.0, 1.0);
					Text(dl, F.sm, { cx, y + 2.0f * s }, kDim, parts[i].first);
					const ImVec2 b0(cx + 62.0f * s, y + 10.0f * s), b1(cx + colW - 14.0f * s, y + 15.0f * s);
					dl->AddRectFilled(b0, b1, IM_COL32(60, 50, 40, 220), 2.0f * s);
					const ImU32 c = v < 0.35 ? kRed : v < 0.6 ? IM_COL32(220, 200, 140, 255) : kGreen;
					dl->AddRectFilled(b0, { b0.x + (b1.x - b0.x) * static_cast<float>(v), b1.y }, c, 2.0f * s);
				}
				if (b.spam < 1.0 && !silent) {
					Text(dl, F.sm, { p1.x - 16.0f * s, y - 16.0f * s }, kRed, "sounds like noise", 1.0f);
				}
				y += hwH;
			} else if (g_act == Act::Auto) {
				const float  x0 = p0.x + 16.0f * s, x1 = p1.x - 16.0f * s;
				const float  f = static_cast<float>(std::clamp(songT / (std::max)(1.0, g_song.length), 0.0, 1.0));
				dl->AddRectFilled({ x0, y + 6.0f * s }, { x1, y + 12.0f * s }, IM_COL32(60, 50, 40, 220), 3.0f * s);
				dl->AddRectFilled({ x0, y + 6.0f * s }, { x0 + (x1 - x0) * f, y + 12.0f * s }, kGoldCol, 3.0f * s);
				y += hwH;
			}

			// the instrument itself, lit as its notes sound: a lute's neck, or a flute's tone holes
			if (cfg.showKeyboard) {
				// what's sounding now, and for how long: your notes, and in "let it play" the song's
				std::vector<InstrumentHud::Sounding> sounding;
				for (const auto& [pitch, t] : g_lit) {
					if (now - t < 0.6) {
						sounding.push_back({ pitch, static_cast<float>(now - t) });
					}
				}
				if (g_act == Act::Auto) {
					for (const auto& n : g_song.all) {
						if (n.start > songT) {
							break;
						}
						if (songT - n.start < 0.6) {
							sounding.push_back({ n.pitch, static_cast<float>(songT - n.start) });
						}
					}
				}
				const InstrumentHud::Fonts fonts{ F.key, F.sm, F.bookIt };
				const ImVec2               ip(p0.x + 16.0f * s, y), isz(W - 32.0f * s, kbH);
				if (g_inst == music::Instrument::Flute) {
					InstrumentHud::DrawFlute(dl, ip, isz, s, Base(), sounding, now, fonts);
				} else {
					InstrumentHud::DrawLute(dl, ip, isz, s, Base(), sounding, now, fonts);
				}
				y += kbH;
			}

			// the keys' help
			const std::string octave = g_octave == 0 ? "" : fmt::format("   (octave {:+d})", g_octave);
			Text(dl, F.sm, { p0.x + W * 0.5f, y + 2.0f * s }, kDim,
				fmt::format("Z-M  A-J  Q-I play  ·  Shift = black keys (S+Z = C#)  ·  Up/Down octave  ·  Tab songbook  ·  Backspace hide  ·  Esc {}{}", g_act == Act::Free ? "put away" : "stop the song",
					octave),
				0.5f);

			// what the room says
			float fy = p0.y - 10.0f * s;
			for (auto it = g_feed.rbegin(); it != g_feed.rend(); ++it) {
				const double age = now - it->t;
				if (age > 9.0) {
					continue;
				}
				const float a = static_cast<float>(age < 7.0 ? 1.0 : 1.0 - (age - 7.0) / 2.0);
				fy -= F.body->FontSize + 4.0f * s;
				Text(dl, F.body, { p0.x + 8.0f * s, fy }, Fade(it->color, a), it->text);
			}
		}

		void SongRow(const Entry& a_e, int a_id)
		{
			const float s = g_ui;
			ImGui::PushID(a_id);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushFont(F.book);
			ImGui::TextUnformatted(a_e.song.title.c_str());
			ImGui::PopFont();
			ImGui::TableNextColumn();
			if (!a_e.error.empty()) {
				ImGui::TextDisabled("%s", a_e.error.c_str());
				ImGui::TableNextColumn();
				ImGui::TableNextColumn();
				ImGui::PopID();
				return;
			}
			const int secs = static_cast<int>(a_e.song.length);
			ImGui::TextDisabled("%d:%02d", secs / 60, secs % 60);
			ImGui::TableNextColumn();
			if (a_e.song.difficulty > 0) {
				std::string stars;
				for (int i = 0; i < 5; ++i) {
					stars += i < a_e.song.difficulty ? "*" : ".";
				}
				ImGui::TextDisabled("%s", stars.c_str());
			} else {
				ImGui::TextDisabled("%zu", a_e.song.all.size());
			}
			ImGui::TableNextColumn();
			if (ImGui::Button("Play", ImVec2(110.0f * s, 0))) {
				StartAuto(a_e.song);
				Overlay::SetMode(Overlay::Mode::Playing);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("The song plays itself on your instrument, perfectly.\nThe crowd won't know.");
			}
			ImGui::PopID();
		}

		void DrawBook()
		{
			float sw = 0.0f, sh = 0.0f;
			Overlay::ScreenSize(sw, sh);
			const float s = g_ui;
			ImGui::SetNextWindowSize(ImVec2((std::min)(860.0f * s, sw * 0.9f), (std::min)(640.0f * s, sh * 0.85f)), ImGuiCond_Always);
			ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::Begin("##songbook", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
			ImGui::PushFont(F.title);
			ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.45f, 1.0f), "Songbook");
			ImGui::PopFont();
			ImGui::Separator();

			// the instrument
			ImGui::TextDisabled("Instrument");
			for (auto inst : { Instrument::Lute, Instrument::Flute }) {
				ImGui::SameLine();
				const bool owned = g_owns[inst == Instrument::Lute ? 0 : 1], on = g_inst == inst;
				ImGui::BeginDisabled(!owned);
				if (on) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.42f, 0.2f, 1.0f));
				}
				if (ImGui::Button(music::InstrumentName(inst), ImVec2(100.0f * s, 0)) && !on) {
					ReleaseAllKeys();
					BackToFree();
					SetInst(inst);
					g_lastInst = inst;
					// put the old instrument away properly (the put-away animation is what takes it out of the hands;
					// starting the new idle before it finished left both instruments held), then take out the new one
					StopIdle();
					PlayIdleFor(inst, 1.6);
					Say(fmt::format("You put away the {} and take out the {}.", InstName(inst == Instrument::Lute ? Instrument::Flute : Instrument::Lute),
							InstName(inst)),
						kDim);
				}
				if (on) {
					ImGui::PopStyleColor();
				}
				ImGui::EndDisabled();
				if (!owned && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("You don't have one");
				}
			}
			ImGui::SameLine(0.0f, 30.0f * s);
			if (ImGui::Button("Free play", ImVec2(110.0f * s, 0))) {
				BackToFree();
				Overlay::SetMode(Overlay::Mode::Playing);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Play whatever you like. The room judges your rhythm, key and melody.");
			}
			ImGui::Spacing();

			// the songs
			const float footer = 120.0f * s;
			ImGui::BeginChild("##songs", ImVec2(0, -footer), true);
			if (ImGui::BeginTable("##list", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("Song", ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthStretch, 0.8f);
				ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch, 0.9f);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 125.0f * s);
				int id = 0;
				for (const auto& e : g_files) {
					SongRow(e, id++);
				}
				ImGui::EndTable();
			}
			if (g_files.empty()) {
				ImGui::Spacing();
				ImGui::PushFont(F.bookIt);
				ImGui::TextWrapped("Your own songs: put MIDI files (.mid) in Data\\SKSE\\Plugins\\SkyrimSongs\\Songs and open the songbook again.");
				ImGui::PopFont();
			}
			ImGui::EndChild();

			// settings and leaving
			auto& cfg = Config::Edit();
			ImGui::SetNextItemWidth(220.0f * s);
			float speed = cfg.speed * 100.0f;
			if (ImGui::SliderFloat("Song speed", &speed, 50.0f, 200.0f, "%.0f%%")) {
				cfg.speed = std::round(speed / 5.0f) * 5.0f / 100.0f;  // in steps of 5%
				Audio::SetSongSpeed(cfg.speed);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("How fast songs play (100%% = as written). Changes a song that's playing, too.");
			}
			ImGui::SameLine(0.0f, 30.0f * s);
			ImGui::SetNextItemWidth(220.0f * s);
			if (ImGui::SliderFloat("Volume", &cfg.volume, 0.0f, 2.0f, "%.2f")) {
				Audio::SetVolume(cfg.volume);
			}
			ImGui::SameLine(0.0f, 30.0f * s);
			if (ImGui::Checkbox("Hide the interface (Backspace)", &cfg.hideHud)) {
				Config::Save();
			}
			if (g_midiCount > 0) {
				std::string names;
				for (const auto& n : g_midiNames) {
					names += (names.empty() ? "" : ", ") + n;
				}
				ImGui::TextDisabled("MIDI keyboard: %s", names.c_str());
			} else {
				ImGui::TextDisabled("No MIDI keyboard found (plug it in before you take out your instrument).");
			}
			ImGui::Spacing();
			if (ImGui::Button("Back to playing", ImVec2(160.0f * s, 0))) {
				Overlay::SetMode(Overlay::Mode::Playing);
			}
			ImGui::SameLine();
			if (ImGui::Button(fmt::format("Put the {} away", InstName(g_inst)).c_str(), ImVec2(180.0f * s, 0))) {
				Stop("put away from the songbook");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("  Esc: back to playing");
			ImGui::End();
		}
	}

	// ============================================================================================== public
	void OnDataLoaded()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		g_gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold);
		g_innkeepers = RE::TESForm::LookupByID<RE::TESFaction>(kInnkeeperFaction);
		for (int k = 0; k < 2; ++k) {
			const auto& ids = k == 0 ? std::vector<RE::FormID>(std::begin(kLutes), std::end(kLutes)) : std::vector<RE::FormID>(std::begin(kFlutes), std::end(kFlutes));
			for (auto id : ids) {
				auto* f = RE::TESForm::LookupByID(id);
				if (!f) {
					continue;
				}
				g_instrumentIds[k].insert(id);
				if (auto* m = f->As<RE::TESModel>(); m && m->GetModel() && *m->GetModel()) {
					std::string p = m->GetModel();
					std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					g_instrumentModels[k].insert(p);
				}
			}
		}
		const auto& cfg = Config::Get();
		g_idles[0] = RE::TESForm::LookupByID<RE::TESIdleForm>(cfg.luteIdle ? cfg.luteIdle : kIdleLute);
		g_idles[1] = RE::TESForm::LookupByID<RE::TESIdleForm>(cfg.fluteIdle ? cfg.fluteIdle : kIdleFlute);
		g_stopIdle = RE::TESForm::LookupByID<RE::TESIdleForm>(cfg.stopIdle ? cfg.stopIdle : kIdleStopLoose);
		auto idles = [](auto& a_ids, std::vector<RE::TESIdleForm*>& a_out) {
			for (auto id : a_ids) {
				if (auto* f = RE::TESForm::LookupByID<RE::TESIdleForm>(id)) {
					a_out.push_back(f);
				}
			}
		};
		idles(kIdleApplaud, g_applaud);
		idles(kIdleDance, g_dance);
		idles(kIdleEnjoy, g_enjoy);
		g_give = RE::TESForm::LookupByID<RE::TESIdleForm>(kIdleGive);
		spdlog::info("Audience idles: applause {}, dance {}, enjoy {}, give {}", g_applaud.size(), g_dance.size(), g_enjoy.size(), g_give != nullptr);
		// a music type that's only silence: the highest priority one
		for (auto* m : dh->GetFormArray<RE::BGSMusicType>()) {
			if (!m || m->tracks.empty()) {
				continue;
			}
			bool silent = true;
			for (auto* t : m->tracks) {
				if (!t || t->GetType() != RE::BSIMusicTrack::TrackType::kSilentTrack) {
					silent = false;
					break;
				}
			}
			if (silent && (!g_silence || m->priority < g_silence->priority)) {
				g_silence = m;
			}
		}
		for (auto* c : dh->GetFormArray<RE::BGSSoundCategory>()) {
			const char* name = c ? c->GetFullName() : nullptr;
			if (name && _stricmp(name, cfg.musicCategory.c_str()) == 0) {
				g_musicCats.push_back(c);
			}
		}
		spdlog::info("Forms: gold {}, innkeepers {}, lute models {}, flute models {}, idles lute {} flute {} stop {}, music categories {}, silence {:08X}",
			g_gold != nullptr, g_innkeepers != nullptr, g_instrumentModels[0].size(), g_instrumentModels[1].size(), g_idles[0] != nullptr,
			g_idles[1] != nullptr, g_stopIdle != nullptr, g_musicCats.size(), g_silence ? g_silence->GetFormID() : 0u);
		std::lock_guard lk(g_mtx);
		LoadSongs();
	}

	void OnGameLoaded()
	{
		std::lock_guard lk(g_mtx);
		Stop("game loaded");
		MuteMusic(false);
		for (auto id : g_staleVisitors) {
			ReleaseActor(ActorOf(id));
		}
		g_staleVisitors.clear();
	}

	void FrameTick()
	{
		std::lock_guard lk(g_mtx);
		SafeTick();
	}

	void OnHotkey(int a_scan)
	{
		if (a_scan == Config::Get().hotkey) {
			g_wantStart = true;
		}
	}

	void OnKey(int a_scan, bool a_down)
	{
		std::lock_guard lk(g_mtx);
		if (!g_playing) {
			return;
		}
		if (a_scan == music::keymap::kLeftShift) {
			g_shiftL = a_down;
			return;
		}
		if (a_scan == music::keymap::kRightShift) {
			g_shiftR = a_down;
			return;
		}
		const auto mode = Overlay::GetMode();
		if (mode == Overlay::Mode::Book) {
			if (a_down && (a_scan == 0x01 || a_scan == Config::Get().hotkey)) {  // Esc or the hotkey closes the songbook
				Overlay::SetMode(Overlay::Mode::Playing);
			}
			return;
		}
		// a note key (while a song plays itself, the note keys rest)
		if (a_down) {
			const int pitch = g_act == Act::Auto ? -1 : music::keymap::Pitch(a_scan, Base(), g_shiftL || g_shiftR);
			if (pitch >= 0) {
				if (auto it = g_keyPitch.find(a_scan); it != g_keyPitch.end()) {
					Unsound(it->second);
				}
				g_keyPitch[a_scan] = pitch;
				const float vel = std::clamp(0.78f + std::uniform_real_distribution<float>(-0.08f, 0.08f)(g_rng), 0.1f, 1.0f);
				Sound(pitch, vel);
				return;
			}
		} else {
			if (auto it = g_keyPitch.find(a_scan); it != g_keyPitch.end()) {
				Unsound(it->second);
				g_keyPitch.erase(it);
			}
			return;
		}
		// the rest (key down). While you play, every other key is swallowed: the hotkey or Esc gets you out.
		if (a_scan == Config::Get().hotkey) {
			Stop("hotkey");
			return;
		}
		switch (a_scan) {
		case 0x01:  // Esc: stop the song, or put the instrument away
			if (g_act != Act::Free) {
				Say("You stop the song.", kDim);
				BackToFree();
			} else {
				Stop("Esc");
			}
			break;
		case 0x0E:  // Backspace: hide or show everything on screen
			{
				auto& cfg = Config::Edit();
				cfg.hideHud = !cfg.hideHud;
				g_hudToggled = Now();
				Config::Save();
			}
			break;
		case 0x0F:  // Tab: the songbook
			ReleaseAllKeys();
			LoadSongs();
			RefreshOwned();
			Overlay::SetMode(Overlay::Mode::Book);
			break;
		case 0xC8:  // Up
			if (g_octave < 2) {
				ReleaseAllKeys();
				++g_octave;
			}
			break;
		case 0xD0:  // Down
			if (g_octave > -2) {
				ReleaseAllKeys();
				--g_octave;
			}
			break;
		default:
			break;
		}
	}

	void SetupUi(float a_screenH)
	{
		g_ui = (std::max)(0.6f, a_screenH / 1080.0f) * Config::Get().uiScale;
		auto& io = ImGui::GetIO();
		char  windir[MAX_PATH]{};
		::GetWindowsDirectoryA(windir, MAX_PATH);
		const std::string ours = "Data\\SKSE\\Plugins\\SkyrimSongs\\Fonts\\";
		const std::string sys = std::string(windir) + "\\Fonts\\";
		std::error_code   ec;
		auto load = [&](const char* a_file, const char* a_fallback, float a_size) -> ImFont* {
			std::string p = ours + a_file;
			if (!std::filesystem::exists(p, ec)) {
				p = sys + a_fallback;
				if (!std::filesystem::exists(p, ec)) {
					return nullptr;
				}
			}
			ImFontConfig fc;
			fc.OversampleH = 2;
			fc.OversampleV = 1;
			return io.Fonts->AddFontFromFileTTF(p.c_str(), a_size * g_ui, &fc);
		};
		F.body = load("BarlowCondensed-Medium.ttf", "arialn.ttf", 21.0f);
		F.sm = load("BarlowCondensed-Medium.ttf", "arialn.ttf", 16.0f);
		F.key = load("BarlowCondensed-SemiBold.ttf", "arialnb.ttf", 22.0f);
		F.big = load("BarlowCondensed-SemiBold.ttf", "arialnb.ttf", 64.0f);
		F.title = load("IMFellEnglishSC-Regular.ttf", "georgia.ttf", 34.0f);
		F.book = load("IMFellEnglish-Regular.ttf", "georgia.ttf", 21.0f);
		F.bookIt = load("IMFellEnglish-Italic.ttf", "georgiai.ttf", 19.0f);
		ImFont* any = F.body ? F.body : io.Fonts->AddFontDefault();
		for (ImFont** f : { &F.body, &F.sm, &F.key, &F.big, &F.title, &F.book, &F.bookIt }) {
			if (!*f) {
				*f = any;
			}
		}
		io.FontDefault = F.body;

		auto& st = ImGui::GetStyle();
		st = ImGuiStyle();
		st.ScaleAllSizes(g_ui);
		st.WindowRounding = 8.0f * g_ui;
		st.FrameRounding = 4.0f * g_ui;
		st.WindowBorderSize = 1.5f;
		st.WindowPadding = ImVec2(20.0f * g_ui, 16.0f * g_ui);
		st.ItemSpacing = ImVec2(8.0f * g_ui, 7.0f * g_ui);
		auto* c = st.Colors;
		c[ImGuiCol_Text] = ImVec4(0.95f, 0.93f, 0.87f, 1.0f);
		c[ImGuiCol_TextDisabled] = ImVec4(0.72f, 0.68f, 0.6f, 1.0f);
		c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.055f, 0.04f, 0.94f);
		c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.18f);
		c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.07f, 0.05f, 0.97f);
		c[ImGuiCol_Border] = ImVec4(0.78f, 0.62f, 0.35f, 0.45f);
		c[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.16f, 0.11f, 1.0f);
		c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.22f, 0.15f, 1.0f);
		c[ImGuiCol_FrameBgActive] = ImVec4(0.34f, 0.27f, 0.18f, 1.0f);
		c[ImGuiCol_Button] = ImVec4(0.26f, 0.2f, 0.13f, 1.0f);
		c[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.35f, 0.18f, 1.0f);
		c[ImGuiCol_ButtonActive] = ImVec4(0.6f, 0.46f, 0.22f, 1.0f);
		c[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.68f, 0.38f, 1.0f);
		c[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.78f, 0.45f, 1.0f);
		c[ImGuiCol_Separator] = ImVec4(0.78f, 0.62f, 0.35f, 0.4f);
		c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 0.9f, 0.7f, 0.04f);
		c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.2f);
		c[ImGuiCol_ScrollbarGrab] = ImVec4(0.45f, 0.35f, 0.2f, 1.0f);
		spdlog::info("Interface: scale {:.2f}, fonts {}", g_ui, std::filesystem::exists(ours + "BarlowCondensed-Medium.ttf", ec) ? "ours" : "Windows'");
	}

	void DrawUi()
	{
		std::lock_guard lk(g_mtx);
		if (!g_playing) {
			return;
		}
		if (!Config::Get().hideHud) {
			DrawHud();
		} else if (const double age = Now() - g_hudToggled; age < 3.0 && Overlay::GetMode() == Overlay::Mode::Playing) {
			// a moment's word that it's hidden, then nothing
			float sw = 0.0f, sh = 0.0f;
			Overlay::ScreenSize(sw, sh);
			const float a = static_cast<float>(age < 2.0 ? 1.0 : 3.0 - age);
			Text(ImGui::GetBackgroundDrawList(), F.sm, { sw * 0.5f, sh - 40.0f * g_ui }, Fade(kDim, a), "Interface hidden  ·  Backspace to show it", 0.5f);
		}
		if (Overlay::GetMode() == Overlay::Mode::Book) {
			DrawBook();
		}
	}

	// ============================================================================================== co-save
	void Save(SKSE::SerializationInterface* a_intfc)
	{
		std::lock_guard lk(g_mtx);
		if (!a_intfc->OpenRecord('BANS', kSaveVersion)) {
			return;
		}
		const auto n = static_cast<std::uint32_t>(g_bans.size());
		a_intfc->WriteRecordData(n);
		for (const auto& [cell, day] : g_bans) {
			a_intfc->WriteRecordData(cell);
			a_intfc->WriteRecordData(day);
		}
		if (a_intfc->OpenRecord('VIST', kSaveVersion)) {
			// a save made mid-performance: the listeners standing with you go home after it's loaded
			const auto v = static_cast<std::uint32_t>(g_visitors.size());
			a_intfc->WriteRecordData(v);
			for (const auto& vis : g_visitors) {
				a_intfc->WriteRecordData(vis.id);
			}
		}
		if (a_intfc->OpenRecord('TIPS', kSaveVersion)) {
			const auto t = static_cast<std::uint32_t>(g_tipped.size());
			a_intfc->WriteRecordData(t);
			for (const auto& [id, day] : g_tipped) {
				a_intfc->WriteRecordData(id);
				a_intfc->WriteRecordData(day);
			}
		}
		if (a_intfc->OpenRecord('STAT', kSaveVersion)) {
			a_intfc->WriteRecordData(g_totalEarned);
			a_intfc->WriteRecordData(g_performances);
		}
	}

	void Load(SKSE::SerializationInterface* a_intfc)
	{
		std::lock_guard lk(g_mtx);
		g_bans.clear();
		g_tipped.clear();
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (version != kSaveVersion) {
				continue;
			}
			if (type == 'BANS') {
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n && i < 10000; ++i) {
					RE::FormID cell = 0, resolved = 0;
					float      day = 0.0f;
					a_intfc->ReadRecordData(cell);
					a_intfc->ReadRecordData(day);
					if (a_intfc->ResolveFormID(cell, resolved)) {
						g_bans[resolved] = day;
					}
				}
			} else if (type == 'VIST') {
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n && i < 16; ++i) {
					RE::FormID id = 0, resolved = 0;
					a_intfc->ReadRecordData(id);
					if (a_intfc->ResolveFormID(id, resolved)) {
						g_staleVisitors.push_back(resolved);
					}
				}
			} else if (type == 'TIPS') {
				std::uint32_t n = 0;
				a_intfc->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n && i < 100000; ++i) {
					RE::FormID id = 0, resolved = 0;
					float      day = 0.0f;
					a_intfc->ReadRecordData(id);
					a_intfc->ReadRecordData(day);
					if (a_intfc->ResolveFormID(id, resolved)) {
						g_tipped[resolved] = day;
					}
				}
			} else if (type == 'STAT') {
				a_intfc->ReadRecordData(g_totalEarned);
				a_intfc->ReadRecordData(g_performances);
			}
		}
		spdlog::info("Co-save: {} inn ban(s), {} septims earned in {} performance(s)", g_bans.size(), g_totalEarned, g_performances);
	}

	void Revert(SKSE::SerializationInterface*)
	{
		std::lock_guard lk(g_mtx);
		g_bans.clear();
		g_tipped.clear();
		g_totalEarned = 0;
		g_performances = 0;
	}
}
