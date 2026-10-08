/*
 * CncPopups.cpp
 *
 * Portrait versions of the Modern UI standard popup and standard numpad.
 *
 * Standard popup, 460 x 480 centred (portrait standard popup of the ALERT mock-up):
 *   title tile      y 28, h 60   short: x 105 w 250   long: x 25 w 410
 *   info tile       y 108, h 248, x 25 w 410 (same width as the long title tile),
 *                   up to 6 centred lines; 20 px gap to the title tile and to X / check
 *   choice buttons  same area as the info tile, up to 3 per row
 *   form            same area: up to 2 groups, each a small label + rows of 50 px buttons
 *                   (up to 4 in one row, 5-6 as rows of 3), the block centred vertically
 *   X / check       110 x 78 at y 376, x 110 / 240
 *
 * Numpad, 460 x 396 directly under the DRO line (mock-ups pd_cnc_numpad_*.svg), so the
 * DRO stays live while a value is typed:
 *   value tile 322 x 52 (value left, unit right) + tag tile 102 x 52, accent outline
 *   keys 102 x 70, 8 px column gap, 6 px row gap:  7 8 9 ... ; bottom row . 0 -
 *   side column: backspace, X, check, POS (optional)
 */

#include "CncPopups.hpp"
#include "Hardware/SysTick.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include <UI/UserInterface.hpp>
#include "Icons/Icons.hpp"
#include "PanelDue.hpp"
#include <General/SafeStrtod.h>
#include <General/String.h>

using namespace CncLayout;
using namespace Cnc;
using namespace CncPopup;

namespace
{
	enum class Mode : uint8_t { None, Confirm, Choose, Form, Numpad, Message, Alert, Response };
	Mode mode = Mode::None;
	bool machineMessage = false;				// Message mode: an M291 message (ClearAlert closes it)
	bool messageWaits = false;					// Message mode: the machine waits for an answer given in DWC
	bool messagePassive = false;				// Message mode: M291 S1, its check mark only closes it (M292)
	uint32_t responseTimeout = 0, responseSince = 0;	// Response mode: ms (0 = no timeout)

	const Colour ButtonText   = UTFT::fromRGB(36, 36, 36);
	const Colour ConfirmGreen = UTFT::fromRGB(86, 184, 52);

	// ---------------- Standard popup ----------------
	constexpr PixelNumber SW = StdPopupW, SH = StdPopupH;
	constexpr PixelNumber TitleY = 28, TitleH = 60, TitleTextDy = 14;
	constexpr PixelNumber TitleShortX = 105, TitleShortW = 250, TitleLongX = 25, TitleLongW = 410;
	constexpr PixelNumber InfoX = 25, InfoY = TitleY + TitleH + 20, InfoW = TitleLongW, InfoH = 248;	// y 108 .. 355
	constexpr PixelNumber LinePitch = 36, LineH = 21;
	constexpr PixelNumber ActW = 110, ActH = 78, ActGap = 20, ActY = 376;
	constexpr PixelNumber CancelX = (SW - 2 * ActW - ActGap) / 2, OkX = CancelX + ActW + ActGap;
	// Three actions (trash / X / check) when the trash button is shown
	constexpr PixelNumber TrashX3 = (SW - 3 * ActW - 2 * ActGap) / 2, CancelX3 = TrashX3 + ActW + ActGap, OkX3 = CancelX3 + ActW + ActGap;
	constexpr size_t MaxLines = 6;
	constexpr size_t MaxChoices = 9;
	constexpr PixelNumber ChoiceH = 60, ChoiceGapX = 10, ChoiceGapY = 12;

	// Form
	constexpr size_t MaxFormGroups = 2, MaxFormItems = 6;
	constexpr PixelNumber FormBtnH = 50, FormGapX = 10, FormGapY = 8, FormLabelH = 22, FormGroupGap = 14;

	// ---------------- Numpad geometry ----------------
	constexpr PixelNumber PW = PopupUnderDroW, PH = PopupUnderDroH;

	PopupWindow *stdPopup = nullptr;
	ModernCard *titleCard;
	StaticTextField *titleField;
	ModernCard *infoCard;
	StaticTextField *infoFields[MaxLines];
	ModernTextButton *choiceButtons[MaxChoices];

	String<24> titleText;
	String<40> lineText[MaxLines];

	ConfirmHandler confirmHandler = nullptr;
	ConfirmHandler cancelHandler = nullptr;
	ConfirmHandler trashHandler = nullptr;
	bool alertBackToNumpad = false;					// X on the ALERT reopens the numpad (value kept)
	ModernIconButton *stdCancelButton, *stdOkButton, *stdTrashButton;
	constexpr PixelNumber WrapWidth = InfoW - 20;		// 390 px text width in the info tile
	ChoiceHandler choiceHandler = nullptr;
	ChoiceAllowed choiceAllowed = nullptr;
	bool choiceInstant = false;							// a tap on a choice runs the handler at once
	int handlerParam = 0;
	size_t numChoices = 0;
	size_t pendingChoice = 0;
	ButtonBase *selectedChoice = nullptr;
	bool swatches = false;							// ChooseColour: choices are colour swatches, selection = light outline
	const Colour *swatchColours = nullptr;

