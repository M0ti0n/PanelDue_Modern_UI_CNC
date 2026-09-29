/*
 * CncWidgets.cpp
 *
 * Portrait CNC UI widgets. Glyphs follow the approved SVG mock-ups and reuse the
 * Modern UI drawing style (same tile colours, 2 px accent outline when selected).
 */

#include "CncWidgets.hpp"
#include "PanelDue.hpp"
#include "UI/Cnc/CncLayout.hpp"

extern const uint8_t glcd28x32[];
extern const uint8_t glcd19x21[];

namespace
{
	const Colour TileBg     = UTFT::fromRGB(28, 34, 43);		// #1c222b
	const Colour PressedBg  = UTFT::fromRGB(42, 50, 64);		// #2a3240
	const Colour GlyphIdle  = UTFT::fromRGB(154, 164, 178);		// #9aa4b2
	const Colour TextColour = UTFT::fromRGB(229, 232, 236);		// #e5e8ec
	const Colour BorderCol  = UTFT::fromRGB(59, 67, 79);		// #3b434f
}

void CncDrawCrosshair(int cx, int cy, int r, int t, Colour glyph, Colour background)
{
	// Ring
	lcd.setColor(glyph);
	lcd.fillCircle(cx, cy, r + t / 2);
	lcd.setColor(background);
	lcd.fillCircle(cx, cy, r - (t + 1) / 2);

	// Arms: from 0.45 r to 1.6 r, as in the mock-up
	const int inner = (r * 45) / 100;
	const int outer = (r * 160) / 100;
	const int h = t / 2;
	lcd.setColor(glyph);
	lcd.fillRect(cx - h, cy - outer, cx - h + t - 1, cy - inner);		// up
	lcd.fillRect(cx - h, cy + inner, cx - h + t - 1, cy + outer);		// down
	lcd.fillRect(cx - outer, cy - h, cx - inner, cy - h + t - 1);		// left
	lcd.fillRect(cx + inner, cy - h, cx + outer, cy - h + t - 1);		// right

	// Centre dot
	lcd.fillCircle(cx, cy, (t * 9) / 10);
}

// ---------------------------------------------------------------------------
// CncNavButton
// ---------------------------------------------------------------------------
CncNavButton::CncNavButton(PixelNumber py, PixelNumber px, PixelNumber size, CncNavIcon pi, Colour pAccent, event_t e, int param)
	: SingleButton(py, px, size), icon(pi), height(size), accent(pAccent)
{
	SetEvent(e, param);
}

void CncNavButton::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}

	const bool active = pressed;						// selected tab
	const Colour glyph = active ? accent : GlyphIdle;
	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;
	const int cx = static_cast<int>(left + width / 2);
	const int cy = static_cast<int>(top + height / 2);

	lcd.setColor(TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	if (active)
	{
		lcd.setColor(accent);
		lcd.drawRoundRect(left, top, right, bottom);
		lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);
	}

	lcd.setColor(glyph);
	switch (icon)
	{
	case CncNavIcon::Joystick:				// base, stick and ball (Modern UI glyph)
		lcd.fillRoundRect(cx - 13, cy + 9, cx + 13, cy + 19);
		lcd.fillRect(cx - 2, cy - 9, cx + 2, cy + 10);
		lcd.fillCircle(cx, cy - 13, 8);
		break;

	case CncNavIcon::Crosshair:
		CncDrawCrosshair(cx, cy, 12, 4, glyph, TileBg);
		break;

	case CncNavIcon::List:					// three bullet-and-line rows (Modern UI glyph)
		for (int row = -1; row <= 1; ++row)
		{
			const int ry = cy + row * 9;
			lcd.fillCircle(cx - 11, ry, 3);
			lcd.fillRect(cx - 3, ry - 2, cx + 13, ry + 2);
		}
		break;

	case CncNavIcon::SdCard:
		// 28 x 36 card, top-right corner cut off (9 px), small notch on the left edge
		{
			const int x0 = cx - 14, y0 = cy - 18, w = 28, h = 36, cut = 9;
			for (int r = 0; r < h; ++r)
			{
				int l = x0;
				if (r >= 7 && r <= 16)
				{
					l += (r <= 9) ? (r - 7) : (r >= 13) ? (16 - r) : 3;		// notch, 3 px deep
				}
				int rr = x0 + w - 1;
				if (r < cut)
				{
					rr -= cut - r;												// chamfer
				}
				lcd.fillRect(l, y0 + r, rr, y0 + r);
			}
		}
		break;

	case CncNavIcon::Spanner:
	default:
		// Open-end spanner from the Modern UI rail, anchored on the tile centre (60 px tall)
		{
			const int iconTop = cy - 30;
			const int headRadius = 13;
			const int headY = iconTop + headRadius;
			const int neckTop = headY + 6;
			const int neckBottom = headY + 19;
			const int neckRows = neckBottom - neckTop;

			lcd.fillCircle(cx, headY, headRadius);
			for (int yy = neckTop; yy <= neckBottom; ++yy)
			{
				const int remaining = neckRows - (yy - neckTop);
				const int halfWidth = 6 + (6 * remaining * remaining + (neckRows * neckRows) / 2) / (neckRows * neckRows);
				lcd.fillRect(cx - halfWidth, yy, cx + halfWidth, yy);
			}
			lcd.fillRoundRect(cx - 6, neckBottom, cx + 6, cy + 30);
			lcd.setColor(TileBg);
			lcd.fillRect(cx - 4, iconTop, cx + 4, headY + 1);		// open jaw
		}
		break;
	}

	changed = false;
}

