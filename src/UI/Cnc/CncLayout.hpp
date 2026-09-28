/*
 * CncLayout.hpp
 *
 * All screen geometry for the portrait CNC UI in one place.
 * Coordinates are logical portrait pixels: 480 wide x 800 high, origin top-left.
 * Values come from the approved SVG mock-ups (pd_cnc_*.svg).
 *
 * Rule: page code uses these names, never raw numbers, so a future landscape
 * layout is a new header instead of a rewrite.
 */

#ifndef SRC_UI_CNC_CNCLAYOUT_HPP_
#define SRC_UI_CNC_CNCLAYOUT_HPP_

#include "UI/DisplaySize.hpp"

#if DISPLAY_X != 800
# error "The CNC UI needs an 800x480 panel (5\" or 7\")"
#endif

namespace CncLayout
{
	// Screen (portrait)
	constexpr PixelNumber ScreenW = DisplayXP;		// 480
	constexpr PixelNumber ScreenH = DisplayYP;		// 800
	constexpr PixelNumber Margin = 6;				// left/right page margin
	constexpr PixelNumber ContentW = ScreenW - 2 * Margin;	// 468

	// Full DRO (CONTROL, WCS)
	constexpr PixelNumber DroHeaderY = 6;			// WORK / MACHINE / ZERO labels
	constexpr PixelNumber DroRowsTop = 28;
	constexpr PixelNumber DroRowH3 = 68, DroGap3 = 7;	// 3 axes
	constexpr PixelNumber DroRowH4 = 50, DroGap4 = 6;	// 4 axes
	constexpr PixelNumber DroAxisLabelX = 6,  DroAxisLabelW = 40;
	constexpr PixelNumber DroWorkX = 50,      DroWorkW = 180;
	constexpr PixelNumber DroMachX = 238,     DroMachW = 140;
	constexpr PixelNumber DroZeroX = 386,     DroZeroW = 88;
	constexpr PixelNumber DroTsfY = 253,      DroTsfH = 50;
	constexpr PixelNumber DroToolX = 6,       DroToolW = 104;
	constexpr PixelNumber DroSpindleX = 118,  DroSpindleW = 178;
	constexpr PixelNumber DroFeedX = 304,     DroFeedW = 170;
	constexpr PixelNumber DroSeparatorY = 311, SeparatorH = 3;

	// Compact DRO (JOB, MACROS, SYSTEM), read-only (mock-ups pd_cnc_compact_dro_*.svg)
	//   3 axes:  X Y Z in row 1, T S F in row 2
	//   4 axes:  X Y Z in row 1, A (under X) T S F in row 2  (layout B)
	namespace Compact
	{
		constexpr PixelNumber HeaderY = 4;
		constexpr PixelNumber Row1Y = 25, Row2Y = 77, RowH = 44;
		constexpr PixelNumber LetterW = 26, AxisTileW = 123, AxisPitch = 159;
		constexpr PixelNumber AxisX(unsigned int i) { return 7 + i * AxisPitch; }	// letter x; tile at +26
		// row 2, 3 axes
		constexpr PixelNumber T3X = 7, T3W = 102, S3X = 119, S3W = 172, F3X = 301, F3W = 172;
		// row 2, 4 axes (A under X)
		constexpr PixelNumber T4X = 164, T4W = 62, S4X = 234, S4W = 116, F4X = 358, F4W = 116;
	}
	constexpr PixelNumber CompactSeparatorY = 130;

	// Page content
	constexpr PixelNumber ContentTop = DroSeparatorY + SeparatorH + 7;	// 321
	constexpr PixelNumber CompactContentTop = CompactSeparatorY + 12;	// 142

	// Sub-tab strip (above the nav bar)
	constexpr PixelNumber SubTabY = 665, SubTabH = 50;
	constexpr PixelNumber ContentBottom = SubTabY - 8;			// 657 with sub-tabs
	constexpr PixelNumber ContentBottomNoTabs = 710;			// without sub-tabs

	// Bottom navigation: 5 master tiles + STOP
	constexpr PixelNumber NavY = 721, NavTile = 73, NavPitch = 79;
	constexpr unsigned int NavCount = 6;
	constexpr PixelNumber NavX(unsigned int i) { return Margin + i * NavPitch; }

	// Popups that must keep the full DRO live sit directly under the DRO line
	constexpr PixelNumber PopupUnderDroX = 10, PopupUnderDroY = 318;
	constexpr PixelNumber PopupUnderDroW = 460, PopupUnderDroH = 396;

