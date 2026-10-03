/*
 * CncProbePage.cpp
 *
 * WCS > PROBE, portrait CNC UI (mock-ups pd_cnc_wcs_probe_*.svg):
 *
 *   ORIGIN               |  MODE                      AUTO
 *   [TL ][TC ][TR ]      |  [TIP Ø     ][CLEARANCE ]
 *   [ML ][MC ][MR ]      |  [SEARCH    ][Z DEPTH   ]
 *   [BL ][BC ][BR ]      |  [STOCK     ][STORE TO v]
 *   [BORE][BOSS][Z TOP]  |
 *   [PROBE OPEN] [          RUN PROBE             ]
 *
 * One origin is picked at a time. Corners, edges and centre are filled with the accent colour
 * when picked; BORE, BOSS and Z TOP get an accent outline instead.
 * The mode (AUTO / SEMI-AUTO / MANUAL) is shown as a label only; it is chosen in SETTINGS.
 * The parameter tiles open the standard numpad (STOCK: X, then Y), STORE TO the standard popup.
 * The probe tile shows probe K0: green outline OPEN, red outline CLOSED (triggered).
 *
 * RUN PROBE / START calls the probe macro of the mode:
 *   AUTO    M98 P"0:/sys/probe/probe_auto.g"   O"TL" D<tip> C<clearance> S<search> Z<depth> L<stock X> B<stock Y> W<1..6>
 *   SEMI    M98 P"0:/sys/probe/probe_semi.g"   O"TL" D<tip> C<clearance> S<search> W<1..6>
 *   MANUAL  M98 P"0:/sys/probe/probe_manual.g" O"TL" D<tool diameter> W<1..6>
 * Origin codes: TL TOP TR LEFT CTR RIGHT BL BOT BR BORE BOSS ZTOP.
 *
 * AUTO opens the auto popup (under the DRO line) with a wide emergency STOP. Its info tile:
 *   [side icon] Side 1 of 2          <- M291 S1 R"PROBE:L 1/2" P"Probing toward +X ..."
 *   Probing toward +X ...            <- the text of that message (amber)
 *   -----------------------------
 *   X   212.415  (check)             <- M291 S1 R"PROBE=X" P"212.415"
 *   Y        -                       <- rows for the axes the origin measures
 *   Result goes to G54
 * Any other info message while it is open replaces the status line. It closes when the macro
 * shows its result (M291 S2), reports an error, or the machine goes back to idle.
 *
 * SEMI-AUTO / MANUAL: the macro asks with M291 S2/S3 and axis controls. The title picks the popup:
 *   "PROBE:<side>"  "Move near [side] side of stock, press PROBE"  (semi-automatic)
 *   "READ:<side>"   "Touch [side] side of stock and press READ"    (manual)
 * side: L R T B, or Z (top of the stock). Any other M291 with axis controls gets the same popup
 * with its own title and text and an OK button.
 */

#include "CncProbePage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncPopups.hpp"
#include "CncControlPage.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "PanelDue.hpp"
#include "Hardware/SysTick.hpp"
#include <ObjectModel/PrinterStatus.hpp>
#include "Icons/Icons.hpp"

using namespace CncLayout;
using namespace CncLayout::Probe;
using namespace Cnc;

namespace
{
	const Colour ProbeGreen = UTFT::fromRGB(86, 184, 52);
	const Colour ButtonText = UTFT::fromRGB(36, 36, 36);

	constexpr size_t NumOrigins = 12;
	const char * const OriginCodes[NumOrigins] = { "TL", "TOP", "TR", "LEFT", "CTR", "RIGHT", "BL", "BOT", "BR", "BORE", "BOSS", "ZTOP" };
	const char * const OriginNames[NumOrigins] =
	{
		"Top left corner", "Top edge", "Top right corner",
		"Left edge", "Centre", "Right edge",
		"Bottom left corner", "Bottom edge", "Bottom right corner",
		"Bore centre", "Boss centre", "Top of stock (Z)"
	};
	const char * const BigLabels[3] = { "BORE", "BOSS", "Z TOP" };

	constexpr size_t NumWcs = 6;
	const char * const WcsNames[NumWcs] = { "G54", "G55", "G56", "G57", "G58", "G59" };

	// Parameters
	enum ParamId : uint8_t { PTip, PAutoClear, PAutoSearch, PZDepth, PStock, PStoreTo, PSemiClear, PSemiSearch, PToolDia, NumParams };

	struct ProbeValues
	{
		float tip = 2.0f;
		float autoClear = 5.0f, autoSearch = 10.0f, zDepth = 3.0f;
		float stockX = 100.0f, stockY = 60.0f;
		float semiClear = 2.0f, semiSearch = 5.0f;
		float toolDia = 6.0f;
		uint8_t storeTo = 0;							// 0 = G54
	} values;

	CncProbe::Mode mode = CncProbe::Mode::Auto;
	CncOrigin origin = CncOrigin::TL;
	CncOrigin runOrigin = CncOrigin::TL;				// origin of the last RUN (glyph in the popups)
	bool autoBusySeen = false;							// the auto probe macro has been seen running
	uint32_t autoStartTime = 0;
	constexpr uint32_t AutoStartTimeout = 5000;			// ms: close the auto popup if the macro never showed up
	bool jobRunning = false, jobActive = false;
	bool haveProbeValue = false, probeClosed = false;
	size_t numAxes = 3;

	// Page fields
	CncOriginButton *originButtons[NumOrigins];
	ButtonBase *selectedOrigin = nullptr;
	StaticTextField *modeLabel;
	String<20> tileText[NumParams];						// one text per parameter, shared by its tiles

