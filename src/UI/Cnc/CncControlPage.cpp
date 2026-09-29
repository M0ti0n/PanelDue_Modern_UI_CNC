/*
 * CncControlPage.cpp
 *
 * CONTROL page, portrait CNC UI (mock-up pd_cnc_control_3axis.svg / _4axis.svg):
 *
 *   AXIS           [ X ][ Y ][ Z ]( [ A ] )          |  HOME  [ALL]
 *   STEP DISTANCE  [0.01][0.1][1][10][100]            |        [ Z ]
 *   MOVE           [  - 0.1  ][  + 0.1  ]             |        [ X ]
 *   ACTION         [RAPID][SAFE Z][ XY0 ]             |        [ Y ]
 *                  [SLOW ][ COOL  ][CUSTOM 1]         |        [ A ]
 *
 * Moves are relative (G91 ... G90). RAPID uses G0, SLOW uses G1 at the jog feed, which is
 * set with the numpad from the DRO F tile. Every HOME button asks for confirmation first.
 * The spindle works like a printer heater: the S tile holds the set speed (numpad), the
 * DRO T tile switches between that speed (on, M3) and standby (off, M5).
 * With no speed set yet, switching on opens the S numpad first. Nothing is hard coded.
 * CUSTOM 1 runs a macro assigned in settings and stays hidden until one is assigned.
 * A macro whose file name starts with '!' asks for confirmation first.
 * RAPID / SLOW is one tall toggle (RAPID on top, SLOW below, the active half in accent): a tap
 * anywhere on it switches the mode.
 *
 * Job running: the whole page is locked (muted, error beep); coolant is on JOB STATUS.
 * Job paused:  everything works again (jog away, clear chips, spindle, coolant) except
 *              HOME, which stays locked until the job ends.
 * Large jog steps (10 and 100) are not queued: a tap while such a jog is still moving
 * is refused, so fast taps cannot stack up long moves.
 */

#include "CncControlPage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncPopups.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "Hardware/SysTick.hpp"
#include "PanelDue.hpp"

using namespace CncLayout;
using namespace CncLayout::Control;
using namespace Cnc;

namespace
{
	constexpr size_t MaxJogAxes = 4;					// X Y Z A
	const char AxisLetters[MaxJogAxes] = { 'X', 'Y', 'Z', 'A' };
	const char * const AxisNames[MaxJogAxes] = { "X", "Y", "Z", "A" };

	constexpr size_t NumSteps = 5;
	const float StepValues[NumSteps] = { 0.01f, 0.1f, 1.0f, 10.0f, 100.0f };
	const char * const StepNames[NumSteps] = { "0.01", "0.1", "1", "10", "100" };
	constexpr size_t DefaultStep = 1;					// 0.1

	constexpr unsigned int DefaultJogFeed = 300;		// mm/min (deg/min for A) in SLOW mode until set on the F tile
	constexpr unsigned int MaxJogFeed = 20000;
	constexpr size_t FirstGuardedStep = 3;				// 10 and 100: no queued jogs
	constexpr int32_t UnknownMaxRpm = 100000;			// upper limit when RRF has not reported the spindle max

	// Home column order (mock-up): ALL, Z, X, Y, A. Value = axis index, -1 = all
	constexpr size_t NumHome = 5;
	const int HomeAxis[NumHome] = { -1, 2, 0, 1, 3 };
	const char * const HomeNames[NumHome] = { "ALL", "Z", "X", "Y", "A" };

	ModernTextButton *axisButtons[MaxJogAxes];
	ModernTextButton *stepButtons[NumSteps];
	ModernTextButton *moveMinus, *movePlus;
	CncSegmentButton *feedMode;
	ModernTextButton *auxButton, *safeZButton, *xy0Button;

	// Custom macro button 1 (slot 0 of the shared custom macros; 2 / 3 are on JOB STATUS)
	constexpr size_t NumCustom = 1;
	ModernTextButton *customButtons[NumCustom];
	ModernTextButton *homeButtons[NumHome];

	ButtonBase *selectedAxis = nullptr;
	ButtonBase *selectedStep = nullptr;
	size_t currentAxis = 0;
	size_t currentStep = DefaultStep;
	size_t numVisibleAxes = 3;