	void ShowSwatchSelection()
	{
		for (size_t i = 0; i < numChoices; ++i)
		{
			choiceButtons[i]->SetBorderColour((i == pendingChoice) ? Text : swatchColours[i]);
		}
	}

	StaticTextField *formLabels[MaxFormGroups];
	CncChoiceButton *formButtons[MaxFormGroups][MaxFormItems];
	FormGroup formGroups[MaxFormGroups];
	uint16_t formSelected[MaxFormGroups];
	size_t formCount = 0;
	FormHandler formHandler = nullptr;

	size_t FormItemsPerRow(size_t n) { return (n <= 4) ? n : 3; }

	// ---------------- Numpad ----------------
	constexpr PixelNumber NM = 17;							// inner margin
	constexpr PixelNumber KW = 102, KH = 70, KGX = 8, KGY = 6;
	constexpr PixelNumber ValH = 52, ValGap = 12;
	constexpr PixelNumber ValW = 3 * KW + 2 * KGX;			// 322
	constexpr PixelNumber KeysY = NM + ValH + ValGap;		// 81
	constexpr size_t MaxValueChars = 9;

	constexpr PixelNumber KeyX(unsigned int col) { return NM + col * (KW + KGX); }
	constexpr PixelNumber KeyY(unsigned int row) { return KeysY + row * (KH + KGY); }

	PopupWindow *numPopup = nullptr;
	StaticTextField *valueField;
	StaticTextField *unitField;
	StaticTextField *tagField;
	ModernTextButton *dotKey, *minusKey, *posKey;

	String<16> valueText;
	String<12> tagText;
	NumpadSpec pad;
	bool fresh = true;						// first key replaces the shown value

	void SetKeyEnabled(ModernTextButton *b, bool enabled, int ascii)
	{
		b->SetColours(enabled ? Text : Muted, Tile);
		b->SetEvent(enabled ? evNumericKey : evNull, ascii);
	}

	void ShowValue()
	{
		valueField->SetValue(valueText.c_str(), true);
	}

	void CreateStandardPopup()
	{
		const Colour accent = Accent();
		stdPopup = new PopupWindow(SH, SW, PageBg, accent);		// PopupWindow draws the double accent border

		// Title tile. AddField prepends: text first, card second, so the card is drawn first.
		DisplayField::SetDefaultFont(glcd28x32);
		DisplayField::SetDefaultColours(Text, Tile);
		titleField = new StaticTextField(TitleY + TitleTextDy, TitleShortX, TitleShortW, TextAlignment::Centre, "");
		stdPopup->AddField(titleField);
		titleCard = new ModernCard(TitleY, TitleShortX, TitleShortW, TitleH, Tile, accent, true);
		stdPopup->AddField(titleCard);

		// Information tile
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Text, Tile);
		for (size_t i = 0; i < MaxLines; ++i)
		{
			infoFields[i] = new StaticTextField(InfoY, InfoX + 10, InfoW - 20, TextAlignment::Centre, "");
			infoFields[i]->Show(false);
			stdPopup->AddField(infoFields[i]);
		}
		infoCard = new ModernCard(InfoY, InfoX, InfoW, InfoH, Tile, Border, true);
		infoCard->Show(false);
		stdPopup->AddField(infoCard);

		// Choice buttons (border + accent fill when selected, as the Modern UI choices)
		DisplayField::SetDefaultColours(Text, Tile, Border, Tile, accent, accent, IconPaletteDark);
		for (size_t i = 0; i < MaxChoices; ++i)
		{
			choiceButtons[i] = new ModernTextButton(InfoY, InfoX, 100, ChoiceH, "", evStandardPopupChoice, (int)i, glcd19x21, true);
			choiceButtons[i]->Show(false);
			stdPopup->AddField(choiceButtons[i]);
		}