	struct ModeTile { CncValueTile *tile; ParamId id; };
	constexpr size_t MaxModeTiles = 6;
	ModeTile modeTiles[3][MaxModeTiles];				// per mode: AUTO, SEMI, MANUAL
	size_t modeTileCount[3] = { 0, 0, 0 };
	CncStatusTile *probeTile;
	ModernTextButton *runButton, *startButton;

	// ---- Parameter tiles ----
	float *ValueOf(ParamId id)
	{
		switch (id)
		{
		case PTip:			return &values.tip;
		case PAutoClear:	return &values.autoClear;
		case PAutoSearch:	return &values.autoSearch;
		case PZDepth:		return &values.zDepth;
		case PSemiClear:	return &values.semiClear;
		case PSemiSearch:	return &values.semiSearch;
		case PToolDia:		return &values.toolDia;
		default:			return nullptr;
		}
	}

	// Refresh the text of a parameter on every tile that shows it
	void UpdateTileText(ParamId id)
	{
		if (id == PStock)
		{
			tileText[id].printf("%gx%g", (double)values.stockX, (double)values.stockY);
		}
		else if (id == PStoreTo)
		{
			tileText[id].copy(WcsNames[values.storeTo]);
		}
		else
		{
			tileText[id].printf("%.3f", (double)*ValueOf(id));
		}
		for (size_t m = 0; m < 3; ++m)
		{
			for (size_t k = 0; k < modeTileCount[m]; ++k)
			{
				if (modeTiles[m][k].id == id)
				{
					modeTiles[m][k].tile->SetValue(tileText[id].c_str());
				}
			}
		}
	}

	void AddModeTile(size_t m, PixelNumber y, PixelNumber x, PixelNumber w, PixelNumber h, const char *label, ParamId id, bool stacked)
	{
		CncValueTile * const t = new CncValueTile(y, x, w, h, label, stacked, id == PStoreTo, evCncProbeParam, id);
		mgr.AddField(t);
		modeTiles[m][modeTileCount[m]++] = { t, id };
	}

	void ShowModeLayout()
	{
		static const char * const modeNames[3] = { "AUTO", "SEMI-AUTO", "MANUAL" };
		modeLabel->SetValue(modeNames[(int)mode], true);
		for (size_t m = 0; m < 3; ++m)					// hide the other layouts first, then show this one
		{
			if (m != (size_t)mode)
			{
				for (size_t k = 0; k < modeTileCount[m]; ++k)
				{
					mgr.Show(modeTiles[m][k].tile, false);
				}
			}
		}
		for (size_t k = 0; k < modeTileCount[(size_t)mode]; ++k)
		{
			mgr.Show(modeTiles[(size_t)mode][k].tile, true);
		}
		// Hide first, then show: START covers the whole row, hiding it erases the others
		const bool manual = (mode == CncProbe::Mode::Manual);
		if (manual)
		{
			mgr.Show(probeTile, false);					// the cutter is the probe: no signal to show
			mgr.Show(runButton, false);
			mgr.Show(startButton, true);
		}
		else
		{
			mgr.Show(startButton, false);
			mgr.Show(probeTile, true);
			mgr.Show(runButton, true);
		}
	}

	void UpdateProbeTile()
	{
		if (!haveProbeValue)
		{
			probeTile->SetState("-", Border);
		}
		else
		{
			probeTile->SetState(probeClosed ? "CLOSED" : "OPEN", probeClosed ? StopRed : ProbeGreen);
		}
	}

	// ---- Parameter editing ----
	void ParamEntered(int id, float value)
	{
		float * const v = ValueOf((ParamId)id);
		if (v != nullptr)
		{
			*v = value;
			UpdateTileText((ParamId)id);
		}
	}

	void StockYEntered(int param, float value)
	{
		UNUSED(param);
		values.stockY = value;
		UpdateTileText(PStock);
	}

	void OpenNumpad(const char *tag, float value, float min, float max, CncPopup::ValueHandler h, int param)
	{
		CncPopup::NumpadSpec pad;
		pad.tag = tag;
		pad.unit = "mm";
		pad.value = value;
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = false;
		pad.min = min;
		pad.max = max;
		pad.pos = nullptr;
		pad.onOk = h;
		pad.param = param;
		CncPopup::Numpad(pad);
	}

	void StockXEntered(int param, float value)
	{
		UNUSED(param);
		values.stockX = value;
		UpdateTileText(PStock);
		OpenNumpad("STOCK Y", values.stockY, 1.0f, 2000.0f, StockYEntered, 0);	// then the Y size
	}

	void StoreToChosen(int param, size_t choice)
	{
		UNUSED(param);
		values.storeTo = (uint8_t)choice;
		UpdateTileText(PStoreTo);
	}

	void EditParam(ParamId id)
	{
		switch (id)
		{
		case PTip:			OpenNumpad("TIP \xC3\x98", values.tip, 0.1f, 20.0f, ParamEntered, id); break;
		case PAutoClear:
		case PSemiClear:	OpenNumpad("CLEAR", *ValueOf(id), 0.1f, 100.0f, ParamEntered, id); break;
		case PAutoSearch:
		case PSemiSearch:	OpenNumpad("SEARCH", *ValueOf(id), 0.5f, 200.0f, ParamEntered, id); break;
		case PZDepth:		OpenNumpad("Z DEPTH", values.zDepth, 0.1f, 50.0f, ParamEntered, id); break;
		case PToolDia:		OpenNumpad("TOOL \xC3\x98", values.toolDia, 0.1f, 50.0f, ParamEntered, id); break;
		case PStock:		OpenNumpad("STOCK X", values.stockX, 1.0f, 2000.0f, StockXEntered, 0); break;
		case PStoreTo:		CncPopup::Choose("STORE TO", WcsNames, NumWcs, values.storeTo, nullptr, StoreToChosen, 0); break;
		default:			break;
		}
	}