// ---------------------------------------------------------------------------
// CncZeroButton
// ---------------------------------------------------------------------------
CncZeroButton::CncZeroButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, char axisLetter, event_t e, int param)
	: SingleButton(py, px, pw), height(ph)
{
	letter[0] = axisLetter;
	letter[1] = 0;
	SetEvent(e, param);
}

void CncZeroButton::SetLetter(char c)
{
	if (letter[0] != c)
	{
		letter[0] = c;
		changed = true;
	}
}

void CncZeroButton::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}

	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;
	const Colour bg = pressed ? PressedBg : TileBg;

	lcd.setColor(bg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);

	// Crosshair on the left third, letter on the right (mock-up: 27 px / 66 px from the left)
	const int cy = static_cast<int>(top + height / 2);
	const int r = (height < 60) ? 8 : 9;
	const Colour fg = locked ? BorderCol : TextColour;
	CncDrawCrosshair(static_cast<int>(left) + 27, cy, r, 3, fg, bg);

	lcd.setTransparentBackground(true);
	lcd.setColor(fg);
	lcd.setFont(glcd28x32);
	const PixelNumber fontHeight = UTFT::GetFontHeight(glcd28x32);
	lcd.setTextPos(left + 52, top + (height - fontHeight) / 2, right - 4);
	lcd.printf("%s", letter);
	lcd.setTransparentBackground(false);

	changed = false;
}

// ---------------------------------------------------------------------------
// CncSegmentButton
// ---------------------------------------------------------------------------
CncSegmentButton::CncSegmentButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph,
									const char *left, const char *right, Colour pAccent, event_t e, bool pStacked)
	: SingleButton(py, px, pw), height(ph), leftText(left), rightText(right), accent(pAccent), rightActive(false), stacked(pStacked)
{
	SetEvent(e, 0);
}

// Print text centred in [x1, x2] at vertical centre cy (measures it off-screen first, like ModernTextButton)
static void PrintCentred(const char *t, PixelNumber x1, PixelNumber x2, PixelNumber top, PixelNumber h, Colour c)
{
	lcd.setTransparentBackground(true);
	lcd.setFont(glcd19x21);
	lcd.setColor(c);
	lcd.setTextPos(0, 9999, x2 - x1);
	lcd.printf("%s", t);
	const PixelNumber tw = lcd.getTextX();
	const PixelNumber fh = UTFT::GetFontHeight(glcd19x21);
	const PixelNumber tx = (x2 - x1 > tw) ? x1 + (x2 - x1 - tw) / 2 : x1;
	lcd.setTextPos(tx, top + (h - fh) / 2, x2);
	lcd.printf("%s", t);
	lcd.setTransparentBackground(false);
}

void CncSegmentButton::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}

	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;
	const PixelNumber mid = left + width / 2;
	const PixelNumber midY = top + height / 2;				// stacked: the line between the halves

	lcd.setColor(pressed ? PressedBg : TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);

	// Active half: accent fill (border colour when locked), inset 4 px
	lcd.setColor(locked ? BorderCol : accent);
	if (stacked)
	{
		if (rightActive)
		{
			lcd.fillRoundRect(left + 4, midY, right - 4, bottom - 4);		// bottom half
		}
		else
		{
			lcd.fillRoundRect(left + 4, top + 4, right - 4, midY);			// top half
		}
	}
	else if (rightActive)
	{
		lcd.fillRoundRect(mid, top + 4, right - 4, bottom - 4);
	}
	else
	{
		lcd.fillRoundRect(left + 4, top + 4, mid, bottom - 4);
	}

	const Colour dark = UTFT::fromRGB(18, 22, 28);
	const Colour idleText = locked ? BorderCol : TextColour;
	const Colour activeText = locked ? GlyphIdle : dark;
	if (stacked)
	{
		PrintCentred(leftText, left + 4, right - 4, top, height / 2, rightActive ? idleText : activeText);
		PrintCentred(rightText, left + 4, right - 4, midY, height / 2, rightActive ? activeText : idleText);
	}
	else
	{
		PrintCentred(leftText, left + 4, mid, top, height, rightActive ? idleText : activeText);
		PrintCentred(rightText, mid, right - 4, top, height, rightActive ? activeText : idleText);
	}

	changed = false;
}

