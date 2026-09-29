/*
 * CncUserInterface.cpp
 *
 * Portrait CNC user interface for PanelDue 800x480 panels (5" / 7").
 * Implements the UI:: interface from UserInterface.hpp; compiled instead of the
 * printer UserInterface.cpp.
 *
 * This file: the frame shared by all pages (bottom navigation + full DRO), page
 * switching, and the UI:: interface. Pages live in their own files:
 *   CncControlPage.cpp   CONTROL
 *   CncWcsPage.cpp       WCS (OFFSETS; PROBE comes later)
 *   CncToolsPage.cpp     WCS > TOOLS
 *   CncProbePage.cpp     WCS > PROBE, auto probe popup, M291 jog popup
 *   CncJobPage.cpp       JOB (JOB STATUS, JOB LIST)
 *   CncKeyboard.cpp      shared keyboard (JOB LIST search, SYSTEM > CONSOLE)
 *   CncSystemPage.cpp    SYSTEM (ALERT, CONSOLE; SETTINGS comes later)
 *   CncMacrosPage.cpp    MACROS (list / grid)
 *   CncPopups.cpp        standard popup + standard numpad, shared by all pages
 * CONTROL and WCS use the full DRO; JOB, MACROS and SYSTEM the compact (read-only) DRO.
 *
 * All geometry comes from CncLayout.hpp.
 */

#include <UI/UserInterface.hpp>

#include "Configuration.hpp"
#include "FlashData.hpp"
#include "PanelDue.hpp"
#include "Version.hpp"
#include "Hardware/SerialIo.hpp"
#include "Hardware/SysTick.hpp"
#include "Icons/Icons.hpp"
#include <UI/MessageLog.hpp>
#include <UI/UserInterfaceConstants.hpp>
#include <UI/Cnc/CncLayout.hpp>
#include <UI/Cnc/CncWidgets.hpp>
#include <UI/Cnc/CncCommon.hpp>
#include <UI/Cnc/CncControlPage.hpp>
#include <UI/Cnc/CncWcsPage.hpp>
#include <UI/Cnc/CncPopups.hpp>
#include <UI/Cnc/CncToolsPage.hpp>
#include <UI/Cnc/CncProbePage.hpp>
#include <UI/Cnc/CncJobPage.hpp>
#include <UI/Cnc/CncKeyboard.hpp>
#include <UI/Cnc/CncSystemPage.hpp>
#include <UI/Cnc/CncMacrosPage.hpp>
#include <UI/Cnc/CncSettingsPage.hpp>

using namespace CncLayout;
using namespace Cnc;

// ---------------------------------------------------------------------------
// Globals the rest of the firmware expects the UI module to provide
// ---------------------------------------------------------------------------
MainWindow mgr;
TextField *fwVersionField, *userCommandField, *ipAddressField;
IntegerField *freeMem;
StaticTextField *touchCalibInstruction, *debugField;
StaticTextField *messageTextFields[numMessageRows], *messageTimeFields[numMessageRows];
DrawDirect *fpThumbnail = nullptr;
const StringTable * strings = &LanguageTables[0];

namespace
{
	// -----------------------------------------------------------------------
	// Fields
	// -----------------------------------------------------------------------
	constexpr size_t DroAxes = 4;						// X Y Z + A; the 4th row is shown only when the machine has 4 axes
	const char AxisLetters[DroAxes] = { 'X', 'Y', 'Z', 'A' };
	const char * const AxisNames[DroAxes] = { "X", "Y", "Z", "A" };

	DisplayField *cncRoot = nullptr;
	FloatField *droWork[DroAxes];
	FloatField *droMachine[DroAxes];
	StaticTextField *droLetter[DroAxes];
	ModernCard *droWorkCard[DroAxes], *droMachCard[DroAxes];
	bool droFourAxes = false;							// full DRO layout: 3 rows of 68 px or 4 rows of 50 px
	StaticTextField *droTool;
	String<8> droToolText;
	IntegerField *droSpindle, *droFeed;
	ModernCard *droToolCard, *droSpindleCard;
	StaticTextField *droSpindleLetter;
	bool spindleOutline = false;
	StaticTextField *droWorkHeader;
	String<12> droWorkHeaderText;
	CncZeroButton *zeroButtons[DroAxes];
	bool jobLocked = false;

	CncNavButton *navTiles[NavCount - 1];
	CncNavButton *compactNavTiles[NavCount - 1];			// same tiles in the compact frame

	// Compact DRO (JOB, MACROS, SYSTEM), read-only
	DisplayField *compactRoot = nullptr;
	constexpr size_t CompactAxes = 4;
	StaticTextField *cmpLetter[CompactAxes];
	FloatField *cmpWork[CompactAxes];
	ModernCard *cmpAxisCard[CompactAxes];
	StaticTextField *cmpTool, *cmpSpindleLetter, *cmpFeedLetter;
	IntegerField *cmpSpindle, *cmpFeed;
	ModernCard *cmpToolCard, *cmpSpindleCard, *cmpFeedCard;
	StaticTextField *cmpHeader;
	bool compactFourAxes = false;

	ButtonPress currentButton;
	bool ignoreUntilRelease = false;		// a popup opened under the finger: repeats must not hit it
	bool stopLatched = false;				// STOP done: its repeats must not stop (and reset) again
	const PopupWindow *popupAtTouch = nullptr;	// popup shown when the last touch was processed
	uint32_t infoTimeout = 0;

	// Full DRO rows: 3 axes = 3 rows of 68 px, 4 axes = 4 rows of 50 px. Both end at the same y, so the
	// T / S / F row and everything below stay where they are. Only the moving is done here; the caller
	// redraws (mgr.Refresh(true)) when the layout changes while the DRO is shown.
	void LayoutFullDro()
	{
		const PixelNumber bigH = 32, smallH = 21;		// glcd28x32 / glcd19x21 row heights
		const PixelNumber h = droFourAxes ? DroRowH4 : DroRowH3;
		const PixelNumber gap = droFourAxes ? DroGap4 : DroGap3;
		for (size_t i = 0; i < DroAxes; ++i)
		{
			const PixelNumber y = DroRowsTop + i * (h + gap);
			droLetter[i]->SetPosition(DroAxisLabelX, y + (h - bigH) / 2);
			droWork[i]->SetPosition(DroWorkX + 6, y + (h - bigH) / 2);
			droWorkCard[i]->SetPosition(DroWorkX, y);
			droWorkCard[i]->SetHeight(h);
			droMachine[i]->SetPosition(DroMachX + 6, y + (h - smallH) / 2);
			droMachCard[i]->SetPosition(DroMachX, y);
			droMachCard[i]->SetHeight(h);
			zeroButtons[i]->SetPosition(DroZeroX, y);
			zeroButtons[i]->SetHeight(h);
		}
		const bool a = droFourAxes;
		droLetter[3]->Show(a);
		droWork[3]->Show(a);
		droWorkCard[3]->Show(a);
		droMachine[3]->Show(a);
		droMachCard[3]->Show(a);
		zeroButtons[3]->Show(a);
	}