	// ======================================================================
	// Popups (both 460 x 396, directly under the DRO line)
	// ======================================================================
	constexpr PixelNumber PW = PopupUnderDroW, PH = PopupUnderDroH;
	constexpr PixelNumber TitleX = 105, TitleY = 14, TitleW = 250, TitleH = 46;
	constexpr PixelNumber PopBtnW = 110, PopBtnH = 78, PopBtnY = PH - 14 - PopBtnH;	// 304
	constexpr PixelNumber PopCancelX = (PW - 2 * PopBtnW - 20) / 2, PopOkX = PopCancelX + PopBtnW + 20;
	constexpr PixelNumber StopW = 3 * PopBtnW;											// wide emergency STOP

	// ---- AUTO popup ----
	PopupWindow *autoPopup = nullptr;
	CncGlyphField *autoTitleGlyph, *autoSideGlyph;
	StaticTextField *autoHeadText, *autoStatusText, *autoTargetText;
	StaticTextField *autoRowLetter[2], *autoRowValue[2];
	CncCheckField *autoRowCheck[2];
	String<48> autoHead, autoStatus;
	String<24> autoTarget;
	String<14> autoRowValueText[2];
	char autoRowAxis[2] = { 'X', 'Y' };				// 0 = row not used
	char autoRowLetterText[2][2] = { { 'X', 0 }, { 'Y', 0 } };

	// ---- Jog popup ----
	PopupWindow *jogPopup = nullptr;
	StaticTextField *jogTitleText;
	CncGlyphField *jogTitleGlyph;
	StaticTextField *jogText1, *jogText2, *jogLine1, *jogLine2;
	CncGlyphField *jogSideGlyph;
	ModernTextButton *jogAxisButtons[4];
	ModernTextButton *jogStepButtons[4];
	ModernTextButton *jogMinus, *jogPlus;
	ModernIconButton *jogCancel;
	ModernTextButton *jogOk;
	ButtonBase *jogSelectedAxis = nullptr, *jogSelectedStep = nullptr;
	String<24> jogTitle;
	String<24> jogOkLabel;
	String<40> jogT1, jogT2, jogL1, jogL2;
	String<12> jogMinusText, jogPlusText;
	uint8_t jogAxisIndex[4];							// axis number behind each axis button
	size_t jogNumAxes = 0, jogAxis = 0, jogStep = 1;
	uint32_t jogSeq = 0;
	const float JogSteps[4] = { 0.01f, 0.1f, 1.0f, 10.0f };
	constexpr size_t FirstOneShotJogStep = 2;				// 1 and 10: no repeats while held
	constexpr size_t FirstGuardedJogStep = 3;				// 10: not before the last move has finished
	constexpr size_t DefaultJogStep = 1;					// 0.1 when a prompt opens
	uint32_t lastGuardedJog = 0;
	const char * const JogStepNames[4] = { "0.01", "0.1", "1", "10" };
	const char AxisLetters[4] = { 'X', 'Y', 'Z', 'A' };

	void AddTitleTile(PopupWindow *p, StaticTextField *&titleText, CncGlyphField *&glyph, const char *text)
	{
		const Colour accent = Accent();
		DisplayField::SetDefaultFont(glcd28x32);
		DisplayField::SetDefaultColours(Text, Tile);
		titleText = new StaticTextField(TitleY + (TitleH - 32) / 2 + 2, TitleX + 12, 150, TextAlignment::Centre, text);
		p->AddField(titleText);
		glyph = new CncGlyphField(TitleY + 4, TitleX + TitleW - 60, 38, CncOrigin::TL, Text, Tile);
		p->AddField(glyph);
		p->AddField(new ModernCard(TitleY, TitleX, TitleW, TitleH, Tile, accent, true));
	}

