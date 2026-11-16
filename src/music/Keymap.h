// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// The computer keyboard as an instrument: three rows of seven keys, each row an octave of the white notes
// (C D E F G A B), the bottom row lowest, and I for the C on top. Hold Shift for the note a semitone up (the
// black keys). 37 notes: three full octaves and the top C, like a 37-key keyboard.
//
//   Q W E R T Y U I  high octave   (base + 24), I = base + 36
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
	inline constexpr int kTopC = 0x17;  // I: the top C
	inline constexpr int kNotes = 37;   // base .. base + 36
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
		if (a_scan == kTopC) {
			return a_base + 36;  // nothing above it
		}
		int r = 0, c = 0;
		if (!Find(a_scan, r, c)) {
			return -1;
		}
		return a_base + r * 12 + kWhite[c] + (a_shift ? 1 : 0);
	}

	// Which key plays a pitch: false if it's outside the 37 notes. Sharps are the white key below with Shift; the
	// top C is I (row 2, column 7).
	struct Label
	{
		int  row = 0, col = 0;  // col 7: the top C
		bool shift = false;
		char letter = '?';
	};
	inline bool LabelFor(int a_pitch, int a_base, Label& a_out)
	{
		const int rel = a_pitch - a_base;
		if (rel < 0 || rel > 36) {
			return false;
		}
		if (rel == 36) {
			a_out = { 2, 7, false, 'I' };
			return true;
		}
		int row = rel / 12, pc = rel % 12;
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

	// Virtual Piano's 61-key sheet layout: 36 white keys from C2 through C7.
	// The sheet sequence is grouped by octave: 1..7, 8 9 0 Q W E R, T Y U I O P A,
	// S D F G H J K, L Z X C V B N, then M. Shift plays the black key immediately
	// above a white key where one exists (C, D, F, G, A). Use physical scan codes.
	inline constexpr int kPianoScans[36] = {
		// C2-B2: 1 2 3 4 5 6 7
		0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		// C3-B3: 8 9 0 Q W E R
		0x09, 0x0A, 0x0B, 0x10, 0x11, 0x12, 0x13,
		// C4-B4: T Y U I O P A
		0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1E,
		// C5-B5: S D F G H J K
		0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
		// C6-B6: L Z X C V B N
		0x26, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31,
		// C7: M
		0x32
	};
	inline constexpr char kPianoLabels[36] = {
		'1', '2', '3', '4', '5', '6', '7',
		'8', '9', '0', 'q', 'w', 'e', 'r',
		't', 'y', 'u', 'i', 'o', 'p', 'a',
		's', 'd', 'f', 'g', 'h', 'j', 'k',
		'l', 'z', 'x', 'c', 'v', 'b', 'n',
		'm'
	};
	inline constexpr int kPianoWhitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
	inline constexpr int kPianoLow = 36;   // C2
	inline constexpr int kPianoHigh = 96;  // C7
	inline constexpr int kPianoWhiteCount = 36;
	inline constexpr int kPianoKeyCount = 61;

	inline int PianoWhitePitch(int a_index)
	{
		if (a_index < 0 || a_index >= kPianoWhiteCount) {
			return -1;
		}
		// The 36th white key is the single C7 at the top of the five-octave range.
		if (a_index == kPianoWhiteCount - 1) {
			return kPianoHigh;
		}
		return kPianoLow + (a_index / 7) * 12 + kPianoWhitePc[a_index % 7];
	}

	inline int PianoKeyIndex(int a_scan)
	{
		for (int i = 0; i < kPianoWhiteCount; ++i) {
			if (kPianoScans[i] == a_scan) {
				return i;
			}
		}
		return -1;
	}

	inline bool PianoHasBlackAfter(int a_whiteIndex)
	{
		const int pitch = PianoWhitePitch(a_whiteIndex);
		if (pitch < 0 || pitch >= kPianoHigh) {
			return false;
		}
		const int pc = pitch % 12;
		return pc != 4 && pc != 11;  // E and B have no black key above them.
	}

	// Return a MIDI pitch, or -1 for a key that is not part of the piano map.
	inline int PianoPitch(int a_scan, bool a_shift)
	{
		const int index = PianoKeyIndex(a_scan);
		if (index < 0) {
			return -1;
		}
		const int white = PianoWhitePitch(index);
		if (!a_shift) {
			return white;
		}
		return PianoHasBlackAfter(index) ? white + 1 : -1;
	}

	// A sheet-style label for a shifted key: symbols for Shift+number, capitals for Shift+letter.
	inline char PianoBlackLabel(int a_whiteIndex)
	{
		if (a_whiteIndex < 0 || a_whiteIndex >= kPianoWhiteCount || !PianoHasBlackAfter(a_whiteIndex)) {
			return '?';
		}
		const char key = kPianoLabels[a_whiteIndex];
		if (key >= 'a' && key <= 'z') {
			return static_cast<char>(key - 'a' + 'A');
		}
		// Shift symbols correspond to the actual number key, not its index in the 36-note array.
		constexpr char symbols[] = "!@#$%^&*()";
		if (key >= '1' && key <= '9') {
			return symbols[key - '1'];
		}
		if (key == '0') {
			return ')';
		}
		return '?';
	}

	// Translate a Virtual Piano music-sheet character to its MIDI pitch. Lowercase letters and digits are white
	// keys; capitals and shifted number-row symbols are the black key immediately above the matching white key.
	// Characters for E/B (and their octave equivalents) have no black key and are rejected.
	inline int PianoSheetPitch(char a_char)
	{
		for (int i = 0; i < kPianoWhiteCount; ++i) {
			if (a_char == kPianoLabels[i]) {
				return PianoWhitePitch(i);
			}
			const char black = PianoBlackLabel(i);
			if (black != '?' && a_char == black) {
				return PianoWhitePitch(i) + 1;
			}
		}
		return -1;
	}

	inline bool IsPianoSheetKey(char a_char)
	{
		return PianoSheetPitch(a_char) >= 0;
	}

	// Reverse the Virtual Piano sheet notation mapping for displaying parsed notes.
	inline char PianoSheetLabelForPitch(int a_pitch)
	{
		for (int i = 0; i < kPianoWhiteCount; ++i) {
			if (PianoWhitePitch(i) == a_pitch) {
				return kPianoLabels[i];
			}
			if (PianoHasBlackAfter(i) && PianoWhitePitch(i) + 1 == a_pitch) {
				return PianoBlackLabel(i);
			}
		}
		return '?';
	}

}