	void CreateDro()
	{
		const Colour accent = Accent();
		const PixelNumber bigH = 32, smallH = 21;		// glcd28x32 / glcd19x21 row heights

		// Column headers
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, PageBg);
		droWorkHeaderText.copy("WORK G54");
		droWorkHeader = new StaticTextField(DroHeaderY - 2, DroWorkX, DroWorkW, TextAlignment::Centre, droWorkHeaderText.c_str());
		mgr.AddField(droWorkHeader);
		mgr.AddField(new StaticTextField(DroHeaderY - 2, DroMachX, DroMachW, TextAlignment::Centre, "MACHINE"));
		mgr.AddField(new StaticTextField(DroHeaderY - 2, DroZeroX, DroZeroW, TextAlignment::Centre, "ZERO"));

		// Rows are created with the 3-axis geometry; LayoutFullDro() (end of this function and whenever the
		// A axis appears or goes) places them for 3 or 4 axes and shows the 4th row only with 4 axes
		for (size_t i = 0; i < DroAxes; ++i)
		{
			const PixelNumber y = DroRowsTop + i * (DroRowH3 + DroGap3);

			// Axis letter in accent
			DisplayField::SetDefaultFont(glcd28x32);
			DisplayField::SetDefaultColours(accent, PageBg);
			droLetter[i] = new StaticTextField(y + (DroRowH3 - bigH) / 2, DroAxisLabelX, DroAxisLabelW, TextAlignment::Centre, AxisNames[i]);
			mgr.AddField(droLetter[i]);

			// Work position (live), large
			DisplayField::SetDefaultColours(Text, Tile);
			droWork[i] = new FloatField(y + (DroRowH3 - bigH) / 2, DroWorkX + 6, DroWorkW - 18, TextAlignment::Right, 3);
			mgr.AddField(droWork[i]);
			droWorkCard[i] = new ModernCard(y, DroWorkX, DroWorkW, DroRowH3, Tile, Border, true);
			mgr.AddField(droWorkCard[i]);

			// Machine position (live), smaller and muted
			DisplayField::SetDefaultFont(glcd19x21);
			DisplayField::SetDefaultColours(Muted, Tile);
			droMachine[i] = new FloatField(y + (DroRowH3 - smallH) / 2, DroMachX + 6, DroMachW - 18, TextAlignment::Right, 3);
			mgr.AddField(droMachine[i]);
			droMachCard[i] = new ModernCard(y, DroMachX, DroMachW, DroRowH3, Tile, Border, true);
			mgr.AddField(droMachCard[i]);

			// Zero button: current position becomes 0 in the active work offset
			zeroButtons[i] = new CncZeroButton(y, DroZeroX, DroZeroW, DroRowH3, AxisLetters[i], evCncZero, (int)i);
			mgr.AddField(zeroButtons[i]);
		}
		LayoutFullDro();

		// Tool / spindle / feed row
		const PixelNumber bigY = DroTsfY + (DroTsfH - bigH) / 2;
		const PixelNumber smallY = DroTsfY + (DroTsfH - smallH) / 2 + 2;

		DisplayField::SetDefaultFont(glcd28x32);
		DisplayField::SetDefaultColours(Text, Tile);
		droToolText.copy("T-");
		droTool = new StaticTextField(bigY, DroToolX + 6, DroToolW - 12, TextAlignment::Centre, droToolText.c_str());
		mgr.AddField(droTool);
		droToolCard = new ModernCard(DroTsfY, DroToolX, DroToolW, DroTsfH, Tile, Border, true);
		mgr.AddField(droToolCard);

