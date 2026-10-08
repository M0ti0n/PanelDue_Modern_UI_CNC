/*
 * CncJobPage.cpp
 *
 * JOB page, portrait CNC UI (mock-ups pd_cnc_job_status_v2_*.svg), under the compact DRO:
 *
 *   [ bracket_v2.nc                    ][  89% ]     progress tile: green running, amber tool
 *   [ ELAPSED  00:12:31 ][ LEFT  00:17:05 ]           change, cyan paused; idle DONE / ABORTED
 *   FEED WORK  T3                 ACTUAL     STEP
 *   [-][ 100% ][+][    1180 ][1%][5%][10%]
 *   SPINDLE  T3                   ACTUAL     STEP
 *   [-][  90% ][+][    7210 ][1%][5%][10%]
 *   [ COOLANT ][ CUSTOM 2 ][ CUSTOM 3 ]
 *   [ Insert T5 and press OK        [ OK ] ]          tool change row (only when needed)
 *   [    PAUSE    ]            [ ABORT ]
 *   [ JOB STATUS ][ JOB LIST ]   (sub-tabs)
 *
 * Overrides are per tool and only work while a job is in progress; all go back to 100 % at
 * the end of the job.
 *  - FEED WORK: M220, sent again on every tool change with the new tool's value.
 *  - SPINDLE: RRF has no spindle override, so it is emulated: the programmed speed is tracked
 *    (spindles[0].active) and M3/M4 S<programmed x override> is sent. The value the panel just
 *    sent comes back as 'active' and is recognised as an echo, not a new programmed speed.
 * Tapping the percentage opens the standard numpad (feed 10-200 %, spindle 50-150 %).
 *
 * Tool change row: M291 prompts whose title starts with TOOL show here during a job
 * (blinking amber outline, OK = M292 P0). Without a prompt, "TOOL CHANGE T3 >>> T5" shows
 * while an automatic change runs (state.nextTool).
 *
 * PAUSE / RESUME / ABORT ask like the Modern UI ("ALERT !", "Do you want to ... this job?").
 * ABORT while running: M25, then M0 once paused (given up after 60 s).
 */

#include "CncJobPage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncPopups.hpp"
#include "CncProbePage.hpp"
#include "CncKeyboard.hpp"
#include "FileManager.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "Hardware/SysTick.hpp"
#include "PanelDue.hpp"
#include "Icons/Icons.hpp"
#include <General/String.h>

using namespace CncLayout;
using namespace CncLayout::Job;
using namespace Cnc;

namespace
{
	const Colour Green      = UTFT::fromRGB(86, 184, 52);		// #56B834
	const Colour Cyan       = UTFT::fromRGB(95, 195, 220);		// #5FC3DC
	const Colour Amber      = UTFT::fromRGB(224, 168, 0);		// #E0A800
	const Colour ButtonText = UTFT::fromRGB(36, 36, 36);		// #242424, text on coloured buttons

	constexpr size_t NumSubTabs = 2;
	const char * const SubTabNames[NumSubTabs] = { "JOB STATUS", "JOB LIST" };

	// Overrides: row 0 = FEED WORK, row 1 = SPINDLE
	constexpr size_t NumRows = 2;
	constexpr int MinPct[NumRows] = { 10, 50 };
	constexpr int MaxPct[NumRows] = { 200, 150 };
	constexpr size_t NumSteps = 3;
	const int StepPct[NumSteps] = { 1, 5, 10 };
	const char * const StepNames[NumSteps] = { "1%", "5%", "10%" };
	constexpr size_t DefaultStep = 1;							// 5 %

	constexpr int MaxToolNumber = 99;
	constexpr size_t NoToolIndex = MaxToolNumber + 1;			// overrides while no tool is loaded

	constexpr uint32_t EchoTime = 3000;							// ms a sent value may take to come back
	constexpr uint32_t AbortPauseTimeout = 60000;				// ms: ABORT waits this long for the pause
	constexpr uint32_t BlinkTime = 500;							// ms: tool change prompt outline
	constexpr unsigned int MaxSpindleRetries = 2;				// the same speed is not sent more often

	// Sub-pages
	DisplayField *subRoots[NumSubTabs];
	ModernTextButton *subTabs[NumSubTabs];
	ButtonBase *selectedSubTab = nullptr;
	size_t currentSub = 0;

	// Fields
	CncTextTile *nameTile, *resultTile, *tcTile;
	StaticTextField *elapsedField, *leftField;
	StaticTextField *rowLabels[NumRows];
	ModernTextButton *minusButtons[NumRows], *plusButtons[NumRows], *valueButtons[NumRows];
	IntegerField *actualFields[NumRows];
	ModernTextButton *stepButtons[NumRows][NumSteps];
	ButtonBase *selectedStep[NumRows] = { nullptr, nullptr };
	size_t stepIndex[NumRows] = { DefaultStep, DefaultStep };
	ModernTextButton *auxButton, *customButtons[2], *tcOkButton, *pauseButton, *abortButton;

	String<40> nameText;
	String<8> resultText;
	String<12> elapsedText, leftText;
	String<20> rowLabelText[NumRows];
	String<8> valueText[NumRows];
	String<48> tcLine1, tcLine2;
	String<alertTextLength> promptText;

	// Job state
	enum class Result : uint8_t { None, Done, Aborted };
	Result result = Result::None;
	OM::PrinterStatus status = OM::PrinterStatus::connecting;
	bool inProgress = false;
	bool cancelSeen = false;
	bool panelAbort = false;
	bool abortPending = false;
	uint32_t abortStart = 0;
	uint32_t elapsedSeconds = 0;
	bool haveElapsed = false;
	unsigned int progress = 0;

	// Tools
	int currentTool = -1;
	int nextTool = -1;

	// Per-tool overrides (percent)
	uint16_t overrides[NumRows][NoToolIndex + 1];

	// Feed override bookkeeping
	int speedFactor = -1;										// last reported M220 value
	int feedSent = -1;
	uint32_t feedSentTime = 0;

	// Spindle override emulation
	// A G-code S equal to the value the panel applied cannot be told apart from it: RRF reports the
	// same set speed either way.
	int32_t spindleActive = 0;									// last reported set speed (rpm, >= 0)
	int32_t spindleProgrammed = 0;								// speed programmed by the job
	int32_t spindleSent = 0;									// last speed the panel sent
	uint32_t spindleSentTime = 0;
	unsigned int spindleRetries = 0;
	bool spindleReverse = false;								// from spindles[0].state
	bool spindleRunning = false;								// from spindles[0].state (RRF keeps 'active' after M5)
	constexpr size_t NumRecentSent = 4;
	int32_t recentSent[NumRecentSent] = {};						// speeds the panel sent lately: their echoes are not
	uint32_t recentSentTime[NumRecentSent] = {};				// new programmed speeds (two taps within one poll)
	size_t recentSentNext = 0;

	bool IsOwnEcho(int32_t rpm)
	{
		const uint32_t now = SystemTick::GetTickCount();
		for (size_t i = 0; i < NumRecentSent; ++i)
		{
			if (recentSent[i] == rpm && recentSent[i] != 0 && now - recentSentTime[i] < EchoTime)
			{
				return true;
			}
		}
		return false;
	}
	int32_t spindleMax = 0;										// 0 = unknown
	int32_t spindleMin = 0;

	// Tool change prompt
	bool promptOpen = false;
	uint32_t promptSeq = 0;
	bool blinkOn = true;
	uint32_t blinkTime = 0;
	bool tcShown = false;
	bool tcOkShown = false;

	size_t ToolIndex(int tool)
	{
		return (tool >= 0 && tool <= MaxToolNumber) ? (size_t)tool : NoToolIndex;
	}

	int CurrentPct(size_t row)
	{
		return overrides[row][ToolIndex(currentTool)];
	}