	String<12> minusText, plusText;
	uint8_t homedBits = 0;								// bit n = axis n homed
	bool spindleRunning = false;
	bool spindleReverse = false;
	bool lockAll = false;								// job running
	bool lockHome = false;								// job running or paused
	uint32_t lastGuardedJog = 0;						// tick of the last large jog sent
	int32_t spindleRpm = 0;								// 0 = not set yet (SPINDLE opens the numpad)
	int32_t spindleMaxRpm = 0;							// 0 = unknown
	unsigned int jogFeed = DefaultJogFeed;
	IntegerField *spindleDisplay = nullptr;				// DRO S tile value
	IntegerField *spindleDisplay2 = nullptr;			// compact DRO S tile value

	void SetSpindleRpm(int32_t rpm)
	{
		spindleRpm = rpm;
		if (spindleDisplay != nullptr)
		{
			spindleDisplay->SetValue(rpm);
		}
		if (spindleDisplay2 != nullptr)
		{
			spindleDisplay2->SetValue(rpm);
		}
	}

	// X, Y, Z get equal widths; with a visible A the row is split in four
	void LayoutAxisRow()
	{
		const PixelNumber n = (numVisibleAxes > 3) ? 4 : 3;
		const PixelNumber w = (LeftW - (n - 1) * ColGap) / n;
		for (size_t i = 0; i < MaxJogAxes; ++i)
		{
			if (i < n)
			{
				axisButtons[i]->SetPositionAndWidth(LeftX + i * (w + ColGap), w);
			}
			mgr.Show(axisButtons[i], i < n);
		}
		mgr.Show(homeButtons[4], numVisibleAxes > 3);
		if (currentAxis >= n)
		{
			currentAxis = 0;
			Select(selectedAxis, axisButtons[0]);
		}
	}

	void UpdateMoveLabels()
	{
		minusText.printf("- %s", StepNames[currentStep]);
		plusText.printf("+ %s", StepNames[currentStep]);
		moveMinus->SetText(minusText.c_str());
		movePlus->SetText(plusText.c_str());
	}

	void UpdateHomeColours()
	{
		bool allHomed = true;
		for (size_t i = 0; i < NumHome; ++i)
		{
			const int axis = HomeAxis[i];
			if (axis < 0)
			{
				continue;
			}
			const bool homed = (homedBits & (1u << axis)) != 0;
			if ((size_t)axis < numVisibleAxes && !homed)
			{
				allHomed = false;
			}
			homeButtons[i]->SetColours(lockHome ? Border : Text, homed ? Tile : NotHomedBg);
		}
		homeButtons[0]->SetColours(lockHome ? Border : Text, allHomed ? Tile : NotHomedBg);
	}

	void UpdateLockedLook()
	{
		for (ModernTextButton *b : axisButtons) { SetButtonLocked(b, lockAll); }
		for (ModernTextButton *b : stepButtons) { SetButtonLocked(b, lockAll); }
		for (ModernTextButton *b : customButtons) { SetButtonLocked(b, lockAll); }
		SetButtonLocked(moveMinus, lockAll);
		SetButtonLocked(movePlus, lockAll);
		SetButtonLocked(safeZButton, lockAll);
		SetButtonLocked(xy0Button, lockAll);
		if (lockAll)
		{
			SetButtonLocked(auxButton, true);
		}
		else
		{
			SetToggleLook(auxButton, AuxOn());			// back to the on / off look
		}
		feedMode->SetLocked(lockAll);
		UpdateHomeColours();
	}

	// Large steps: refuse while the previous large jog may still be moving
	bool JogAllowed()
	{
		if (currentStep < FirstGuardedStep)
		{
			return true;								// small steps may queue, they are short
		}
		return GuardedJogAllowed(lastGuardedJog);		// also while paused (RRF reports paused, not busy)
	}

	void Jog(int direction)
	{
		const char axis = AxisLetters[currentAxis];
		const float distance = StepValues[currentStep] * (float)direction;
		if (feedMode->IsRightActive())
		{
			SerialIo::Sendf("G91\nG1 %c%.3f F%u\nG90\n", axis, (double)distance, jogFeed);
		}
		else
		{
			SerialIo::Sendf("G91\nG0 %c%.3f\nG90\n", axis, (double)distance);
		}
	}