	// CONTROL page (glcd19x21 labels are 21 px high)
	namespace Control
	{
		constexpr PixelNumber LabelH = 23;				// label row incl. 2 px gap
		constexpr PixelNumber RowH = 52;				// button rows
		constexpr PixelNumber GroupGap = 8;				// between a row and the next label
		constexpr PixelNumber ActionGap = 8;			// between the two ACTION rows
		constexpr PixelNumber LeftX = Margin, LeftW = 358;	// left block
		constexpr PixelNumber ColGap = 10;				// gap between buttons in a row
		constexpr PixelNumber HomeX = DroZeroX, HomeW = DroZeroW;	// home column under the DRO ZERO column
		constexpr PixelNumber SepX = LeftX + LeftW + (HomeX - LeftX - LeftW - SeparatorH) / 2;	// vertical accent line

		constexpr PixelNumber LabelAxisY = ContentTop;							// 321
		constexpr PixelNumber RowAxisY   = LabelAxisY + LabelH;					// 344
		constexpr PixelNumber LabelStepY = RowAxisY + RowH + GroupGap;			// 404
		constexpr PixelNumber RowStepY   = LabelStepY + LabelH;					// 427
		constexpr PixelNumber LabelMoveY = RowStepY + RowH + GroupGap;			// 487
		constexpr PixelNumber RowMoveY   = LabelMoveY + LabelH;					// 510
		constexpr PixelNumber LabelActY  = RowMoveY + RowH + GroupGap;			// 570
		constexpr PixelNumber RowAct1Y   = LabelActY + LabelH;					// 593
		constexpr PixelNumber RowAct2Y   = RowAct1Y + RowH + ActionGap;			// 653
		constexpr PixelNumber Bottom     = RowAct2Y + RowH;						// 705

		// Home column: 5 buttons evenly spaced from the AXIS row to the bottom of the last ACTION row
		constexpr PixelNumber HomePitch = (Bottom - RowH - RowAxisY) / 4;		// 77
	}

	// Sub-tab strip: equal-width tabs across the content width
	constexpr PixelNumber SubTabX(unsigned int i, unsigned int n) { return Margin + (i * ContentW) / n; }
	constexpr PixelNumber SubTabW(unsigned int i, unsigned int n) { return SubTabX(i + 1, n) - SubTabX(i, n); }

	// WCS > OFFSETS page (left block + OFFSET STEP column, same split as CONTROL)
	namespace Offsets
	{
		constexpr PixelNumber LabelH = Control::LabelH, Gap = 8;
		constexpr PixelNumber LeftX = Control::LeftX, LeftW = Control::LeftW;
		constexpr PixelNumber RightX = Control::HomeX, RightW = Control::HomeW;
		constexpr PixelNumber SepX = Control::SepX;

		constexpr PixelNumber LabelWcsY = ContentTop;						// 321
		constexpr PixelNumber RowWcsY = LabelWcsY + LabelH;					// 344
		constexpr PixelNumber RowWcsH = 52, WcsGap = 6;
		constexpr PixelNumber LabelGridY = RowWcsY + RowWcsH + Gap;			// 404
		constexpr PixelNumber GridY = LabelGridY + LabelH;					// 427
		constexpr PixelNumber CellH = 53, CellGap = 8;
		constexpr PixelNumber CellW = (LeftW - CellGap) / 2;				// 175
		constexpr PixelNumber CellLetterW = 26;
		constexpr PixelNumber ActY = GridY + 2 * CellH + CellGap + Gap;		// 549
		constexpr PixelNumber Bottom = ContentBottom;						// 657
		constexpr PixelNumber ActGap = 8;
		constexpr PixelNumber ActH = (Bottom - ActY - ActGap) / 2;			// 50

		// OFFSET column (offset step): header OFFSET, Z+, total, Z-, STEP (size), CLEAR
		constexpr PixelNumber StepUpY = RowWcsY, StepBtnH = 52;			// Z+ 344, level with the G5x row
		constexpr PixelNumber StepTotalY = StepUpY + StepBtnH + 8, StepTotalH = 44;
		constexpr PixelNumber StepDownY = StepTotalY + StepTotalH + 8;	// Z-
		constexpr PixelNumber StepSizeLabelY = StepDownY + StepBtnH + 8;
		constexpr PixelNumber StepSizeY = StepSizeLabelY + LabelH, StepSizeH = 44;
		constexpr PixelNumber StepClearY = Bottom - ActH, StepClearH = ActH;	// CLEAR, level with the last action row
	}