		// Form: group labels and buttons (positions set when a form opens)
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, PageBg);
		for (size_t g = 0; g < MaxFormGroups; ++g)
		{
			formLabels[g] = new StaticTextField(InfoY, InfoX + 2, InfoW - 4, TextAlignment::Left, "");
			formLabels[g]->Show(false);
			stdPopup->AddField(formLabels[g]);
			for (size_t i = 0; i < MaxFormItems; ++i)
			{
				formButtons[g][i] = new CncChoiceButton(InfoY, InfoX, 100, FormBtnH, accent, evCncFormItem, (int)(g * 16 + i));
				formButtons[g][i]->Show(false);
				stdPopup->AddField(formButtons[g][i]);
			}
		}

		// Trash (hidden unless ConfirmDeletable): the Modern UI's delete button, dark icon on light blue
		DisplayField::SetDefaultColours(UTFT::fromRGB(36, 36, 36), UTFT::fromRGB(95, 195, 220));
		stdTrashButton = new ModernIconButton(ActY, TrashX3, ActW, ActH, IconTrash, evStatusJobDeleteOpen);
		stdTrashButton->Show(false);
		stdPopup->AddField(stdTrashButton);

		// X / check
		DisplayField::SetDefaultColours(ButtonText, StopRed);
		stdCancelButton = new ModernIconButton(ActY, CancelX, ActW, ActH, IconCancel, evStandardPopupCancel);
		stdPopup->AddField(stdCancelButton);
		DisplayField::SetDefaultColours(ButtonText, ConfirmGreen);
		stdOkButton = new ModernIconButton(ActY, OkX, ActW, ActH, IconOk, evStandardPopupConfirm);
		stdPopup->AddField(stdOkButton);
	}

	void CreateNumpad()
	{
		const Colour accent = Accent();
		numPopup = new PopupWindow(PH, PW, PageBg, accent);		// PopupWindow draws the double accent border

		// Value tile: value left, unit right
		DisplayField::SetDefaultFont(glcd28x32);
		DisplayField::SetDefaultColours(Text, Tile);
		valueField = new StaticTextField(NM + (ValH - 32) / 2, NM + 16, 196, TextAlignment::Left, "");
		numPopup->AddField(valueField);
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, Tile);
		unitField = new StaticTextField(NM + (ValH - 21) / 2, NM + ValW - 14 - 90, 90, TextAlignment::Right, "");
		numPopup->AddField(unitField);
		numPopup->AddField(new ModernCard(NM, NM, ValW, ValH, Tile, Border, true));

		// Tag tile: what is being set, accent text and outline
		DisplayField::SetDefaultColours(accent, Tile);
		tagField = new StaticTextField(NM + (ValH - 21) / 2, KeyX(3) + 3, KW - 6, TextAlignment::Centre, "");
		numPopup->AddField(tagField);
		numPopup->AddField(new ModernCard(NM, KeyX(3), KW, ValH, Tile, accent, true));

		// Digits 1-9, then . 0 -
		static const char * const digitLabels[10] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
		DisplayField::SetDefaultColours(Text, Tile, Border, Tile, accent, accent, IconPaletteDark);
		for (unsigned int d = 1; d <= 9; ++d)
		{
			const unsigned int i = d - 1;
			numPopup->AddField(new ModernTextButton(KeyY(i / 3), KeyX(i % 3), KW, KH, digitLabels[d], evNumericKey, '0' + (int)d, glcd28x32));
		}
		dotKey = new ModernTextButton(KeyY(3), KeyX(0), KW, KH, ".", evNumericKey, '.', glcd28x32);
		numPopup->AddField(dotKey);
		numPopup->AddField(new ModernTextButton(KeyY(3), KeyX(1), KW, KH, "0", evNumericKey, '0', glcd28x32));
		minusKey = new ModernTextButton(KeyY(3), KeyX(2), KW, KH, "-", evNumericKey, '-', glcd28x32);
		numPopup->AddField(minusKey);

		// Side column: backspace, X, check, POS
		posKey = new ModernTextButton(KeyY(3), KeyX(3), KW, KH, "POS", evCncNumPos, 0, glcd19x21);
		numPopup->AddField(posKey);
		DisplayField::SetDefaultColours(Muted, Tile);
		numPopup->AddField(new ModernIconButton(KeyY(0), KeyX(3), KW, KH, IconBackspace, evNumericBack));
		DisplayField::SetDefaultColours(ButtonText, StopRed);
		numPopup->AddField(new ModernIconButton(KeyY(1), KeyX(3), KW, KH, IconCancel, evNumericCancel));
		DisplayField::SetDefaultColours(ButtonText, ConfirmGreen);
		numPopup->AddField(new ModernIconButton(KeyY(2), KeyX(3), KW, KH, IconOk, evNumericOk));
	}

	void ResetStandardPopup(const char *title)
	{
		titleText.copy(title);
		const bool longTitle = titleText.strlen() > 9;
		const PixelNumber x = longTitle ? TitleLongX : TitleShortX;
		const PixelNumber w = longTitle ? TitleLongW : TitleShortW;
		titleCard->SetPositionAndWidth(x, w);
		titleField->SetPositionAndWidth(x, w);
		titleField->SetValue(titleText.c_str(), true);
		titleCard->Show(true);
		titleField->Show(true);

		infoCard->SetHeight(InfoH);								// FormQuestion shortens it
		infoCard->Show(false);
		for (StaticTextField *f : infoFields)
		{
			f->Show(false);
		}
		for (ModernTextButton *b : choiceButtons)
		{
			b->Show(false);
			b->Press(false, 0);
			b->SetBorderColour(Border);
		}
		numChoices = 0;
		swatches = false;
		choiceInstant = false;
		selectedChoice = nullptr;
		cancelHandler = nullptr;
		trashHandler = nullptr;
		stdTrashButton->Show(false);

		// Default two-action layout (Message may switch to a single centred check mark,
		// Alert to a single centred X)
		stdCancelButton->Show(true);
		stdCancelButton->SetPosition(CancelX, ActY);
		stdOkButton->Show(true);
		stdOkButton->SetPosition(OkX, ActY);
		stdOkButton->SetColours(ButtonText, ConfirmGreen);

		for (size_t g = 0; g < MaxFormGroups; ++g)
		{
			formLabels[g]->Show(false);
			for (CncChoiceButton *b : formButtons[g])
			{
				b->Show(false);
				b->SetSelected(false);
			}
		}
		formCount = 0;
	}

	bool FormValid()
	{
		for (size_t g = 0; g < formCount; ++g)
		{
			if (formSelected[g] == 0)
			{
				return false;
			}
		}
		return true;
	}

	// The check mark is greyed until every group has a selection
	void UpdateFormOk()
	{
		stdOkButton->SetColours(ButtonText, FormValid() ? ConfirmGreen : Border);
	}

	void ShowFormSelection(size_t g)
	{
		for (size_t i = 0; i < formGroups[g].n; ++i)
		{
			formButtons[g][i]->SetSelected((formSelected[g] & (1u << i)) != 0);
		}
	}

	void OpenPopup(PopupWindow *p, Mode m)
	{
		mgr.ClearAllPopups();
		mode = m;
		if (p == numPopup)
		{
			mgr.SetPopup(p, PopupUnderDroX, PopupUnderDroY);
		}
		else
		{
			mgr.SetPopup(p, StdPopupX, StdPopupY);
		}
	}

	// ---- Numpad input ----
	void Key(char c)
	{
		if (fresh)
		{
			valueText.Clear();
			fresh = false;
		}

		if (c == '-')
		{
			if (valueText.strlen() > 0 && valueText[0] == '-')
			{
				valueText.Erase(0);
			}
			else if (valueText.strlen() < MaxValueChars)
			{
				valueText.Insert(0, '-');
			}
			else
			{
				return;								// full: the key does nothing
			}
			ShowValue();
			return;
		}

		if (valueText.strlen() >= MaxValueChars)
		{
			return;									// full: the key does nothing
		}

		if (c == '.')
		{
			if (valueText.Contains('.') >= 0)
			{
				return;								// only one decimal point
			}
			const bool noDigit = valueText.IsEmpty() || valueText.Equals("-");
			if (noDigit)
			{
				valueText.cat('0');
			}
			valueText.cat('.');
		}
		else if (valueText.Equals("0"))
		{
			valueText.Clear();
			valueText.cat(c);						// no leading zeros
		}
		else if (valueText.Equals("-0"))
		{
			valueText.Truncate(1);
			valueText.cat(c);
		}
		else
		{
			valueText.cat(c);
		}
		ShowValue();
	}

	void Backspace()
	{
		fresh = false;
		const size_t len = valueText.strlen();
		if (len > 0)
		{
			valueText.Truncate(len - 1);
			ShowValue();
		}
	}

	void FormatValue(float v, unsigned int decimals)
	{
		if (decimals == 0)
		{
			valueText.printf("%ld", (long)((v < 0.0f) ? v - 0.5f : v + 0.5f));
		}
		else
		{
			valueText.printf("%.*f", (int)decimals, (double)v);
		}
	}

	// Parse the entered text. False if it is not a complete number.
	bool ParseValue(float& out)
	{
		if (valueText.IsEmpty() || valueText.Equals("-"))
		{
			return false;
		}
		const char *end = nullptr;
		out = SafeStrtof(valueText.c_str(), &end);
		return end != nullptr && *end == 0;
	}
	// Word-wrap text by pixel width into up to MaxLines lines; a line break starts a new line.
	// The lines point into a static buffer that stays valid until the next call.
	size_t WrapText(const char *text, const char *lines[])
	{
		static String<40> wrapped[MaxLines];
		size_t n = 0;
		const char *p = text;
		lcd.setFont(glcd19x21);
		while (*p != 0 && n < MaxLines)
		{
			while (*p == ' ')
			{
				++p;
			}
			if (*p == 0)
			{
				break;
			}
			if (*p == '\n')
			{
				wrapped[n].Clear();					// empty line
				lines[n] = wrapped[n].c_str();
				++n;
				++p;
				continue;
			}
			// Longest run of whole words that fits; a single over-long word is cut where it fits
			size_t len = 0, fit = 0;
			while (len < wrapped[n].Capacity() && p[len] != 0 && p[len] != '\n')
			{
				++len;
				const bool wordEnd = (p[len] == 0 || p[len] == ' ' || p[len] == '\n');
				if (DisplayField::GetTextWidth(p, 9999, len) > WrapWidth)
				{
					if (fit == 0)
					{
						fit = (len > 1) ? len - 1 : 1;	// the first word alone is too wide
					}
					break;
				}
				if (wordEnd)
				{
					fit = len;
				}
			}
			if (fit == 0)
			{
				fit = (len > 0) ? len : 1;			// text ended without a word end (capacity reached)
			}
			len = fit;
			wrapped[n].copy(p, len);
			lines[n] = wrapped[n].c_str();
			++n;
			p += len;
			if (*p == '\n')
			{
				++p;
			}
		}
		return n;
	}
}

