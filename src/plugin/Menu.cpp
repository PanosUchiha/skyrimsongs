// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
//
// The settings page in SKSE Menu Framework (if it's installed): the INI's settings, changed live, saved to the INI.
#include "Menu.h"
#include "Audio.h"
#include "Config.h"

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "SKSEMenuFramework.h"

namespace Menu
{
	namespace
	{
		namespace UI = ImGuiMCP;

		bool g_dirty = false;

		void Hint(const char* a_text)
		{
			UI::TextDisabled("%s", a_text);
			UI::Spacing();
		}
		void Changed(bool a_changed) { g_dirty |= a_changed; }

		void SaveRow()
		{
			UI::Spacing();
			UI::Separator();
			if (UI::Button("Save to SkyrimSongs.ini")) {
				Config::Save();
				g_dirty = false;
			}
			UI::SameLine();
			if (UI::Button("Undo changes")) {
				Config::Load();
				g_dirty = false;
			}
			if (g_dirty) {
				UI::SameLine();
				UI::TextDisabled("(not saved yet: changes last until you quit)");
			}
		}

		void __stdcall Playing()
		{
			auto& v = Config::Edit();
			UI::SeparatorText("Taking out your instrument");
			UI::Text("Hotkey: scan code 0x%02X (change it in SkyrimSongs.ini, Hotkey =)", v.hotkey);
			Changed(UI::Checkbox("Need a lute or flute in your pack", &v.requireInstrument));
			Changed(UI::Checkbox("Switch to third person while playing", &v.thirdPerson));
			Changed(UI::Checkbox("Quiet the game's music while playing", &v.muteMusic));
			UI::SeparatorText("Sound");
			if (UI::SliderFloat("Volume", &v.volume, 0.0f, 2.0f, "%.2f")) {
				Audio::SetVolume(v.volume);
				g_dirty = true;
			}
			if (UI::SliderFloat("Room echo", &v.reverb, 0.0f, 0.6f, "%.2f")) {
				Audio::SetReverb(v.reverb);
				g_dirty = true;
			}
			UI::SeparatorText("Playing along");
			float speed = v.speed * 100.0f;
			if (UI::SliderFloat("Song speed", &speed, 50.0f, 100.0f, "%.0f%%")) {
				v.speed = speed / 100.0f;
				g_dirty = true;
			}
			Changed(UI::SliderInt("Timing correction", &v.latencyMs, -200, 200, "%d ms"));
			Hint("If every note you play is judged late, raise this; early, lower it.");
			UI::SeparatorText("Screen");
			Changed(UI::Checkbox("Show the key guide", &v.showKeyboard));
			Changed(UI::SliderFloat("Interface size", &v.uiScale, 0.6f, 1.6f, "%.2f"));
			SaveRow();
		}

		void __stdcall Crowd()
		{
			auto& v = Config::Edit();
			Changed(UI::SliderFloat("Earshot", &v.listenRange, 300.0f, 4000.0f, "%.0f units"));
			Hint("How far away people can hear you (70 is about a metre).");
			Changed(UI::SliderFloat("How often they tip", &v.tipRate, 0.0f, 4.0f, "%.2f"));
			Changed(UI::SliderFloat("How much they tip", &v.tipSize, 0.0f, 4.0f, "%.2f"));
			Changed(UI::SliderInt("Most one patron gives", &v.maxTipPerListener, 0, 500, "%d septims"));
			Hint("Per performance: put the instrument away and take it out again for a fresh audience.");
			Changed(UI::SliderFloat("Patience", &v.patience, 0.2f, 3.0f, "%.2f"));
			Hint("How quickly the room turns on a bad set (higher = quicker).");
			Changed(UI::Checkbox("Innkeepers throw out bad bards", &v.throwOut));
			Hint("And won't let you play there again that day.");
			Changed(UI::SliderFloat("Speech experience", &v.speechXp, 0.0f, 3.0f, "%.2f per septim"));
			SaveRow();
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			spdlog::info("SKSE Menu Framework not installed: settings are in SkyrimSongs.ini");
			return;
		}
		SKSEMenuFramework::SetSection("Skyrim Songs");
		SKSEMenuFramework::AddSectionItem("Playing", Playing);
		SKSEMenuFramework::AddSectionItem("The crowd", Crowd);
		spdlog::info("Settings page added to SKSE Menu Framework");
	}
}