// ---------------------------------------------------------------------------
// CncChoiceButton
// ---------------------------------------------------------------------------
CncChoiceButton::CncChoiceButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, Colour pAccent, event_t e, int param)
	: SingleButton(py, px, pw), height(ph), text(""), accent(pAccent)
{
	SetEvent(e, param);
}

void CncChoiceButton::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}

	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;
	const bool selected = pressed && !disabled;
	const Colour dark = UTFT::fromRGB(18, 22, 28);

	lcd.setColor(selected ? accent : TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	if (!disabled)
	{
		lcd.setColor(selected ? accent : BorderCol);
		lcd.drawRoundRect(left, top, right, bottom);
		lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);
	}

	PrintCentred(text, left + 4, right - 4, top, height, disabled ? BorderCol : selected ? dark : TextColour);

	if (dot && !disabled)
	{
		lcd.setColor(selected ? dark : accent);
		lcd.fillCircle(static_cast<int>(right) - 9, static_cast<int>(top) + 9, 3);
	}

	changed = false;
}

// ---------------------------------------------------------------------------
// CncArrowButton
// ---------------------------------------------------------------------------
CncArrowButton::CncArrowButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, bool pUp, event_t e, int param)
	: SingleButton(py, px, pw), height(ph), up(pUp)
{
	SetEvent(e, param);
}

void CncArrowButton::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}

	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;

	lcd.setColor((pressed && !disabled) ? PressedBg : TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);

	// Filled triangle, 28 wide and 16 tall, centred
	const int cx = static_cast<int>(left + width / 2);
	const int cy = static_cast<int>(top + height / 2);
	const int halfW = 14, h = 16;
	lcd.setColor(disabled ? BorderCol : TextColour);
	for (int r = 0; r < h; ++r)
	{
		const int hw = (halfW * (r + 1)) / h;					// row width grows from the tip
		const int yy = up ? (cy - h / 2 + r) : (cy + h / 2 - 1 - r);
		lcd.fillRect(cx - hw, yy, cx + hw, yy);
	}

	changed = false;
}

// ---------------------------------------------------------------------------
// Origin glyphs and probe widgets
// ---------------------------------------------------------------------------
namespace
{
	void ThickLine(int x1, int y1, int x2, int y2, int t)			// horizontal or vertical only
	{
		const int h = t / 2;
		if (y1 == y2)
		{
			lcd.fillRect(min(x1, x2), y1 - h, max(x1, x2), y1 - h + t - 1);
		}
		else
		{
			lcd.fillRect(x1 - h, min(y1, y2), x1 - h + t - 1, max(y1, y2));
		}
	}

	void Ring(int cx, int cy, int r, int t, Colour col, Colour bg)
	{
		lcd.setColor(col);
		lcd.fillCircle(cx, cy, r);
		lcd.setColor(bg);
		lcd.fillCircle(cx, cy, r - t);
	}
}