namespace CncPopup
{
	void Create()
	{
		CreateStandardPopup();
		CreateNumpad();
		DisplayField::SetDefaultFont(DEFAULT_FONT);
	}

	void Confirm(const char *title, const char *line1, const char *line2, const char *line3, const char *line4,
					ConfirmHandler onOk, int param)
	{
		const char * const lines[4] = { line1, line2, line3, line4 };
		size_t n = 0;
		while (n < 4 && lines[n] != nullptr)
		{
			++n;
		}
		ConfirmLines(title, lines, n, onOk, param);
	}

	// Title + information lines, popup not opened yet
	static void FillInfo(const char *title, const char * const lines[], size_t n)
	{
		ResetStandardPopup(title);
		if (n > MaxLines)
		{
			n = MaxLines;
		}
		if (n == 0)
		{
			static const char * const empty[1] = { "" };
			lines = empty;
			n = 1;
		}

		// Lines centred as a block in the info tile
		infoCard->Show(true);
		const PixelNumber firstTop = InfoY + (InfoH - ((n - 1) * LinePitch + LineH)) / 2;
		for (size_t i = 0; i < n; ++i)
		{
			lineText[i].copy(lines[i]);
			infoFields[i]->SetPosition(InfoX + 10, firstTop + i * LinePitch);
			infoFields[i]->SetValue(lineText[i].c_str(), true);
			infoFields[i]->Show(true);
		}
	}