	// ---- HOME (confirmed) ----
	void DoHome(int axis)
	{
		if (JobActive())
		{
			Refuse("HOME is locked until the job ends.");	// a job started while the popup was open
			return;
		}
		if (axis < 0)
		{
			SerialIo::Sendf("G28\n");
		}
		else
		{
			SerialIo::Sendf("G28 %c\n", AxisLetters[axis]);
		}
	}

	void AskHome(int axis)
	{
		String<24> title, line;
		if (axis < 0)
		{
			title.copy("HOME ALL");
			line.copy("Home all axes?");
		}
		else
		{
			title.printf("HOME %c", AxisLetters[axis]);
			line.printf("Home %c axis?", AxisLetters[axis]);
		}
		CncPopup::Confirm(title.c_str(), line.c_str(), "Tool clear of stock and clamps?", nullptr, nullptr, DoHome, axis);
	}

	// ---- Spindle ----
	void SendSpindleOn()
	{
		SerialIo::Sendf("%s S%ld\n", spindleReverse ? "M4" : "M3", (long)spindleRpm);
	}

	// param 1: started from the SPINDLE button, start the spindle after the speed is set
	void SpindleRpmEntered(int param, float value)
	{
		SetSpindleRpm((int32_t)(value + 0.5f));
		if (MachineBusy())
		{
			Refuse(CNC_LOCKED_JOB);
			return;
		}
		if (spindleRunning)
		{
			SendSpindleOn();							// new speed for the running spindle
		}
		else if (param == 1)
		{
			spindleReverse = false;
			SendSpindleOn();
		}
	}

	void OpenSpindleNumpadFor(int param)
	{
		CncPopup::NumpadSpec pad;
		pad.tag = "S";
		pad.unit = "rpm";
		pad.value = (float)spindleRpm;
		pad.decimals = 0;
		pad.allowDecimal = false;
		pad.allowMinus = false;
		pad.min = 1.0f;
		pad.max = (float)((spindleMaxRpm > 0) ? spindleMaxRpm : UnknownMaxRpm);
		pad.pos = nullptr;
		pad.onOk = SpindleRpmEntered;
		pad.param = param;
		CncPopup::Numpad(pad);
	}

	// ---- Shared state shown here ----
	void CustomChanged()
	{
		for (size_t i = 0; i < NumCustom; ++i)
		{
			customButtons[i]->SetText(CustomLabel(i));
			mgr.Show(customButtons[i], CustomUsed(i));
		}
	}

	void AuxChanged()
	{
		auxButton->SetText(AuxLabel());
		SetToggleLook(auxButton, AuxOn());
		if (lockAll)
		{
			SetButtonLocked(auxButton, true);			// toggled on JOB during a job: keep the locked look
		}
	}

	// ---- Jog feed ----
	void JogFeedEntered(int param, float value)
	{
		UNUSED(param);
		jogFeed = (unsigned int)(value + 0.5f);
		feedMode->SetRightActive(true);					// the feed applies to SLOW moves: select SLOW
	}
}