	void CreateAutoPopup()
	{
		autoPopup = new PopupWindow(PH, PW, PageBg, Accent());
		StaticTextField *title;
		AddTitleTile(autoPopup, title, autoTitleGlyph, "PROBE");

		const PixelNumber ix = 25, iy = 72, iw = PW - 50, ih = PopBtnY - 12 - iy;
		const Colour accent = Accent();
		// [side icon] Side 1 of 2
		autoSideGlyph = new CncGlyphField(iy + 8, ix + 12, 36, CncOrigin::TL, Text, Tile);
		autoPopup->AddField(autoSideGlyph);
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Text, Tile);
		autoHeadText = new StaticTextField(iy + 16, ix + 58, iw - 78, TextAlignment::Left, "");
		autoPopup->AddField(autoHeadText);
		// status line (amber: in progress)
		DisplayField::SetDefaultColours(UTFT::fromRGB(224, 168, 0), Tile);
		autoStatusText = new StaticTextField(iy + 50, ix + 20, iw - 40, TextAlignment::Left, "");
		autoPopup->AddField(autoStatusText);
		// axis rows: letter, value, check mark
		for (size_t r = 0; r < 2; ++r)
		{
			const PixelNumber ry = iy + 94 + r * 40;
			DisplayField::SetDefaultFont(glcd28x32);
			DisplayField::SetDefaultColours(accent, Tile);
			autoRowLetter[r] = new StaticTextField(ry, ix + 22, 30, TextAlignment::Left, autoRowLetterText[r]);
			autoPopup->AddField(autoRowLetter[r]);
			DisplayField::SetDefaultColours(Text, Tile);
			autoRowValue[r] = new StaticTextField(ry, ix + 60, 150, TextAlignment::Right, "");
			autoPopup->AddField(autoRowValue[r]);
			autoRowCheck[r] = new CncCheckField(ry + 4, ix + 222, 24, ProbeGreen, Tile);
			autoPopup->AddField(autoRowCheck[r]);
		}
		autoPopup->AddField(new ModernCard(iy + 84, ix + 20, iw - 40, 2, Border, Border, false));	// divider
		// where the result goes
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, Tile);
		autoTargetText = new StaticTextField(iy + ih - 30, ix + 20, iw - 40, TextAlignment::Left, "");
		autoPopup->AddField(autoTargetText);
		autoPopup->AddField(new ModernCard(iy, ix, iw, ih, Tile, Border, true));

		// Wide emergency STOP instead of X: a running macro can only be stopped this way
		autoPopup->AddField(new ModernStopButton(PopBtnY, (PW - StopW) / 2, StopW, PopBtnH, evEmergencyStop));
	}

	void CreateJogPopup()
	{
		const Colour accent = Accent();
		jogPopup = new PopupWindow(PH, PW, PageBg, accent);
		AddTitleTile(jogPopup, jogTitleText, jogTitleGlyph, "PROBE");

		// Instruction: either "text1 [side glyph] text2" or two plain lines
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Text, PageBg);
		jogText1 = new StaticTextField(80, 25, 100, TextAlignment::Left, "");
		jogPopup->AddField(jogText1);
		jogSideGlyph = new CncGlyphField(72, 150, 36, CncOrigin::ML, accent, PageBg);
		jogPopup->AddField(jogSideGlyph);
		jogText2 = new StaticTextField(80, 190, 250, TextAlignment::Left, "");
		jogPopup->AddField(jogText2);
		jogLine1 = new StaticTextField(70, 20, PW - 40, TextAlignment::Centre, "");
		jogPopup->AddField(jogLine1);
		jogLine2 = new StaticTextField(94, 20, PW - 40, TextAlignment::Centre, "");
		jogPopup->AddField(jogLine2);

		// AXIS, STEP, MOVE
		const PixelNumber L = 25, W = PW - 50, labelW = 58;
		DisplayField::SetDefaultColours(Muted, PageBg);
		jogPopup->AddField(new StaticTextField(124 + 11, L, labelW, TextAlignment::Left, "AXIS"));
		jogPopup->AddField(new StaticTextField(172 + 11, L, labelW, TextAlignment::Left, "STEP"));
		DisplayField::SetDefaultColours(Text, Tile, Border, Tile, accent, accent, IconPaletteDark);
		for (size_t i = 0; i < 4; ++i)
		{
			jogAxisButtons[i] = new ModernTextButton(124, L + labelW, 60, 42, "", evCncProbeJogAxis, (int)i, glcd19x21, true);
			jogPopup->AddField(jogAxisButtons[i]);
		}
		const PixelNumber sw = (W - labelW - 3 * 8) / 4;
		for (size_t i = 0; i < 4; ++i)
		{
			jogStepButtons[i] = new ModernTextButton(172, L + labelW + i * (sw + 8), sw, 42, JogStepNames[i], evCncProbeJogStep, (int)i, glcd19x21, true);
			jogPopup->AddField(jogStepButtons[i]);
		}
		const PixelNumber jw = (W - 12) / 2;
		jogMinus = new ModernTextButton(220, L, jw, 56, "", evCncProbeJogMove, -1, glcd28x32, true);
		jogPopup->AddField(jogMinus);
		jogPlus = new ModernTextButton(220, L + jw + 12, jw, 56, "", evCncProbeJogMove, 1, glcd28x32, true);
		jogPopup->AddField(jogPlus);

		// X (M292 P1) and PROBE / READ / OK (M292 P0)
		DisplayField::SetDefaultColours(ButtonText, StopRed);
		jogCancel = new ModernIconButton(PopBtnY, PopCancelX, PopBtnW, PopBtnH, IconCancel, evCncProbeJogCancel);
		jogPopup->AddField(jogCancel);
		DisplayField::SetDefaultColours(ButtonText, ProbeGreen, ProbeGreen, ProbeGreen, ProbeGreen, ProbeGreen, IconPaletteDark);
		jogOk = new ModernTextButton(PopBtnY, PopOkX, PopBtnW, PopBtnH, "OK", evCncProbeJogOk, 0, glcd19x21, false);
		jogPopup->AddField(jogOk);
	}

	void UpdateJogMoveLabels()
	{
		jogMinusText.printf("- %s", JogStepNames[jogStep]);
		jogPlusText.printf("+ %s", JogStepNames[jogStep]);
		jogMinus->SetText(jogMinusText.c_str());
		jogPlus->SetText(jogPlusText.c_str());
	}

	CncOrigin SideGlyph(char side)
	{
		switch (side)
		{
		case 'L':	return CncOrigin::ML;
		case 'R':	return CncOrigin::MR;
		case 'T':	return CncOrigin::TC;
		case 'B':	return CncOrigin::BC;
		case 'Z':	return CncOrigin::ZTop;
		default:	return CncOrigin::None;
		}
	}

	// Place "text1 [glyph] text2" centred on one line
	void LayoutSentence()
	{
		DisplayField::SetDefaultFont(glcd19x21);
		const PixelNumber w1 = DisplayField::GetTextWidth(jogT1.c_str(), PW);
		const PixelNumber w2 = DisplayField::GetTextWidth(jogT2.c_str(), PW);
		constexpr PixelNumber gw = 36, gap = 6;
		const PixelNumber total = w1 + gap + gw + gap + w2;
		const PixelNumber x0 = (total < PW - 20) ? (PW - total) / 2 : 10;
		jogText1->SetPositionAndWidth(x0, w1 + 2);
		jogSideGlyph->SetPosition(x0 + w1 + gap, 72);
		jogText2->SetPositionAndWidth(x0 + w1 + gap + gw + gap, w2 + 2);
	}

	// Rows for the axes the origin measures
	void AutoRowsForOrigin(CncOrigin o)
	{
		switch (o)
		{
		case CncOrigin::ML: case CncOrigin::MR:		autoRowAxis[0] = 'X'; autoRowAxis[1] = 0; break;
		case CncOrigin::TC: case CncOrigin::BC:		autoRowAxis[0] = 'Y'; autoRowAxis[1] = 0; break;
		case CncOrigin::ZTop:						autoRowAxis[0] = 'Z'; autoRowAxis[1] = 0; break;
		default:									autoRowAxis[0] = 'X'; autoRowAxis[1] = 'Y'; break;
		}
	}

	void SetAutoStatus(const char *text)
	{
		autoStatus.copy(text);
		autoStatusText->SetValue(autoStatus.c_str(), true);
	}

	// Start state of the auto popup's info tile
	void ResetAutoTile()
	{
		autoSideGlyph->SetKind(origin);
		autoHead.copy(OriginNames[(size_t)origin]);
		autoHeadText->SetValue(autoHead.c_str(), true);
		SetAutoStatus("Probing ...");
		AutoRowsForOrigin(origin);
		for (size_t r = 0; r < 2; ++r)
		{
			const bool used = (autoRowAxis[r] != 0);
			autoRowLetterText[r][0] = autoRowAxis[r];
			autoRowLetter[r]->SetValue(autoRowLetterText[r], true);
			autoRowValueText[r].copy("-");
			autoRowValue[r]->SetValue(autoRowValueText[r].c_str(), true);
			autoRowLetter[r]->Show(used);
			autoRowValue[r]->Show(used);
			autoRowCheck[r]->Show(false);
		}
		autoTarget.printf("Result goes to %s", WcsNames[values.storeTo]);
		autoTargetText->SetValue(autoTarget.c_str(), true);
	}

	void CloseAll()
	{
		if (mgr.IsPopupActive(autoPopup) || mgr.IsPopupActive(jogPopup))
		{
			mgr.ClearAllPopups();
		}
	}
}