	void ConfirmDeletable(const char *title, const char *line1, const char *line2, const char *line3, const char *line4,
							ConfirmHandler onOk, ConfirmHandler onTrash, int param)
	{
		const char * const lines[4] = { line1, line2, line3, line4 };
		size_t n = 0;
		while (n < 4 && lines[n] != nullptr)
		{
			++n;
		}
		FillInfo(title, lines, n);
		confirmHandler = onOk;
		choiceHandler = nullptr;
		handlerParam = param;
		trashHandler = onTrash;
		stdTrashButton->SetPosition(TrashX3, ActY);
		stdTrashButton->Show(true);
		stdCancelButton->SetPosition(CancelX3, ActY);
		stdOkButton->SetPosition(OkX3, ActY);
		OpenPopup(stdPopup, Mode::Confirm);
	}

	void ConfirmWithCancel(const char *title, const char *line1, const char *line2, const char *line3, const char *line4,
							ConfirmHandler onOk, ConfirmHandler onCancel, int param)
	{
		const char * const lines[4] = { line1, line2, line3, line4 };
		size_t n = 0;
		while (n < 4 && lines[n] != nullptr)
		{
			++n;
		}
		FillInfo(title, lines, n);
		confirmHandler = onOk;
		choiceHandler = nullptr;
		handlerParam = param;
		cancelHandler = onCancel;
		OpenPopup(stdPopup, Mode::Confirm);
	}

	void ConfirmLines(const char *title, const char * const lines[], size_t n, ConfirmHandler onOk, int param)
	{
		FillInfo(title, lines, n);
		confirmHandler = onOk;
		choiceHandler = nullptr;
		handlerParam = param;
		OpenPopup(stdPopup, Mode::Confirm);
	}

	// ALERT ! with only X, centred. backToNumpad: X reopens the numpad as it was.
	static void ShowAlert(const char *text, bool backToNumpad, const char *title = "ALERT !")
	{
		const char *lines[MaxLines];
		const size_t n = WrapText(text, lines);
		FillInfo(title, lines, n);
		stdOkButton->Show(false);
		stdCancelButton->SetPosition((SW - ActW) / 2, ActY);
		confirmHandler = nullptr;
		cancelHandler = nullptr;
		choiceHandler = nullptr;
		alertBackToNumpad = backToNumpad;		// the numpad window is replaced, its content kept
		OpenPopup(stdPopup, Mode::Alert);
	}

	void Alert(const char *text)
	{
		ShowAlert(text, false);
	}

	void Info(const char *title, const char *text)
	{
		ShowAlert(text, false, title);
	}

	void Response(const char *text, bool isError, uint32_t timeoutMs, bool force)
	{
		// As the Modern UI: a reply replaces the reply shown (an error too) and a message the machine
		// does not wait for (M291 S0/S1); it is not shown over any other popup in use
		const bool replace = (mode == Mode::Response);
		const bool overMessage = (mode == Mode::Message && machineMessage && !IsBlockingMessageOpen());
		if (!replace && !overMessage && (mode != Mode::None || mgr.GetPopup() != nullptr))
		{
			if (!force || IsBlockingMessageOpen())
			{
				return;
			}
		}

		const char *lines[MaxLines];
		const size_t n = WrapText(text, lines);
		FillInfo(isError ? "ALERT !" : "", lines, n);
		if (!isError)
		{
			titleCard->Show(false);					// ordinary replies have no title tile (Modern UI)
			titleField->Show(false);
		}
		stdOkButton->Show(false);
		stdCancelButton->SetPosition((SW - ActW) / 2, ActY);
		confirmHandler = nullptr;
		cancelHandler = nullptr;
		choiceHandler = nullptr;
		alertBackToNumpad = false;
		responseTimeout = isError ? 0 : timeoutMs;	// errors stay until X or the next reply
		responseSince = SystemTick::GetTickCount();
		if (replace || overMessage)
		{
			mode = Mode::Response;
			machineMessage = false;
			stdPopup->Refresh(true);				// the newest reply replaces what is shown
		}
		else
		{
			OpenPopup(stdPopup, Mode::Response);
		}
	}