void CncDrawOriginGlyph(CncOrigin kind, int cx, int cy, Colour col, Colour bg)
{
	constexpr int off = 9, ln = 13, sw = 4, dot = 4;
	lcd.setColor(col);
	switch (kind)
	{
	case CncOrigin::Bore:							// thin ring with a centre dot
		Ring(cx, cy, 12, 2, col, bg);
		lcd.setColor(col);
		lcd.fillCircle(cx, cy, dot);
		return;

	case CncOrigin::Boss:							// solid disc with a small hole
		lcd.fillCircle(cx, cy, 13);
		lcd.setColor(bg);
		lcd.fillCircle(cx, cy, 4);
		return;

	case CncOrigin::ZTop:							// arrow down onto a block
		lcd.drawRect(cx - 14, cy + 3, cx + 14, cy + 12);
		lcd.drawRect(cx - 13, cy + 4, cx + 13, cy + 11);
		lcd.fillRect(cx - 1, cy - 14, cx, cy);
		for (int i = 0; i < 6; ++i)
		{
			lcd.fillRect(cx - 6 + i, cy - 5 + i, cx + 5 - i, cy - 5 + i);	// arrow head
		}
		return;

	case CncOrigin::MC:								// square with cross-hair, centre dot
		lcd.drawRect(cx - 9, cy - 7, cx + 9, cy + 7);
		lcd.drawRect(cx - 8, cy - 6, cx + 8, cy + 6);
		lcd.fillRect(cx - 1, cy - 15, cx, cy + 15);
		lcd.fillRect(cx - 17, cy - 1, cx + 17, cy);
		lcd.fillCircle(cx, cy, dot);
		return;

	case CncOrigin::TC:
	case CncOrigin::BC:								// top / bottom edge: bar, dot in its middle
		{
			const int yy = (kind == CncOrigin::TC) ? cy - off : cy + off;
			ThickLine(cx - ln, yy, cx + ln, yy, sw);
			lcd.fillCircle(cx, yy, dot);
		}
		return;

	case CncOrigin::ML:
	case CncOrigin::MR:								// left / right edge
		{
			const int xx = (kind == CncOrigin::ML) ? cx - off : cx + off;
			ThickLine(xx, cy - ln, xx, cy + ln, sw);
			lcd.fillCircle(xx, cy, dot);
		}
		return;

	case CncOrigin::TL:
	case CncOrigin::TR:
	case CncOrigin::BL:
	case CncOrigin::BR:								// corner: L shape, dot on the vertex
		{
			const bool left = (kind == CncOrigin::TL || kind == CncOrigin::BL);
			const bool topSide = (kind == CncOrigin::TL || kind == CncOrigin::TR);
			const int xx = left ? cx - off : cx + off;
			const int yy = topSide ? cy - off : cy + off;
			const int ex = xx + (left ? (ln + 4) : -(ln + 4));
			const int ey = yy + (topSide ? (ln + 4) : -(ln + 4));
			ThickLine(xx, ey, xx, yy, sw);
			ThickLine(xx, yy, ex, yy, sw);
			lcd.fillCircle(xx, yy, dot + 1);
		}
		return;

	default:
		return;
	}
}

// ---- CncOriginButton ----
CncOriginButton::CncOriginButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, CncOrigin k, const char *plabel,
									bool pOutlineStyle, Colour pAccent, event_t e, int param)
	: SingleButton(py, px, pw), height(ph), kind(k), label(plabel), accent(pAccent), outlineStyle(pOutlineStyle)
{
	SetEvent(e, param);
}

void CncOriginButton::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}
	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;
	const bool filled = pressed && !outlineStyle;
	const Colour bg = filled ? accent : TileBg;
	const Colour dark = UTFT::fromRGB(18, 22, 28);

	lcd.setColor(bg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor((pressed) ? accent : BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);
	if (pressed && outlineStyle)
	{
		lcd.drawRoundRect(left + 2, top + 2, right - 2, bottom - 2);	// 3 px accent outline
	}

	const Colour glyph = filled ? dark : TextColour;
	const int cx = static_cast<int>(left + width / 2);
	if (label != nullptr)
	{
		CncDrawOriginGlyph(kind, cx, static_cast<int>(top) + 28, glyph, bg);
		PrintCentred(label, left + 2, right - 2, bottom - 26, 22, glyph);
	}
	else
	{
		CncDrawOriginGlyph(kind, cx, static_cast<int>(top + height / 2), glyph, bg);
	}
	changed = false;
}

// ---- CncGlyphField ----
CncGlyphField::CncGlyphField(PixelNumber py, PixelNumber px, PixelNumber size, CncOrigin k, Colour pGlyph, Colour pBackground)
	: DisplayField(py, px, size), height(size), kind(k), glyph(pGlyph), background(pBackground)
{
}

void CncGlyphField::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}
	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	lcd.setColor(background);
	lcd.fillRect(left, top, left + width - 1, top + height - 1);
	CncDrawOriginGlyph(kind, static_cast<int>(left + width / 2), static_cast<int>(top + height / 2), glyph, background);
	changed = false;
}

// ---- CncValueTile ----
CncValueTile::CncValueTile(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, const char *plabel,
							bool pStacked, bool pDropdown, event_t e, int param)
	: SingleButton(py, px, pw), height(ph), label(plabel), value(""), stacked(pStacked), dropdown(pDropdown)
{
	SetEvent(e, param);
}

static void PrintAligned(const char *t, PixelNumber x1, PixelNumber x2, PixelNumber textTop, bool alignRight, Colour c)
{
	lcd.setTransparentBackground(true);
	lcd.setFont(glcd19x21);
	lcd.setColor(c);
	PixelNumber tx = x1;
	if (alignRight)
	{
		lcd.setTextPos(0, 9999, x2 - x1);
		lcd.printf("%s", t);
		const PixelNumber tw = lcd.getTextX();
		tx = (x2 - x1 > tw) ? x2 - tw : x1;
	}
	lcd.setTextPos(tx, textTop, x2);
	lcd.printf("%s", t);
	lcd.setTransparentBackground(false);
}

