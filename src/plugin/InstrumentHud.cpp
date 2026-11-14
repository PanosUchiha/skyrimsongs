// SkyrimSongs - play the lute and flute in Skyrim.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "InstrumentHud.h"

#include "../music/Keymap.h"
#include "../music/Songs.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace InstrumentHud
{
	namespace
	{
		constexpr float kPi = 3.14159265f;

		ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

		ImU32 Mix(ImU32 a_a, ImU32 a_b, float a_t)
		{
			a_t = std::clamp(a_t, 0.0f, 1.0f);
			auto ch = [&](int sh) {
				const float x = static_cast<float>((a_a >> sh) & 0xFF), y = static_cast<float>((a_b >> sh) & 0xFF);
				return static_cast<ImU32>(x + (y - x) * a_t + 0.5f) << sh;
			};
			return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | ch(IM_COL32_A_SHIFT);
		}

		ImU32 Alpha(ImU32 a_c, float a_a)
		{
			const auto a = static_cast<ImU32>(((a_c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a_a, 0.0f, 1.0f));
			return (a_c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
		}

		// a quad with a colour at each corner (a b c d clockwise)
		void GradQuad(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd)
		{
			const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
			dl->PrimReserve(6, 4);
			const auto i = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
			dl->PrimWriteIdx(i);
			dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 1));
			dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 2));
			dl->PrimWriteIdx(i);
			dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 2));
			dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 3));
			dl->PrimWriteVtx(a, uv, ca);
			dl->PrimWriteVtx(b, uv, cb);
			dl->PrimWriteVtx(c, uv, cc);
			dl->PrimWriteVtx(d, uv, cd);
		}

		// a soft glow: rings fading out
		void Glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float strength)
		{
			if (strength <= 0.01f) {
				return;
			}
			for (int k = 6; k >= 1; --k) {
				const float t = k / 6.0f;
				dl->AddCircleFilled(c, r * (1.0f + 1.6f * t), Alpha(col, strength * 0.16f * (1.0f - t) + 0.02f * strength), 32);
			}
		}

		void Label(ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 col, const std::string& s, float alignX = 0.5f, float alignY = 0.5f, bool shadow = true)
		{
			const ImVec2 sz = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, s.c_str());
			p.x -= sz.x * alignX;
			p.y -= sz.y * alignY;
			if (shadow) {
				dl->AddText(f, f->FontSize, p + ImVec2(1, 1), IM_COL32(0, 0, 0, static_cast<int>(((col >> IM_COL32_A_SHIFT) & 0xFF) * 0.6f)), s.c_str());
			}
			dl->AddText(f, f->FontSize, p, col, s.c_str());
		}

		// where a pitch is on the instrument: its row (0 low .. 2 high), column (0..6, 7 = the top C) and sharp;
		// notes outside the 37 are folded in by octaves
		struct Cell
		{
			int  row = 0, col = 0;
			bool sharp = false;
		};
		Cell Place(int a_pitch, int a_base)
		{
			int rel = a_pitch - a_base;
			while (rel < 0) {
				rel += 12;
			}
			while (rel > 36) {
				rel -= 12;
			}
			if (rel == 36) {
				return { 2, 7, false };
			}
			music::keymap::Label l;
			music::keymap::LabelFor(a_base + rel, a_base, l);
			return { l.row, l.col, l.shift };
		}

		// how lit a struck note still is (1 when struck, fading over half a second)
		float LitOf(float a_age) { return std::clamp(1.0f - a_age / 0.5f, 0.0f, 1.0f); }
	}

	// ================================================================================================== the lute
	void DrawLute(ImDrawList* dl, ImVec2 pos, ImVec2 size, float s, int base, const std::vector<Sounding>& notes, double now, const Fonts& f)
	{
		const float W = size.x, H = size.y;
		const float yc = pos.y + H * 0.54f;
		dl->PushClipRect(pos, pos + size, true);

		// ---- the body: a rounded belly of pale spruce with a carved rosette, at the right
		const float  bodyR = H * 0.95f;
		const ImVec2 bodyC(pos.x + W - bodyR * 0.25f, yc);
		for (int k = 14; k >= 0; --k) {
			const float t = k / 14.0f;
			dl->AddCircleFilled(bodyC, bodyR * (0.35f + 0.65f * t), Mix(Rgb(214, 178, 118), Rgb(120, 84, 44), t * t), 64);
		}
		dl->AddCircle(bodyC, bodyR - 2.0f * s, Rgb(60, 38, 20, 230), 96, 4.0f * s);
		dl->AddCircle(bodyC, bodyR - 7.0f * s, Rgb(232, 214, 176, 120), 96, 1.2f * s);
		// the rosette: a dark hole behind a carved lattice
		const ImVec2 roseC(bodyC.x - bodyR * 0.42f, yc);
		const float  roseR = H * 0.30f;
		dl->AddCircleFilled(roseC, roseR, Rgb(26, 17, 9), 48);
		for (int ring = 0; ring < 3; ++ring) {
			const float rr = roseR * (0.35f + 0.28f * ring);
			const int   n = 8 + ring * 6;
			for (int i = 0; i < n; ++i) {
				const float a = (i + 0.5f * ring) * 2.0f * kPi / n;
				dl->AddCircle(roseC + ImVec2(std::cos(a), std::sin(a)) * rr, roseR * 0.13f, Rgb(196, 160, 102, 200), 12, 1.2f * s);
			}
			dl->AddCircle(roseC, rr, Rgb(160, 124, 74, 150), 48, 1.0f * s);
		}
		dl->AddCircle(roseC, roseR, Rgb(84, 56, 28), 48, 3.0f * s);
		dl->AddCircle(roseC, roseR + 5.0f * s, Rgb(70, 46, 24, 200), 48, 1.5f * s);
		dl->AddCircle(roseC, roseR + 9.0f * s, Rgb(232, 210, 168, 110), 48, 1.0f * s);

		// ---- the neck: carved walnut, darker at the edges, with grain; widening toward the body
		const float x0 = pos.x + 14.0f * s, x1 = roseC.x - roseR - 16.0f * s;
		const float hl = H * 0.38f, hr = H * 0.44f;
		auto        topY = [&](float x) { return yc - (hl + (hr - hl) * (x - x0) / (x1 - x0)); };
		auto        botY = [&](float x) { return yc + (hl + (hr - hl) * (x - x0) / (x1 - x0)); };
		// its shadow on the body and the panel
		dl->AddQuadFilled({ x0 + 4 * s, topY(x0) + 6 * s }, { x1 + 4 * s, topY(x1) + 6 * s }, { x1 + 4 * s, botY(x1) + 6 * s }, { x0 + 4 * s, botY(x0) + 6 * s },
			Rgb(0, 0, 0, 110));
		constexpr int kBands = 12;
		for (int b = 0; b < kBands; ++b) {
			const float t0 = b / static_cast<float>(kBands), t1 = (b + 1) / static_cast<float>(kBands);
			auto        shade = [](float t) {
                const float edge = std::abs(t - 0.42f) / 0.58f;  // the light falls from just above the middle
                return Mix(Rgb(132, 86, 48), Rgb(46, 28, 15), std::pow(edge, 1.4f));
			};
			const float yl0 = topY(x0) + (botY(x0) - topY(x0)) * t0, yl1 = topY(x0) + (botY(x0) - topY(x0)) * t1;
			const float yr0 = topY(x1) + (botY(x1) - topY(x1)) * t0, yr1 = topY(x1) + (botY(x1) - topY(x1)) * t1;
			GradQuad(dl, { x0, yl0 }, { x1, yr0 }, { x1, yr1 }, { x0, yl1 }, shade(t0), shade(t0), shade(t1), shade(t1));
		}
		// grain
		for (int g = 0; g < 16; ++g) {
			const float t = (g + 0.5f) / 16.0f;
			ImVec2      pts[48];
			for (int i = 0; i < 48; ++i) {
				const float x = x0 + (x1 - x0) * i / 47.0f;
				const float y = topY(x) + (botY(x) - topY(x)) * t + std::sin(x * 0.013f / s + g * 1.7f) * 1.6f * s + std::sin(x * 0.041f / s + g) * 0.6f * s;
				pts[i] = { x, y };
			}
			dl->AddPolyline(pts, 48, g % 2 ? Rgb(20, 12, 6, 40) : Rgb(190, 140, 90, 22), 0, (g % 3 == 0 ? 1.4f : 0.8f) * s);
		}
		// ivory binding along both edges, the nut at the left
		dl->AddLine({ x0, topY(x0) }, { x1, topY(x1) }, Rgb(236, 226, 200, 210), 1.8f * s);
		dl->AddLine({ x0, botY(x0) }, { x1, botY(x1) }, Rgb(236, 226, 200, 160), 1.8f * s);
		dl->AddRectFilled({ x0 - 2 * s, topY(x0) - 1 * s }, { x0 + 6 * s, botY(x0) + 1 * s }, Rgb(236, 226, 200), 2.0f * s);
		dl->AddLine({ x0 + 6 * s, topY(x0) }, { x0 + 6 * s, botY(x0) }, Rgb(120, 100, 70, 200), 1.0f * s);

		// ---- frets: eight places, each a little narrower than the last, as on a real neck
		const float xa = x0 + 10.0f * s, xb = x1 - 8.0f * s;
		float       w[8], sum = 0.0f;
		for (int k = 0; k < 8; ++k) {
			w[k] = std::pow(0.94f, static_cast<float>(k));
			sum += w[k];
		}
		float edges[9];
		edges[0] = xa;
		for (int k = 0; k < 8; ++k) {
			edges[k + 1] = edges[k] + (xb - xa) * w[k] / sum;
		}
		auto slotX = [&](int c) { return (edges[c] + edges[c + 1]) * 0.5f; };
		for (int k = 1; k <= 8; ++k) {
			const float x = edges[k];
			dl->AddLine({ x + 1.6f * s, topY(x) + 1 }, { x + 1.6f * s, botY(x) - 1 }, Rgb(0, 0, 0, 120), 2.0f * s);
			dl->AddLine({ x, topY(x) + 1 }, { x, botY(x) - 1 }, Rgb(150, 140, 126), 2.6f * s);
			dl->AddLine({ x - 0.5f * s, topY(x) + 1 }, { x - 0.5f * s, botY(x) - 1 }, Rgb(240, 236, 226, 220), 1.0f * s);
		}
		// mother-of-pearl inlays
		for (int c : { 2, 4 }) {
			dl->AddCircleFilled({ slotX(c), yc }, 3.2f * s, Rgb(232, 230, 220, 140), 16);
		}
		dl->AddCircleFilled({ slotX(6), yc - H * 0.12f }, 3.0f * s, Rgb(232, 230, 220, 120), 16);
		dl->AddCircleFilled({ slotX(6), yc + H * 0.12f }, 3.0f * s, Rgb(232, 230, 220, 120), 16);
		// the note names, engraved above
		static constexpr const char* kNames[8] = { "C", "D", "E", "F", "G", "A", "B", "C" };
		for (int c = 0; c < 8; ++c) {
			Label(dl, f.label, { slotX(c), topY(slotX(c)) - 11.0f * s }, Rgb(226, 206, 160, 170), kNames[c]);
		}

		// ---- the strings: three courses, bass (Z) lowest on screen, treble (Q) on top
		const float spacing = H * 0.25f;
		auto        rowY = [&](int r) { return yc + (1 - r) * spacing; };
		// the latest note on each course
		const Sounding* last[3]{};
		Cell            lastCell[3]{};
		for (const auto& n : notes) {
			const Cell c = Place(n.pitch, base);
			if (!last[c.row] || n.age < last[c.row]->age) {
				last[c.row] = &n;
				lastCell[c.row] = c;
			}
		}
		for (int r = 0; r < 3; ++r) {
			const float  y = rowY(r);
			const float  thick = (r == 0 ? 2.6f : r == 1 ? 1.9f : 1.4f) * s;
			const int    pair = r == 2 ? 1 : 2;  // the top course is a single string (the chanterelle)
			const ImU32  gut = r == 0 ? Rgb(196, 168, 120) : Rgb(222, 204, 160);
			const float  vib = last[r] ? std::exp(-last[r]->age / 0.5f) : 0.0f;
			const float  fretX = last[r] ? edges[lastCell[r].col + (lastCell[r].sharp ? 1 : 0)] : xb;
			for (int k = 0; k < pair; ++k) {
				const float yy = y + (pair == 2 ? (k == 0 ? -2.0f : 2.0f) * s : 0.0f);
				// shadow on the wood
				dl->AddLine({ x0, yy + 2.5f * s }, { x1 + 30 * s, yy + 2.5f * s }, Rgb(0, 0, 0, 90), thick);
				if (vib < 0.02f) {
					dl->AddLine({ x0, yy }, { x1 + 30 * s, yy }, gut, thick);
					continue;
				}
				// stopped at the fret, it shimmers from there to the bridge
				dl->AddLine({ x0, yy }, { fretX, yy }, gut, thick);
				constexpr int kN = 64;
				ImVec2        pts[kN];
				const float   amp = 4.5f * s * vib;
				const float   tt = static_cast<float>(now) * 2.0f * kPi;
				for (int i = 0; i < kN; ++i) {
					const float u = i / (kN - 1.0f);
					const float x = fretX + (x1 + 30 * s - fretX) * u;
					const float d = amp * (std::sin(kPi * u) * std::sin(tt * 13.0f + k) + 0.35f * std::sin(2 * kPi * u) * std::sin(tt * 26.0f + 1.3f));
					pts[i] = { x, yy + d };
				}
				dl->AddPolyline(pts, kN, Rgb(246, 206, 110, static_cast<int>(70 * vib)), 0, thick * 4.0f);
				dl->AddPolyline(pts, kN, Mix(gut, Rgb(255, 222, 130), vib), 0, thick);
			}
		}

		// ---- the key letters on the frets: lit as their notes sound; a diamond on the fret for each sharp
		for (int r = 0; r < 3; ++r) {
			const float y = rowY(r);
			for (int c = 0; c < (r == 2 ? 8 : 7); ++c) {
				float lit = 0.0f;
				bool  sharpLit = false;
				float sharpGlow = 0.0f;
				for (const auto& n : notes) {
					const Cell cell = Place(n.pitch, base);
					if (cell.row == r && cell.col == c) {
						if (cell.sharp) {
							sharpGlow = (std::max)(sharpGlow, LitOf(n.age));
							sharpLit = true;
						} else {
							lit = (std::max)(lit, LitOf(n.age));
						}
					}
				}
				const ImVec2 cp(slotX(c), y);
				const float  rad = 13.0f * s;
				Glow(dl, cp, rad, Rgb(255, 200, 90), lit);
				dl->AddCircleFilled(cp + ImVec2(0, 2 * s), rad, Rgb(0, 0, 0, 120), 32);
				dl->AddCircleFilled(cp, rad, Mix(Rgb(30, 20, 12, 215), Rgb(244, 200, 96), lit), 32);
				dl->AddCircle(cp, rad, Mix(Rgb(200, 164, 104, 170), Rgb(255, 236, 170), lit), 32, 1.4f * s);
				const char letter = c == 7 ? 'I' : music::keymap::kLetters[r][c];
				Label(dl, f.key, cp, Mix(Rgb(242, 236, 222), Rgb(46, 30, 12), lit), std::string(1, letter));
				// the sharp above it, on the next fret
				if (c < 7 && c != 2 && c != 6) {
					const ImVec2 dp(edges[c + 1], y - 15.0f * s);
					const float  d = 4.2f * s;
					Glow(dl, dp, d, Rgb(255, 200, 90), sharpGlow);
					dl->AddQuadFilled(dp + ImVec2(0, -d), dp + ImVec2(d, 0), dp + ImVec2(0, d), dp + ImVec2(-d, 0),
						sharpLit ? Mix(Rgb(200, 180, 150, 120), Rgb(255, 214, 110), sharpGlow) : Rgb(226, 214, 186, 90));
				}
			}
		}
		dl->PopClipRect();
	}

	// ================================================================================================== the flute
	void DrawFlute(ImDrawList* dl, ImVec2 pos, ImVec2 size, float s, int base, const std::vector<Sounding>& notes, double now, const Fonts& f)
	{
		const float W = size.x, H = size.y;
		dl->PushClipRect(pos, pos + size, true);
		const float r = H * 0.13f;  // the tube's radius
		const float yc = pos.y + r + 16.0f * s;
		const float xL = pos.x + 70.0f * s, xR = pos.x + W - 18.0f * s;

		// the latest note, and how much breath is in it
		const Sounding* last = nullptr;
		for (const auto& n : notes) {
			if (!last || n.age < last->age) {
				last = &n;
			}
		}
		const Cell  lastCell = last ? Place(last->pitch, base) : Cell{};
		const float breath = last ? std::clamp(1.0f - last->age / 1.2f, 0.0f, 1.0f) : 0.0f;

		// ---- the tube: polished silver, a bright highlight high on the curve, darker below
		dl->AddRectFilled({ xL + 3 * s, yc - r + 6 * s }, { xR + 3 * s, yc + r + 6 * s }, Rgb(0, 0, 0, 100), r);
		constexpr int kBands = 14;
		auto          silver = [](float t) {
            // t: 0 top edge .. 1 bottom edge
            if (t < 0.22f) {
                return Mix(Rgb(96, 104, 116), Rgb(250, 252, 255), t / 0.22f);
            }
            if (t < 0.45f) {
                return Mix(Rgb(250, 252, 255), Rgb(176, 184, 196), (t - 0.22f) / 0.23f);
            }
            if (t < 0.8f) {
                return Mix(Rgb(176, 184, 196), Rgb(112, 120, 132), (t - 0.45f) / 0.35f);
            }
            return Mix(Rgb(112, 120, 132), Rgb(54, 60, 70), (t - 0.8f) / 0.2f);
		};
		for (int b = 0; b < kBands; ++b) {
			const float t0 = b / static_cast<float>(kBands), t1 = (b + 1) / static_cast<float>(kBands);
			const float y0 = yc - r + 2 * r * t0, y1 = yc - r + 2 * r * t1;
			GradQuad(dl, { xL, y0 }, { xR, y0 }, { xR, y1 }, { xL, y1 }, silver(t0), silver(t0), silver(t1), silver(t1));
		}
		// a soft reflection of the room along the body
		dl->AddLine({ xL + 10 * s, yc - r * 0.55f }, { xR - 10 * s, yc - r * 0.55f }, Rgb(255, 255, 255, 110), 1.5f * s);
		dl->AddLine({ xL + 10 * s, yc + r * 0.55f }, { xR - 10 * s, yc + r * 0.55f }, Rgb(220, 230, 245, 40), 1.0f * s);
		// the crown and the foot, and the joints
		dl->AddRectFilled({ xL - 10 * s, yc - r * 0.92f }, { xL + 2 * s, yc + r * 0.92f }, Rgb(150, 158, 170), 4 * s);
		dl->AddRectFilled({ xL - 14 * s, yc - r * 0.6f }, { xL - 8 * s, yc + r * 0.6f }, Rgb(200, 206, 214), 3 * s);
		for (float jx : { xL + (xR - xL) * 0.25f, xR - (xR - xL) * 0.13f }) {
			dl->AddRectFilled({ jx - 5 * s, yc - r - 1.5f * s }, { jx + 5 * s, yc + r + 1.5f * s }, Rgb(150, 158, 170), 2 * s);
			dl->AddLine({ jx - 5 * s, yc - r }, { jx - 5 * s, yc + r }, Rgb(255, 255, 255, 140), 1.0f * s);
			dl->AddLine({ jx + 5 * s, yc - r }, { jx + 5 * s, yc + r }, Rgb(40, 44, 52, 160), 1.0f * s);
		}
		// the lip plate and the embouchure hole
		const ImVec2 lipC(xL + 48.0f * s, yc);
		{
			const ImVec2 a = lipC - ImVec2(26 * s, r * 1.25f), b = lipC + ImVec2(26 * s, r * 1.25f);
			dl->AddRectFilled(a + ImVec2(2 * s, 4 * s), b + ImVec2(2 * s, 4 * s), Rgb(0, 0, 0, 90), 12 * s);
			dl->AddRectFilled(a, b, Rgb(120, 128, 140), 12 * s);
			// the plate's own curve: bright along its upper edge, darker under
			const float  inset = 3 * s;
			const ImVec2 ia = a + ImVec2(inset, inset), ib = b - ImVec2(inset, inset);
			const float  mid = ia.y + (ib.y - ia.y) * 0.3f;
			GradQuad(dl, ia, { ib.x, ia.y }, { ib.x, mid }, { ia.x, mid }, Rgb(196, 204, 214), Rgb(196, 204, 214), Rgb(250, 252, 255), Rgb(250, 252, 255));
			GradQuad(dl, { ia.x, mid }, { ib.x, mid }, ib, { ia.x, ib.y }, Rgb(250, 252, 255), Rgb(250, 252, 255), Rgb(150, 158, 170), Rgb(150, 158, 170));
			dl->AddRect(a, b, Rgb(90, 98, 110), 12 * s, 0, 1.2f * s);
		}
		dl->AddEllipseFilled(lipC, { 11 * s, 6.5f * s }, Rgb(18, 20, 26), 0.0f, 24);
		dl->AddEllipse(lipC, { 11 * s, 6.5f * s }, Rgb(250, 252, 255, 160), 0.0f, 24, 1.0f * s);

		// the breath: wisps of air curling off the mouthpiece while a note sounds
		if (breath > 0.0f) {
			for (int i = 0; i < 7; ++i) {
				const float ph = static_cast<float>(std::fmod(now * 0.9 + i / 7.0, 1.0));
				const float x = lipC.x - 6 * s - ph * 34 * s + std::sin(ph * 9.0f + i) * 4 * s;
				const float y = lipC.y - 10 * s - ph * 22 * s;
				dl->AddCircleFilled({ x, y }, (2.0f + 5.0f * ph) * s, Rgb(210, 230, 255, static_cast<int>(70 * breath * (1.0f - ph))), 16);
			}
		}

		// ---- the keywork: a rod along the top, seven tone holes, the I trill key at the foot
		const float hx0 = xL + (xR - xL) * 0.34f, hx1 = xR - (xR - xL) * 0.18f;
		auto        holeX = [&](int c) { return hx0 + (hx1 - hx0) * c / 6.0f; };
		dl->AddLine({ hx0 - 20 * s, yc - r * 0.95f }, { xR - 30 * s, yc - r * 0.95f }, Rgb(232, 236, 242, 200), 2.0f * s);
		dl->AddLine({ hx0 - 20 * s, yc - r * 0.95f + 2 * s }, { xR - 30 * s, yc - r * 0.95f + 2 * s }, Rgb(60, 66, 76, 140), 1.0f * s);
		for (int c = 0; c <= 7; ++c) {
			const bool  trill = c == 7;
			const float x = trill ? xR - (xR - xL) * 0.06f : holeX(c);
			float       lit = 0.0f, sharpLit = 0.0f;
			for (const auto& n : notes) {
				const Cell cell = Place(n.pitch, base);
				if ((trill && cell.col == 7) || (!trill && cell.col == c && cell.col != 7)) {
					(cell.sharp ? sharpLit : lit) = (std::max)(cell.sharp ? sharpLit : lit, LitOf(n.age));
				}
			}
			const ImVec2 hc(x, yc + r * 0.1f);
			const float  cupR = trill ? r * 0.42f : r * 0.66f;
			// the post holding the key
			dl->AddLine({ x, yc - r * 0.95f }, { x, hc.y - cupR }, Rgb(200, 206, 214), 2.0f * s);
			Glow(dl, hc, cupR, Rgb(255, 214, 120), lit);
			dl->AddCircleFilled(hc + ImVec2(0, 2 * s), cupR + 2 * s, Rgb(0, 0, 0, 90), 32);
			dl->AddCircleFilled(hc, cupR + 2 * s, Rgb(236, 240, 246), 32);
			dl->AddCircleFilled(hc, cupR, Rgb(104, 112, 124), 32);
			dl->AddCircleFilled(hc, cupR * 0.72f, Mix(Rgb(20, 22, 28), Rgb(255, 218, 128), lit), 32);
			dl->AddCircle(hc, cupR * 0.72f, Rgb(250, 252, 255, 120), 32, 1.0f * s);
			// the note's name, over the hole while it sounds
			if (last && (std::max)(lit, sharpLit) > 0.0f && (trill ? lastCell.col == 7 : lastCell.col == c)) {
				Label(dl, f.label, { x, yc - r - 14.0f * s }, Rgb(255, 226, 150, static_cast<int>(255 * (std::max)(lit, sharpLit))),
					music::NoteName(last->pitch));
			}
			if (!trill && c != 2 && c != 6) {
				// the small key that gives the sharp
				const ImVec2 sc(x + cupR + 9 * s, hc.y - cupR * 0.4f);
				Glow(dl, sc, 4.0f * s, Rgb(255, 214, 120), sharpLit);
				dl->AddEllipseFilled(sc, { 5.0f * s, 3.4f * s }, Mix(Rgb(200, 206, 214), Rgb(255, 220, 130), sharpLit), 0.0f, 16);
				dl->AddEllipse(sc, { 5.0f * s, 3.4f * s }, Rgb(80, 86, 96, 180), 0.0f, 16, 1.0f * s);
			}
			// the three letters for this hole, one per register: high, middle, low
			const float ly = yc + r + 20.0f * s;
			for (int row = 2; row >= 0; --row) {
				if (trill && row != 2) {
					continue;
				}
				const bool  active = last && breath > 0.0f && lastCell.row == row && (trill ? lastCell.col == 7 : lastCell.col == c);
				const char  letter = trill ? 'I' : music::keymap::kLetters[row][c];
				const float yy = ly + (2 - row) * 21.0f * s;
				const float on = active ? (std::max)(lit, sharpLit) : 0.0f;
				if (on > 0.0f) {
					dl->AddRectFilled({ x - 12 * s, yy - 10 * s }, { x + 12 * s, yy + 10 * s }, Rgb(255, 214, 120, static_cast<int>(60 * on)), 4 * s);
				}
				Label(dl, f.key, { x, yy }, Mix(row == 1 ? Rgb(214, 222, 234) : Rgb(132, 142, 158), Rgb(255, 226, 150), on), std::string(1, letter));
			}
		}
		// the registers, and the breath gauge
		static constexpr const char* kReg[3] = { "low", "mid", "high" };
		const float ly = yc + r + 20.0f * s;
		for (int row = 2; row >= 0; --row) {
			const bool on = last && breath > 0.0f && lastCell.row == row;
			Label(dl, f.label, { pos.x + 30 * s, ly + (2 - row) * 21.0f * s }, on ? Rgb(255, 226, 150) : Rgb(150, 160, 176), kReg[row]);
		}
		const ImVec2 g0(pos.x + 64 * s, ly - 9 * s), g1(pos.x + 70 * s, ly + 2 * 21.0f * s + 9 * s);
		dl->AddRectFilled(g0, g1, Rgb(30, 34, 42, 220), 3 * s);
		const float gh = (g1.y - g0.y) * breath;
		GradQuad(dl, { g0.x, g1.y - gh }, { g1.x, g1.y - gh }, g1, { g0.x, g1.y }, Rgb(220, 236, 255), Rgb(220, 236, 255), Rgb(120, 160, 220),
			Rgb(120, 160, 220));
		dl->AddRect(g0, g1, Rgb(150, 160, 176, 160), 3 * s, 0, 1.0f * s);
		dl->PopClipRect();
	}
}