	void ResetOverrides()
	{
		for (size_t r = 0; r < NumRows; ++r)
		{
			for (uint16_t& v : overrides[r])
			{
				v = 100;
			}
		}
	}

	void FormatTime(String<12>& s, uint32_t seconds)
	{
		s.printf("%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600), (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
	}

	// ---- Look ----
	void SetFilled(ModernTextButton *b, Colour fill, bool enabled)
	{
		if (enabled)
		{
			b->SetColours(ButtonText, fill);
			b->SetBorderColour(fill);
		}
		else
		{
			b->SetColours(Border, Tile);
			b->SetBorderColour(Border);
		}
	}

	void UpdateNameAndResult()
	{
		if (inProgress || result != Result::None)
		{
			nameTile->SetText(nameText.IsEmpty() ? "-" : nameText.c_str());
			nameTile->SetTextColour(Text);
		}
		else
		{
			nameTile->SetText("No job yet");
			nameTile->SetTextColour(Muted);
		}

		if (inProgress)
		{
			resultText.printf("%u%%", progress);
			Colour c = Green;
			switch (status)
			{
			case OM::PrinterStatus::paused:
			case OM::PrinterStatus::pausing:
			case OM::PrinterStatus::resuming:
				c = Cyan;
				break;
			case OM::PrinterStatus::cancelling:
				c = StopRed;
				break;
			case OM::PrinterStatus::toolChange:
				c = Amber;
				break;
			default:
				if (promptOpen || (nextTool >= 0 && nextTool != currentTool))
				{
					c = Amber;							// RRF reports 'processing' during a tool change in a job
				}
				break;
			}
			resultTile->SetOutline(c, 3);
			resultTile->SetTextColour(Text);
		}
		else if (result == Result::Done)
		{
			resultText.copy("DONE");
			resultTile->SetOutline(Green, 3);
			resultTile->SetTextColour(Text);
		}
		else if (result == Result::Aborted)
		{
			resultText.copy("ABORTED");
			resultTile->SetOutline(StopRed, 3);
			resultTile->SetTextColour(Text);
		}
		else
		{
			resultText.copy("-");
			resultTile->SetOutline(Border, 2);
			resultTile->SetTextColour(Muted);
		}
		resultTile->SetText(resultText.c_str());
	}

	void UpdateTimes()
	{
		if (haveElapsed && (inProgress || result != Result::None))
		{
			FormatTime(elapsedText, elapsedSeconds);
		}
		else
		{
			elapsedText.copy("-");
		}
		elapsedField->SetValue(elapsedText.c_str());
		if (!inProgress)
		{
			leftText.copy("-");
			leftField->SetValue(leftText.c_str());
		}
	}

	void UpdateOverrideRow(size_t row)
	{
		const int pct = CurrentPct(row);
		rowLabelText[row].copy(row == 0 ? "FEED WORK" : "SPINDLE");
		if (inProgress && currentTool >= 0)
		{
			rowLabelText[row].catf("  T%d", currentTool);
		}
		rowLabels[row]->SetValue(rowLabelText[row].c_str());

		valueText[row].printf("%d%%", pct);
		valueButtons[row]->SetText(valueText[row].c_str());
		valueButtons[row]->SetColours(inProgress ? Text : Muted, Tile);
		SetButtonLocked(minusButtons[row], !inProgress || pct <= MinPct[row]);
		SetButtonLocked(plusButtons[row], !inProgress || pct >= MaxPct[row]);
		for (size_t s = 0; s < NumSteps; ++s)
		{
			SetButtonLocked(stepButtons[row][s], !inProgress);
			stepButtons[row][s]->Press(inProgress && s == stepIndex[row], 0);	// selection shown only during a job
		}
		selectedStep[row] = stepButtons[row][stepIndex[row]];
	}

	void UpdateOverrideRows()
	{
		for (size_t r = 0; r < NumRows; ++r)
		{
			UpdateOverrideRow(r);
		}
	}

	void UpdatePauseAbort()
	{
		const char *pauseText = "PAUSE";
		Colour pauseFill = Cyan;
		bool pauseEnabled = inProgress;
		switch (status)
		{
		case OM::PrinterStatus::paused:
			pauseText = "RESUME";
			pauseFill = Green;
			break;
		case OM::PrinterStatus::pausing:
			pauseText = "PAUSING";
			pauseEnabled = false;
			break;
		case OM::PrinterStatus::resuming:
			pauseText = "RESUMING";
			pauseEnabled = false;
			break;
		case OM::PrinterStatus::cancelling:
		case OM::PrinterStatus::connecting:
			pauseEnabled = false;
			break;
		default:
			break;
		}
		pauseButton->SetText(pauseText);
		SetFilled(pauseButton, pauseFill, pauseEnabled);

		const bool aborting = abortPending || status == OM::PrinterStatus::cancelling;
		abortButton->SetText(aborting ? "ABORTING" : "ABORT");
		SetFilled(abortButton, StopRed, inProgress && !aborting);
	}

	// Tool change row. The OK button sits on the tile, so it is redrawn after the tile.
	void RedrawToolRow()
	{
		mgr.Redraw(tcTile);
		if (tcOkShown)
		{
			mgr.Redraw(tcOkButton);
		}
	}

	void UpdateToolRow()
	{
		const bool atc = inProgress && nextTool >= 0 && nextTool != currentTool;
		const bool show = promptOpen || atc;
		if (promptOpen)
		{
			tcTile->SetText(tcLine1.c_str(), tcLine2.IsEmpty() ? nullptr : tcLine2.c_str());
			tcTile->SetTextRight(TcOkW + 8);
			tcTile->SetOutline(blinkOn ? Amber : Border, 3);
		}
		else if (atc)
		{
			tcLine1.copy("TOOL CHANGE  ");
			if (currentTool >= 0)
			{
				tcLine1.catf("T%d", currentTool);
			}
			else
			{
				tcLine1.cat("T-");
			}
			tcLine1.catf(" >>> T%d", nextTool);
			tcTile->SetText(tcLine1.c_str());
			tcTile->SetTextRight(0);
			tcTile->SetOutline(Amber, 3);
		}

		if (show != tcShown)
		{
			tcShown = show;
			mgr.Show(tcTile, show);
		}
		if (promptOpen != tcOkShown)
		{
			tcOkShown = promptOpen;
			mgr.Show(tcOkButton, promptOpen);
		}
		if (show)
		{
			RedrawToolRow();
		}
	}

	void UpdateAux()
	{
		auxButton->SetText(AuxLabel());
		SetToggleLook(auxButton, AuxOn());
	}

	void AuxChanged()
	{
		UpdateAux();
	}

	void CustomChanged()
	{
		for (size_t k = 0; k < 2; ++k)
		{
			const size_t slot = 1 + k;
			customButtons[k]->SetText(CustomLabel(slot));
			mgr.Show(customButtons[k], CustomUsed(slot));	// draws only while JOB STATUS is on screen
		}
	}

	void UpdateAll()
	{
		UpdateNameAndResult();
		UpdateTimes();
		UpdateOverrideRows();
		UpdatePauseAbort();
		UpdateToolRow();
	}

	// ---- Feed override (M220) ----
	void SendFeed(int pct)
	{
		SerialIo::Sendf("M220 S%d\n", pct);
		feedSent = pct;
		feedSentTime = SystemTick::GetTickCount();
	}

	// ---- Spindle override (emulated) ----
	// Paused (or on the way): pause.g / resume.g own the spindle (M5, M3 R1 restores the speed
	// from before the pause), so the emulation neither learns nor sends anything then.
	bool SpindleFrozen()
	{
		return status == OM::PrinterStatus::pausing || status == OM::PrinterStatus::paused
			|| status == OM::PrinterStatus::resuming || status == OM::PrinterStatus::cancelling
			|| status == OM::PrinterStatus::connecting;
	}

	void ApplySpindle()
	{
		if (!inProgress || SpindleFrozen() || !spindleRunning || spindleActive == 0 || spindleProgrammed == 0)
		{
			return;							// spindle off (M5 keeps 'active'): the new value applies when the job switches it on
		}
		const int pct = CurrentPct(1);
		int32_t desired = (int32_t)(((int64_t)spindleProgrammed * pct + 50) / 100);
		if (spindleMax > 0 && desired > spindleMax)
		{
			desired = spindleMax;			// RRF would clamp it and the value would never come back
		}
		if (spindleMin > 0 && desired < spindleMin)
		{
			desired = spindleMin;
		}
		if (desired == spindleActive)
		{
			return;
		}
		const uint32_t now = SystemTick::GetTickCount();
		if (desired == spindleSent)
		{
			if (now - spindleSentTime < EchoTime || spindleRetries >= MaxSpindleRetries)
			{
				return;						// on its way, or RRF does not take it: stop asking
			}
			++spindleRetries;
		}
		else
		{
			spindleRetries = 0;
		}
		SerialIo::Sendf("%s S%ld\n", spindleReverse ? "M4" : "M3", (long)desired);
		spindleSent = desired;
		spindleSentTime = now;
		recentSent[recentSentNext] = desired;
		recentSentTime[recentSentNext] = now;
		recentSentNext = (recentSentNext + 1) % NumRecentSent;
	}

	void SetPct(size_t row, int pct)
	{
		if (pct < MinPct[row]) { pct = MinPct[row]; }
		if (pct > MaxPct[row]) { pct = MaxPct[row]; }
		overrides[row][ToolIndex(currentTool)] = (uint8_t)pct;
		if (row == 0)
		{
			SendFeed(pct);
		}
		else
		{
			ApplySpindle();
		}
		UpdateOverrideRow(row);
	}

	void PctEntered(int row, float value)
	{
		if (inProgress)
		{
			SetPct((size_t)row, (int)(value + 0.5f));
		}
	}

	void OpenPctNumpad(size_t row)
	{
		CncPopup::NumpadSpec pad;
		pad.tag = (row == 0) ? "FEED" : "SPINDLE";
		pad.unit = "%";
		pad.value = (float)CurrentPct(row);
		pad.decimals = 0;
		pad.allowDecimal = false;
		pad.allowMinus = false;
		pad.min = (float)MinPct[row];
		pad.max = (float)MaxPct[row];
		pad.pos = nullptr;
		pad.onOk = PctEntered;
		pad.param = (int)row;
		CncPopup::Numpad(pad);
	}

	// ---- PAUSE / RESUME / ABORT ----
	void DoPause(int param)
	{
		UNUSED(param);
		if (inProgress && status != OM::PrinterStatus::paused && status != OM::PrinterStatus::pausing)
		{
			SerialIo::Sendf("M25\n");
		}
	}

	void DoResume(int param)
	{
		UNUSED(param);
		if (inProgress && status == OM::PrinterStatus::paused)
		{
			SerialIo::Sendf("M24\n");
		}
	}

	// Popups the panel opens by itself must not replace a prompt the machine waits for
	bool CanShowAutoPopup()
	{
		return !CncPopup::IsBlockingMessageOpen() && !CncProbe::JogPromptOpen() && !CncProbe::AutoProbeRunning();
	}

	void DoAbort(int param)
	{
		UNUSED(param);
		if (!inProgress)
		{
			return;
		}
		panelAbort = true;
		if (status == OM::PrinterStatus::paused)
		{
			SerialIo::Sendf("M0\n");				// cancels the paused job
		}
		else
		{
			SerialIo::Sendf("M25\n");				// pause first, M0 follows once paused
			abortPending = true;
			abortStart = SystemTick::GetTickCount();
		}
		UpdatePauseAbort();
	}

	void JobStarted()
	{
		result = Result::None;
		cancelSeen = false;
		panelAbort = false;
		abortPending = false;
		progress = 0;
		elapsedSeconds = 0;
		haveElapsed = true;
		leftText.copy("-");
		leftField->SetValue(leftText.c_str());
		ResetOverrides();
		feedSent = -1;
		spindleSent = 0;
		spindleRetries = 0;
		promptOpen = false;
	}

	void JobEnded(OM::PrinterStatus oldStatus, OM::PrinterStatus newStatus)
	{
		// DONE only when the job runs out into idle. From paused it was cancelled (M0), even if
		// 'cancelling' was never seen; halted / off / a reset (lost connection) is not a finished job.
		const bool normalEnd = newStatus == OM::PrinterStatus::idle
			&& (oldStatus == OM::PrinterStatus::printing || oldStatus == OM::PrinterStatus::simulating
				|| oldStatus == OM::PrinterStatus::busy || oldStatus == OM::PrinterStatus::toolChange);
		// ABORT still waiting for the pause (M0 not sent) when the job ran out: it finished (DONE)
		const bool aborted = cancelSeen || (panelAbort && !abortPending) || !normalEnd;
		result = aborted ? Result::Aborted : Result::Done;
		abortPending = false;
		promptOpen = false;
		nextTool = -1;
		ResetOverrides();
		if (speedFactor >= 0 && speedFactor != 100)
		{
			SendFeed(100);
		}
		if (!aborted && CanShowAutoPopup())
		{
			String<12> t;
			FormatTime(t, elapsedSeconds);
			String<80> info;
			info.printf("%s\nTime %s", nameText.IsEmpty() ? "Job finished." : nameText.c_str(), t.c_str());
			CncPopup::Notice("JOB DONE", info.c_str());
		}
	}

	// Two lines of the tool change prompt, word-wrapped by pixel width
	void WrapPrompt(const char *text)
	{
		constexpr PixelNumber W = ContentW - 28 - (TcOkW + 8);
		tcLine1.Clear();
		tcLine2.Clear();
		lcd.setFont(glcd19x21);
		const char *p = text;
		while (*p == ' ') { ++p; }
		size_t len = 0, fit = 0;
		while (p[len] != 0 && p[len] != '\n' && len < tcLine1.Capacity())
		{
			++len;
			if (DisplayField::GetTextWidth(p, 9999, len) > W)
			{
				if (fit == 0) { fit = len - 1; }
				break;
			}
			if (p[len] == 0 || p[len] == ' ' || p[len] == '\n')
			{
				fit = len;
			}
		}
		if (fit == 0) { fit = len; }
		tcLine1.copy(p, fit);
		p += fit;
		while (*p == ' ' || *p == '\n') { ++p; }
		tcLine2.copy(p);								// clipped at the tile edge if still too long
	}

	void CreateStatus()
	{
		// File name + progress / result
		DisplayField::SetDefaultColours(Text, Tile);
		nameTile = new CncTextTile(NameY, NameX, NameW, NameH, glcd19x21, TextAlignment::Left);
		mgr.AddField(nameTile);
		resultTile = new CncTextTile(NameY, ResultX, ResultW, NameH, glcd19x21, TextAlignment::Centre);
		mgr.AddField(resultTile);

		// ELAPSED / LEFT
		DisplayField::SetDefaultFont(glcd28x32);
		DisplayField::SetDefaultColours(Text, Tile);
		elapsedText.copy("-");
		leftText.copy("-");
		elapsedField = new StaticTextField(TimesY + (TimesH - 32) / 2, ElapsedX + 100, TimeW - 112, TextAlignment::Right, elapsedText.c_str());
		mgr.AddField(elapsedField);
		leftField = new StaticTextField(TimesY + (TimesH - 32) / 2, LeftX + 100, TimeW - 112, TextAlignment::Right, leftText.c_str());
		mgr.AddField(leftField);
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, Tile);
		mgr.AddField(new StaticTextField(TimesY + (TimesH - 21) / 2 + 1, ElapsedX + 12, 96, TextAlignment::Left, "ELAPSED"));
		mgr.AddField(new StaticTextField(TimesY + (TimesH - 21) / 2 + 1, LeftX + 12, 96, TextAlignment::Left, "LEFT"));
		AddCard(TimesY, ElapsedX, TimeW, TimesH);
		AddCard(TimesY, LeftX, TimeW, TimesH);

		// Override rows
		for (size_t r = 0; r < NumRows; ++r)
		{
			const PixelNumber ly = (r == 0) ? Label1Y : Label2Y;
			const PixelNumber y = (r == 0) ? Ovr1Y : Ovr2Y;
			rowLabelText[r].copy(r == 0 ? "FEED WORK" : "SPINDLE");
			rowLabels[r] = AddLabel(ly, Margin + 2, 200, rowLabelText[r].c_str());
			AddLabel(ly, ActualX, ActualW, "ACTUAL", TextAlignment::Right);
			AddLabel(ly, StepX, ScreenW - Margin - StepX, "STEP", TextAlignment::Right);

			minusButtons[r] = AddButton(y, MinusX, PmW, OvrH, "-", evCncJobOvrMinus, (int)r, glcd28x32);
			valueText[r].copy("100%");
			valueButtons[r] = AddButton(y, ValueX, ValueW, OvrH, valueText[r].c_str(), evCncJobOvrValue, (int)r, glcd28x32);
			plusButtons[r] = AddButton(y, PlusX, PmW, OvrH, "+", evCncJobOvrPlus, (int)r, glcd28x32);

			DisplayField::SetDefaultFont(glcd28x32);
			DisplayField::SetDefaultColours(Muted, Tile);
			actualFields[r] = new IntegerField(y + (OvrH - 32) / 2, ActualX + 6, ActualW - 18, TextAlignment::Right);
			mgr.AddField(actualFields[r]);
			AddCard(y, ActualX, ActualW, OvrH);

			for (size_t s = 0; s < NumSteps; ++s)
			{
				stepButtons[r][s] = AddButton(y, StepX + s * StepPitch, StepW, OvrH, StepNames[s], evCncJobOvrStep, (int)(r * 4 + s));
			}
		}

		// COOLANT / VACUUM, CUSTOM 2, CUSTOM 3 (slots 1 and 2; CUSTOM 1 is on CONTROL)
		auxButton = AddButton(BtnY, BtnX(0), BtnW, BtnH, AuxLabel(), evCncJobAux, 0);
		for (size_t k = 0; k < 2; ++k)
		{
			customButtons[k] = AddButton(BtnY, BtnX(1 + k), (k == 1) ? ScreenW - Margin - BtnX(2) : BtnW, BtnH, "", evCncJobCustom, (int)(1 + k));
		}

		// Tool change row: OK first so it is drawn on top of the tile
		DisplayField::SetDefaultColours(ButtonText, Green, Green, Green, Green, Green, IconPaletteDark);
		tcOkButton = new ModernTextButton(TcOkY, TcOkX, TcOkW, TcOkH, "OK", evCncJobToolOk, 0, glcd28x32, true);
		mgr.AddField(tcOkButton);
		tcTile = new CncTextTile(TcY, Margin, ContentW, TcH, glcd19x21, TextAlignment::Left);
		mgr.AddField(tcTile);
		tcOkButton->Show(false);
		tcTile->Show(false);

		// PAUSE / RESUME, ABORT
		DisplayField::SetDefaultColours(ButtonText, Cyan, Cyan, Cyan, Cyan, Cyan, IconPaletteDark);
		pauseButton = new ModernTextButton(PauseY, PauseX, PauseW, PauseH, "PAUSE", evCncJobPause, 0, glcd19x21, true);
		mgr.AddField(pauseButton);
		DisplayField::SetDefaultColours(ButtonText, StopRed, StopRed, StopRed, StopRed, StopRed, IconPaletteDark);
		abortButton = new ModernTextButton(PauseY, AbortX, AbortW, PauseH, "ABORT", evCncJobAbort, 0, glcd19x21, true);
		mgr.AddField(abortButton);
	}