void CncValueTile::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}
	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;

	lcd.setColor(pressed ? PressedBg : TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);

	const PixelNumber fh = UTFT::GetFontHeight(glcd19x21);
	const PixelNumber valueRight = dropdown ? right - 24 : right - 10;
	if (stacked)
	{
		PrintAligned(label, left + 10, right - 6, top + 8, false, GlyphIdle);
		PrintAligned(value, left + 10, valueRight, bottom - 8 - fh, true, TextColour);
	}
	else
	{
		const PixelNumber ty = top + (height - fh) / 2;
		PrintAligned(label, left + 10, left + width / 2, ty, false, GlyphIdle);
		PrintAligned(value, left + width / 2, valueRight, ty, true, TextColour);
	}
	if (dropdown)
	{
		// small down triangle, 10 x 6
		const int tx = static_cast<int>(right) - 17;
		const int tyMid = stacked ? static_cast<int>(bottom) - 8 - static_cast<int>(fh) / 2 : static_cast<int>(top + height / 2);
		lcd.setColor(GlyphIdle);
		for (int r = 0; r < 6; ++r)
		{
			lcd.fillRect(tx - 5 + r, tyMid - 3 + r, tx + 5 - r, tyMid - 3 + r);
		}
	}
	changed = false;
}

// ---- CncCheckField ----
CncCheckField::CncCheckField(PixelNumber py, PixelNumber px, PixelNumber size, Colour pColour, Colour pBackground)
	: DisplayField(py, px, size), height(size), colour(pColour), background(pBackground)
{
}

void CncCheckField::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}
	const int left = static_cast<int>(x + xOffset);
	const int top = static_cast<int>(y + yOffset);
	const int s = static_cast<int>(height);
	lcd.setColor(background);
	lcd.fillRect(left, top, left + s - 1, top + s - 1);
	// short stroke down-right, long stroke up-right, 3 px thick
	lcd.setColor(colour);
	const int x0 = left + s / 8, y0 = top + s / 2;
	const int x1 = left + (3 * s) / 8, y1 = top + (3 * s) / 4;
	const int x2 = left + (7 * s) / 8, y2 = top + s / 6;
	for (int t = 0; t < 3; ++t)
	{
		lcd.drawLine(x0, y0 + t, x1, y1 + t);
		lcd.drawLine(x1, y1 + t, x2, y2 + t);
	}
	changed = false;
}

// ---- CncStatusTile ----
CncStatusTile::CncStatusTile(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, const char *pCaption)
	: DisplayField(py, px, pw), height(ph), caption(pCaption), value(""), outline(BorderCol)
{
}

void CncStatusTile::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}
	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;

	lcd.setColor(TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(outline);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);
	lcd.drawRoundRect(left + 2, top + 2, right - 2, bottom - 2);

	PrintCentred(caption, left + 4, right - 4, top + 4, 22, GlyphIdle);
	PrintCentred(value, left + 4, right - 4, top + height / 2 - 2, height / 2, TextColour);
	changed = false;
}

// ---------------------------------------------------------------------------
// CncTextTile
// ---------------------------------------------------------------------------
CncTextTile::CncTextTile(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, LcdFont pf, TextAlignment pa)
	: DisplayField(py, px, pw), height(ph), line1(""), line2(nullptr), outline(BorderCol), textColour(TextColour),
	  font(pf), align(pa), thickness(2), textRight(0)
{
}

void CncTextTile::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}
	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;

	lcd.setColor(TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(outline);
	for (PixelNumber i = 0; i < thickness; ++i)
	{
		lcd.drawRoundRect(left + i, top + i, right - i, bottom - i);
	}

	const PixelNumber inset = (align == TextAlignment::Left) ? 14 : 5;	// "ABORTED" needs 99 of the 110 px result tile
	const PixelNumber x1 = left + inset;
	const PixelNumber x2 = right - inset - textRight;
	const PixelNumber fh = UTFT::GetFontHeight(font);
	const bool two = (line2 != nullptr && line2[0] != 0);
	const PixelNumber lineGap = 4;
	const PixelNumber block = two ? 2 * fh + lineGap : fh;
	PixelNumber ty = top + (height - block) / 2;
	lcd.setTransparentBackground(true);
	lcd.setFont(font);
	lcd.setColor(textColour);
	for (int n = 0; n < (two ? 2 : 1); ++n)
	{
		const char * const t = (n == 0) ? line1 : line2;
		if (t != nullptr)
		{
			PixelNumber tx = x1;
			if (align != TextAlignment::Left)
			{
				lcd.setTextPos(0, 9999, x2 - x1);
				lcd.printf("%s", t);
				const PixelNumber tw = lcd.getTextX();
				if (x2 - x1 > tw)
				{
					tx = (align == TextAlignment::Centre) ? x1 + (x2 - x1 - tw) / 2 : x2 - tw;
				}
			}
			lcd.setTextPos(tx, ty, x2);
			lcd.printf("%s", t);
		}
		ty += fh + lineGap;
	}
	lcd.setTransparentBackground(false);
	changed = false;
}