		droSpindle = new IntegerField(bigY, DroSpindleX + 34, DroSpindleW - 46, TextAlignment::Right);
		mgr.AddField(droSpindle);
		droFeed = new IntegerField(bigY, DroFeedX + 34, DroFeedW - 46, TextAlignment::Right);
		mgr.AddField(droFeed);

		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, Tile);
		droSpindleLetter = new StaticTextField(smallY, DroSpindleX + 12, 20, TextAlignment::Left, "S");
		mgr.AddField(droSpindleLetter);
		mgr.AddField(new StaticTextField(smallY, DroFeedX + 12, 20, TextAlignment::Left, "F"));
		droSpindleCard = new ModernCard(DroTsfY, DroSpindleX, DroSpindleW, DroTsfH, Tile, Border, true);
		mgr.AddField(droSpindleCard);
		AddCard(DroTsfY, DroFeedX, DroFeedW, DroTsfH);

		// T tile switches the spindle on / standby; S and F tiles open the standard numpad
		mgr.AddField(new ModernTouchArea(DroTsfY, DroToolX, DroToolW, DroTsfH, evCncDroTool, 0));
		mgr.AddField(new ModernTouchArea(DroTsfY, DroSpindleX, DroSpindleW, DroTsfH, evCncDroSpindle, 0));
		mgr.AddField(new ModernTouchArea(DroTsfY, DroFeedX, DroFeedW, DroTsfH, evCncDroFeed, 0));

		// Accent separator under the DRO
		mgr.AddField(new ModernCard(DroSeparatorY, Margin, ContentW, SeparatorH, accent, accent, false));
	}

	// The nav bar exists twice (full-DRO frame and compact frame): a field can only be in one list
	void CreateNav(CncNavButton *tiles[])
	{
		const Colour accent = Accent();
		static const CncNavIcon icons[NavCount - 1] =
			{ CncNavIcon::Joystick, CncNavIcon::Crosshair, CncNavIcon::List, CncNavIcon::SdCard, CncNavIcon::Spanner };

		for (unsigned int i = 0; i < NavCount - 1; ++i)
		{
			tiles[i] = new CncNavButton(NavY, NavX(i), NavTile, icons[i], accent, evCncNav, (int)i);
			mgr.AddField(tiles[i]);
		}
		mgr.AddField(new ModernStopButton(NavY, NavX(NavCount - 1), NavTile, NavTile, evEmergencyStop));
	}

	// Row 2 of the compact DRO: A (4 axes only), T, S, F
	void LayoutCompactRow2()
	{
		using namespace CncLayout::Compact;
		const bool four = compactFourAxes;
		const PixelNumber tx = four ? T4X : T3X, tw = four ? T4W : T3W;
		const PixelNumber sx = four ? S4X : S3X, sw = four ? S4W : S3W;
		const PixelNumber fx = four ? F4X : F3X, fw = four ? F4W : F3W;
		cmpToolCard->SetPositionAndWidth(tx, tw);
		cmpTool->SetPositionAndWidth(tx + 4, tw - 8);
		cmpSpindleCard->SetPositionAndWidth(sx, sw);
		cmpSpindleLetter->SetPositionAndWidth(sx + 10, 16);
		cmpSpindle->SetPositionAndWidth(sx + 28, sw - 38);
		cmpFeedCard->SetPositionAndWidth(fx, fw);
		cmpFeedLetter->SetPositionAndWidth(fx + 10, 16);
		cmpFeed->SetPositionAndWidth(fx + 28, fw - 38);
		cmpLetter[3]->Show(four);
		cmpWork[3]->Show(four);
		cmpAxisCard[3]->Show(four);
	}

	void CreateCompactDro()
	{
		using namespace CncLayout::Compact;
		const Colour accent = Accent();
		static const char * const letters[CompactAxes] = { "X", "Y", "Z", "A" };

		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, PageBg);
		cmpHeader = new StaticTextField(HeaderY, 32, 200, TextAlignment::Left, droWorkHeaderText.c_str());
		mgr.AddField(cmpHeader);

		for (size_t i = 0; i < CompactAxes; ++i)
		{
			const PixelNumber lx = (i < 3) ? AxisX(i) : AxisX(0);
			const PixelNumber y = (i < 3) ? Row1Y : Row2Y;
			DisplayField::SetDefaultFont(glcd28x32);
			DisplayField::SetDefaultColours(accent, PageBg);
			cmpLetter[i] = new StaticTextField(y + (RowH - 32) / 2, lx, LetterW, TextAlignment::Centre, letters[i]);
			mgr.AddField(cmpLetter[i]);
			DisplayField::SetDefaultColours(Text, Tile);
			cmpWork[i] = new FloatField(y + (RowH - 32) / 2, lx + LetterW + 4, AxisTileW - 10, TextAlignment::Right, 3);
			mgr.AddField(cmpWork[i]);
			cmpAxisCard[i] = new ModernCard(y, lx + LetterW, AxisTileW, RowH, Tile, Border, true);
			mgr.AddField(cmpAxisCard[i]);
		}

		// T, S, F (positions set by LayoutCompactRow2)
		DisplayField::SetDefaultFont(glcd28x32);
		DisplayField::SetDefaultColours(Text, Tile);
		cmpTool = new StaticTextField(Row2Y + (RowH - 32) / 2, T3X, T3W, TextAlignment::Centre, droToolText.c_str());
		mgr.AddField(cmpTool);
		cmpToolCard = new ModernCard(Row2Y, T3X, T3W, RowH, Tile, Border, true);
		mgr.AddField(cmpToolCard);
		cmpSpindle = new IntegerField(Row2Y + (RowH - 32) / 2, S3X, S3W, TextAlignment::Right);
		mgr.AddField(cmpSpindle);
		cmpFeed = new IntegerField(Row2Y + (RowH - 32) / 2, F3X, F3W, TextAlignment::Right);
		mgr.AddField(cmpFeed);
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, Tile);
		cmpSpindleLetter = new StaticTextField(Row2Y + (RowH - 21) / 2 + 2, S3X, 16, TextAlignment::Left, "S");
		mgr.AddField(cmpSpindleLetter);
		cmpFeedLetter = new StaticTextField(Row2Y + (RowH - 21) / 2 + 2, F3X, 16, TextAlignment::Left, "F");
		mgr.AddField(cmpFeedLetter);
		cmpSpindleCard = new ModernCard(Row2Y, S3X, S3W, RowH, Tile, Border, true);
		mgr.AddField(cmpSpindleCard);
		cmpFeedCard = new ModernCard(Row2Y, F3X, F3W, RowH, Tile, Border, true);
		mgr.AddField(cmpFeedCard);

		mgr.AddField(new ModernCard(CompactSeparatorY, Margin, ContentW, SeparatorH, accent, accent, false));
		LayoutCompactRow2();
	}

	// Pages: index = nav tile (CONTROL, WCS, JOB, MACROS, SYSTEM)
	DisplayField *pageRoots[NavCount - 1];
	unsigned int currentPage = 0;

	void ShowPage(unsigned int page)
	{
		currentPage = page;
		for (unsigned int i = 0; i < NavCount - 1; ++i)
		{
			navTiles[i]->Press(i == page, 0);
			compactNavTiles[i]->Press(i == page, 0);
		}
		CncKeyboard::Close(false);					// belongs to the page left (its Spin notices); full refresh follows
		if (page != 3)
		{
			CncMacros::CancelPick();				// SETTINGS > CUSTOM n > MACRO left without a pick
		}
		if (page == 2)
		{
			CncJob::PageShown();
		}
		else if (page == 3)
		{
			CncMacros::PageShown();
		}
		mgr.SetRoot((page == 1) ? CncWcs::CurrentRoot() : (page == 2) ? CncJob::CurrentRoot()
					: (page == 4) ? CncSystem::CurrentRoot() : pageRoots[page]);
		mgr.Refresh(true);
	}

	bool CompactPageShown()
	{
		return currentPage >= 2;
	}

	// T and S tiles: accent outline while the spindle runs (like an active heater on the printer UI).
	// The card repaint covers the text, so redraw the text too.
	void SetSpindleOutline(bool on)
	{
		if (on != spindleOutline)
		{
			spindleOutline = on;
			const Colour c = on ? Accent() : Border;
			droToolCard->SetBorderColour(c);
			droSpindleCard->SetBorderColour(c);
			cmpToolCard->SetBorderColour(c);
			cmpSpindleCard->SetBorderColour(c);
			mgr.Redraw(droToolCard);
			mgr.Redraw(droTool);
			mgr.Redraw(droSpindleCard);
			mgr.Redraw(droSpindleLetter);
			mgr.Redraw(droSpindle);
			mgr.Redraw(cmpToolCard);
			mgr.Redraw(cmpTool);
			mgr.Redraw(cmpSpindleCard);
			mgr.Redraw(cmpSpindleLetter);
			mgr.Redraw(cmpSpindle);
		}
	}

	// M291 answers
	uint32_t alertSeq = 0;
	String<alertTextLength + 32> alertText;

	void AlertOk(int param)
	{
		UNUSED(param);
		SerialIo::Sendf("M292 P0 S%lu\n", (unsigned long)alertSeq);
	}

	// M291 S1 closed on the panel: answer that message (its own seq, a later prompt may have come)
	void MessageClosed(int seq)
	{
		SerialIo::Sendf("M292 P0 S%lu\n", (unsigned long)(uint32_t)seq);
	}

	void AlertCancel(int param)
	{
		UNUSED(param);
		SerialIo::Sendf("M292 P1 S%lu\n", (unsigned long)alertSeq);
	}

	void DoEmergencyStop()
	{
		if (stopLatched)
		{
			return;							// finger still on STOP: already done
		}
		stopLatched = true;
		// M112 for old firmware, F0 0F (invalid UTF-8) for new firmware. First, before any redraw.
		SerialIo::Sendf("M112 ;" "\xF0" "\x0F" "\n");
		CncJob::EmergencyStop();
		CncPopup::Close();
		CncKeyboard::Close();
		CncProbe::ClosePopups();
		TouchBeep();
		Delay(1000);
		SerialIo::Sendf("M999\n");
		Delay(1000);
	}
}