	void Spin()
	{
		if (mode == Mode::Response && responseTimeout != 0 && SystemTick::GetTickCount() - responseSince >= responseTimeout)
		{
			Close();
		}
	}

	void CloseResponse()
	{
		if (mode == Mode::Response)
		{
			Close();
		}
	}

	bool IsAlertOpen()
	{
		return mode == Mode::Alert;
	}

	void Message(const char *title, const char *text, bool withCancel, ConfirmHandler onOk, ConfirmHandler onCancel, int param, bool waits, bool passive)
	{
		const char *lines[MaxLines];
		const size_t n = WrapText(text, lines);
		FillInfo(title, lines, n);
		if (!withCancel)
		{
			stdCancelButton->Show(false);
			stdOkButton->SetPosition((SW - ActW) / 2, ActY);	// single check mark, centred
		}
		confirmHandler = onOk;
		cancelHandler = onCancel;
		choiceHandler = nullptr;
		handlerParam = param;
		OpenPopup(stdPopup, Mode::Message);
		machineMessage = true;
		messageWaits = waits;
		messagePassive = passive;
	}

	void Notice(const char *title, const char *text)
	{
		Message(title, text, false, nullptr, nullptr, 0);
		machineMessage = false;
	}

	bool IsMessageOpen()
	{
		return mode == Mode::Message && machineMessage;
	}

	bool CanShowPassive()
	{
		return mode == Mode::None || mode == Mode::Response
			|| (mode == Mode::Message && machineMessage && !IsBlockingMessageOpen());
	}

	bool IsBlockingMessageOpen()
	{
		return mode == Mode::Message
			&& (messageWaits || cancelHandler != nullptr || (confirmHandler != nullptr && !messagePassive));
	}

	void Choose(const char *title, const char * const labels[], size_t n, size_t selected,
					ChoiceAllowed allowed, ChoiceHandler onOk, int param, bool instant, size_t columns)
	{
		ResetStandardPopup(title);
		if (n > MaxChoices)
		{
			n = MaxChoices;
		}

		// Up to 3 per row (5 choices: 3 + 2), 4 choices in one row (INFO TIMEOUT, as the Modern UI)
		const size_t perRow = (columns != 0 && columns <= n) ? columns : ((n <= 4) ? n : 3);
		const size_t rows = (n + perRow - 1) / perRow;
		const PixelNumber w = (InfoW - (perRow - 1) * ChoiceGapX) / perRow;
		const PixelNumber blockH = rows * ChoiceH + (rows - 1) * ChoiceGapY;
		const PixelNumber top = InfoY + (InfoH - blockH) / 2;
		for (size_t i = 0; i < n; ++i)
		{
			const size_t row = i / perRow;
			const size_t inRow = (row + 1 < rows) ? perRow : n - row * perRow;
			const PixelNumber rowW = inRow * w + (inRow - 1) * ChoiceGapX;
			const PixelNumber x = InfoX + (InfoW - rowW) / 2 + (i % perRow) * (w + ChoiceGapX);
			ModernTextButton * const b = choiceButtons[i];
			b->SetPosition(x, top + row * (ChoiceH + ChoiceGapY));
			b->SetPositionAndWidth(x, w);
			b->SetText(labels[i]);
			b->SetColours((allowed == nullptr || allowed(i)) ? Text : Border, Tile);
			b->Press(i == selected, 0);
			b->Show(true);
		}
		numChoices = n;
		pendingChoice = (selected < n) ? selected : 0;
		selectedChoice = choiceButtons[pendingChoice];

		choiceHandler = onOk;
		choiceAllowed = allowed;
		choiceInstant = instant;
		confirmHandler = nullptr;
		handlerParam = param;
		OpenPopup(stdPopup, Mode::Choose);
	}

	void ChooseColour(const char *title, const Colour colours[], size_t n, size_t selected, ChoiceHandler onOk, int param)
	{
		ResetStandardPopup(title);
		if (n > MaxChoices)
		{
			n = MaxChoices;
		}

		// 4 per row (8 accent colours: 4 x 2, as the Modern UI ACCENT COLOR popup)
		const size_t perRow = (n < 4) ? n : 4;
		const size_t rows = (n + perRow - 1) / perRow;
		const PixelNumber w = (InfoW - (perRow - 1) * ChoiceGapX) / perRow;
		const PixelNumber blockH = rows * ChoiceH + (rows - 1) * ChoiceGapY;
		const PixelNumber top = InfoY + (InfoH - blockH) / 2;
		for (size_t i = 0; i < n; ++i)
		{
			const PixelNumber x = InfoX + (i % perRow) * (w + ChoiceGapX);
			ModernTextButton * const b = choiceButtons[i];
			b->SetPosition(x, top + (i / perRow) * (ChoiceH + ChoiceGapY));
			b->SetPositionAndWidth(x, w);
			b->SetText("");
			b->SetColours(colours[i], colours[i]);
			b->Show(true);
		}
		numChoices = n;
		pendingChoice = (selected < n) ? selected : 0;
		swatches = true;
		swatchColours = colours;
		ShowSwatchSelection();
		selectedChoice = nullptr;

		choiceHandler = onOk;
		choiceAllowed = nullptr;
		confirmHandler = nullptr;
		handlerParam = param;
		OpenPopup(stdPopup, Mode::Choose);
	}