	// WCS > TOOLS page (mock-up pd_cnc_wcs_tools_3axis.svg)
	namespace Tools
	{
		constexpr PixelNumber LabelY = ContentTop;							// 321: TOOLS / TOOL NAME / Z OFFSET
		constexpr PixelNumber RowsY = LabelY + Control::LabelH;				// 344
		constexpr PixelNumber RowH = 54, RowGap = 8;
		constexpr unsigned int NumRows = 4;
		constexpr PixelNumber RowY(unsigned int r) { return RowsY + r * (RowH + RowGap); }	// 344 406 468 530
		constexpr PixelNumber NumX = Margin, NumW = 60;						// T# button
		constexpr PixelNumber NameX = NumX + NumW + 8, NameW = 204;			// 74
		constexpr PixelNumber ZX = NameX + NameW + 8, ZW = 122;				// 286
		constexpr PixelNumber ArrowX = ZX + ZW + 8, ArrowW = ScreenW - Margin - ArrowX;	// 416, 58
		constexpr PixelNumber ArrowH = 2 * RowH + RowGap;					// 116: two rows tall
		constexpr PixelNumber ActH = 52, ActY = ContentBottom - ActH;		// 605
		constexpr PixelNumber ActGap = 10, ActW = (ContentW - 2 * ActGap) / 3;
	}

	// WCS > PROBE page (mock-ups pd_cnc_wcs_probe_*.svg)
	namespace Probe
	{
		constexpr PixelNumber LabelY = ContentTop;							// 321: ORIGIN / PARAMETERS
		constexpr PixelNumber GridY = LabelY + Control::LabelH;				// 344
		constexpr PixelNumber BtnW = 66, BtnH = 52, BtnGap = 6, BigBtnH = 72;
		constexpr PixelNumber GridX = Margin;
		constexpr PixelNumber BigRowY = GridY + 3 * (BtnH + BtnGap);		// 518: BORE BOSS Z TOP
		constexpr PixelNumber PickBottom = BigRowY + BigBtnH;				// 590
		constexpr PixelNumber SepX = GridX + 3 * BtnW + 2 * BtnGap + 9;		// 225
		constexpr PixelNumber ParX = SepX + SeparatorH + 12, ParW = ScreenW - Margin - ParX;	// 240, 234
		constexpr PixelNumber TileGap = 8;
		constexpr PixelNumber AutoTileW = (ParW - TileGap) / 2;				// 113
		constexpr PixelNumber AutoTileH = (PickBottom - GridY - 2 * TileGap) / 3;	// 76
		constexpr PixelNumber RowTileH = 36, RowTileGap = 6;
		constexpr PixelNumber BottomY = 601, BottomH = ContentBottom - BottomY;	// 56
		constexpr PixelNumber StatusW = 150;
	}

	// JOB > JOB STATUS page (mock-ups pd_cnc_job_status_v2_*.svg), under the compact DRO
	namespace Job
	{
		constexpr PixelNumber Gap = 8;
		constexpr PixelNumber NameY = CompactContentTop + 2, NameH = 56;			// 144
		constexpr PixelNumber NameX = Margin, NameW = 350;
		constexpr PixelNumber ResultX = NameX + NameW + 8, ResultW = ScreenW - Margin - ResultX;	// 364, 110
		constexpr PixelNumber TimesY = NameY + NameH + Gap, TimesH = 50;			// 208
		constexpr PixelNumber TimeW = (ContentW - 8) / 2;							// 230
		constexpr PixelNumber ElapsedX = Margin, LeftX = Margin + TimeW + 8;		// 6, 244
		constexpr PixelNumber LabelH = 22;
		constexpr PixelNumber OvrH = 60;
		constexpr PixelNumber Label1Y = TimesY + TimesH + Gap;					// 266 FEED WORK
		constexpr PixelNumber Ovr1Y = Label1Y + LabelH;							// 288
		constexpr PixelNumber Label2Y = Ovr1Y + OvrH + Gap;						// 356 SPINDLE
		constexpr PixelNumber Ovr2Y = Label2Y + LabelH;							// 378
		constexpr PixelNumber MinusX = Margin, PmW = 50;
		constexpr PixelNumber ValueX = MinusX + PmW + 6, ValueW = 80;			// 62
		constexpr PixelNumber PlusX = ValueX + ValueW + 6;						// 148
		constexpr PixelNumber ActualX = PlusX + PmW + 8, ActualW = 112;			// 206
		constexpr PixelNumber StepX = ActualX + ActualW + 8, StepW = 46, StepPitch = 50;	// 326
		constexpr PixelNumber BtnY = Ovr2Y + OvrH + Gap, BtnH = 58;				// 446 COOLANT CUSTOM 3 CUSTOM 4
		constexpr PixelNumber BtnW = (ContentW - 16) / 3;						// 150
		constexpr PixelNumber BtnX(unsigned int i) { return Margin + i * (BtnW + 8); }
		constexpr PixelNumber TcY = BtnY + BtnH + Gap, TcH = 76;				// 512 tool change row
		constexpr PixelNumber TcOkW = 96, TcOkH = 60;
		constexpr PixelNumber TcOkX = ScreenW - Margin - 8 - TcOkW, TcOkY = TcY + (TcH - TcOkH) / 2;	// 370
		constexpr PixelNumber PauseY = 598, PauseH = 56;
		constexpr PixelNumber PauseX = Margin, PauseW = 230;
		constexpr PixelNumber AbortW = 150, AbortX = ScreenW - Margin - AbortW;	// 324
	}

