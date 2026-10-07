/*
 * CncWidgets.hpp
 *
 * Widgets used only by the portrait CNC UI. All glyphs are drawn with UTFT primitives
 * relative to the tile centre, so they work at any tile size.
 */

#ifndef SRC_UI_CNC_CNCWIDGETS_HPP_
#define SRC_UI_CNC_CNCWIDGETS_HPP_

#include <UI/Display.hpp>

// Bottom navigation tile: icon only. Selected = accent outline (2 px) + accent glyph,
// the same look as the Modern UI rail tiles (ModernMasterNavButton).
enum class CncNavIcon : uint8_t
{
	Joystick,		// CONTROL
	Crosshair,		// WCS
	List,			// JOB
	SdCard,			// MACROS
	Spanner			// SYSTEM
};

class CncNavButton : public SingleButton
{
	CncNavIcon icon;
	PixelNumber height;
	Colour accent;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncNavButton(PixelNumber py, PixelNumber px, PixelNumber size, CncNavIcon pi, Colour pAccent, event_t e, int param);
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// DRO zero button: bordered tile with a small crosshair and the axis letter.
// Jog icon pairs for the - / + move buttons: each pair has an icon for the - side and one for the + side
enum class JogIcon : uint8_t { None, LeftRight, DownUp, Diag, Rotary };

// Draws one icon of a pair (plusIcon = the + side: right / up / 45 degrees / CW), vector drawn, centred on
// cx, cy and about 'size' px high. LeftRight: arrows, DownUp: arrows, Diag: 225 / 45 degree arrows,
// Rotary: arrow on an arc, CCW for the - side and the mirrored CW for the + side.
void DrawJogIcon(JogIcon pair, bool plusIcon, int cx, int cy, int size, Colour c);

// A text button that also carries a jog icon at its outer edge (a - button on the left, a + button on the
// right), or in pair mode (settings tile) both icons of the pair at the left. The text stays as it is.
class CncJogButton : public ModernTextButton
{
	JogIcon pair = JogIcon::None;
	bool plusIcon = false;				// which icon of the pair this button shows (pair mode: the first one)
	bool leftEdge = true;
	bool pairMode = false;

	// With an icon at the outer edge the text is centred in the space beside it (the icon occupies 14 px of
	// margin plus its own height - 22 px), not in the whole button: it is moved away from the icon by half of that.
	void UpdateTextShift()
	{
		const int occupied = 14 + (int)GetHeight() - 22;
		SetTextShift((pair == JogIcon::None || pairMode) ? 0 : (leftEdge ? occupied / 2 : -(occupied / 2)));
	}

public:
	CncJogButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph,
					const char * _ecv_array null pt, event_t e, int param = 0,
					LcdFont pf = nullptr, bool borderVisible = false, TextAlignment pa = TextAlignment::Centre)
		: ModernTextButton(py, px, pw, ph, pt, e, param, pf, borderVisible, pa) { }