namespace CncProbe
{
	void Create()
	{
		const Colour accent = Accent();

		// Labels
		AddLabel(LabelY, GridX + 2, 150, "ORIGIN");
		AddLabel(LabelY, ParX + 2, 60, "MODE");						// 56 px of text: the mode name has the rest of the row
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(accent, PageBg);
		modeLabel = new StaticTextField(LabelY, ParX + 62, ParW - 62, TextAlignment::Right, "");	// SEMI-AUTO is 112+ px: it was cut in 118
		mgr.AddField(modeLabel);

		// Origin picker
		for (size_t i = 0; i < NumOrigins; ++i)
		{
			const bool big = (i >= 9);
			const PixelNumber x = GridX + (i % 3) * (BtnW + BtnGap);
			const PixelNumber y = big ? BigRowY : GridY + (i / 3) * (BtnH + BtnGap);
			originButtons[i] = new CncOriginButton(y, x, BtnW, big ? BigBtnH : BtnH, (CncOrigin)i,
													big ? BigLabels[i - 9] : nullptr, big, accent, evCncProbeOrigin, (int)i);
			mgr.AddField(originButtons[i]);
		}
		mgr.AddField(new ModernCard(LabelY, SepX, SeparatorH, PickBottom - LabelY, accent, accent, false));

		// Parameter tiles: AUTO 2 x 3 stacked, SEMI / MANUAL one row each
		{
			static const char * const autoLabels[6] = { "TIP \xC3\x98", "CLEAR D", "SEARCH", "Z DEPTH", "STOCK", "SAVE TO" };	// 97 px fit: CLEARANCE and STORE TO were cut
			static const ParamId autoIds[6] = { PTip, PAutoClear, PAutoSearch, PZDepth, PStock, PStoreTo };
			for (size_t k = 0; k < 6; ++k)
			{
				AddModeTile(0, GridY + (k / 2) * (AutoTileH + TileGap), ParX + (k % 2) * (AutoTileW + TileGap),
							AutoTileW, AutoTileH, autoLabels[k], autoIds[k], true);
			}
			static const char * const semiLabels[4] = { "TIP \xC3\x98", "CLEARANCE", "SEARCH", "STORE TO" };
			static const ParamId semiIds[4] = { PTip, PSemiClear, PSemiSearch, PStoreTo };
			for (size_t k = 0; k < 4; ++k)
			{
				AddModeTile(1, GridY + k * (RowTileH + RowTileGap), ParX, ParW, RowTileH, semiLabels[k], semiIds[k], false);
			}
			static const char * const manLabels[2] = { "TOOL \xC3\x98", "STORE TO" };
			static const ParamId manIds[2] = { PToolDia, PStoreTo };
			for (size_t k = 0; k < 2; ++k)
			{
				AddModeTile(2, GridY + k * (RowTileH + RowTileGap), ParX, ParW, RowTileH, manLabels[k], manIds[k], false);
			}
		}

		// Bottom row: probe signal + RUN PROBE, or START (manual)
		probeTile = new CncStatusTile(BottomY, Margin, StatusW, BottomH, "PROBE");
		mgr.AddField(probeTile);
		DisplayField::SetDefaultColours(ButtonText, ProbeGreen, ProbeGreen, ProbeGreen, ProbeGreen, ProbeGreen, IconPaletteDark);
		runButton = new ModernTextButton(BottomY, Margin + StatusW + 8, ContentW - StatusW - 8, BottomH, "RUN PROBE", evCncProbeRun, 0, glcd28x32, false);
		mgr.AddField(runButton);
		startButton = new ModernTextButton(BottomY, Margin, ContentW, BottomH, "START", evCncProbeRun, 0, glcd28x32, false);
		mgr.AddField(startButton);

		// Initial state
		selectedOrigin = originButtons[(size_t)origin];
		originButtons[(size_t)origin]->Press(true, 0);
		for (size_t i = 0; i < NumParams; ++i)
		{
			UpdateTileText((ParamId)i);
		}
		UpdateProbeTile();
		ShowModeLayout();

		CreateAutoPopup();
		CreateJogPopup();
	}