// End

// ---------------------------------------------------------------------------
// CncFileRow (JOB LIST)
// ---------------------------------------------------------------------------
CncFileRow::CncFileRow(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, Colour pAccent, event_t e, int param)
	: SingleButton(py, px, pw), height(ph), text(nullptr), tag(nullptr), accent(pAccent), matchStart(0), matchLen(0), textLen(0), selected(false), grid(false)
{
	SetEvent(e, param);
}

void CncFileRow::SetEntry(const char *t, const char *ptag, size_t mStart, size_t mLen, size_t len)
{
	const uint8_t ms = (uint8_t)((mStart > 255) ? 255 : mStart);
	const uint8_t ml = (uint8_t)((mLen > 255) ? 255 : mLen);
	const uint8_t tl = (uint8_t)((len > 255) ? 255 : len);
	if (tl != textLen)
	{
		textLen = tl;
		changed = true;
	}
	// Same pointer = same text, except after a new listing (same buffer): the JOB LIST then
	// marks every row changed itself
	if (t != text || ptag != tag || ms != matchStart || ml != matchLen)
	{
		text = t;
		tag = ptag;
		matchStart = ms;
		matchLen = ml;
		changed = true;
	}
}

void CncFileRow::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}

	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;

	lcd.setColor(TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(selected ? accent : BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);

	lcd.setFont(glcd19x21);
	const PixelNumber fh = UTFT::GetFontHeight(glcd19x21);
	const PixelNumber ty = top + (height - fh) / 2;
	lcd.setTransparentBackground(true);

	// Tag on the right (grid: bottom-right corner)
	PixelNumber textRight = right - 12;
	if (tag != nullptr && tag[0] != 0)
	{
		const PixelNumber tw = DisplayField::GetTextWidth(tag, 9999, 99);
		lcd.setColor(GlyphIdle);
		if (grid)
		{
			lcd.setTextPos(right - 10 - tw, bottom - 6 - fh, right - 8);	// bottom-right corner
		}
		else
		{
			lcd.setTextPos(right - 12 - tw, ty, right - 10);
		}
		lcd.printf("%s", tag);
		if (!grid)
		{
			textRight = right - 12 - tw - 14;
		}
	}

	if (text != nullptr)
	{
		PixelNumber x1 = left + 14;
		const PixelNumber avail = (textRight > x1) ? textRight - x1 : 0;
		const size_t len = (textLen != 0) ? min<size_t>(textLen, strlen(text)) : strlen(text);
		size_t shown = len;
		bool dots = false;
		if (DisplayField::GetTextWidth(text, 9999, len) > avail)
		{
			// Too long: as many characters as fit, then ".." (binary search: few measurements)
			const PixelNumber dotsW = DisplayField::GetTextWidth("..", 9999, 2);
			size_t lo = 0, hi = len;						// lo fits, hi does not
			while (hi - lo > 1)
			{
				const size_t mid = (lo + hi) / 2;
				if (DisplayField::GetTextWidth(text, 9999, mid) + dotsW <= avail)
				{
					lo = mid;
				}
				else
				{
					hi = mid;
				}
			}
			shown = lo;
			dots = true;
		}

		// Up to three parts: before the match, the match (accent), after it
		const size_t ms = (matchLen != 0 && matchStart < shown) ? matchStart : shown;
		const size_t me = (matchLen != 0) ? min<size_t>(matchStart + matchLen, shown) : shown;
		PixelNumber nameY = ty;
		if (grid)
		{
			const PixelNumber w = DisplayField::GetTextWidth(text, 9999, shown) + (dots ? DisplayField::GetTextWidth("..", 9999, 2) : 0);
			x1 = left + (width - w) / 2;
			if (tag != nullptr && tag[0] != 0)
			{
				nameY = ty - 6;								// a little above the centre, clear of the tag
			}
		}
		lcd.setTextPos(x1, nameY, textRight);
		lcd.setColor(TextColour);
		lcd.printf("%.*s", (int)ms, text);
		if (me > ms)
		{
			lcd.setColor(accent);
			lcd.printf("%.*s", (int)(me - ms), text + ms);
			lcd.setColor(TextColour);
		}
		if (shown > me)
		{
			lcd.printf("%.*s", (int)(shown - me), text + me);
		}
		if (dots)
		{
			lcd.printf("..");
		}
	}
	lcd.setTransparentBackground(false);
	changed = false;
}