namespace Cnc
{
	void GoToPage(unsigned int page)
	{
		ShowPage(page);
	}

	void IgnoreRepeats()
	{
		ignoreUntilRelease = true;
	}
}

namespace UI
{
	void InitColourScheme(const ColourScheme *scheme) { UNUSED(scheme); }

	void CreateFields(uint32_t language, const ColourScheme& colours, uint32_t p_infoTimeout)
	{
		UNUSED(language);
		infoTimeout = p_infoTimeout;

		mgr.Init(PageBg);
		DisplayField::SetDefaultFont(DEFAULT_FONT);
		ButtonWithText::SetFont(DEFAULT_FONT);
		CharButtonRow::SetFont(DEFAULT_FONT);
		SingleButton::SetTextMargin(textButtonMargin);
		SingleButton::SetIconMargin(iconButtonMargin);

		// Fields the rest of the firmware writes to even when they are not on screen
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Text, PageBg);
		debugField = new StaticTextField(0, 0, ScreenW, TextAlignment::Left, "");
		fwVersionField = new TextField(0, 0, ScreenW, TextAlignment::Left, "", VERSION_TEXT);
		userCommandField = new TextField(0, 0, ScreenW, TextAlignment::Left, nullptr, "_");
		ipAddressField = new TextField(0, 0, ScreenW, TextAlignment::Left, "IP: ", "");
		freeMem = new IntegerField(0, 0, ScreenW, TextAlignment::Left, "Free RAM: ");
		for (size_t r = 0; r < numMessageRows; ++r)
		{
			messageTimeFields[r] = new StaticTextField(0, 0, messageTimeWidth, TextAlignment::Left, nullptr);
			messageTextFields[r] = new StaticTextField(0, 0, messageTextWidth, TextAlignment::Left, nullptr);
		}

		// Portrait screen: two frames (full DRO + nav, compact DRO + nav), then the pages on top
		mgr.SetRoot(nullptr);
		CreateNav(navTiles);
		CreateDro();
		cncRoot = mgr.GetRoot();
		mgr.SetRoot(nullptr);
		CreateNav(compactNavTiles);
		CreateCompactDro();
		compactRoot = mgr.GetRoot();
		for (DisplayField *&r : pageRoots)
		{
			r = compactRoot;							// replaced below by each page
		}
		pageRoots[0] = CncControl::Create(cncRoot);
		CncControl::SetSpindleDisplay(droSpindle, cmpSpindle);
		CncWcs::Create(cncRoot);
		CncJob::Create(compactRoot);
		CncSystem::Create(compactRoot);
		pageRoots[3] = CncMacros::Create(compactRoot);
		CncPopup::Create();
		CncKeyboard::Create();
		navTiles[0]->Press(true, 0);				// CONTROL selected at start
		compactNavTiles[0]->Press(true, 0);

		// Touch calibration runs in the native landscape orientation
		DisplayField::SetDefaultColours(colours.labelTextColour, colours.defaultBackColour);
		touchCalibInstruction = new StaticTextField(DisplayY/2 - 10, 0, DisplayX, TextAlignment::Centre, strings->touchTheSpot);