	// JOB > JOB LIST page (mock-ups pd_cnc_job_list_v3_*.svg), under the compact DRO
	//   label row: FILES + path (or SEARCH "text" + match count)
	//   10 rows on the left; side column SEARCH / up / down / SD
	namespace JobList
	{
		constexpr PixelNumber LabelY = CompactContentTop;						// 142
		constexpr PixelNumber RowsY = 165, RowH = 40, RowPitch = 47;
		constexpr unsigned int NumRows = 10;									// last row 588 .. 627
		constexpr unsigned int NumRowsKeyboard = 3;								// rows above the keyboard sheet
		constexpr PixelNumber RowY(unsigned int r) { return RowsY + r * RowPitch; }
		constexpr PixelNumber RowX = Margin, RowW = 400;
		constexpr PixelNumber SideW = 54, SideX = ScreenW - Margin - SideW;	// 420
		constexpr PixelNumber LabelW = RowW;									// the path ends above the rows
		constexpr PixelNumber SearchY = RowsY, SearchH = 87;					// same size as SD
		constexpr PixelNumber UpY = SearchY + SearchH + 7, ArrowH = 134;		// 259
		constexpr PixelNumber DownY = UpY + ArrowH + 7;						// 400
		constexpr PixelNumber SdY = DownY + ArrowH + 7, SdH = SearchH;			// 541 .. 627
		constexpr PixelNumber EmptyY = RowY(2) + (RowH - 21) / 2;				// "Loading...", "No files": in row 2
		// label row: left word (FILES / SEARCH), query (search only), right text (path / counts)
		constexpr PixelNumber LabelLeftX = RowX + 2, LabelLeftW = 84;
		constexpr PixelNumber QueryX = LabelLeftX + LabelLeftW + 2, QueryW = 128;	// 94
		constexpr PixelNumber QueryTextW = QueryW - 30;						// room for the quotes and ".."
		constexpr PixelNumber RightX = QueryX, RightW = RowX + LabelW - RightX;	// FILES: path from x 94
		constexpr PixelNumber RightSearchX = QueryX + QueryW + 4, RightSearchW = RowX + LabelW - RightSearchX;
	}

