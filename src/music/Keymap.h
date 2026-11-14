// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// The computer keyboard as an instrument: three rows of seven keys, each row an octave of the white notes
// (C D E F G A B), the bottom row lowest. Hold Shift for the note a semitone up (the black keys).
//
//   Q W E R T Y U    high octave   (base + 24)
//   A S D F G H J    middle octave (base + 12)
//   Z X C V B N M    low octave    (base)
//
// Keys are DirectInput scan codes, which is what the game reports.
namespace music::keymap
{
	inline constexpr int kRows[3][7] = {
		{ 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32 },  // Z X C V B N M
		{ 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24 },  // A S D F G H J
		{ 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16 },  // Q W E R T Y U
	};
	inline constexpr char kLetters[3][7] = {
		{ 'Z', 'X', 'C', 'V', 'B', 'N', 'M' },
		{ 'A', 'S', 'D', 'F', 'G', 'H', 'J' },
		{ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U' },
	};
	inline constexpr int kWhite[7] = { 0, 2, 4, 5, 7, 9, 11 };
	inline constexpr int kLeftShift = 0x2A, kRightShift = 0x36;

	// The key's row and column, or false if it isn't a note key
	inline bool Find(int a_scan, int& a_row, int& a_col)
	{
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 7; ++c) {
				if (kRows[r][c] == a_scan) {
					a_row = r;
					a_col = c;
					return true;
				}
			}
		}
		return false;
	}

	// The pitch a key plays, or -1
	inline int Pitch(int a_scan, int a_base, bool a_shift)
	{
		int r = 0, c = 0;
		if (!Find(a_scan, r, c)) {
			return -1;
		}
		return a_base + r * 12 + kWhite[c] + (a_shift ? 1 : 0);
	}

	// Which key plays a pitch: false if it's outside the three rows. Sharps (and E#/B#, which don't exist on the
	// keyboard) are the white key below with Shift.
	struct Label
	{
		int  row = 0, col = 0;
		bool shift = false;
		char letter = '?';
	};
	inline bool LabelFor(int a_pitch, int a_base, Label& a_out)
	{
		const int rel = a_pitch - a_base;
		if (rel < 0 || rel > 3 * 12 - 1 + 1) {
			return false;
		}
		int row = rel / 12, pc = rel % 12;
		if (row > 2) {
			return false;
		}
		int  col = -1;
		bool shift = false;
		for (int c = 6; c >= 0; --c) {
			if (kWhite[c] == pc) {
				col = c;
				break;
			}
			if (kWhite[c] == pc - 1) {
				col = c;
				shift = true;
				break;
			}
		}
		if (col < 0) {
			return false;
		}
		a_out = { row, col, shift, kLetters[row][col] };
		return true;
	}
}