		mgr.SetRoot(nullptr);
	}

	void ShowDefaultPage()
	{
		ShowPage(0);
	}

	// ---------------------------------------------------------------------
	// Live values
	// ---------------------------------------------------------------------
	void UpdateAxisPosition(size_t axis, float fval)
	{
		Cnc::SetWorkPosition(axis, fval);				// numpad POS on WCS > OFFSETS
		if (axis < DroAxes)
		{
			droWork[axis]->SetValue(fval);
		}
		if (axis < CompactAxes)
		{
			cmpWork[axis]->SetValue(fval);
		}
	}

	void UpdateAxisMachinePosition(size_t axis, float fval)
	{
		SetMachinePosition(axis, fval);
		if (axis < DroAxes)
		{
			droMachine[axis]->SetValue(fval);
		}
	}

	void SetCurrentTool(int32_t tool)
	{
		droToolText.Clear();
		if (tool < 0)
		{
			droToolText.copy("T-");
		}
		else
		{
			droToolText.printf("T%d", (int)tool);
		}
		droTool->SetValue(droToolText.c_str(), true);
		cmpTool->SetValue(droToolText.c_str(), true);
		CncTools::SetActiveTool((int)tool);
		CncJob::SetTool((int)tool);
	}

	void SetNextTool(int32_t tool) { CncJob::SetNextTool((int)tool); }

	void SetToolPresent(size_t toolIndex, bool present) { CncTools::SetPresent(toolIndex, present); }
	void SetToolName(size_t toolIndex, const char *name) { CncTools::SetName(toolIndex, name); }
	void RemoveToolsFrom(size_t firstToolIndex) { CncTools::RemoveFrom(firstToolIndex); }

	void UpdateCurrentMoveRequestedSpeed(float value)
	{
		const int mmPerMin = (int)(value * 60.0f + 0.5f);		// RRF reports mm/s, show mm/min
		droFeed->SetValue(mmPerMin);
		cmpFeed->SetValue(mmPerMin);
		CncJob::SetRequestedSpeed(mmPerMin);
	}

	void SetSpindleActive(size_t spindleIndex, int32_t activeRpm)
	{
		if (spindleIndex == 0)
		{
			CncJob::SetSpindleActive(activeRpm);			// spindle override emulation (may send M3/M4)
			CncControl::SetSpindleActive((activeRpm < 0) ? -activeRpm : activeRpm);	// updates the S tiles
		}
	}

	// ---------------------------------------------------------------------
	// Touch
	// ---------------------------------------------------------------------
	static void DoProcessTouch(ButtonPress bp);
	static void ReleaseButton(ButtonPress bp);

	// Touch repeats (finger held) arrive like new touches. When a touch opened a popup, the repeats
	// would land on the popup's buttons under the finger (e.g. the check mark of ABORT's confirmation),
	// so nothing but STOP is taken until the finger is lifted.
	void ProcessTouch(ButtonPress bp)
	{
		if (!bp.IsValid() || (ignoreUntilRelease && bp.GetEvent() != evEmergencyStop))
		{
			return;
		}
		const PopupWindow * const popupBefore = mgr.GetPopup();
		const event_t ev = bp.GetEvent();
		DoProcessTouch(bp);
		// A popup opened or closed under the finger, or a one-shot action (JOB LIST row / SD,
		// keyboard SHIFT / #+=): the repeats of a held finger must not act again
		if (mgr.GetPopup() != popupBefore
			|| ev == evFile || ev == evChangeCard || ev == evShift || ev == evCncKbMode
			|| ev == evMacro || ev == evMacroControlPage
			|| ev == evSettingsAlwaysDimToggle || ev == evSettingsHeaterCombineOpen	// SETTINGS toggles / machine tiles
			|| ev == evInvertX || ev == evInvertY
			|| ev == evCncAux || ev == evCncCustom || ev == evCncJobAux || ev == evCncJobCustom	// toggles / macros: once per tap
			|| ((ev == evCncJobOvrMinus || ev == evCncJobOvrPlus) && Cnc::JobInProgress())		// one override command per tap
			|| ev == evSendKeyboardCommand)
		{
			ignoreUntilRelease = true;
		}
	}

	static void DoProcessTouch(ButtonPress bp)
	{
		currentButton = bp;
		TouchBeep();

		if (CncPopup::ProcessTouch(bp) || CncKeyboard::ProcessTouch(bp) || CncProbe::ProcessPopupTouch(bp))
		{
			return;
		}

		switch ((Event)bp.GetEvent())
		{
		case evCncDroTool:
			CncControl::ToggleSpindle();
			break;

		case evCncDroSpindle:
			CncControl::OpenSpindleNumpad();
			break;

		case evCncDroFeed:
			CncControl::OpenFeedNumpad();
			break;

		case evEmergencyStop:
			mgr.Press(bp, true);
			DoEmergencyStop();
			break;

		case evCncNav:
			if ((unsigned int)bp.GetIParam() != currentPage)
			{
				ShowPage((unsigned int)bp.GetIParam());
			}
			break;

		case evCncZero:
			if (jobLocked)
			{
				Refuse("ZERO is locked while a job is running or paused.");
			}
			else
			{
				mgr.Press(bp, true);
				SerialIo::Sendf("G10 L20 P{move.workplaceNumber + 1} %c0\n", AxisLetters[bp.GetIParam()]);
				CncRequestWorkplaceOffsets();
			}
			break;

		default:
			{
				bool redraw = false;
				if (currentPage == 3)
				{
					if (CncMacros::ProcessTouch(bp))
					{
						break;
					}
				}
				else if (currentPage == 4)
				{
					if (CncSystem::ProcessTouch(bp, redraw))
					{
						if (redraw)
						{
							ShowPage(currentPage);		// sub-tab changed
						}
						break;
					}
				}
				else if (currentPage == 2)
				{
					if (CncJob::ProcessTouch(bp, redraw))
					{
						if (redraw)
						{
							ShowPage(currentPage);		// sub-tab changed
						}
						break;
					}
				}
				else if (CncControl::ProcessTouch(bp))
				{
					break;
				}
				else if (CncWcs::ProcessTouch(bp, redraw))
				{
					if (redraw)
					{
						ShowPage(currentPage);			// sub-tab changed
					}
					break;
				}
				mgr.Press(bp, true);
			}
			break;
		}
	}

	// Touches outside the open popup: only STOP, except around the JOB LIST keyboard sheet,
	// which leaves the rows above it and the nav bar usable (the keyboard closes first).
	void ProcessTouchOutsidePopup(ButtonPress bp)
	{
		if (!bp.IsValid())
		{
			return;
		}
		if (bp.GetEvent() == evEmergencyStop)
		{
			DoEmergencyStop();
		}
		else if (CncJob::KeyboardOpen() && !ignoreUntilRelease)
		{
			switch (CncJob::TouchOutsideKeyboard(bp))
			{
			case CncJob::OutsideTouch::Used:
				TouchBeep();
				ignoreUntilRelease = true;			// finger held: the repeats must not reopen it
				break;
			case CncJob::OutsideTouch::Process:
				ProcessTouch(bp);
				break;
			default:
				break;
			}
		}
		else if (CncMacros::KeyboardOpen() && !ignoreUntilRelease)
		{
			switch (CncMacros::TouchOutsideKeyboard(bp))
			{
			case CncMacros::OutsideTouch::Used:
				TouchBeep();
				ignoreUntilRelease = true;
				break;
			case CncMacros::OutsideTouch::Process:
				ProcessTouch(bp);
				break;
			default:
				break;
			}
		}
		else if (CncSystem::KeyboardOpen() && !ignoreUntilRelease)
		{
			if (CncSystem::TouchOutsideKeyboard(bp))
			{
				ProcessTouch(bp);					// nav tile: the keyboard has closed
			}
		}
	}

	// Called by the main loop for every touch (first and repeats) before and after it is processed.
	// A popup that came from the machine (M291, a reply, the jog prompt) while the finger was held
	// on a repeating button must not get the repeats: its buttons may be under the finger.
	void CncTouchSeen(bool repeat)
	{
		if (repeat && mgr.GetPopup() != popupAtTouch)
		{
			ignoreUntilRelease = true;
		}
	}

	void CncTouchDone()
	{
		popupAtTouch = mgr.GetPopup();
	}

	void ProcessRelease(ButtonPress bp)
	{
		ignoreUntilRelease = false;				// finger lifted
		stopLatched = false;
		if (currentButton.IsValid() && (!bp.IsValid() || currentButton.GetButton() != bp.GetButton()
											|| currentButton.GetIndex() != bp.GetIndex()))
		{
			ReleaseButton(currentButton);		// released somewhere else (a popup came up meanwhile)
		}
		ReleaseButton(bp);
	}

	static void ReleaseButton(ButtonPress bp)
	{
		if (!bp.IsValid())
		{
			return;
		}
		if (CncPopup::ProcessRelease(bp) || CncKeyboard::ProcessRelease(bp) || CncProbe::ProcessPopupRelease(bp))
		{
			currentButton.Clear();
			return;
		}
		switch ((Event)bp.GetEvent())
		{
		case evCncNav:
		case evCncDroTool:
		case evCncDroSpindle:
		case evCncDroFeed:
			break;						// stays selected / touch area, nothing to release
		default:
			if (!CncJob::ProcessRelease(bp) && !CncSystem::ProcessRelease(bp) && !CncMacros::ProcessRelease(bp) && !CncControl::ProcessRelease(bp) && !CncWcs::ProcessRelease(bp))
			{
				mgr.Press(bp, false);
			}
			break;
		}
		currentButton.Clear();
	}

	void OnButtonPressTimeout()
	{
		if (currentButton.IsValid())
		{
			ReleaseButton(currentButton);		// repeat: not a real release, keeps ignoreUntilRelease
		}
	}

	// ---------------------------------------------------------------------
	// Stubs: filled in by later steps (or never needed on a CNC machine)
	// ---------------------------------------------------------------------
	void ActivateScreensaver() { }
	bool DeactivateScreensaver() { return true; }
	void AnimateScreensaver() { }
	void ShowFirmwareUpdatePopup() { }
	void ShowAxis(size_t axis, bool b, const char* axisLetter) { UNUSED(axis); UNUSED(b); UNUSED(axisLetter); }
	void UpdateCurrentTemperature(size_t heater, float fval) { UNUSED(heater); UNUSED(fval); }
	void UpdateHeaterStatus(const size_t heater, const OM::HeaterStatus status) { UNUSED(heater); UNUSED(status); }
	void ChangeStatus(OM::PrinterStatus oldStatus, OM::PrinterStatus newStatus)
	{
		// JOB first: it keeps the job-in-progress flag that JobActive() / MachineBusy() use
		const bool started = CncJob::StatusChanged(oldStatus, newStatus);
		if (newStatus == OM::PrinterStatus::connecting && oldStatus != OM::PrinterStatus::connecting)
		{
			CncWcs::Disconnected();
		}
		const bool locked = JobActive(newStatus);	// called before GetStatus() changes
		const bool running = JobActive(newStatus) && newStatus != OM::PrinterStatus::paused;
		CncProbe::StatusChanged(newStatus == OM::PrinterStatus::idle, newStatus == OM::PrinterStatus::busy);
		CncControl::SetJobState(running, locked);
		CncWcs::SetJobState(running, locked);
		if (locked != jobLocked)
		{
			jobLocked = locked;
			for (CncZeroButton *z : zeroButtons)
			{
				z->SetLocked(locked);
			}
		}
		if (started)
		{
			CncJob::ShowStatusTab();				// a job started: show JOB STATUS
			ShowPage(2);
		}
	}
	void UpdateTimesLeft(size_t index, unsigned int seconds)
	{
		if (index == 0)
		{
			CncJob::SetTimeLeft(seconds);			// file based estimate
		}
	}
	void UpdateDuration(uint32_t duration) { CncJob::SetDuration(duration); }
	void UpdateWarmupDuration(uint32_t warmupDuration) { UNUSED(warmupDuration); }
	void SetSimulatedTime(uint32_t simulatedTime) { UNUSED(simulatedTime); }
	bool IsSetupTab() { return false; }
	bool IsJobStatusPageShown() { return currentPage == 2 && CncJob::StatusTabSelected(); }
	void Tick() { }
	void Spin()
	{
		CncProbe::Spin();
		CncPopup::Spin();						// reply popup timeout (INFO TIMEOUT)
		CncJob::Spin();
		CncSystem::Spin(currentPage == 4);
		CncMacros::Spin();
	}
	void PrintStarted() { }
	void PrintingFilenameChanged(const char data[]) { CncJob::SetFileName(data); }
	void LastJobFileNameAvailable(const bool available) { UNUSED(available); }
	void SetLastFileSimulated(const bool lastFileSimulated) { UNUSED(lastFileSimulated); }
	void UpdatePrintingFields() { }
	void SetPrintProgressPercent(unsigned int percent) { CncJob::SetProgress(percent); }
	void UpdateGeometry(unsigned int p_numAxes, bool p_isDelta) { UNUSED(p_numAxes); UNUSED(p_isDelta); }
	void UpdateHomedStatus(size_t axis, bool isHomed) { CncControl::SetHomed(axis, isHomed); }
	void UpdateZProbe(const char data[])
	{
		int value = 0;
		for (const char *p = data; *p >= '0' && *p <= '9'; ++p)
		{
			value = value * 10 + (*p - '0');
		}
		CncProbe::SetProbeValue(value);
	}
	void UpdateMachineName(const char data[]) { UNUSED(data); }
	void UpdateIP(const char data[]) { CncSettings::SetIP(data); }
	void UpdateCncGlobal(const char *name, const char *data) { CncSettings::UpdateGlobal(name, data); }
	void CncGlobalsArriving() { CncSettings::GlobalsArriving(); }
	void CncGlobalsDone(bool complete) { CncSettings::GlobalsDone(complete); }
	// Messages from the machine (M291), shown in the standard popup.
	// S0/S1: message, check mark closes it. S2: check mark = OK (M292 P0). S3: X = cancel (M292 P1).
	// Choices, number and text input are not supported on the panel yet: answer those in DWC.
	void ProcessAlert(const Alert& alert)
	{
		alertSeq = alert.seq;
		const char *title = alert.title.IsEmpty() ? "MESSAGE" : alert.title.c_str();
		switch (alert.mode)
		{
		case Alert::Mode::Info:
		case Alert::Mode::InfoClose:
			if (CncProbe::AutoProbeRunning())
			{
				CncProbe::AutoMessage(alert.title.c_str(), alert.text.c_str());	// progress of the auto probe macro
			}
			else if (CncPopup::CanShowPassive() && !CncKeyboard::IsOpen() && !CncProbe::JogPromptOpen())
			{
				// as the Modern UI: not over a popup in use (typing a value is not thrown away).
				// S1 (close button): closing it answers RRF (M292), as the Modern UI does.
				CncPopup::Message(title, alert.text.c_str(), false,
									(alert.mode == Alert::Mode::InfoClose) ? MessageClosed : nullptr, nullptr, (int)alert.seq, false, true);
			}
			break;
		case Alert::Mode::InfoConfirm:
		case Alert::Mode::ConfirmCancel:
			// Tool change prompt during a job (title starts with TOOL, no jog controls): inline on JOB STATUS
			if (alert.controls == 0 && strncmp(title, "TOOL", 4) == 0 && CncJob::ShowToolPrompt(alert.text.c_str(), alert.seq))
			{
				if (!IsJobStatusPageShown())
				{
					CncJob::ShowStatusTab();
					ShowPage(2);
				}
			}
			else if (alert.mode == Alert::Mode::ConfirmCancel)
			{
				if (!CncProbe::ShowJogPrompt(title, alert.text.c_str(), alert.controls, true, alert.seq))
				{
					CncPopup::Message(title, alert.text.c_str(), true, AlertOk, AlertCancel, 0);
				}
			}
			else if (!CncProbe::ShowJogPrompt(title, alert.text.c_str(), alert.controls, false, alert.seq))
			{
				CncPopup::Message(title, alert.text.c_str(), false, AlertOk, nullptr, 0);
			}
			break;
		default:
			{
				alertText.copy("Answer this prompt in DWC.\n");	// first, so wrapping never cuts it off
				alertText.cat(alert.text.c_str());
				CncPopup::Message(title, alertText.c_str(), false, nullptr, nullptr, 0, true);	// waits: not replaced by replies
			}
			break;
		}
	}

	void ClearAlert()
	{
		if (CncPopup::IsMessageOpen())
		{
			CncPopup::Close();					// answered elsewhere (DWC, timeout)
		}
		CncProbe::CloseJogPrompt();
		CncJob::CloseToolPrompt();
	}

	void ProcessSimpleAlert(const char* _ecv_array text)
	{
		if (CncProbe::AutoProbeRunning())
		{
			CncProbe::AutoMessage("", text);		// progress of the auto probe macro
		}
		else if (CncPopup::CanShowPassive() && !CncKeyboard::IsOpen() && !CncProbe::JogPromptOpen())
		{
			// never replace a prompt that waits for an answer or a popup in use (Modern UI)
			CncPopup::Message("MESSAGE", text, false, nullptr, nullptr, 0);
		}
	}
	void NewResponseReceived(const char* _ecv_array text)
	{
		CncSystem::Response(text);					// SYSTEM > CONSOLE log
		const bool isError = (strstr(text, "Error:") != nullptr);
		if (isError && CncProbe::AutoProbeRunning())
		{
			CncProbe::Error(text);				// shown in the auto probe popup
			return;
		}
		if (isError && Cnc::JobInProgress())
		{
			CncJob::Error(text);				// ALERT ! during a job (step 8), also over other popups; first:
			return;								// a typed command without a reply must not hide a job error
		}
		if (CncSystem::LastReplyWasConsole())
		{
			return;								// a command typed in CONSOLE: its reply is shown there
		}
		// As the Modern UI: an "Error" reply as ALERT ! until X, others in a popup that closes after
		// INFO TIMEOUT (0 = not shown); the next reply replaces it; not while SYSTEM is shown, never
		// over another popup in use
		const bool modernError = StringStartsWith(text, "Error");
		if (currentPage != 4 && (modernError || nvData.infoTimeout != 0))
		{
			CncPopup::Response(text, modernError, (uint32_t)nvData.infoTimeout * 1000);
		}
	}
	bool CanDimDisplay() { return true; }
	void UpdateFileLastModifiedText(const char data[]) { UNUSED(data); }
	void UpdateFileGeneratedByText(const char data[]) { UNUSED(data); }
	void UpdateFileObjectHeight(float f) { UNUSED(f); }
	void UpdateFileLayerHeight(float f) { UNUSED(f); }
	void UpdateFileSize(int size) { UNUSED(size); }
	void UpdateFileFilament(int len) { UNUSED(len); }
	bool UpdateFileThumbnailChunk(const struct Thumbnail &thumbnail, uint32_t pixels_offset, const qoi_rgba_t *pixels, size_t pixels_count)
		{ UNUSED(thumbnail); UNUSED(pixels_offset); UNUSED(pixels); UNUSED(pixels_count); return false; }
	unsigned int GetThumbnailTargetWidth() { return 0; }
	unsigned int GetThumbnailTargetHeight() { return 0; }
	void UpdateJobLayer(unsigned int layer) { UNUSED(layer); }
	void UpdateJobNumLayers(unsigned int layers) { UNUSED(layers); }
	void UpdateCurrentMoveTopSpeed(float value) { UNUSED(value); }
	void UpdateCurrentMoveExtrusionRate(float value) { UNUSED(value); }
	void UpdateFilamentDiameter(size_t extruder, float value) { UNUSED(extruder); UNUSED(value); }
	void UpdateFanPercent(size_t fanIndex, int rpm) { UNUSED(fanIndex); UNUSED(rpm); }
	void UpdateFanName(size_t fanIndex, const char *name) { UNUSED(fanIndex); UNUSED(name); }
	void SetFanThermostatic(size_t fanIndex, bool thermostatic) { UNUSED(fanIndex); UNUSED(thermostatic); }
	void UpdateActiveTemperature(size_t index, int ival) { UNUSED(index); UNUSED(ival); }
	void UpdateToolTemp(size_t toolIndex, size_t toolHeaterIndex, int32_t temp, bool active) { UNUSED(toolIndex); UNUSED(toolHeaterIndex); UNUSED(temp); UNUSED(active); }
	void UpdateStandbyTemperature(size_t index, int ival) { UNUSED(index); UNUSED(ival); }
	void UpdateColdExtrudeTemperature(float value) { UNUSED(value); }
	void UpdateColdRetractTemperature(float value) { UNUSED(value); }
	void UpdateExtrusionFactor(size_t index, int ival) { UNUSED(index); UNUSED(ival); }
	void UpdatePrintTimeText(uint32_t seconds, bool isSimulated) { UNUSED(seconds); UNUSED(isSimulated); }
	void UpdateSpeedPercent(int ival) { CncJob::SetSpeedFactor(ival); }
	void UpdatePressureAdvance(size_t index, float value) { UNUSED(index); UNUSED(value); }
	void UpdateStatusCurrentObject(int objectIndex) { UNUSED(objectIndex); }
	void UpdateStatusObjectName(size_t objectIndex, const char *name) { UNUSED(objectIndex); UNUSED(name); }
	void UpdateStatusObjectCancelled(size_t objectIndex, bool cancelled) { UNUSED(objectIndex); UNUSED(cancelled); }
	void BeginStatusObjectCoordinate(size_t objectIndex, bool xAxis) { UNUSED(objectIndex); UNUSED(xAxis); }
	void ClearStatusObjectCoordinate(size_t objectIndex, bool xAxis) { UNUSED(objectIndex); UNUSED(xAxis); }
	void UpdateStatusObjectCoordinate(size_t objectIndex, bool xAxis, float value) { UNUSED(objectIndex); UNUSED(xAxis); UNUSED(value); }
	void UpdateStatusObjectCount(size_t count) { UNUSED(count); }
	bool IsDisplayingFileInfo() { return false; }
	void AllToolsSeen() { }

	void DisplayFilesPopup(int cardNumber, unsigned int numVolumes) { UNUSED(cardNumber); UNUSED(numVolumes); }
	void DisplayMacrosPopup() { }
	void FileListCardButtonUpdate(unsigned int numVolumes) { UNUSED(numVolumes); }
	void FileListLoaded(bool filesNotMacros, int errCode)
	{
		if (filesNotMacros)
		{
			CncJob::SetListError(errCode);
		}
		else
		{
			CncMacros::SetListError(errCode);
		}
	}
	void EnableFileNavButtons(bool filesNotMacros, bool scrollEarlier, bool scrollLater, bool parentDir)
		{ UNUSED(filesNotMacros); UNUSED(scrollEarlier); UNUSED(scrollLater); UNUSED(parentDir); }
	void UpdateFileButton(bool filesNotMacros, unsigned int buttonIndex, const char * _ecv_array null text, const char * _ecv_array null param)
		{ UNUSED(filesNotMacros); UNUSED(buttonIndex); UNUSED(text); UNUSED(param); }
	// FileManager calls this whenever the gcode listing was loaded or a new one requested
	// (end of FileSet::StatusJobPageUpdated): the JOB LIST rebuilds from the cached listing.
	void EnableStatusJobNavButtons(bool scrollEarlier, bool scrollLater, bool parentDir)
	{
		UNUSED(scrollEarlier); UNUSED(scrollLater); UNUSED(parentDir);
		CncJob::FilesChanged();
	}
	void SetVolumeMounted(size_t volume, bool mounted) { CncJob::SetVolumeMounted(volume, mounted); }
	void UpdateStatusJobFileButton(unsigned int buttonIndex, const char * _ecv_array null text, const char * _ecv_array null param)
		{ UNUSED(buttonIndex); UNUSED(text); UNUSED(param); }
	// Same hook for the macro listing (end of FileSet::ControlMacrosPageUpdated): MACROS rebuilds
	void EnableControlMacroNavButtons(bool scrollEarlier, bool scrollLater, bool parentDir)
	{
		UNUSED(scrollEarlier); UNUSED(scrollLater); UNUSED(parentDir);
		CncMacros::FilesChanged();
	}
	void UpdateControlMacroFileButton(unsigned int buttonIndex, const char * _ecv_array null text, const char * _ecv_array null param)
		{ UNUSED(buttonIndex); UNUSED(text); UNUSED(param); }
	unsigned int GetNumScrolledFiles(bool filesNotMacros) { UNUSED(filesNotMacros); return 0; }
	bool UpdateMacroShortList(unsigned int buttonIndex, const char * _ecv_array null fileName) { UNUSED(buttonIndex); UNUSED(fileName); return false; }

	void SetBabystepOffset(size_t index, float f) { CncWcs::SetOffsetStep(index, f); }	// RRF babystep = offset step
	void SetAxisLetter(size_t index, char l) { UNUSED(index); UNUSED(l); }
	void SetAxisMin(size_t index, float val) { UNUSED(index); UNUSED(val); }
	void SetAxisMax(size_t index, float val) { UNUSED(index); UNUSED(val); }
	void SetAxisVisible(size_t index, bool v)
	{
		CncWcs::SetAxisVisible(index, v);
		if (CncControl::SetAxisVisible(index, v) && currentPage == 0)
		{
			mgr.Refresh(true);				// buttons moved: redraw the page
		}
		if (index == 3 && (v != compactFourAxes || v != droFourAxes))
		{
			compactFourAxes = v;			// compact DRO layout B (A under X)
			LayoutCompactRow2();
			droFourAxes = v;				// full DRO: 4 rows of 50 px
			LayoutFullDro();
			mgr.Refresh(true);				// either DRO frame may be showing: redraw whichever it is
		}
	}
	void SetAxisWorkplaceOffset(size_t axisIndex, size_t workplaceIndex, float offset) { CncWcs::SetWorkplaceOffset(axisIndex, workplaceIndex, offset); }
	void SetCurrentWorkplaceNumber(uint8_t workplaceNumber)
	{
		if (workplaceNumber < 6)
		{
			droWorkHeaderText.printf("WORK G%u", 54u + workplaceNumber);
		}
		else
		{
			droWorkHeaderText.printf("WORK G59.%u", (unsigned int)(workplaceNumber - 5));	// G59.1..G59.3
		}
		droWorkHeader->SetValue(droWorkHeaderText.c_str(), true);
		cmpHeader->SetValue(droWorkHeaderText.c_str(), true);
		CncWcs::SetActiveWorkplace(workplaceNumber);
	}

	void UpdateToolStatus(size_t index, OM::ToolStatus status)
	{
		if (status == OM::ToolStatus::active)
		{
			CncTools::SetActiveTool((int)index);
		}
	}
	void SetToolExtruder(size_t toolIndex, uint8_t extruder) { UNUSED(toolIndex); UNUSED(extruder); }
	void SetToolFan(size_t toolIndex, uint8_t fan) { UNUSED(toolIndex); UNUSED(fan); }
	void SetToolHeater(size_t toolIndex, uint8_t toolHeaterIndex, uint8_t heaterIndex) { UNUSED(toolIndex); UNUSED(toolHeaterIndex); UNUSED(heaterIndex); }
	void SetToolSpindle(int8_t toolIndex, int8_t spindleNumber) { UNUSED(toolIndex); UNUSED(spindleNumber); }
	bool RemoveToolHeaters(const size_t toolIndex, const uint8_t firstIndexToDelete) { UNUSED(toolIndex); UNUSED(firstIndexToDelete); return false; }
	void SetToolOffset(size_t toolIndex, size_t axisIndex, float offset)
	{
		if (axisIndex == 2)
		{
			CncTools::SetZOffset(toolIndex, offset);		// Z length offset
		}
	}

	void SetBedOrChamberHeater(const uint8_t heaterIndex, const int8_t heaterNumber, bool bed) { UNUSED(heaterIndex); UNUSED(heaterNumber); UNUSED(bed); }

	void SetSpindleCurrent(size_t spindleIndex, int32_t currentRpm)
	{
		if (spindleIndex == 0)
		{
			CncJob::SetSpindleCurrent(currentRpm);	// JOB STATUS SPINDLE actual
		}
	}
	void SetSpindleLimit(size_t spindleIndex, uint32_t value, bool max)
	{
		if (spindleIndex == 0 && max)
		{
			CncControl::SetSpindleMax((int32_t)value);
			CncJob::SetSpindleMax((int32_t)value);
		}
		else if (spindleIndex == 0)
		{
			CncJob::SetSpindleMin((int32_t)value);
		}
	}
	void SetSpindleState(size_t spindleIndex, OM::SpindleState state)
	{
		if (spindleIndex == 0)
		{
			CncControl::SetSpindleState(state);
			CncJob::SetSpindleState(state != OM::SpindleState::stopped, state == OM::SpindleState::reverse);
			SetSpindleOutline(state != OM::SpindleState::stopped);
		}
	}
	void SetSpindleTool(int8_t spindleNumber, int8_t toolIndex) { UNUSED(spindleNumber); UNUSED(toolIndex); }
}

// End