	// MACROS page (mock-ups pd_cnc_macros_list.svg / pd_cnc_macros_grid.svg), no sub-tabs:
	// label row, list (11 rows) or grid (2 x 5 tiles) on the left, side column
	// LIST/GRID toggle, up, down (same column as JOB LIST)
	namespace Macros
	{
		constexpr PixelNumber LabelY = CompactContentTop;						// 142: MACROS + path, or SEARCH "text" + counts
		constexpr PixelNumber LabelLeftX = Margin + 2, LabelLeftW = 92;		// "MACROS" is 89 px
		constexpr PixelNumber QueryX = LabelLeftX + LabelLeftW + 2, QueryW = 128, QueryTextW = QueryW - 30;
		constexpr PixelNumber PathX = QueryX, PathW = JobList::RowX + JobList::LabelW - PathX;
		constexpr PixelNumber CountX = QueryX + QueryW + 4, CountW = JobList::RowX + JobList::LabelW - CountX;
		constexpr PixelNumber AreaX = JobList::RowX, AreaW = JobList::RowW, AreaY = JobList::RowsY;	// 6, 400, 165
		constexpr unsigned int ListRows = 11;									// last row 635 .. 674
		constexpr PixelNumber ListRowH = JobList::RowH, ListPitch = JobList::RowPitch;
		constexpr unsigned int GridCols = 2, GridRows = 5;						// 10 tiles
		constexpr PixelNumber GridGap = 10, GridTileW = (AreaW - GridGap) / 2, GridTileH = 94, GridPitchY = 104;	// 195 wide
		constexpr unsigned int NumCells = ListRows;								// fields, used as rows or tiles
		constexpr PixelNumber SideX = JobList::SideX, SideW = JobList::SideW;
		constexpr PixelNumber SearchY = JobList::SearchY, SearchH = JobList::SearchH;		// SEARCH: as on JOB LIST
		constexpr PixelNumber ToggleY = JobList::SdY, ToggleH = JobList::SdH;				// LIST/GRID: in the SD slot
		constexpr unsigned int ListRowsKeyboard = 3, GridRowsKeyboard = 1;		// above the keyboard sheet
		constexpr PixelNumber UpY = JobList::UpY, DownY = JobList::DownY, ArrowH = JobList::ArrowH;
		constexpr PixelNumber EmptyY = AreaY + 2 * ListPitch + (ListRowH - 21) / 2;	// message in row 2 (list)
		constexpr PixelNumber EmptyGridY = AreaY + GridPitchY + (GridTileH - 21) / 2;	// message in tile row 1 (grid)
		constexpr PixelNumber EmptyGridKbY = AreaY + (GridTileH - 21) / 2;			// grid while typing: tile row 0
	}

	// Shared portrait keyboard (mock-up pd_cnc_system_console_keyboard*.svg): a sheet across the
	// screen that ends above the nav bar, so STOP stays visible. Positions inside the sheet.
	namespace Keyboard
	{
		constexpr PixelNumber W = ScreenW, H = 374;
		constexpr PixelNumber X = 0, Y = NavY - 6 - H;						// 341 .. 714
		constexpr PixelNumber TopY = 11, TopH = 50;							// arrows, text field, X
		constexpr PixelNumber UpX = 11, DownX = 59, ArrowW = 42;
		constexpr PixelNumber FieldX = 107, FieldW = 298;
		constexpr PixelNumber FieldTextInset = 11, FieldTextW = FieldW - 16;	// text inside the field
		constexpr PixelNumber FieldTextAvail = FieldW - 24;					// text width left for the cursor
		constexpr PixelNumber CloseX = 412, CloseY = 10, CloseW = 58, CloseH = 52;
		constexpr PixelNumber KeysY = 73, KeyH = 50, KeyPitchY = 57;
		constexpr PixelNumber KeyW = 40, KeyPitchX = 47;
		constexpr PixelNumber KeyRowY(unsigned int r) { return KeysY + r * KeyPitchY; }	// 73 130 187 244 301
		constexpr PixelNumber Row0X = 11, Row2X = 34, Row3X = 82;			// digits/QWERTY, ASDF, ZXCV
		constexpr PixelNumber WideW = 64;									// shift, backspace, #+=
		constexpr PixelNumber BackX = 412;
		constexpr PixelNumber DotX = 82, SpaceX = 176, SpaceW = 206;
		constexpr PixelNumber EnterX = 388, EnterW = 88, EnterY = 300, EnterH = 52;	// 1 px taller each side (mock-up)
	}

	// SYSTEM > CONSOLE (mock-ups pd_cnc_system_console_v2_*.svg), under the compact DRO
	namespace Console
	{
		constexpr PixelNumber LabelY = CompactContentTop;						// 142: CONSOLE / NEWEST FIRST
		constexpr PixelNumber LabelW = 200, LabelLeftX = Margin + 2, LabelRightX = ScreenW - Margin - 2 - LabelW;
		constexpr PixelNumber LogX = Margin + 1, LogW = ContentW - 2;			// 7 .. 472
		constexpr PixelNumber LogY = 167, LogH = 413;							// .. 579
		constexpr unsigned int NumLines = 15;
		constexpr PixelNumber LineY0 = 175, LinePitch = 27;					// text tops (21 px font)
		constexpr PixelNumber LineY(unsigned int i) { return LineY0 + i * LinePitch; }	// last 553
		constexpr PixelNumber TimeX = LogX + 2, TimeW = 64;					// age ("59m59" is ~62 px), ends at 72
		constexpr PixelNumber TextX = LogX + 76, TextW = LogX + LogW - 8 - TextX;	// 83 .. 464
		constexpr unsigned int LinesAboveKeyboard = 6;						// rows 0..5 stay visible while typing
		constexpr PixelNumber InputY = 602, InputH = 54;
		constexpr PixelNumber InputX = LogX, InputW = 386;						// "Tap to enter G-code..."
		constexpr PixelNumber KbButtonX = InputX + InputW + 10, KbButtonW = LogX + LogW - KbButtonX;	// 403, 70
	}