// ---------------------------------------------------------------------------
// CncGlyphButton (SEARCH tile, keyboard SHIFT)
// ---------------------------------------------------------------------------
CncGlyphButton::CncGlyphButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, CncGlyph g, Colour pAccent, event_t e, int param)
	: SingleButton(py, px, pw), height(ph), accent(pAccent), glyph(g), character(0), active(false)
{
	SetEvent(e, param);
}

void CncGlyphButton::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}

	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;
	const int cx = static_cast<int>(left + width / 2);
	const int cy = static_cast<int>(top + height / 2);
	const Colour bg = (pressed && glyph == CncGlyph::Char) ? accent : TileBg;
	const Colour col = (glyph == CncGlyph::Char) ? (pressed ? UTFT::fromRGB(18, 22, 28) : TextColour)
						: active ? accent : TextColour;

	lcd.setColor(bg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor((active || (pressed && glyph == CncGlyph::Char)) ? accent : BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);

	lcd.setColor(col);
	switch (glyph)
	{
	case CncGlyph::Search:
		{
			// Ring (radius 11, 4 px) up-left of centre, handle to the bottom right
			const int rx = cx - 4, ry = cy - 4, r = 11;
			lcd.fillCircle(rx, ry, r + 2);
			lcd.setColor(bg);
			lcd.fillCircle(rx, ry, r - 2);
			lcd.setColor(col);
			const int hx = rx + (r * 72) / 100, hy = ry + (r * 72) / 100;
			for (int o = -2; o <= 2; ++o)
			{
				lcd.drawLine(hx + o, hy, cx + 13 + o, cy + 13);
				lcd.drawLine(hx, hy + o, cx + 13, cy + 13 + o);
			}
		}
		break;

	case CncGlyph::Shift:
		{
			// Arrow up: head 22 wide, stem 10 wide; filled when active (lower case), outline otherwise
			const int headTop = cy - 11, headBase = cy + 1, stemBottom = cy + 11;
			if (active)
			{
				for (int yy = headTop; yy <= headBase; ++yy)
				{
					const int hw = ((yy - headTop) * 11) / (headBase - headTop);
					lcd.drawLine(cx - hw, yy, cx + hw, yy);
				}
				lcd.fillRect(cx - 5, headBase, cx + 5, stemBottom);
			}
			else
			{
				for (int o = 0; o <= 1; ++o)
				{
					lcd.drawLine(cx, headTop + o, cx - 11 + o, headBase);
					lcd.drawLine(cx, headTop + o, cx + 11 - o, headBase);
					lcd.drawLine(cx - 11, headBase - o, cx - 5, headBase - o);
					lcd.drawLine(cx + 5, headBase - o, cx + 11, headBase - o);
					lcd.drawLine(cx - 5 + o, headBase, cx - 5 + o, stemBottom);
					lcd.drawLine(cx + 5 - o, headBase, cx + 5 - o, stemBottom);
					lcd.drawLine(cx - 5, stemBottom - o, cx + 5, stemBottom - o);
				}
			}
		}
		break;

	case CncGlyph::Char:
		{
			const char s[2] = { character, 0 };
			PrintCentred(s, left + 2, right - 2, top, height, col);
		}
		break;

	case CncGlyph::Grid:									// four rounded squares (mock-up)
		for (int r = 0; r < 2; ++r)
		{
			for (int c = 0; c < 2; ++c)
			{
				const int x0 = cx - 13 + c * 15, y0 = cy - 13 + r * 15;
				lcd.fillRoundRect(x0, y0, x0 + 10, y0 + 10);
			}
		}
		break;

	case CncGlyph::List:									// three bullet-and-line rows (nav JOB glyph)
		for (int row = -1; row <= 1; ++row)
		{
			const int ry = cy + row * 9;
			lcd.fillCircle(cx - 10, ry, 3);
			lcd.fillRect(cx - 2, ry - 2, cx + 14, ry + 2);
		}
		break;
	}
	changed = false;
}

// ---------------------------------------------------------------------------
// CncKeyRow (keyboard)
// ---------------------------------------------------------------------------
CncKeyRow::CncKeyRow(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ps, PixelNumber ph, const char *s, Colour pAccent, event_t e)
	: ButtonRow(py, px, pw, ps, strlen(s), e), height(ph), text(s), accent(pAccent)
{
}

