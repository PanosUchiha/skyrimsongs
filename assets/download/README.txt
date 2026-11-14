SkyrimSongs 0.1.2
=================

Play the lute and flute for real, anywhere in Skyrim, with your keyboard or a USB MIDI keyboard. Whoever is in
earshot listens: play well and they toss you coins, play badly and they heckle - and in an inn, a bad enough set
gets you thrown out.

Requirements: SKSE64 and Address Library for SKSE Plugins, for your game version. One DLL works on 1.5.97 and
every 1.6 release (Steam and GOG). No plugin (.esp) needed.

How to play
  - Carry a lute or a flute (the game's own, the Bards College quest ones too; RequireInstrument = 0 in the INI
    lets you play without one). Press O to take it out and Esc to put it away. Your weapon must be sheathed.
    While you play, every other key (hotkeys too) is kept from the game; Esc is the only way out.
  - 37 notes: three rows of keys are three octaves of the white notes, lowest at the bottom, and I is the top C:
        Q W E R T Y U I    high       (I = top C)
        A S D F G H J      middle
        Z X C V B N M      low        (C D E F G A B)
    Hold Shift for the sharp (the black key above). Up / Down arrows move every row an octave.
  - A MIDI keyboard plugged in before you take out your instrument just works (velocity and the sustain pedal too).
  - Tab opens the songbook (Esc closes it): switch instrument, play along with a song (the notes slide to the line with the key to
    press), or let a song play itself (the cheat). Esc stops a song; Esc again puts the instrument away.
  - Your MIDI files: put .mid files in Data\SKSE\Plugins\SkyrimSongs\Songs.

The crowd judges free play on rhythm (a steady beat), key (staying in one scale), melody (steps and small leaps,
not random jumps), pace and variety; playing along, on hitting each note in time. Tips come faster and bigger the
happier the room and the better your Speech; you gain Speech experience for what you earn.

Settings: in game with SKSE Menu Framework (Mod Control Panel > Skyrim Songs), or Data\SKSE\Plugins\SkyrimSongs.ini.
Log: Documents\My Games\Skyrim Special Edition\SKSE\SkyrimSongs.log

Source and license (GPL-3.0): https://github.com/evodube/skyrimsongs
Fonts (OFL): Barlow Condensed, IM Fell English.