	bool ProcessStatusTouch(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncJobOvrMinus:
		case evCncJobOvrPlus:
			{
				const size_t row = (size_t)bp.GetIParam();
				const int dir = (bp.GetEvent() == evCncJobOvrPlus) ? 1 : -1;
				const int pct = CurrentPct(row);
				if (!inProgress)
				{
					Refuse("Overrides work only while a job is running.");
				}
				else if ((dir > 0 && pct >= MaxPct[row]) || (dir < 0 && pct <= MinPct[row]))
				{
					String<64> reason;
					reason.printf("%s override is at its limit (%d to %d %%).", (row == 0) ? "Feed" : "Spindle", MinPct[row], MaxPct[row]);
					Refuse(reason.c_str());
				}
				else
				{
					mgr.Press(bp, true);
					SetPct(row, pct + dir * StepPct[stepIndex[row]]);
				}
			}
			return true;

		case evCncJobOvrValue:
			if (inProgress)
			{
				OpenPctNumpad((size_t)bp.GetIParam());
			}
			else
			{
				Refuse("Overrides work only while a job is running.");
			}
			return true;

		case evCncJobOvrStep:
			{
				const size_t row = (size_t)bp.GetIParam() / 4;
				const size_t s = (size_t)bp.GetIParam() % 4;
				if (!inProgress)
				{
					Refuse("Overrides work only while a job is running.");
				}
				else if (row < NumRows && s < NumSteps)
				{
					stepIndex[row] = s;
					Select(selectedStep[row], bp.GetButton());
				}
			}
			return true;

		case evCncJobAux:
			ToggleAux();
			return true;

		case evCncJobCustom:
			RunCustomMacro((size_t)bp.GetIParam());
			return true;

		case evCncJobToolOk:
			if (promptOpen)
			{
				SerialIo::Sendf("M292 P0 S%lu\n", (unsigned long)promptSeq);
				promptOpen = false;
				UpdateToolRow();
				UpdateNameAndResult();
			}
			return true;

		case evCncJobPause:
			if (!inProgress)
			{
				Refuse("No job is running.");
			}
			else if (status == OM::PrinterStatus::paused)
			{
				CncPopup::Confirm("ALERT !", "Do you want to RESUME", "this job?", nullptr, nullptr, DoResume, 0);
			}
			else if (status == OM::PrinterStatus::pausing || status == OM::PrinterStatus::resuming
						|| status == OM::PrinterStatus::cancelling)
			{
				Refuse("Wait until the machine has finished pausing or resuming.");
			}
			else
			{
				CncPopup::Confirm("ALERT !", "Do you want to PAUSE", "this job?", nullptr, nullptr, DoPause, 0);
			}
			return true;

		case evCncJobAbort:
			if (!inProgress)
			{
				Refuse("No job is running.");
			}
			else if (abortPending || status == OM::PrinterStatus::cancelling)
			{
				Refuse("The job is already being aborted.");
			}
			else
			{
				CncPopup::Confirm("ALERT !", "Do you want to ABORT", "this job?", nullptr, nullptr, DoAbort, 0);
			}
			return true;

		default:
			return false;
		}
	}
}