namespace CncControl
{
	DisplayField *Create(DisplayField *baseRoot)
	{
		mgr.SetRoot(baseRoot);
		const Colour accent = Accent();

		// Labels
		AddLabel(LabelAxisY, LeftX + 2, 120, "AXIS");
		AddLabel(LabelAxisY, HomeX, HomeW, "HOME", TextAlignment::Centre);
		AddLabel(LabelStepY, LeftX + 2, 250, "STEP DISTANCE");
		AddLabel(LabelMoveY, LeftX + 2, 120, "MOVE");
		AddLabel(LabelActY, LeftX + 2, 120, "ACTION");

		// AXIS row (positions set by LayoutAxisRow)
		for (size_t i = 0; i < MaxJogAxes; ++i)
		{
			axisButtons[i] = AddButton(RowAxisY, LeftX, 100, RowH, AxisNames[i], evCncAxis, (int)i, glcd28x32);
		}

		// STEP DISTANCE row
		{
			const PixelNumber w = (LeftW - (NumSteps - 1) * ColGap) / NumSteps;
			for (size_t i = 0; i < NumSteps; ++i)
			{
				stepButtons[i] = AddButton(RowStepY, LeftX + i * (w + ColGap), w, RowH, StepNames[i], evCncStep, (int)i);
			}
		}

		// MOVE row
		{
			const PixelNumber w = (LeftW - ColGap) / 2;
			moveMinus = AddButton(RowMoveY, LeftX, w, RowH, "-", evCncMove, -1, glcd28x32);
			movePlus = AddButton(RowMoveY, LeftX + w + ColGap, w, RowH, "+", evCncMove, 1, glcd28x32);
		}

		// ACTION grid, 3 x 2: RAPID / SLOW is one tall toggle over both rows of the first column
		{
			const PixelNumber w = (LeftW - 2 * ColGap) / 3;
			feedMode = new CncSegmentButton(RowAct1Y, LeftX, w, 2 * RowH + ActionGap, "RAPID", "SLOW", accent, evCncFeedMode, true);
			mgr.AddField(feedMode);
			safeZButton = AddButton(RowAct1Y, LeftX + w + ColGap, w, RowH, "SAFE Z", evCncSafeZ, 0);
			xy0Button = AddButton(RowAct1Y, LeftX + 2 * (w + ColGap), w, RowH, "XY0", evCncXY0, 0);
			auxButton = AddButton(RowAct2Y, LeftX + w + ColGap, w, RowH, AuxLabel(), evCncAux, 0);
			customButtons[0] = AddButton(RowAct2Y, LeftX + 2 * (w + ColGap), w, RowH, "", evCncCustom, 0);
			for (ModernTextButton *b : customButtons)
			{
				b->Show(false);							// until a macro is assigned
			}
		}

		// Vertical accent separator between the left block and the HOME column
		mgr.AddField(new ModernCard(LabelAxisY, SepX, SeparatorH, Bottom - LabelAxisY, accent, accent, false));

		// HOME column
		for (size_t i = 0; i < NumHome; ++i)
		{
			homeButtons[i] = AddButton(RowAxisY + i * HomePitch, HomeX, HomeW, RowH, HomeNames[i], evCncHome, HomeAxis[i],
										(i == 0) ? glcd19x21 : glcd28x32);
		}

		// Shared state: CUSTOM 1, COOLANT / VACUUM
		AddCustomListener(CustomChanged);
		AddAuxListener(AuxChanged);
		CustomChanged();
		AuxChanged();

		// Initial state
		selectedAxis = axisButtons[0];
		axisButtons[0]->Press(true, 0);
		selectedStep = stepButtons[DefaultStep];
		stepButtons[DefaultStep]->Press(true, 0);
		LayoutAxisRow();
		UpdateMoveLabels();
		UpdateHomeColours();

		return mgr.GetRoot();
	}

	bool ProcessTouch(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncAxis:
		case evCncStep:
		case evCncFeedMode:
		case evCncMove:
		case evCncSafeZ:
		case evCncXY0:
		case evCncHome:
		case evCncCustom:
		case evCncAux:
			if (lockAll || (bp.GetEvent() == evCncHome && lockHome))
			{
				Refuse(lockAll ? "Locked while a job is running.\nPause the job to use CONTROL."
								: "HOME is locked until the job ends.");
				return true;
			}
			break;

		default:
			return false;
		}

		switch ((Event)bp.GetEvent())
		{
		case evCncAxis:
			currentAxis = (size_t)bp.GetIParam();
			Select(selectedAxis, bp.GetButton());
			return true;

		case evCncStep:
			currentStep = (size_t)bp.GetIParam();
			Select(selectedStep, bp.GetButton());
			UpdateMoveLabels();
			return true;

		case evCncFeedMode:
			feedMode->SetRightActive(!feedMode->IsRightActive());
			return true;

		case evCncMove:
		case evCncSafeZ:
		case evCncXY0:
		case evCncHome:
		case evCncCustom:
			if (MachineBusy())
			{
				Refuse(CNC_LOCKED_JOB);
				return true;
			}
			switch ((Event)bp.GetEvent())
			{
			case evCncMove:
				if (!JogAllowed())
				{
					Refuse("Wait until the last move has finished.");
					break;
				}
				mgr.Press(bp, true);
				Jog(bp.GetIParam());
				break;
			case evCncSafeZ:
				if (RefuseUnhomed())
				{
					break;
				}
				mgr.Press(bp, true);
				SerialIo::Sendf("M98 P\"0:/macros/safe_z.g\"\n");
				break;
			case evCncXY0:
				if (RefuseUnhomed())
				{
					break;
				}
				mgr.Press(bp, true);
				SerialIo::Sendf("M98 P\"0:/macros/goto_xy0.g\"\n");
				break;
			case evCncHome:
				AskHome(bp.GetIParam());				// confirmation popup, homes on the check mark
				break;
			case evCncCustom:
				mgr.Press(bp, true);
				RunCustomMacro((size_t)bp.GetIParam());	// '!' macro: confirmation popup first
				break;
			default:
				break;
			}
			return true;

		case evCncAux:
			ToggleAux();								// the listener updates the look here and on JOB
			return true;

		default:
			return false;
		}
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncAxis:
		case evCncStep:
		case evCncFeedMode:
		case evCncAux:
			return true;								// selection / toggle state stays