	void SetIcons(JogIcon p, bool plus) { if (p != pair || plus != plusIcon) { pair = p; plusIcon = plus; changed = true; } UpdateTextShift(); }
	void SetOuterEdge(bool left) { leftEdge = left; UpdateTextShift(); }
	void SetPairMode(bool on) { pairMode = on; UpdateTextShift(); }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

class CncZeroButton : public SingleButton
{
	PixelNumber height;
	char letter[2];
	bool locked = false;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncZeroButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, char axisLetter, event_t e, int param);
	void SetLetter(char c);
	void SetLocked(bool l) { if (l != locked) { locked = l; changed = true; } }	// muted look while a job runs
	void SetHeight(PixelNumber h) { if (h != height) { height = h; changed = true; } }	// 3 or 4 axis DRO rows
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Two-option toggle in one tile, e.g. RAPID | SLOW. The active half is filled with the
// accent colour (dark text); tapping the tile switches to the other option.
class CncSegmentButton : public SingleButton
{
	PixelNumber height;
	const char *leftText;
	const char *rightText;
	Colour accent;
	bool rightActive;
	bool locked = false;
	bool stacked;											// two halves one above the other: first text on top, second below

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	// rightActive = the second text (right half, or the bottom half when stacked) is the active one
	CncSegmentButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph,
					const char *left, const char *right, Colour pAccent, event_t e, bool pStacked = false);
	bool IsRightActive() const { return rightActive; }
	void SetRightActive(bool r) { if (r != rightActive) { rightActive = r; changed = true; } }
	void SetLocked(bool l) { if (l != locked) { locked = l; changed = true; } }	// muted look, caller refuses touches
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Choice button for popup forms (COPY TO target / axes, ...). Selected = accent fill with dark
// text (the pressed state, so Cnc::Select() works on it), disabled = muted text without outline,
// optional dot in the top-right corner (e.g. "this WCS already has offsets").
class CncChoiceButton : public SingleButton
{
	PixelNumber height;
	const char *text;
	Colour accent;
	bool disabled = false;
	bool dot = false;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncChoiceButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, Colour pAccent, event_t e, int param);
	void SetText(const char *t) { text = t; changed = true; }
	void SetDisabled(bool d) { if (d != disabled) { disabled = d; changed = true; } }
	void SetDot(bool d) { if (d != dot) { dot = d; changed = true; } }
	void SetSelected(bool s) { Press(s, 0); }
	bool IsSelected() const { return pressed; }
	bool IsDisabled() const { return disabled; }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Scroll arrow tile (up or down triangle). Disabled = muted triangle, caller refuses touches.
class CncArrowButton : public SingleButton
{
	PixelNumber height;
	bool up;
	bool disabled = false;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncArrowButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, bool pUp, event_t e, int param);
	void SetDisabled(bool d) { if (d != disabled) { disabled = d; changed = true; } }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Probe origins (WCS > PROBE). Grid order: TL TC TR / ML MC MR / BL BC BR, then BORE BOSS ZTOP.
enum class CncOrigin : uint8_t { TL, TC, TR, ML, MC, MR, BL, BC, BR, Bore, Boss, ZTop, None };

// Simplified line icon of an origin (corner = L, edge = bar, centre = crosshair; the dot marks
// the point that becomes the origin). 'bg' is the tile colour behind it.
void CncDrawOriginGlyph(CncOrigin kind, int cx, int cy, Colour col, Colour bg);

// Origin picker button. Corners, edges, centre: selected = accent fill, dark glyph.
// BORE, BOSS, Z TOP (outline style): selected = accent outline, glyph keeps its colour.
class CncOriginButton : public SingleButton
{
	PixelNumber height;
	CncOrigin kind;
	const char *label;								// small text under the glyph, or nullptr
	Colour accent;
	bool outlineStyle;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncOriginButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, CncOrigin k, const char *plabel,
					bool pOutlineStyle, Colour pAccent, event_t e, int param);
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Just an origin glyph on a background colour (popup title, instruction line)
class CncGlyphField : public DisplayField
{
	PixelNumber height;
	CncOrigin kind;
	Colour glyph, background;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncGlyphField(PixelNumber py, PixelNumber px, PixelNumber size, CncOrigin k, Colour pGlyph, Colour pBackground);
	void SetKind(CncOrigin k) { if (k != kind) { kind = k; changed = true; } }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Parameter tile that is also a button: small muted label and a value.
// Stacked: label top-left, value bottom-right. Row: label left, value right, one line.
// 'dropdown' adds a small triangle after the value (the tile opens a choice).
class CncValueTile : public SingleButton
{
	PixelNumber height;
	const char *label;
	const char *value;
	bool stacked;
	bool dropdown;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncValueTile(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, const char *plabel,
					bool pStacked, bool pDropdown, event_t e, int param);
	void SetValue(const char *v) { value = v; changed = true; }		// v must stay valid
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Status tile: two centred lines (small muted caption, value) and a 3 px coloured outline
class CncStatusTile : public DisplayField
{
	PixelNumber height;
	const char *caption;
	const char *value;
	Colour outline;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncStatusTile(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, const char *pCaption);
	void SetState(const char *v, Colour pOutline) { if (v != value || pOutline != outline) { value = v; outline = pOutline; changed = true; } }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Green check mark (a measured value is in)
class CncCheckField : public DisplayField
{
	PixelNumber height;
	Colour colour, background;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncCheckField(PixelNumber py, PixelNumber px, PixelNumber size, Colour pColour, Colour pBackground);
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Text tile: tile background, outline of 0..3 px in any colour, one or two lines of text.
// Used for the JOB STATUS file name, progress / result and tool change tiles.
// Text pointers must stay valid; 'textRight' reserves room on the right (a button on top).
class CncTextTile : public DisplayField
{
	PixelNumber height;
	const char *line1;
	const char *line2;
	Colour outline, textColour;
	LcdFont font;
	TextAlignment align;
	uint8_t thickness;
	PixelNumber textRight;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncTextTile(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, LcdFont pf, TextAlignment pa);
	void SetText(const char *l1, const char *l2 = nullptr) { line1 = l1; line2 = l2; changed = true; }
	void SetOutline(Colour c, uint8_t t) { if (c != outline || t != thickness) { outline = c; thickness = t; changed = true; } }
	void SetTextColour(Colour c) { if (c != textColour) { textColour = c; changed = true; } }
	void SetTextRight(PixelNumber r) { textRight = r; changed = true; }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// JOB LIST / MACROS row: file or folder name on the left, optional small muted tag on the right
// (FOLDER, UP). Selected = 2 px accent outline. The part of the name that matched the search
// is drawn in the accent colour. Names that do not fit end in "..". Text pointers must stay valid.
// Grid style (MACROS grid tiles): name centred, tag in the bottom-right corner.
// 'len' limits the name (e.g. without ".g"); 0 = the whole string.
class CncFileRow : public SingleButton
{
	PixelNumber height;
	const char *text;
	const char *tag;
	Colour accent;
	uint8_t matchStart, matchLen;
	uint8_t textLen;
	bool selected;
	bool grid;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncFileRow(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, Colour pAccent, event_t e, int param);
	void SetEntry(const char *t, const char *ptag, size_t mStart = 0, size_t mLen = 0, size_t len = 0);
	void SetGridStyle(bool g) { if (g != grid) { grid = g; changed = true; } }
	void SetGeometry(PixelNumber px, PixelNumber py, PixelNumber pw, PixelNumber ph) { SetPosition(px, py); SetPositionAndWidth(px, pw); height = ph; changed = true; }
	void SetSelected(bool s) { if (s != selected) { selected = s; changed = true; } }
	void Press(bool p, int index) override { UNUSED(p); UNUSED(index); }		// selection shows instead
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Tile with a drawn glyph: SEARCH (magnifier), keyboard SHIFT (arrow) or a single character key.
// Active (SEARCH on, SHIFT = lower case) = accent outline and accent glyph (the toggle look);
// SHIFT also fills its arrow. A character key is pressed like the other keys (accent fill).
enum class CncGlyph : uint8_t { Search, Shift, Char, Grid, List };	// Grid / List: MACROS view toggle

class CncGlyphButton : public SingleButton
{
	PixelNumber height;
	Colour accent;
	CncGlyph glyph;
	char character;
	bool active;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncGlyphButton(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, CncGlyph g, Colour pAccent, event_t e, int param);
	void SetActive(bool a) { if (a != active) { active = a; changed = true; } }
	bool IsActive() const { return active; }
	void SetGlyph(CncGlyph g, char c = 0) { if (g != glyph || c != character) { glyph = g; character = c; changed = true; } }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// A row of keyboard keys in one field (saves RAM against one button per key): 'text' holds one
// character per key, keys are 'pw' wide at a pitch of 'ps'. Pressed key = accent fill.
class CncKeyRow : public ButtonRow
{
	PixelNumber height;
	const char *text;
	Colour accent;

protected:
	PixelNumber GetHeight() const override { return height; }
	void CheckEvent(PixelNumber x, PixelNumber y, int& bestError, ButtonPress& best) override;

public:
	CncKeyRow(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ps, PixelNumber ph, const char *s, Colour pAccent, event_t e);
	int GetIParam(unsigned int index) const override { return (unsigned char)text[index]; }
	void Press(bool p, int index) override;
	void SetText(const char *s);							// same number of keys
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// SYSTEM > ALERT row: ERR / WARN badge, the text (cut with ".."), an accent dot while unread.
// The age is a separate text field on top (it changes every second, the row does not).
// Text pointer must stay valid.
class CncAlertRow : public SingleButton
{
	PixelNumber height;
	const char *text;
	Colour accent;
	bool error;
	bool unread;

protected:
	PixelNumber GetHeight() const override { return height; }

public:
	CncAlertRow(PixelNumber py, PixelNumber px, PixelNumber pw, PixelNumber ph, Colour pAccent, event_t e, int param);
	void SetEntry(const char *t, bool isError, bool isUnread) { text = t; error = isError; unread = isUnread; changed = true; }
	void Press(bool p, int index) override { UNUSED(p); UNUSED(index); }
	void Refresh(bool full, PixelNumber xOffset, PixelNumber yOffset) override;
};

// Crosshair glyph: ring of thickness t around radius r, four arms, centre dot.
void CncDrawCrosshair(int cx, int cy, int r, int t, Colour glyph, Colour background);

#endif /* SRC_UI_CNC_CNCWIDGETS_HPP_ */