// ---------------------------------------------------------------------------
// JOB LIST (mock-ups pd_cnc_job_list_v3_*.svg)
//
//   FILES                                0:/gcodes      or   SEARCH "brack"      2 of 14
//   [ fixtures                    FOLDER ]  [SEARCH]
//   [ bracket_v2.nc                      ]  [  ^   ]
//   ...  10 rows                            [  v   ]
//                                           [  SD  ]    only when a second card is mounted
//
// The listing is the panel's cached M20 listing of the open folder (FileManager), so paging
// and search send nothing to the machine. Tap a file to select it, tap it again to run it
// (standard "ALERT !" confirmation, M32). Tap a folder to open it; ".." goes back up.
// SEARCH opens the shared keyboard; the list filters as you type (case-insensitive, files of
// the open folder only). ENTER keeps the filter, X clears it.
// ---------------------------------------------------------------------------
namespace
{
	namespace JL = CncLayout::JobList;

	constexpr uint8_t ParentEntry = 0xFF;						// ".." row: parent folder
	constexpr uint8_t RootEntry = 0xFE;							// ".." row after a listing error: card root
	constexpr size_t MaxEntries = 100;							// FileManager keeps up to 96 per folder (+ "..")
	constexpr size_t MaxVolumes = 8;

	CncFileRow *fileRows[JL::NumRows];
	CncGlyphButton *searchTile;
	CncArrowButton *listUp, *listDown;
	ModernTextButton *sdTile;
	StaticTextField *labelLeft, *labelQuery, *labelRight, *emptyField;

