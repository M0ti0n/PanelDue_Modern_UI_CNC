/*
 * CncKeyboard.cpp
 *
 * Shared portrait keyboard (see CncKeyboard.hpp). Geometry: CncLayout::Keyboard, taken from
 * pd_cnc_system_console_keyboard*.svg with the sheet raised so it ends above the nav bar.
 */

#include "CncKeyboard.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include <UI/UserInterface.hpp>
#include "Icons/Icons.hpp"
#include "PanelDue.hpp"
#include <General/String.h>

using namespace CncLayout;
using namespace CncLayout::Keyboard;
using namespace Cnc;

namespace
{
	const Colour ButtonText   = UTFT::fromRGB(36, 36, 36);
	const Colour ConfirmGreen = UTFT::fromRGB(86, 184, 52);

	// Key rows 1..3 (row 0 = digits never changes). Upper case, lower case, symbols.
	constexpr size_t NumChangingRows = 3;
	const char * const UpperRows[NumChangingRows] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
	const char * const LowerRows[NumChangingRows] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
	const char * const SymbolRows[NumChangingRows] = { "[]{}()<>=+", "_:;\"'#$%&", "*/\\^!?@" };

	PopupWindow *popup = nullptr;
	StaticTextField *textField;
	CncArrowButton *upButton, *downButton;
	CncKeyRow *rows[NumChangingRows];
	CncGlyphButton *shiftKey;
	ModernTextButton *modeKey;

	String<CncKeyboard::MaxText> text;
	String<CncKeyboard::MaxText + 2> shown;					// the tail that fits + cursor
	CncKeyboard::Client client = { nullptr, nullptr, nullptr, nullptr, false };
	const CncKeyboard::Client *owner = nullptr;			// the page whose keyboard is open
	bool open = false;
	bool lower = false;
	bool symbols = false;
	bool upEnabled = false, downEnabled = false;

	// Show the end of the text (where the cursor is) when it is wider than the field
	void ShowText()
	{
		constexpr PixelNumber Avail = FieldTextAvail;
		lcd.setFont(glcd19x21);
		const char *p = text.c_str();
		while (*p != 0 && DisplayField::GetTextWidth(p, 9999, strlen(p)) > Avail)
		{
			++p;
		}
		shown.copy(p);
		shown.cat('_');
		textField->SetValue(shown.c_str(), true);
	}

	void ShowKeys()
	{
		for (size_t r = 0; r < NumChangingRows; ++r)
		{
			rows[r]->SetText(symbols ? SymbolRows[r] : lower ? LowerRows[r] : UpperRows[r]);
		}
		if (symbols)
		{
			shiftKey->SetGlyph(CncGlyph::Char, '~');
			shiftKey->SetEvent(evKey, (int)'~');
			shiftKey->SetActive(false);
			modeKey->SetText("ABC");
			SetToggleLook(modeKey, true);					// symbol page on: accent outline + text
		}
		else
		{
			shiftKey->SetGlyph(CncGlyph::Shift);
			shiftKey->SetEvent(evShift, 0);
			shiftKey->SetActive(lower);
			modeKey->SetText("#+=");
			SetToggleLook(modeKey, false);
		}
	}

	void Changed()
	{
		ShowText();
		if (client.changed != nullptr)
		{
			client.changed(text.c_str());
		}
	}
}

namespace CncKeyboard
{
	void Create()
	{
		const Colour accent = Accent();
		popup = new PopupWindow(H, W, PageBg, accent);		// double accent border

		// Top row: arrows, text field, X. AddField prepends: text before its card.
		upButton = new CncArrowButton(TopY, UpX, ArrowW, TopH, true, evUp, -1);
		popup->AddField(upButton);
		downButton = new CncArrowButton(TopY, DownX, ArrowW, TopH, false, evUp, 1);
		popup->AddField(downButton);
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Text, Tile);
		textField = new StaticTextField(TopY + (TopH - 21) / 2, FieldX + FieldTextInset, FieldTextW, TextAlignment::Left, "");
		popup->AddField(textField);
		popup->AddField(new ModernCard(TopY, FieldX, FieldW, TopH, Tile, Border, true));
		DisplayField::SetDefaultColours(ButtonText, StopRed);
		popup->AddField(new ModernIconButton(CloseY, CloseX, CloseW, CloseH, IconCancel, evCncKbClose));