	// ---------------------------------------------------------------------
	// Page touches
	// ---------------------------------------------------------------------
	bool ProcessTouch(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncProbeOrigin:
			origin = (CncOrigin)bp.GetIParam();
			Select(selectedOrigin, bp.GetButton());
			return true;

		case evCncProbeParam:
			EditParam((ParamId)bp.GetIParam());
			return true;

		case evCncProbeRun:
			{
				if (jobActive)
				{
					Refuse(CNC_LOCKED_JOB_ACTIVE);
					return true;
				}
				if (GetStatus() != OM::PrinterStatus::idle)
				{
					Refuse("Wait until the machine is idle.");	// e.g. a probe macro still running
					return true;
				}
				if (CncControl::RefuseUnhomed())
				{
					return true;
				}
				if (mode != Mode::Manual && haveProbeValue && probeClosed)
				{
					Refuse("Probe is closed (triggered).\nCheck the probe and its wiring.");
					return true;
				}
				const char * const code = OriginCodes[(size_t)origin];
				runOrigin = origin;
				const unsigned int w = values.storeTo + 1u;
				switch (mode)
				{
				case Mode::Auto:
					SerialIo::Sendf("M98 P\"0:/sys/probe/probe_auto.g\" O\"%s\" D%.3f C%.3f S%.3f Z%.3f L%.3f B%.3f W%u\n",
									code, (double)values.tip, (double)values.autoClear, (double)values.autoSearch,
									(double)values.zDepth, (double)values.stockX, (double)values.stockY, w);
					// Auto popup: the machine now probes on its own
					CncPopup::Close();
					autoBusySeen = false;
					autoStartTime = SystemTick::GetTickCount();
					autoTitleGlyph->SetKind(origin);
					ResetAutoTile();
					mgr.ClearAllPopups();
					mgr.SetPopup(autoPopup, PopupUnderDroX, PopupUnderDroY);
					break;
				case Mode::Semi:
					SerialIo::Sendf("M98 P\"0:/sys/probe/probe_semi.g\" O\"%s\" D%.3f C%.3f S%.3f W%u\n",
									code, (double)values.tip, (double)values.semiClear, (double)values.semiSearch, w);
					break;
				default:
					SerialIo::Sendf("M98 P\"0:/sys/probe/probe_manual.g\" O\"%s\" D%.3f W%u\n",
									code, (double)values.toolDia, w);
					break;
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
		case evCncProbeOrigin:					// selection stays
		case evCncProbeParam:					// opens a popup
		case evCncProbeRun:
			return true;
		default:
			return false;
		}
	}

	// ---------------------------------------------------------------------
	// Jog popup touches (M291 with axis controls, on any page)
	// ---------------------------------------------------------------------
	bool ProcessPopupTouch(ButtonPress bp)
	{
		if (!mgr.IsPopupActive(jogPopup))
		{
			return false;
		}
		switch ((Event)bp.GetEvent())
		{
		case evCncProbeJogAxis:
			if ((size_t)bp.GetIParam() < jogNumAxes)
			{
				jogAxis = (size_t)bp.GetIParam();
				Select(jogSelectedAxis, bp.GetButton());
			}
			return true;

		case evCncProbeJogStep:
			jogStep = (size_t)bp.GetIParam();
			Select(jogSelectedStep, bp.GetButton());
			UpdateJogMoveLabels();
			return true;

		case evCncProbeJogMove:
			if (jogNumAxes != 0)
			{
				if (jogStep >= FirstOneShotJogStep)
				{
					IgnoreRepeats();				// 1 and 10: one move per tap, holding must not queue more
					if (jogStep >= FirstGuardedJogStep && !GuardedJogAllowed(lastGuardedJog, true))
					{
						// No alert here: it would replace this prompt while the macro still waits for it.
						// The tap is dropped, as with the dial.
						return true;
					}
				}
				mgr.Press(bp, true);
				const float d = JogSteps[jogStep] * (float)bp.GetIParam();
				SerialIo::Sendf("G91\nG1 %c%.3f F%u\nG90\n", AxisLetters[jogAxisIndex[jogAxis]], (double)d, CncControl::JogFeed());
			}
			return true;

		case evCncProbeJogOk:
			SerialIo::Sendf("M292 P0 S%lu\n", (unsigned long)jogSeq);
			mgr.ClearAllPopups();
			return true;

		case evCncProbeJogCancel:
			SerialIo::Sendf("M292 P1 S%lu\n", (unsigned long)jogSeq);
			mgr.ClearAllPopups();
			return true;

		default:
			return false;
		}
	}

	bool ProcessPopupRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncProbeJogMove:
			mgr.Press(bp, false);
			return true;
		case evCncProbeJogAxis:
		case evCncProbeJogStep:
		case evCncProbeJogOk:
		case evCncProbeJogCancel:
			return true;
		default:
			return false;
		}
	}

	// ---------------------------------------------------------------------
	// Machine data
	// ---------------------------------------------------------------------
	void SetProbeValue(int value)
	{
		const bool closed = (value >= 500);
		if (!haveProbeValue || closed != probeClosed)
		{
			haveProbeValue = true;
			probeClosed = closed;
			UpdateProbeTile();
		}
	}

	void SetJobState(bool running, bool active)
	{
		jobRunning = running;
		jobActive = active;
	}

	void SetNumAxes(size_t n)
	{
		numAxes = n;
	}

	void SetMode(Mode m)
	{
		if (m != mode)
		{
			mode = m;
			ShowModeLayout();
		}
	}

	bool ShowJogPrompt(const char *title, const char *text, uint32_t controls, bool withCancel, uint32_t seq)
	{
		jogSeq = seq;
		SetJogWheelLook(false);								// the dial sleeps when a prompt opens (CncControl::Spin also sees the new context)

		// Axis buttons for the axes the prompt allows (X Y Z A)
		jogNumAxes = 0;
		for (size_t a = 0; a < 4 && a < max<size_t>(numAxes, 3); ++a)
		{
			if (controls & (1u << a))
			{
				jogAxisIndex[jogNumAxes++] = (uint8_t)a;
			}
		}
		if (jogNumAxes == 0)
		{
			return false;								// no axis controls: the standard message popup
		}
		static const char * const axisNames[4] = { "X", "Y", "Z", "A" };
		const PixelNumber L = 25 + 58, W = PW - 50 - 58;
		const PixelNumber aw = (W - (jogNumAxes - 1) * 8) / jogNumAxes;
		for (size_t i = 0; i < 4; ++i)
		{
			if (i < jogNumAxes)
			{
				jogAxisButtons[i]->SetText(axisNames[jogAxisIndex[i]]);
				jogAxisButtons[i]->SetPositionAndWidth(L + i * (aw + 8), aw);
				jogAxisButtons[i]->Press(false, 0);
			}
			jogAxisButtons[i]->Show(i < jogNumAxes);
		}
		jogAxis = 0;
		jogAxisButtons[0]->Press(true, 0);
		jogSelectedAxis = jogAxisButtons[0];
		jogStep = DefaultJogStep;							// every prompt starts small
		for (size_t i = 0; i < 4; ++i)
		{
			jogStepButtons[i]->Press(i == jogStep, 0);
		}
		jogSelectedStep = jogStepButtons[jogStep];
		UpdateJogMoveLabels();

		// Probe prompt ("PROBE:<side>" semi-automatic, "READ:<side>" manual) or any other prompt
		const bool semiPrompt = (strncmp(title, "PROBE:", 6) == 0);
		const bool manualPrompt = (strncmp(title, "READ:", 5) == 0);
		const bool probePrompt = semiPrompt || manualPrompt;
		const CncOrigin side = semiPrompt ? SideGlyph(title[6]) : manualPrompt ? SideGlyph(title[5]) : CncOrigin::None;
		const bool sentence = probePrompt && side != CncOrigin::None;
		if (sentence)
		{
			const bool top = (side == CncOrigin::ZTop);
			jogTitle.copy("PROBE");
			jogTitleGlyph->SetKind(runOrigin);
			if (semiPrompt)
			{
				jogT1.copy("Move near");
				jogT2.copy(top ? "top of stock, press PROBE" : "side of stock, press PROBE");
				jogOkLabel.copy("PROBE");
			}
			else
			{
				jogT1.copy("Touch");
				jogT2.copy(top ? "top of stock and press READ" : "side of stock and press READ");
				jogOkLabel.copy("READ");
			}
			jogSideGlyph->SetKind(side);
			jogText1->SetValue(jogT1.c_str(), true);
			jogText2->SetValue(jogT2.c_str(), true);
			LayoutSentence();
			jogL1.Clear();
			jogL2.Clear();
		}
		else
		{
			jogTitle.copy(probePrompt ? "PROBE" : title);
			jogTitleGlyph->SetKind(probePrompt ? runOrigin : CncOrigin::None);
			jogOkLabel.copy("OK");
			jogT1.Clear();
			jogT2.Clear();
			// the text on up to two lines, split at the last space that fits ~34 characters
			const char *t = text;
			size_t len = strlen(t), cut = len;
			if (len > 34)
			{
				cut = 34;
				while (cut > 0 && t[cut] != ' ')
				{
					--cut;
				}
				if (cut == 0)
				{
					cut = 34;
				}
			}
			jogL1.copy(t, cut);
			jogL2.copy((cut < len) ? t + cut + (t[cut] == ' ' ? 1 : 0) : "");
		}
		// Probe prompts: "PROBE" + origin glyph; other prompts: their title over the whole tile
		if (probePrompt)
		{
			jogTitleText->SetPositionAndWidth(TitleX + 12, 150);
		}
		else
		{
			jogTitleText->SetPositionAndWidth(TitleX + 6, TitleW - 12);
		}
		jogTitleGlyph->Show(probePrompt);
		jogTitleText->SetValue(jogTitle.c_str(), true);
		jogText1->Show(sentence);
		jogText2->Show(sentence);
		jogSideGlyph->Show(sentence);
		jogLine1->SetValue(jogL1.c_str(), true);
		jogLine2->SetValue(jogL2.c_str(), true);
		jogLine1->Show(!sentence);
		jogLine2->Show(!sentence);
		jogOk->SetText(jogOkLabel.c_str());

		// X only when the prompt can be cancelled (M291 S3); otherwise the OK button is centred
		jogCancel->Show(withCancel);
		jogOk->SetPositionAndWidth(withCancel ? PopOkX : (PW - PopBtnW) / 2, PopBtnW);

		CncPopup::Close();
		mgr.ClearAllPopups();
		mgr.SetPopup(jogPopup, PopupUnderDroX, PopupUnderDroY);
		return true;
	}

	bool AutoProbeRunning()
	{
		return mgr.IsPopupActive(autoPopup);
	}

	void AutoMessage(const char *title, const char *text)
	{
		if (strncmp(title, "PROBE:", 6) == 0 && title[6] != 0)
		{
			// "PROBE:<side> <n>/<m>": side icon + "Side n of m", the text is the status line
			const CncOrigin side = SideGlyph(title[6]);
			if (side != CncOrigin::None)
			{
				autoSideGlyph->SetKind(side);
			}
			const char *p = strchr(title, ' ');
			int n = 0, m = 0;
			if (p != nullptr)
			{
				++p;
				while (*p >= '0' && *p <= '9') { n = n * 10 + (*p++ - '0'); }
				if (*p == '/')
				{
					++p;
					while (*p >= '0' && *p <= '9') { m = m * 10 + (*p++ - '0'); }
				}
			}
			if (n > 0 && m > 0)
			{
				autoHead.printf("Side %d of %d", n, m);
				autoHeadText->SetValue(autoHead.c_str(), true);
			}
			SetAutoStatus(text);
		}
		else if (strncmp(title, "PROBE=", 6) == 0 && title[6] != 0)
		{
			// "PROBE=<axis>": the measured value of that axis, with a check mark
			for (size_t r = 0; r < 2; ++r)
			{
				if (autoRowAxis[r] == title[6])
				{
					autoRowValueText[r].copy(text);
					autoRowValue[r]->SetValue(autoRowValueText[r].c_str(), true);
					autoRowCheck[r]->Show(true);
				}
			}
		}
		else
		{
			SetAutoStatus(text);
		}
	}

	bool JogPromptOpen()
	{
		return mgr.IsPopupActive(jogPopup);
	}

	void JogWheel(int clicks)
	{
		if (!mgr.IsPopupActive(jogPopup) || jogNumAxes == 0)
		{
			return;
		}
		if (jogStep >= FirstGuardedJogStep)
		{
			clicks = (clicks > 0) ? 1 : -1;				// 10: exactly one step per command, never clicks x step
		}
		else if (clicks > 5)
		{
			clicks = 5;										// a fast spin is cut, not queued
		}
		else if (clicks < -5)
		{
			clicks = -5;
		}
		if (jogStep >= FirstGuardedJogStep && !GuardedJogAllowed(lastGuardedJog, true))
		{
			return;											// the last large move is still running: this turn is dropped
		}
		const float d = JogSteps[jogStep] * (float)clicks;
		SerialIo::Sendf("G91\nG1 %c%.3f F%u\nG90\n", AxisLetters[jogAxisIndex[jogAxis]], (double)d, CncControl::JogFeed());
	}

	void JogWheelOk()
	{
		if (mgr.IsPopupActive(jogPopup))
		{
			SerialIo::Sendf("M292 P0 S%lu\n", (unsigned long)jogSeq);
			mgr.ClearAllPopups();
		}
	}

	void SetJogWheelLook(bool awake)
	{
		if (jogMinus != nullptr && jogPlus != nullptr)
		{
			const Colour c = awake ? Accent() : Text;
			jogMinus->SetColours(c, Tile);
			jogPlus->SetColours(c, Tile);
		}
	}

	void ClosePopups()
	{
		CloseAll();
	}

	void CloseJogPrompt()
	{
		if (mgr.IsPopupActive(jogPopup))
		{
			mgr.ClearAllPopups();
		}
	}

	void StatusChanged(bool idle, bool busy)
	{
		if (!mgr.IsPopupActive(autoPopup))
		{
			return;
		}
		if (busy)
		{
			autoBusySeen = true;
		}
		else if (idle && autoBusySeen)
		{
			mgr.ClearAllPopups();						// the macro ended without a result message
		}
	}

	void Spin()
	{
		// The macro never showed up as running (too short, or not started): don't leave the
		// popup open with only an emergency STOP to get out of it
		if (mgr.IsPopupActive(autoPopup) && !autoBusySeen
			&& SystemTick::GetTickCount() - autoStartTime > AutoStartTimeout
			&& GetStatus() == OM::PrinterStatus::idle)
		{
			mgr.ClearAllPopups();
		}
	}

	void Error(const char *text)
	{
		if (mgr.IsPopupActive(autoPopup))
		{
			mgr.ClearAllPopups();
			CncPopup::Alert(text);
		}
	}
}

// End