	uint8_t view[MaxEntries];									// indices into the FileManager listing
	size_t viewCount = 0;
	size_t fileTotal = 0;										// files (not folders) in the open folder
	size_t listScroll = 0;										// first entry shown
	bool searching = false;										// SEARCH is on (filter shown in the label row)
	bool kbMode = false;										// keyboard sheet open over the list
	int listError = 0;											// last M20 error (0 = none)
	uint8_t mountedVolumes = 1;									// bit per SD card, card 0 always
	bool haveListPath = false;

	String<CncKeyboard::MaxText> query;
	String<FileManager::maxPathLength> listPath;				// folder of the rows shown
	String<FileManager::maxPathLength> selectedName;			// selected file (empty = none)
	String<FileManager::maxPathLength> runDir;
	String<FileManager::maxPathLength> runName;
	String<40> queryText;
	String<48> rightText;
	String<48> emptyText;

	bool FilterOn()
	{
		return searching && !query.IsEmpty();
	}

	unsigned int VisibleRows()
	{
		return kbMode ? JL::NumRowsKeyboard : JL::NumRows;
	}

	// Case-insensitive position of 'q' in 's', -1 if not found
	int MatchPos(const char *s, const char *q)
	{
		const size_t ql = strlen(q);
		for (size_t i = 0; s[i] != 0; ++i)
		{
			size_t k = 0;
			while (k < ql && s[i + k] != 0 && tolower((unsigned char)s[i + k]) == tolower((unsigned char)q[k]))
			{
				++k;
			}
			if (k == ql)
			{
				return (int)i;
			}
		}
		return -1;
	}

	// Rebuild the list of rows from the cached listing and the filter
	void RebuildView()
	{
		viewCount = 0;
		fileTotal = 0;
		if (!FileManager::IsJobListLoaded())
		{
			if (listError != 0)
			{
				view[viewCount++] = RootEntry;					// way out of a folder that cannot be read
			}
			return;
		}
		const size_t n = min<size_t>(FileManager::GetJobFileCount(), ParentEntry);
		if (!FilterOn() && FileManager::IsJobListInSubdir())
		{
			view[viewCount++] = ParentEntry;
		}
		bool selectedSeen = false;
		for (size_t i = 0; i < n && viewCount < MaxEntries; ++i)
		{
			const char * const e = FileManager::GetJobFile(i);
			const bool folder = (e[0] == '*');
			if (folder && strcasecmp(e + 1, "macros") == 0
				&& (strcmp(FileManager::GetFilesDir(), "0:/") == 0 || strcmp(FileManager::GetFilesDir(), "0:") == 0))
			{
				continue;									// root 0:/ (old firmware): its listing would go to MACROS
			}
			if (!folder)
			{
				++fileTotal;
			}
			if (FilterOn() && (folder || MatchPos(e, query.c_str()) < 0))
			{
				continue;
			}
			if (!folder && selectedName.Equals(e))
			{
				selectedSeen = true;
			}
			view[viewCount++] = (uint8_t)i;
		}
		if (!selectedSeen)
		{
			selectedName.Clear();								// selected file gone or filtered out
		}
		const unsigned int rows = VisibleRows();
		if (listScroll >= viewCount)
		{
			listScroll = (viewCount == 0) ? 0 : ((viewCount - 1) / rows) * rows;
		}
	}

	// Keep the tail of a path that fits in 'width' pixels, with ".." in front when cut
	void FitPath(String<48>& out, const char *prefix, const char *path, PixelNumber width)
	{
		lcd.setFont(glcd19x21);
		const char *p = path;
		for (;;)
		{
			out.copy(prefix);
			if (p != path)
			{
				out.cat("..");
			}
			out.cat(p);
			if (*p == 0 || DisplayField::GetTextWidth(out.c_str(), 9999, out.strlen()) <= width)
			{
				return;
			}
			++p;
		}
	}

	void UpdateLabel()
	{
		const char * const path = FileManager::GetFilesDir();
		if (searching)
		{
			// Move the right text out of the way first: hiding it clears its old (wider) area
			if (labelRight->GetMinX() != JL::RightSearchX)
			{
				mgr.Show(labelRight, false);
				labelRight->SetPositionAndWidth(JL::RightSearchX, JL::RightSearchW);
			}
			labelLeft->SetValue("SEARCH");
			queryText.copy("\"");
			lcd.setFont(glcd19x21);
			size_t n = query.strlen();
			while (n > 0 && DisplayField::GetTextWidth(query.c_str(), 9999, n) > JL::QueryTextW)
			{
				--n;
			}
			queryText.catn(query.c_str(), n);
			if (n < query.strlen())
			{
				queryText.cat("..");
			}
			queryText.cat('"');
			labelQuery->SetValue(queryText.c_str(), true);
			mgr.Show(labelQuery, true);

			String<24> counts;
			counts.printf("%u of %u", (unsigned int)(FilterOn() ? viewCount : fileTotal), (unsigned int)fileTotal);
			String<48> full;
			full.printf("%s in %s", counts.c_str(), path);
			lcd.setFont(glcd19x21);
			rightText.copy((!full.IsEmpty() && full.strlen() < full.Capacity()
							&& DisplayField::GetTextWidth(full.c_str(), 9999, full.strlen()) <= JL::RightSearchW) ? full.c_str() : counts.c_str());
		}
		else
		{
			labelLeft->SetValue("FILES");
			mgr.Show(labelQuery, false);
			if (labelRight->GetMinX() != JL::RightX)
			{
				mgr.Show(labelRight, false);
				labelRight->SetPositionAndWidth(JL::RightX, JL::RightW);
			}
			FitPath(rightText, "", path, JL::RightW);
		}
		labelRight->SetValue(rightText.c_str(), true);
		mgr.Show(labelRight, true);
	}

	unsigned int VisibleVolumes()
	{
		return mountedVolumes & ~1u;
	}