	// SYSTEM > ALERT (mock-up pd_cnc_system_alert.svg): rolling history of the machine's
	// Error: / Warning: messages, newest first, one page
	namespace Alerts
	{
		constexpr PixelNumber LabelY = CompactContentTop;						// 142: ALERTS / "n / 11  NEWEST FIRST"
		constexpr PixelNumber LabelLeftX = Margin + 2, LabelLeftW = 100;		// "ALERTS" is 81 px
		constexpr PixelNumber LabelRightW = 300, LabelRightX = ScreenW - Margin - 2 - LabelRightW;	// "11 / 11   NEWEST FIRST" 251 px
		constexpr unsigned int NumRows = 11;
		constexpr PixelNumber RowX = Margin + 1, RowW = ContentW - 2;			// 7 .. 472
		constexpr PixelNumber RowY0 = 165, RowH = 38, RowPitch = 44;			// last row 605 .. 642
		constexpr PixelNumber RowY(unsigned int i) { return RowY0 + i * RowPitch; }
		constexpr PixelNumber BadgeDx = 5, BadgeW = 68, BadgeH = 26;			// ERR / WARN badge ("WARN" is 62 px)
		constexpr PixelNumber AgeX = RowX + BadgeDx + BadgeW + 4, AgeW = 64;	// "59m59" is ~62 px
		constexpr PixelNumber TextDx = AgeX + AgeW + 8 - RowX;				// text from x 156
		constexpr PixelNumber DotDx = RowW - 11, DotR = 4;						// unread dot
		constexpr PixelNumber EmptyY = RowY(2) + (RowH - 21) / 2;				// "No alerts"
	}

	// SYSTEM > SETTINGS (mock-ups pd_cnc_settings_p1_system.svg / pd_cnc_settings_p2_custom.svg):
	// IP + FACTORY RESET, section label + "PAGE n / 2", 2 x 6 tiles, page arrows
	namespace Settings
	{
		constexpr PixelNumber TopY = CompactContentTop, TopH = 44;				// 142 .. 185
		constexpr PixelNumber ColW = 229, ColGap = 10;
		constexpr PixelNumber ColX(unsigned int c) { return Margin + c * (ColW + ColGap); }	// 6, 245
		constexpr PixelNumber IpLabelDx = 12, IpLabelW = 40;					// "IP" muted, left
		constexpr PixelNumber IpTextW = ColW - IpLabelDx - IpLabelW - 12;		// address, right aligned
		constexpr PixelNumber SectionY = 195;									// SYSTEM / CUSTOMIZATION (21 px font)
		constexpr PixelNumber SectionW = 220;
		constexpr PixelNumber SectionLeftX = Margin + 2, SectionRightX = ScreenW - Margin - 2 - SectionW;
		constexpr PixelNumber TilesY = 218, TileH = 56, TilePitch = 62;		// rows 218, 280, 342, 404, 466, 528
		constexpr unsigned int NumRows = 6, NumTiles = 2 * NumRows;
		constexpr PixelNumber TileY(unsigned int i) { return TilesY + (i / 2) * TilePitch; }
		constexpr PixelNumber TileX(unsigned int i) { return ColX(i % 2); }
		constexpr PixelNumber ArrowY = 596, ArrowH = 56, ArrowW = 150;
		constexpr PixelNumber UpX = 80, DownX = 250;
	}

	// Standard portrait popup (alerts, confirmations), centred
	constexpr PixelNumber StdPopupW = 460, StdPopupH = 480;
	constexpr PixelNumber StdPopupX = (ScreenW - StdPopupW) / 2, StdPopupY = (ScreenH - StdPopupH) / 2;	// 10, 160
}

#endif /* SRC_UI_CNC_CNCLAYOUT_HPP_ */
