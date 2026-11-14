// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// Data\SKSE\Plugins\SkyrimSongs.ini
namespace Config
{
	struct Values
	{
		// Playing
		int   hotkey = 0x18;              // O: take out your instrument / put it away (DirectInput scan code)
		bool  requireInstrument = true;   // you need a lute or flute in your pack
		bool  thirdPerson = true;         // switch to third person while you play (and back after)
		bool  muteMusic = true;           // quiet the game's music while you play
		std::string musicCategory = "Music";  // the sound category the game's music plays in (its name in the audio settings)
		float volume = 0.9f;
		float reverb = 0.16f;
		int   latencyMs = 0;              // play along: added to your timing if notes register late on your setup
		float speed = 1.0f;               // play along: song speed (0.5 - 1.0)
		float uiScale = 1.0f;
		bool  showKeyboard = true;        // the key guide at the bottom of the screen

		// The crowd
		float listenRange = 1500.0f;  // how far you can be heard (game units; 70 is about a metre)
		float tipRate = 1.0f;
		float tipSize = 1.0f;
		int   maxTipPerListener = 40;  // gold one patron gives in one performance at most
		float patience = 1.0f;         // how quickly a bad set turns them
		bool  throwOut = true;         // a hostile crowd in an inn gets you thrown out (and banned there for the day)
		float speechXp = 0.5f;         // Speech experience per septim earned

		// Animation (the idles' editor IDs)
		std::string luteIdle = "IdleLuteStart";
		std::string fluteIdle = "IdleFluteStart";
		std::string stopIdle = "IdlePlayerStop";

		// Diagnostics
		bool logInput = false;
	};

	const Values& Get();
	Values&       Edit();  // the in-game settings page changes these live
	void          Load();
	void          Save();
}