	void ShowRows()
	{
		if (sdTile == nullptr)
		{
			return;												// machine data before the page exists
		}
		const unsigned int rows = VisibleRows();

		// Message in row 2 (loading, error, nothing there, nothing matches). Hiding it clears its
		// area, and hiding a row clears the message: hide before the rows, show after them.
		const bool loaded = FileManager::IsJobListLoaded();
		const bool message = (viewCount == 0) || (!loaded && listError != 0);
		if (!message && emptyField->IsVisible())
		{
			mgr.Show(emptyField, false);
		}

		for (unsigned int r = 0; r < JL::NumRows; ++r)
		{
			CncFileRow * const row = fileRows[r];
			const size_t vi = listScroll + r;
			const bool vis = (r < rows && vi < viewCount);
			if (vis)
			{
				const uint8_t e = view[vi];
				if (e == ParentEntry || e == RootEntry)
				{
					row->SetEntry("..", (e == ParentEntry) ? "UP" : "BACK");
					row->SetSelected(false);
				}
				else
				{
					const char * const name = FileManager::GetJobFile(e);
					if (name[0] == '*')
					{
						row->SetEntry(name + 1, "FOLDER");
						row->SetSelected(false);
					}
					else
					{
						const int m = FilterOn() ? MatchPos(name, query.c_str()) : -1;
						row->SetEntry(name, nullptr, (m < 0) ? 0 : (size_t)m, (m < 0) ? 0 : query.strlen());
						row->SetSelected(!selectedName.IsEmpty() && selectedName.Equals(name));
					}
				}
			}
			mgr.Show(row, vis);
		}

		// Side column
		searchTile->SetActive(searching);
		const bool upOk = listScroll > 0, downOk = listScroll + rows < viewCount;
		listUp->SetDisabled(!upOk);
		listDown->SetDisabled(!downOk);
		mgr.Show(listUp, !kbMode);
		mgr.Show(listDown, !kbMode);
		const unsigned int card = FileManager::GetJobCardNumber();
		SetToggleLook(sdTile, card != 0);
		mgr.Show(sdTile, !kbMode && (VisibleVolumes() != 0 || card != 0));
		if (kbMode)
		{
			CncKeyboard::SetArrowsEnabled(upOk, downOk);
		}
		UpdateLabel();

		if (message)
		{
			if (!loaded)
			{
				emptyText.copy((listError != 0) ? "Cannot read this folder." : "Loading...");
			}
			else if (FilterOn())
			{
				emptyText.copy("No files match ");
				emptyText.cat(queryText.c_str());				// quoted and shortened by UpdateLabel
			}
			else
			{
				emptyText.copy("No files");
			}
			emptyField->SetValue(emptyText.c_str(), true);
			mgr.Show(emptyField, true);
		}
	}

	// The folder shown changed (or another card): start at the top, nothing selected, no search
	void ListFolderChanged()
	{
		haveListPath = false;									// the next listing is expected, not a change from elsewhere
		listScroll = 0;
		selectedName.Clear();
		if (kbMode)
		{
			CncKeyboard::Close();
			kbMode = false;
		}
		searching = false;
		query.Clear();
	}

	void RequestList()
	{
		listError = 0;
		FileManager::DisplayFilesPage();						// M20 of the open folder (the answer calls FilesChanged)
	}

	// ---- RUN ----
	void DoRun(int param)
	{
		UNUSED(param);
		if (JobInProgress())
		{
			Refuse("A job is already running.");
			return;
		}
		if (status != OM::PrinterStatus::idle)
		{
			Refuse((status == OM::PrinterStatus::off) ? "The machine power is off." : "Wait until the machine is idle.");
			return;
		}
		SerialIo::Sendf("M32 ");
		SerialIo::SendFilename(CondStripDrive(runDir.c_str()), runName.c_str());
		SerialIo::Sendf("\n");									// the job name comes from the machine once it runs
	}

	// M30 wants the path relative to the gcodes folder ("0:/gcodes/" is added by RRF), as the Modern UI sends it
	const char *StripGcodesFolder(const char *dir)
	{
		if (strcmp(dir, "/gcodes") == 0 || strcmp(dir, "0:/gcodes") == 0)
		{
			return "";
		}
		if (strncmp(dir, "/gcodes/", 8) == 0)
		{
			return dir + 8;
		}
		if (strncmp(dir, "0:/gcodes/", 10) == 0)
		{
			return dir + 10;
		}
		return dir;
	}

	void ShowRunConfirm();

	// ---- DELETE (the trash button of the RUN question, as in the Modern UI) ----
	void DoDelete(int param)
	{
		UNUSED(param);
		if (JobInProgress())
		{
			Refuse("A job is already running.");
			return;
		}
		SerialIo::Sendf("M30 ");
		SerialIo::SendFilename(CondStripDrive(StripGcodesFolder(runDir.c_str())), runName.c_str());
		SerialIo::Sendf("\n");
		RequestList();										// the list without the deleted job
	}

	void BackToRun(int param)
	{
		UNUSED(param);
		ShowRunConfirm();									// X on the delete question: the RUN question again
	}

	void AskDelete(int param)
	{
		UNUSED(param);
		CncPopup::ConfirmWithCancel("ALERT !", "Do you want to DELETE", "this job?", runName.c_str(), nullptr, DoDelete, BackToRun, 0);
	}

	void ShowRunConfirm()
	{
		CncPopup::ConfirmDeletable("ALERT !", "Do you want to RUN", "this job?", runName.c_str(), nullptr, DoRun, AskDelete, 0);
	}

	void AskRun(const char *name)
	{
		if (JobInProgress())
		{
			Refuse("A job is already running.");
			return;
		}
		if (status == OM::PrinterStatus::connecting)
		{
			Refuse("No connection to the machine.");
			return;
		}
		if (status != OM::PrinterStatus::idle)
		{
			Refuse((status == OM::PrinterStatus::off) ? "The machine power is off." : "Wait until the machine is idle.");
			return;
		}
		runDir.copy(FileManager::GetFilesDir());
		runName.copy(name);
		ShowRunConfirm();										// RUN question: trash / X / check
	}

	// ---- Keyboard ----
	void KbChanged(const char *text)
	{
		query.copy(text);
		listScroll = 0;
		RebuildView();
		ShowRows();
	}

	void KbEnter(const char *text)
	{
		query.copy(text);
		kbMode = false;
		if (query.IsEmpty())
		{
			searching = false;									// nothing typed: back to the plain list
		}
		listScroll = 0;
		RebuildView();
		ShowRows();
	}

	void KbCancel()
	{
		kbMode = false;
		searching = false;
		query.Clear();
		listScroll = 0;
		RebuildView();
		ShowRows();
	}

	void KbArrow(int dir)
	{
		const unsigned int rows = VisibleRows();
		if (dir < 0 && listScroll > 0)
		{
			listScroll = (listScroll > rows) ? listScroll - rows : 0;
		}
		else if (dir > 0 && listScroll + rows < viewCount)
		{
			listScroll += rows;
		}
		ShowRows();
	}

	const CncKeyboard::Client kbClient = { KbChanged, KbEnter, KbCancel, KbArrow, false };

	void OpenSearchKeyboard()
	{
		searching = true;
		kbMode = true;
		listScroll = 0;
		RebuildView();
		ShowRows();												// hides what the sheet covers
		CncKeyboard::Open(query.c_str(), kbClient);
		ShowRows();												// keyboard arrows
	}

	// The keyboard closed without ENTER or X (another popup replaced it, or the page changed)
	void KeyboardGone()
	{
		if (kbMode)
		{
			kbMode = false;
			if (query.IsEmpty())
			{
				searching = false;
			}
			RebuildView();
			ShowRows();
		}
	}

	void NextCard()
	{
		const unsigned int card = FileManager::GetJobCardNumber();
		for (unsigned int k = 1; k <= MaxVolumes; ++k)
		{
			const unsigned int c = (card + k) % MaxVolumes;
			if ((c == 0 || (mountedVolumes & (1u << c)) != 0) && c != card && FileManager::SelectCard(c))
			{
				ListFolderChanged();
				listError = 0;
				RebuildView();
				ShowRows();
				return;
			}												// refused (volume count not known yet): try the next
		}
	}

	void CreateList(DisplayField *tabsRoot)
	{
		mgr.SetRoot(tabsRoot);
		const Colour accent = Accent();

		// Label row
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, PageBg);
		labelLeft = new StaticTextField(JL::LabelY, JL::LabelLeftX, JL::LabelLeftW, TextAlignment::Left, "FILES");
		mgr.AddField(labelLeft);
		DisplayField::SetDefaultColours(accent, PageBg);
		labelQuery = new StaticTextField(JL::LabelY, JL::QueryX, JL::QueryW, TextAlignment::Left, "");
		labelQuery->Show(false);
		mgr.AddField(labelQuery);
		DisplayField::SetDefaultColours(Muted, PageBg);
		labelRight = new StaticTextField(JL::LabelY, JL::RightX, JL::RightW, TextAlignment::Right, "");
		mgr.AddField(labelRight);