		// Character keys
		static const char * const Digits = "1234567890";
		popup->AddField(new CncKeyRow(KeyRowY(0), Row0X, KeyW, KeyPitchX, KeyH, Digits, accent, evKey));
		rows[0] = new CncKeyRow(KeyRowY(1), Row0X, KeyW, KeyPitchX, KeyH, UpperRows[0], accent, evKey);
		rows[1] = new CncKeyRow(KeyRowY(2), Row2X, KeyW, KeyPitchX, KeyH, UpperRows[1], accent, evKey);
		rows[2] = new CncKeyRow(KeyRowY(3), Row3X, KeyW, KeyPitchX, KeyH, UpperRows[2], accent, evKey);
		for (CncKeyRow *r : rows)
		{
			popup->AddField(r);
		}
		shiftKey = new CncGlyphButton(KeyRowY(3), Row0X, WideW, KeyH, CncGlyph::Shift, accent, evShift, 0);
		popup->AddField(shiftKey);
		DisplayField::SetDefaultColours(Text, Tile, Border, Tile, accent, accent, IconPaletteDark);
		popup->AddField(new ModernIconButton(KeyRowY(3), BackX, WideW, KeyH, IconBackspace, evBackspace, 0, true));

		// Bottom row
		DisplayField::SetDefaultColours(Text, Tile, Border, Tile, accent, accent, IconPaletteDark);
		modeKey = new ModernTextButton(KeyRowY(4), Row0X, WideW, KeyH, "#+=", evCncKbMode, 0, glcd19x21, true);
		popup->AddField(modeKey);
		static const char * const DotMinus = ".-";
		popup->AddField(new CncKeyRow(KeyRowY(4), DotX, KeyW, KeyPitchX, KeyH, DotMinus, accent, evKey));
		popup->AddField(new ModernTextButton(KeyRowY(4), SpaceX, SpaceW, KeyH, "SPACE", evKey, (int)' ', glcd19x21, true));
		DisplayField::SetDefaultColours(ButtonText, ConfirmGreen, ConfirmGreen, ConfirmGreen, ConfirmGreen, ConfirmGreen, IconPaletteDark);
		popup->AddField(new ModernIconButton(EnterY, EnterX, EnterW, EnterH, IconEnter, evSendKeyboardCommand));

		DisplayField::SetDefaultFont(DEFAULT_FONT);
	}

	void Open(const char *initial, const Client& c)
	{
		client = c;
		owner = &c;
		text.copy(initial);
		symbols = false;
		lower = false;
		ShowKeys();
		ShowText();
		SetArrowsEnabled(false, false);
		mgr.ClearAllPopups();
		mgr.SetPopup(popup, X, Y);
		open = true;
	}

	void Close(bool redraw)
	{
		if (open)
		{
			open = false;
			if (mgr.GetPopup() == popup)
			{
				mgr.ClearPopup(redraw, popup);
			}
		}
	}

	bool IsOpen()
	{
		return open && mgr.GetPopup() == popup;
	}

	bool IsOpenFor(const Client *c)
	{
		return IsOpen() && owner == c;
	}

	void SetArrowsEnabled(bool up, bool down)
	{
		upEnabled = up && client.arrow != nullptr;
		downEnabled = down && client.arrow != nullptr;
		upButton->SetDisabled(!upEnabled);
		downButton->SetDisabled(!downEnabled);
	}

	const char *GetText()
	{
		return text.c_str();
	}

	void SetText(const char *t)
	{
		text.copy(t);
		ShowText();
	}

	bool ProcessTouch(ButtonPress bp)
	{
		if (!IsOpen())
		{
			return false;
		}
		switch ((Event)bp.GetEvent())
		{
		case evKey:
			mgr.Press(bp, true);
			if (text.strlen() < CncKeyboard::MaxText)
			{
				text.cat((char)bp.GetIParam());
				Changed();
			}
			return true;

		case evBackspace:
			mgr.Press(bp, true);
			if (!text.IsEmpty())
			{
				text.Truncate(text.strlen() - 1);
				Changed();
			}
			return true;

		case evShift:
			lower = !lower;
			ShowKeys();
			return true;

		case evCncKbMode:
			symbols = !symbols;
			ShowKeys();
			return true;

		case evUp:
			{
				const int dir = bp.GetIParam();
				if ((dir < 0) ? upEnabled : downEnabled)
				{
					mgr.Press(bp, true);
					client.arrow(dir);
				}
			}
			return true;

		case evCncKbClose:
			Close();
			if (client.cancel != nullptr)
			{
				client.cancel();
			}
			return true;

		case evSendKeyboardCommand:
			if (!client.keepOpenOnEnter)
			{
				Close();
			}
			if (client.enter != nullptr)
			{
				client.enter(text.c_str());
			}
			return true;

		default:
			return false;
		}
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evKey:
		case evBackspace:
		case evUp:
			mgr.Press(bp, false);
			return true;

		case evShift:
		case evCncKbMode:
		case evCncKbClose:
		case evSendKeyboardCommand:
			return true;

		default:
			return false;
		}
	}
}

// End