	// question != nullptr (one group only): no label above the group; the answers sit at the bottom of the
	// info area and the question is centred in a tile between the title and the answers
	static void FormImpl(const char *title, const FormGroup groups[], size_t nGroups, FormHandler onOk, int param,
							const char *question)
	{
		ResetStandardPopup(title);
		if (nGroups > MaxFormGroups)
		{
			nGroups = MaxFormGroups;
		}
		const bool q = (question != nullptr && nGroups == 1);

		// Height of the whole block, to centre it in the info area
		PixelNumber blockH = 0;
		for (size_t g = 0; g < nGroups; ++g)
		{
			const size_t n = min<size_t>(groups[g].n, MaxFormItems);
			const size_t rows = (n + FormItemsPerRow(n) - 1) / FormItemsPerRow(n);
			blockH += (q ? 0 : FormLabelH) + rows * FormBtnH + (rows - 1) * FormGapY;
			if (g + 1 < nGroups)
			{
				blockH += FormGroupGap;
			}
		}
		PixelNumber y = q ? InfoY + InfoH - blockH : InfoY + ((blockH < InfoH) ? (InfoH - blockH) / 2 : 0);
		const PixelNumber answersTop = y;

		for (size_t g = 0; g < nGroups; ++g)
		{
			formGroups[g] = groups[g];
			formGroups[g].n = min<size_t>(groups[g].n, MaxFormItems);
			const size_t n = formGroups[g].n;
			formSelected[g] = groups[g].selected & ~groups[g].disabled;

			if (!q)
			{
				formLabels[g]->SetPosition(InfoX + 2, y);
				formLabels[g]->SetValue(groups[g].label, true);
				formLabels[g]->Show(true);
				y += FormLabelH;
			}

			const size_t perRow = FormItemsPerRow(n);
			const PixelNumber w = (InfoW - (perRow - 1) * FormGapX) / perRow;
			for (size_t i = 0; i < n; ++i)
			{
				CncChoiceButton * const b = formButtons[g][i];
				const PixelNumber bx = InfoX + (i % perRow) * (w + FormGapX);
				b->SetPosition(bx, y + (i / perRow) * (FormBtnH + FormGapY));
				b->SetPositionAndWidth(bx, w);
				b->SetText(groups[g].items[i]);
				b->SetDisabled((groups[g].disabled & (1u << i)) != 0);
				b->SetDot((groups[g].marked & (1u << i)) != 0);
				b->Show(true);
			}
			ShowFormSelection(g);
			const size_t rows = (n + perRow - 1) / perRow;
			y += rows * FormBtnH + (rows - 1) * FormGapY + FormGroupGap;
		}
		formCount = nGroups;
		if (q)
		{
			// Question tile from the info area top down to a gap above the answers, text centred in it
			const PixelNumber tileH = answersTop - FormGroupGap - InfoY;
			infoCard->SetHeight(tileH);
			infoCard->Show(true);
			lineText[0].copy(question);
			// Centred across the tile, and 20 px above the middle of it
			const PixelNumber mid = (tileH - LineH) / 2;
			infoFields[0]->SetPositionAndWidth(InfoX + 10, InfoW - 20);
			infoFields[0]->SetPosition(InfoX + 10, InfoY + ((mid > 20) ? mid - 20 : 0));
			infoFields[0]->SetValue(lineText[0].c_str(), true);
			infoFields[0]->Show(true);
		}
		UpdateFormOk();

		formHandler = onOk;
		confirmHandler = nullptr;
		choiceHandler = nullptr;
		handlerParam = param;
		OpenPopup(stdPopup, Mode::Form);
	}

	void Form(const char *title, const FormGroup groups[], size_t nGroups, FormHandler onOk, int param)
	{
		FormImpl(title, groups, nGroups, onOk, param, nullptr);
	}

	void FormQuestion(const char *title, const char *question, const char * const answers[], size_t n,
						FormHandler onOk, int param)
	{
		const FormGroup group = { "", answers, n, false, 0, 0, 0 };		// no default answer
		FormImpl(title, &group, 1, onOk, param, question);
	}

	void Numpad(const NumpadSpec& s)
	{
		pad = s;
		tagText.copy(s.tag);
		tagField->SetValue(tagText.c_str(), true);
		unitField->SetValue(s.unit, true);
		SetKeyEnabled(dotKey, s.allowDecimal, '.');
		SetKeyEnabled(minusKey, s.allowMinus, '-');
		posKey->Show(s.pos != nullptr);
		FormatValue(s.value, s.decimals);
		fresh = true;
		ShowValue();
		OpenPopup(numPopup, Mode::Numpad);
	}

	bool IsOpen()
	{
		return mode != Mode::None;
	}