		// Rows
		for (unsigned int r = 0; r < JL::NumRows; ++r)
		{
			fileRows[r] = new CncFileRow(JL::RowY(r), JL::RowX, JL::RowW, JL::RowH, accent, evFile, (int)r);
			fileRows[r]->Show(false);
			mgr.AddField(fileRows[r]);
		}
		DisplayField::SetDefaultColours(Muted, PageBg);
		emptyField = new StaticTextField(JL::EmptyY, JL::RowX, JL::RowW, TextAlignment::Centre, "");
		emptyField->Show(false);
		mgr.AddField(emptyField);

		// Side column: SEARCH, up, down, SD
		searchTile = new CncGlyphButton(JL::SearchY, JL::SideX, JL::SideW, JL::SearchH, CncGlyph::Search, accent, evKeyboard, 0);
		mgr.AddField(searchTile);
		listUp = new CncArrowButton(JL::UpY, JL::SideX, JL::SideW, JL::ArrowH, true, evScrollFiles, -1);
		mgr.AddField(listUp);
		listDown = new CncArrowButton(JL::DownY, JL::SideX, JL::SideW, JL::ArrowH, false, evScrollFiles, 1);
		mgr.AddField(listDown);
		sdTile = AddButton(JL::SdY, JL::SideX, JL::SideW, JL::SdH, "SD", evChangeCard, 0);
		sdTile->Show(false);
	}

	bool ProcessListTouch(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evFile:
			{
				const size_t vi = listScroll + (size_t)bp.GetIParam();
				if ((unsigned int)bp.GetIParam() >= VisibleRows() || vi >= viewCount)
				{
					return true;
				}
				const uint8_t e = view[vi];
				if (e == ParentEntry)
				{
					ListFolderChanged();
					listError = 0;
					FileManager::RequestFilesPageParentDir();	// "Loading..." until the listing arrives
				}
				else if (e == RootEntry)
				{
					ListFolderChanged();
					listError = 0;
					FileManager::SelectCard(FileManager::GetJobCardNumber());	// back to the card root, new listing
					RebuildView();
					ShowRows();
				}
				else
				{
					const char * const name = FileManager::GetJobFile(e);
					if (name[0] == '*')
					{
						ListFolderChanged();
						listError = 0;
						FileManager::RequestFilesPageSubdir(name + 1);
					}
					else if (selectedName.Equals(name))
					{
						AskRun(name);							// second tap on the selected file
					}
					else
					{
						selectedName.copy(name);
						ShowRows();
					}
				}
			}
			return true;

		case evScrollFiles:
			mgr.Press(bp, true);
			KbArrow(bp.GetIParam());
			return true;

		case evKeyboard:
			OpenSearchKeyboard();								// new search, or edit the kept one
			return true;

		case evChangeCard:
			NextCard();
			return true;

		default:
			return false;
		}
	}
}

namespace CncJob
{
	void Create(DisplayField *baseRoot)
	{
		ResetOverrides();

		mgr.SetRoot(baseRoot);
		AddSubTabs(SubTabNames, NumSubTabs, subTabs);
		DisplayField * const tabsRoot = mgr.GetRoot();

		// JOB STATUS
		CreateStatus();
		subRoots[0] = mgr.GetRoot();

		// JOB LIST
		CreateList(tabsRoot);
		subRoots[1] = mgr.GetRoot();

		selectedSubTab = subTabs[0];
		subTabs[0]->Press(true, 0);
		for (size_t r = 0; r < NumRows; ++r)
		{
			selectedStep[r] = stepButtons[r][stepIndex[r]];
		}
		AddAuxListener(AuxChanged);
		AddCustomListener(CustomChanged);
		UpdateAux();
		for (size_t k = 0; k < 2; ++k)
		{
			customButtons[k]->SetText(CustomLabel(1 + k));
			customButtons[k]->Show(CustomUsed(1 + k));	// no drawing while the screen is built
		}
		UpdateAll();
	}

	DisplayField *CurrentRoot()
	{
		return subRoots[currentSub];
	}

	void ShowStatusTab()
	{
		if (kbMode)
		{
			CncKeyboard::Close();
			KeyboardGone();
		}
		currentSub = 0;
		Select(selectedSubTab, subTabs[0]);
	}

	void PageShown()
	{
		if (currentSub == 1)
		{
			RequestList();									// fresh listing each time JOB LIST is opened
			RebuildView();
			ShowRows();
		}
	}

	void FilesChanged()
	{
		const char * const path = FileManager::GetFilesDir();
		if (FileManager::IsJobListLoaded() && (!haveListPath || !listPath.Equals(path)))
		{
			if (haveListPath)
			{
				ListFolderChanged();						// opened elsewhere (another card, DWC does not change it)
			}
			listPath.copy(path);
			haveListPath = true;
		}
		for (CncFileRow *r : fileRows)
		{
			if (r != nullptr && r->IsVisible())
			{
				r->SetChanged();						// new listing in the same buffer: names may differ at the same address
			}
		}
		RebuildView();
		ShowRows();
	}

	void SetListError(int err)
	{
		listError = err;
	}

	void SetVolumeMounted(size_t volume, bool mounted)
	{
		if (volume == 0 || volume >= MaxVolumes)
		{
			return;
		}
		const uint8_t bit = (uint8_t)(1u << volume);
		const uint8_t old = mountedVolumes;
		mountedVolumes = mounted ? (mountedVolumes | bit) : (mountedVolumes & ~bit);
		if (mountedVolumes != old)
		{
			if (!mounted && FileManager::GetJobCardNumber() == volume)
			{
				FileManager::SelectCard(0);					// the card shown was removed
				ListFolderChanged();
				listError = 0;
				RebuildView();
			}
			ShowRows();
		}
	}

	bool KeyboardOpen()
	{
		return kbMode && CncKeyboard::IsOpenFor(&kbClient);
	}