void CncKeyRow::CheckEvent(PixelNumber px, PixelNumber py, int& bestError, ButtonPress& best)
{
	constexpr int MaxError = 8;								// as Display.cpp
	if (!IsVisible() || GetEvent() == nullEvent)
	{
		return;
	}
	const int yError = (py < GetMinY()) ? GetMinY() - py : (py > GetMaxY()) ? py - GetMaxY() : 0;
	if (yError >= MaxError || yError >= bestError)
	{
		return;
	}
	for (unsigned int i = 0; i < numButtons; ++i)
	{
		const int minX = x + i * step, maxX = minX + width - 1;
		const int xError = ((int)px < minX) ? minX - (int)px : ((int)px > maxX) ? (int)px - maxX : 0;
		if (xError < MaxError && xError + yError < bestError)
		{
			bestError = xError + yError;
			best.Set(this, i);
		}
	}
}

void CncKeyRow::Press(bool p, int index)
{
	const int w = p ? index : -1;
	if (w != whichPressed)
	{
		whichPressed = w;
		changed = true;
	}
}

void CncKeyRow::SetText(const char *s)
{
	if (s != text)
	{
		text = s;
		changed = true;
	}
}

void CncKeyRow::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	if (!full && !changed)
	{
		return;
	}
	for (unsigned int i = 0; i < numButtons; ++i)
	{
		const PixelNumber left = x + xOffset + i * step;
		const PixelNumber top = y + yOffset;
		const PixelNumber right = left + width - 1;
		const PixelNumber bottom = top + height - 1;
		const bool p = ((int)i == whichPressed);
		lcd.setColor(p ? accent : TileBg);
		lcd.fillRoundRect(left, top, right, bottom);
		lcd.setColor(p ? accent : BorderCol);
		lcd.drawRoundRect(left, top, right, bottom);
		lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);
		const char s[2] = { text[i], 0 };
		PrintCentred(s, left + 2, right - 2, top, height, p ? UTFT::fromRGB(18, 22, 28) : TextColour);
	}
	changed = false;
}

// ---------------------------------------------------------------------------
// CncAlertRow (SYSTEM > ALERT)
// ---------------------------------------------------------------------------
CncAlertRow::CncAlertRow(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, Colour pAccent, event_t e, int param)
	: SingleButton(py, px, pw), height(ph), text(nullptr), accent(pAccent), error(false), unread(false)
{
	SetEvent(e, param);
}

void CncAlertRow::Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset)
{
	using namespace CncLayout::Alerts;
	if (!full && !changed)
	{
		return;
	}
	const PixelNumber left = x + xOffset;
	const PixelNumber top = y + yOffset;
	const PixelNumber right = left + width - 1;
	const PixelNumber bottom = top + height - 1;

	lcd.setColor(TileBg);
	lcd.fillRoundRect(left, top, right, bottom);
	lcd.setColor(BorderCol);
	lcd.drawRoundRect(left, top, right, bottom);
	lcd.drawRoundRect(left + 1, top + 1, right - 1, bottom - 1);

	// Badge
	const PixelNumber by = top + (height - BadgeH) / 2;
	lcd.setColor(error ? UTFT::fromRGB(201, 50, 24) : UTFT::fromRGB(224, 168, 0));
	lcd.fillRoundRect(left + BadgeDx, by, left + BadgeDx + BadgeW - 1, by + BadgeH - 1);
	PrintCentred(error ? "ERR" : "WARN", left + BadgeDx, left + BadgeDx + BadgeW - 1, by, BadgeH,
					error ? UTFT::fromRGB(245, 245, 245) : UTFT::fromRGB(36, 36, 36));	// white reads better on red

	// Text, cut to fit before the dot
	if (text != nullptr)
	{
		lcd.setFont(glcd19x21);
		const PixelNumber x1 = left + TextDx, x2 = left + DotDx - 10;
		const PixelNumber avail = x2 - x1;
		const size_t len = strlen(text);
		size_t shown = len;
		bool dots = false;
		if (DisplayField::GetTextWidth(text, 9999, len) > avail)
		{
			const PixelNumber dotsW = DisplayField::GetTextWidth("..", 9999, 2);
			size_t lo = 0, hi = len;
			while (hi - lo > 1)
			{
				const size_t mid = (lo + hi) / 2;
				if (DisplayField::GetTextWidth(text, 9999, mid) + dotsW <= avail)
				{
					lo = mid;
				}
				else
				{
					hi = mid;
				}
			}
			shown = lo;
			while (shown > 0 && (text[shown] & 0xC0) == 0x80)
			{
				--shown;								// whole UTF-8 characters only
			}
			dots = true;
		}
		lcd.setTransparentBackground(true);
		lcd.setColor(TextColour);
		lcd.setTextPos(x1, top + (height - UTFT::GetFontHeight(glcd19x21)) / 2, x2);
		lcd.printf("%.*s%s", (int)shown, text, dots ? ".." : "");
		lcd.setTransparentBackground(false);
	}

	if (unread)
	{
		lcd.setColor(accent);
		lcd.fillCircle(left + DotDx, top + height / 2, DotR);
	}
	changed = false;
}