		case evCncMove:
		case evCncCustom:
		case evCncSafeZ:
		case evCncXY0:
			mgr.Press(bp, false);
			return true;

		case evCncHome:
			return true;								// never pressed: it opens the confirmation

		default:
			return false;
		}
	}

	bool RefuseUnhomed()
	{
		if ((homedBits & 0x07) != 0x07)
		{
			Refuse("Home X, Y and Z first.");		// G53 / absolute moves from an unknown position
			return true;
		}
		return false;
	}

	void SetHomed(size_t axis, bool homed)
	{
		if (axis >= MaxJogAxes)
		{
			return;
		}
		const uint8_t bit = (uint8_t)(1u << axis);
		const uint8_t newBits = homed ? (homedBits | bit) : (homedBits & ~bit);
		if (newBits != homedBits)
		{
			homedBits = newBits;
			UpdateHomeColours();
		}
	}

	bool SetAxisVisible(size_t axis, bool visible)
	{
		if (axis != 3)
		{
			return false;								// only the A axis changes the layout
		}
		const size_t n = visible ? 4 : 3;
		if (n == numVisibleAxes)
		{
			return false;
		}
		numVisibleAxes = n;
		LayoutAxisRow();
		UpdateHomeColours();
		return true;
	}

	void SetSpindleActive(int32_t rpm)
	{
		if (rpm > 0)
		{
			SetSpindleRpm(rpm);							// speed set elsewhere (job, console, DWC); 0 = standby, keep the set speed
		}
	}

	void SetJobState(bool running, bool active)
	{
		if (running != lockAll || active != lockHome)
		{
			lockAll = running;
			lockHome = active;
			UpdateLockedLook();
		}
	}


	void SetSpindleDisplay(IntegerField *f, IntegerField *compact)
	{
		spindleDisplay = f;
		spindleDisplay2 = compact;
		SetSpindleRpm(spindleRpm);
	}

	void ToggleSpindle()
	{
		if (MachineBusy())
		{
			Refuse(CNC_LOCKED_JOB);
			return;
		}
		if (spindleRunning)
		{
			SerialIo::Sendf("M5\n");					// standby: 0 rpm, the set speed stays on the S tile
		}
		else if (spindleRpm > 0)
		{
			spindleReverse = false;
			SendSpindleOn();							// back on at the set speed
		}
		else
		{
			OpenSpindleNumpadFor(1);					// no speed yet: ask, then start
		}
	}

	void SetSpindleMax(int32_t rpm)
	{
		spindleMaxRpm = rpm;
	}

	void OpenSpindleNumpad()
	{
		if (MachineBusy())
		{
			Refuse("Locked while a job is running.\nUse the spindle override on JOB.");
			return;
		}
		OpenSpindleNumpadFor(0);
	}

	unsigned int JogFeed()
	{
		return jogFeed;
	}

	void OpenFeedNumpad()
	{
		CncPopup::NumpadSpec pad;
		pad.tag = "F";
		pad.unit = "mm/min";
		pad.value = (float)jogFeed;
		pad.decimals = 0;
		pad.allowDecimal = false;
		pad.allowMinus = false;
		pad.min = 1.0f;
		pad.max = (float)MaxJogFeed;
		pad.pos = nullptr;
		pad.onOk = JogFeedEntered;
		pad.param = 0;
		CncPopup::Numpad(pad);
	}

	void SetSpindleState(OM::SpindleState state)
	{
		spindleReverse = (state == OM::SpindleState::reverse);
		const bool running = (state != OM::SpindleState::stopped);
		spindleRunning = running;
	}
}

// End