	OutsideTouch TouchOutsideKeyboard(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evKeyboard:								// SEARCH while typing: leave search
			CncKeyboard::Close();
			KbCancel();
			return OutsideTouch::Used;

		case evFile:									// a row: keep the filter, then act on the row
		case evCncNav:									// another page: keep the filter
			CncKeyboard::Close();
			KeyboardGone();								// rows stay where they are, so the row index still fits
			return OutsideTouch::Process;

		default:
			return OutsideTouch::Ignored;
		}
	}

	bool StatusTabSelected()
	{
		return currentSub == 0;
	}

	bool ProcessTouch(ButtonPress bp, bool& redraw)
	{
		redraw = false;
		if (bp.GetEvent() == evCncSubTab)
		{
			const size_t sub = (size_t)bp.GetIParam();
			if (sub != currentSub && sub < NumSubTabs)
			{
				currentSub = sub;
				Select(selectedSubTab, bp.GetButton());
				redraw = true;							// ShowPage -> PageShown (new listing)
			}
			return true;
		}
		return (currentSub == 0) ? ProcessStatusTouch(bp) : ProcessListTouch(bp);
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncSubTab:
		case evCncJobOvrStep:
			return true;							// selection stays

		case evCncJobOvrMinus:
		case evCncJobOvrPlus:
		case evScrollFiles:
			mgr.Press(bp, false);
			return true;

		case evFile:
		case evKeyboard:
		case evChangeCard:
			return true;							// selection / toggle look, nothing to release

		case evCncJobOvrValue:
		case evCncJobAux:
		case evCncJobCustom:
		case evCncJobToolOk:
		case evCncJobPause:
		case evCncJobAbort:
			return true;							// never pressed (popup or toggle look)

		default:
			return false;
		}
	}

	bool StatusChanged(OM::PrinterStatus oldStatus, OM::PrinterStatus newStatus)
	{
		bool flag = inProgress;
		switch (newStatus)
		{
		case OM::PrinterStatus::printing:
		case OM::PrinterStatus::simulating:
		case OM::PrinterStatus::pausing:
		case OM::PrinterStatus::paused:
		case OM::PrinterStatus::resuming:
		case OM::PrinterStatus::cancelling:
			flag = true;
			break;
		case OM::PrinterStatus::toolChange:
		case OM::PrinterStatus::busy:
		case OM::PrinterStatus::connecting:
			break;									// part of the job if one is in progress (connection lost: keep it)
		default:
			flag = false;
			break;
		}
		if (newStatus == OM::PrinterStatus::cancelling)
		{
			cancelSeen = true;
		}

		const bool started = flag && !inProgress;
		const bool ended = !flag && inProgress;
		status = newStatus;
		inProgress = flag;
		SetJobInProgress(flag);
		if (started)
		{
			JobStarted();
		}
		else if (ended)
		{
			JobEnded(oldStatus, newStatus);
		}

		if (abortPending && newStatus == OM::PrinterStatus::paused)
		{
			abortPending = false;
			SerialIo::Sendf("M0\n");				// paused now: cancel the job
		}
		if (inProgress && (newStatus == OM::PrinterStatus::printing || newStatus == OM::PrinterStatus::simulating))
		{
			ApplySpindle();							// resumed: overrides changed while paused apply now
		}
		UpdateAll();
		return started;
	}

	void SetFileName(const char *path)
	{
		if (path == nullptr || path[0] == 0)
		{
			return;									// keep the last name after the job
		}
		const char *name = path;
		for (const char *p = path; *p != 0; ++p)
		{
			if (*p == '/' || *p == ':')
			{
				name = p + 1;
			}
		}
		String<40> truncated;
		truncated.copy(name);						// compare what is stored, or long names redraw on every poll
		if (!nameText.Equals(truncated.c_str()))
		{
			nameText.copy(truncated.c_str());
			UpdateNameAndResult();
		}
	}

	void SetProgress(unsigned int percent)
	{
		if (inProgress && percent != progress)
		{
			progress = (percent > 100) ? 100 : percent;
			UpdateNameAndResult();
		}
	}

	void SetDuration(uint32_t seconds)
	{
		// 0 comes when the job ends (duration no longer reported): keep the last time
		if (inProgress && seconds != 0 && seconds != elapsedSeconds)
		{
			elapsedSeconds = seconds;
			haveElapsed = true;
			FormatTime(elapsedText, seconds);
			elapsedField->SetValue(elapsedText.c_str());
		}
	}

	void SetTimeLeft(unsigned int seconds)
	{
		if (inProgress)
		{
			FormatTime(leftText, seconds);
			leftField->SetValue(leftText.c_str());
		}
	}

	void SetRequestedSpeed(int mmPerMin)
	{
		actualFields[0]->SetValue(mmPerMin);
	}

	void SetSpeedFactor(int percent)
	{
		speedFactor = percent;
		if (feedSent >= 0)
		{
			if (percent == feedSent)
			{
				feedSent = -1;						// our M220 arrived
				return;
			}
			if (SystemTick::GetTickCount() - feedSentTime < EchoTime)
			{
				return;								// old value, ours is on its way
			}
			feedSent = -1;
		}
		if (inProgress && percent != CurrentPct(0))
		{
			// changed elsewhere (DWC, console, G-code): becomes this tool's feed override
			overrides[0][ToolIndex(currentTool)] = (uint16_t)((percent < 1) ? 1 : percent);
			UpdateOverrideRow(0);
		}
	}

	void SetSpindleActive(int32_t rpm)
	{
		if (rpm < 0)
		{
			rpm = -rpm;								// old firmware: sign = direction (taken from the state instead)
		}
		spindleActive = rpm;
		if (rpm == 0)
		{
			return;									// off / standby: keep the programmed speed
		}
		if (!inProgress)
		{
			spindleProgrammed = rpm;
			return;
		}
		if (SpindleFrozen())
		{
			return;									// pause.g / resume.g / changes while paused: not the job's speed
		}
		if (rpm == spindleSent)
		{
			spindleRetries = 0;						// it arrived: a later M5 / M3 may be answered again
		}
		else if (rpm != spindleProgrammed && !IsOwnEcho(rpm))
		{
			spindleProgrammed = rpm;				// new speed from the job
		}
		ApplySpindle();
	}

	void SetSpindleState(bool running, bool reverse)
	{
		spindleReverse = reverse;
		if (running != spindleRunning)
		{
			spindleRunning = running;
			if (running)
			{
				spindleRetries = 0;					// switched on again (M3 after M5): apply the override afresh
				ApplySpindle();
			}
		}
	}

	void SetSpindleMin(int32_t rpm)
	{
		spindleMin = rpm;
	}

	void SetSpindleCurrent(int32_t rpm)
	{
		actualFields[1]->SetValue((rpm < 0) ? -rpm : rpm);
	}

	void SetSpindleMax(int32_t rpm)
	{
		spindleMax = rpm;
	}

	void SetTool(int tool)
	{
		if (tool == currentTool || (tool < 0 && inProgress))
		{
			return;									// RRF deselects the old tool during a change: keep its overrides
		}
		currentTool = tool;
		if (inProgress)
		{
			// the new tool's own overrides
			const int feed = CurrentPct(0);
			if (feed != speedFactor)
			{
				SendFeed(feed);
			}
			ApplySpindle();
		}
		UpdateOverrideRows();
		UpdateToolRow();
	}

	void SetNextTool(int tool)
	{
		if (tool != nextTool)
		{
			nextTool = tool;
			UpdateToolRow();
		}
	}

	bool ShowToolPrompt(const char *text, uint32_t seq)
	{
		if (!inProgress)
		{
			return false;
		}
		CncPopup::CloseResponse();					// a reply popup must not hide the prompt's OK
		promptText.copy(text);
		WrapPrompt(promptText.c_str());
		promptSeq = seq;
		promptOpen = true;
		blinkOn = true;
		blinkTime = SystemTick::GetTickCount();
		UpdateToolRow();
		UpdateNameAndResult();
		return true;
	}

	void CloseToolPrompt()
	{
		if (promptOpen)
		{
			promptOpen = false;
			UpdateToolRow();
			UpdateNameAndResult();
		}
	}

	bool ToolPromptOpen()
	{
		return promptOpen;
	}

	void EmergencyStop()
	{
		if (inProgress)
		{
			panelAbort = true;
		}
	}

	void Error(const char *text)
	{
		if (inProgress && CanShowAutoPopup())
		{
			CncPopup::Response(text, true, 0, true);	// ALERT ! until X (or the next reply), over other popups
		}
	}

	void Spin()
	{
		if (kbMode && !CncKeyboard::IsOpenFor(&kbClient))
		{
			KeyboardGone();							// replaced by another popup (M291, ALERT, ...)
		}
		const uint32_t now = SystemTick::GetTickCount();
		if (promptOpen && now - blinkTime >= BlinkTime)
		{
			blinkTime = now;
			blinkOn = !blinkOn;
			tcTile->SetOutline(blinkOn ? Amber : Border, 3);
			RedrawToolRow();
		}
		if (abortPending && now - abortStart >= AbortPauseTimeout)
		{
			abortPending = false;
			panelAbort = false;
			UpdatePauseAbort();
			if (CanShowAutoPopup())
			{
				Refuse("The job did not pause within 60 s.\nIt was not cancelled.");
			}
		}
	}
}

// End
