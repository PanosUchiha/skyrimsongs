// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
//
// The settings page in SKSE Menu Framework (if it's installed): the INI's settings, changed live, saved to the INI.
#include "Menu.h"
#include "Audio.h"
#include "Config.h"

#include "../music/Keymap.h"

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "SKSEMenuFramework.h"

namespace Menu
{
	namespace
	{
		namespace UI = ImGuiMCP;

		bool g_dirty = false;

		// ---- the hotkey: "Press a key", read straight from Windows (so no other input hook can hide it)
		bool             g_capturing = false;
		std::array<bool, 256> g_wasDown{};

		// a DirectInput scan code (what the game and the INI use) for a Windows virtual key; 0 if none
		int ScanFor(int a_vk)
		{
			const UINT sc = ::MapVirtualKeyA(static_cast<UINT>(a_vk), 4 /* MAPVK_VK_TO_VSC_EX */);
			if (!sc) {
				return 0;
			}
			return (sc & 0xFF) | (((sc >> 8) == 0xE0 || (sc >> 8) == 0xE1) ? 0x80 : 0);
		}

		std::string KeyName(int a_scan)
		{
			char       buf[64]{};
			const LONG lp = ((a_scan & 0x7F) << 16) | ((a_scan & 0x80) ? (1 << 24) : 0);
			if (::GetKeyNameTextA(lp, buf, sizeof(buf)) > 0) {
				return buf;
			}
			return fmt::format("0x{:02X}", a_scan);
		}

		// the keys you play with can't also be the hotkey
		bool UsedWhilePlaying(int a_scan)
		{
			static constexpr int kTaken[] = { 0x01, 0x0F, 0x2A, 0x36, 0xC8, 0xD0, 0x17 };
			if (std::find(std::begin(kTaken), std::end(kTaken), a_scan) != std::end(kTaken)) {
				return true;
			}
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 7; ++c) {
					if (music::keymap::kRows[r][c] == a_scan) {
						return true;
					}
				}
			}
			return false;
		}

		std::string g_captureNote;

		void HotkeyRow()
		{
			auto& v = Config::Edit();
			UI::Text("Hotkey: %s", KeyName(v.hotkey).c_str());
			UI::SameLine();
			if (!g_capturing) {
				if (UI::Button("Change...")) {
					g_capturing = true;
					g_captureNote.clear();
					for (int vk = 0; vk < 256; ++vk) {
						g_wasDown[vk] = (::GetAsyncKeyState(vk) & 0x8000) != 0;  // what's already held doesn't count
					}
				}
			} else {
				UI::TextColored(UI::ImVec4{ 0.95f, 0.8f, 0.4f, 1.0f }, "Press a key... (Esc cancels)");
				for (int vk = 8; vk < 256; ++vk) {
					if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
						continue;  // the left/right versions are reported too
					}
					const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
					const bool pressed = down && !g_wasDown[vk];
					g_wasDown[vk] = down;
					if (!pressed) {
						continue;
					}
					if (vk == VK_ESCAPE) {
						g_capturing = false;
						break;
					}
					const int scan = ScanFor(vk);
					if (!scan) {
						continue;
					}
					if (UsedWhilePlaying(scan)) {
						g_captureNote = KeyName(scan) + " is used while you play - pick another key.";
						continue;
					}
					v.hotkey = scan;
					g_capturing = false;
					g_captureNote.clear();
					Config::Save();  // straight to the INI
					spdlog::info("Hotkey changed in game to {} (0x{:02X})", KeyName(scan), scan);
					break;
				}
			}
			if (!g_captureNote.empty()) {
				UI::TextDisabled("%s", g_captureNote.c_str());
			}
		}

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
			HotkeyRow();
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