	void Close()
	{
		if (mode != Mode::None)
		{
			mode = Mode::None;
			mgr.ClearAllPopups();
		}
	}

	bool ProcessTouch(ButtonPress bp)
	{
		if (mode == Mode::None)
		{
			return false;
		}

		switch ((Event)bp.GetEvent())
		{
		// ---- Standard popup ----
		case evStandardPopupChoice:
			{
				const size_t i = (size_t)bp.GetIParam();
				if (i >= numChoices || (choiceAllowed != nullptr && !choiceAllowed(i)))
				{
					return true;					// greyed choice: nothing happens
				}
				pendingChoice = i;
				if (choiceInstant && mode == Mode::Choose && choiceHandler != nullptr)
				{
					const ChoiceHandler sh = choiceHandler;
					const int param = handlerParam;
					Close();							// before the handler, which may open another popup or leave the page
					sh(param, i);
					return true;
				}
				if (swatches)
				{
					ShowSwatchSelection();			// outline only: pressed would fill it with the accent
				}
				else
				{
					Select(selectedChoice, bp.GetButton());
				}
			}
			return true;

		case evCncFormItem:
			{
				const size_t g = (size_t)bp.GetIParam() / 16, i = (size_t)bp.GetIParam() % 16;
				if (mode != Mode::Form || g >= formCount || i >= formGroups[g].n || (formGroups[g].disabled & (1u << i)) != 0)
				{
					return true;					// greyed item: nothing happens
				}
				if (formGroups[g].multi)
				{
					formSelected[g] ^= (uint16_t)(1u << i);
				}
				else
				{
					formSelected[g] = (uint16_t)(1u << i);
				}
				ShowFormSelection(g);
				UpdateFormOk();
			}
			return true;

		case evStandardPopupConfirm:
			if (mode == Mode::Form)
			{
				if (!FormValid())
				{
					return true;					// greyed check mark: nothing happens
				}
				const FormHandler fh = formHandler;
				const int param = handlerParam;
				uint16_t sel[MaxFormGroups];
				for (size_t g = 0; g < MaxFormGroups; ++g)
				{
					sel[g] = (g < formCount) ? formSelected[g] : 0;
				}
				Close();
				if (fh != nullptr)
				{
					fh(param, sel);
				}
				return true;
			}
			{
				const Mode m = mode;
				const ConfirmHandler ch = confirmHandler;
				const ChoiceHandler sh = choiceHandler;
				const int param = handlerParam;
				const size_t choice = pendingChoice;
				Close();							// before the handler, which may open another popup
				if ((m == Mode::Confirm || m == Mode::Message) && ch != nullptr)
				{
					ch(param);
				}
				else if (m == Mode::Choose && sh != nullptr)
				{
					sh(param, choice);
				}
			}
			return true;

		case evStatusJobDeleteOpen:					// the trash button of ConfirmDeletable
			if (mode == Mode::Confirm && trashHandler != nullptr)
			{
				const ConfirmHandler h = trashHandler;
				const int param = handlerParam;
				Close();								// before the handler, which opens the next question
				h(param);
			}
			return true;

		case evStandardPopupCancel:
			if (mode == Mode::Alert && alertBackToNumpad)
			{
				alertBackToNumpad = false;
				OpenPopup(numPopup, Mode::Numpad);		// same value, same limits
				return true;
			}
			{
				const ConfirmHandler h = (mode == Mode::Message || mode == Mode::Confirm) ? cancelHandler : nullptr;
				const int param = handlerParam;
				Close();
				if (h != nullptr)
				{
					h(param);
				}
			}
			return true;

		case evNumericCancel:
			Close();
			return true;

		// ---- Numpad ----
		case evNumericKey:
			mgr.Press(bp, true);
			Key((char)bp.GetIParam());
			return true;

		case evNumericBack:
			mgr.Press(bp, true);
			Backspace();
			return true;

		case evCncNumPos:
			mgr.Press(bp, true);
			if (pad.pos != nullptr)
			{
				FormatValue(pad.pos(pad.param), 3);
				fresh = true;
				ShowValue();
			}
			return true;

		case evNumericOk:
			{
				float v;
				if (!ParseValue(v) || v < pad.min || v > pad.max)
				{
					// ALERT, and X brings the numpad back with the typed value
					String<48> msg;
					const int dec = (int)pad.decimals;
					msg.printf("Enter a value from %.*f to %.*f.", dec, (double)pad.min, dec, (double)pad.max);
					ShowAlert(msg.c_str(), true);
					return true;
				}
				const ValueHandler h = pad.onOk;
				const int param = pad.param;
				Close();
				if (h != nullptr)
				{
					h(param, v);
				}
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
		case evNumericKey:
		case evNumericBack:
		case evCncNumPos:
			mgr.Press(bp, false);
			return true;

		case evStandardPopupChoice:				// selection stays
		case evCncFormItem:
		case evStandardPopupConfirm:			// popup already closed
		case evStandardPopupCancel:
		case evStatusJobDeleteOpen:
		case evNumericOk:
		case evNumericCancel:
			return true;

		default:
			return false;
		}
	}
}

// End
