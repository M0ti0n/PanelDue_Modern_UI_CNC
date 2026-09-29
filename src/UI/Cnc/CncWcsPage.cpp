/*
 * CncWcsPage.cpp
 *
 * WCS page, portrait CNC UI (mock-ups pd_cnc_wcs_offsets_*.svg, pd_cnc_wcs_offsets_step_popup.svg)
 *
 *   WORK OFFSET                                   |  OFFSET
 *   [G54][G55][G56][G57][G58][G59]                |  [  Z+  ]
 *   OFFSET G55                                    |  [+0.040]
 *   X [ +250.000 ]    Y [  +50.000 ]              |  [  Z-  ]
 *   Z [  -25.000 ]    A [   +0.000 ]              |   STEP  [0.02]
 *   [ACTIVATE][COPY TO][ SAVE ]                   |
 *   [ RESET  ][ SHIFT ][APPLY |->|]               |  [CLEAR ]
 *   [ OFFSETS ][ TOOLS ][ PROBE ]   (sub-tabs)
 *
 * G5x buttons: tap to view; the active WCS is filled with the accent colour, the viewed one
 * has an accent outline. SAVE gets an accent outline while there are unsaved changes.
 * Tapping an offset cell opens the standard numpad (POS = current machine position, which
 * makes the current point 0). RESET, APPLY OFFSET and CLEAR ask for confirmation.
 * COPY TO opens the standard popup form (target G5x + axes); a target that already has
 * offsets gets an OVERWRITE confirmation with old -> new per axis.
 * OFFSET STEP (RRF babystepping, M290) moves Z live, also during a job, without touching the
 * work offset; APPLY |->| folds it into the active WCS Z. STEP opens the step presets.
 *
 * During a job only the job's (active) WCS is locked: editing, RESET, COPY TO into it and
 * ACTIVATE are refused; the other five stay editable (set up the next fixture while cutting).
 * APPLY OFFSET is refused while the job runs, allowed while paused.
 * SHIFT: pick the axes (standard popup form), then the amount on the numpad; the viewed WCS
 * moves by that amount on every picked axis (offset + amount).
 */

#include "CncWcsPage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncPopups.hpp"
#include "CncToolsPage.hpp"
#include "CncProbePage.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "Hardware/SysTick.hpp"
#include "PanelDue.hpp"
#include "Icons/Icons.hpp"

using namespace CncLayout;
using namespace CncLayout::Offsets;
using namespace Cnc;

namespace
{
	constexpr size_t NumWcs = 6;
	const char * const WcsNames[NumWcs] = { "G54", "G55", "G56", "G57", "G58", "G59" };
	constexpr size_t MaxAxes = 4;
	const char * const AxisNames[MaxAxes] = { "X", "Y", "Z", "A" };
	const char AxisLetters[MaxAxes] = { 'X', 'Y', 'Z', 'A' };
	constexpr size_t ZAxis = 2;

	constexpr size_t NumSubTabs = 3;
	const char * const SubTabNames[NumSubTabs] = { "OFFSETS", "TOOLS", "PROBE" };

	constexpr size_t NumOffsetSteps = 5;
	const float OffsetSteps[NumOffsetSteps] = { 0.005f, 0.02f, 0.1f, 0.5f, 1.0f };
	const char * const OffsetStepNames[NumOffsetSteps] = { "0.005", "0.02", "0.1", "0.5", "1" };
	constexpr size_t DefaultOffsetStep = 1;					// 0.02
	constexpr size_t FirstLargeOffsetStep = 3;				// 0.5 and 1: not while a job runs

	// Changes arriving shortly after connecting are the initial load, not unsaved edits
	constexpr uint32_t InitialLoadTime = 8000;				// ms

	// Sub-pages
	DisplayField *subRoots[NumSubTabs];
	ModernTextButton *subTabs[NumSubTabs];
	ButtonBase *selectedSubTab = nullptr;
	size_t currentSub = 0;

	// OFFSETS fields
	ModernTextButton *wcsButtons[NumWcs];
	StaticTextField *gridLabel;
	String<16> gridLabelText;
	FloatField *offsetFields[MaxAxes];
	StaticTextField *offsetLetters[MaxAxes];
	ModernCard *offsetCards[MaxAxes];
	ModernTouchArea *offsetTouch[MaxAxes];
	ModernTextButton *activateButton, *saveButton, *applyOffsetButton;
	ModernTextButton *copyButton, *shiftButton, *resetButton;
	FloatField *offsetStepTotal;
	ModernTextButton *offsetStepButton;

	// State
	float offsets[MaxAxes][NumWcs] = {};
	size_t viewedWcs = 0;
	size_t activeWcs = 0;
	size_t numAxes = 3;
	float offsetStepZ = 0.0f;
	size_t offsetStepIndex = DefaultOffsetStep;
	bool jobRunning = false;							// job running (not paused)
	bool jobActive = false;								// job running or paused
	bool unsaved = false;
	uint32_t firstOffsetTime = 0;


	void UpdateGridLabel()
	{
		gridLabelText.printf("OFFSET %s", WcsNames[viewedWcs]);
		gridLabel->SetValue(gridLabelText.c_str(), true);
	}

	void UpdateOffsetFields()
	{
		for (size_t a = 0; a < MaxAxes; ++a)
		{
			offsetFields[a]->SetValue(offsets[a][viewedWcs]);
		}
	}

	// Active = accent fill (pressed look); viewed = accent outline
	void UpdateWcsButtons()
	{
		const Colour accent = Accent();
		for (size_t i = 0; i < NumWcs; ++i)
		{
			wcsButtons[i]->SetBorderColour((i == viewedWcs) ? accent : Border);
			mgr.Press(ButtonPress(wcsButtons[i], 0), i == activeWcs);
		}
	}

	void UpdateSaveDot()
	{
		saveButton->SetBorderColour(unsaved ? Accent() : Border);
	}

	void MarkUnsaved()
	{
		if (!unsaved)
		{
			unsaved = true;
			UpdateSaveDot();
		}
	}

	// A axis cell only when the machine has a visible A axis (the slot stays empty otherwise)
	void UpdateAxisCells()
	{
		const bool showA = numAxes > 3;
		if (showA)
		{
			mgr.Show(offsetCards[3], true);				// the card first: it would cover the value and letter
			mgr.Show(offsetTouch[3], true);
			mgr.Show(offsetLetters[3], true);
			mgr.Show(offsetFields[3], true);
		}
		else
		{
			mgr.Show(offsetFields[3], false);
			mgr.Show(offsetLetters[3], false);
			mgr.Show(offsetCards[3], false);
			mgr.Show(offsetTouch[3], false);
		}
	}

	// During a job only the active WCS matters to the cut: it is locked, the other five stay editable
	bool WcsEditable(size_t wcs)
	{
		return !jobActive || wcs != activeWcs;
	}

	// ALERT for an edit of the job's own work offset
	void RefuseJobWcs()
	{
		String<64> msg;
		msg.printf("%s is the job's work offset.\nLocked until the job ends.", WcsNames[activeWcs]);
		Refuse(msg.c_str());
	}

	void UpdateLockedLook()
	{
		const bool editable = WcsEditable(viewedWcs);
		SetButtonLocked(activateButton, jobActive || viewedWcs == activeWcs);	// no-op when already active
		SetButtonLocked(applyOffsetButton, jobRunning);							// fine while paused: net position unchanged
		SetButtonLocked(resetButton, !editable);
		SetButtonLocked(shiftButton, !editable);
		for (size_t a = 0; a < MaxAxes; ++a)
		{
			offsetFields[a]->SetColours(editable ? Text : Muted, Tile);	// values stay readable, editing is refused
		}
		for (ModernTextButton *b : wcsButtons)
		{
			b->SetColours(Text, Tile);				// viewing stays possible during a job
		}
	}

	void UpdateOffsetStepButton()
	{
		offsetStepButton->SetText(OffsetStepNames[offsetStepIndex]);
	}

	// ---- Offset STEP SIZE (standard popup, preset choice) ----
	bool OffsetStepAllowed(size_t i)
	{
		return i < FirstLargeOffsetStep || !MachineBusy();		// 0.5 / 1 mm steps only when idle or paused
	}

	void OffsetStepChosen(int param, size_t choice)
	{
		UNUSED(param);
		if (!OffsetStepAllowed(choice))
		{
			Refuse("0.5 and 1 mm steps: not while a job is running.");
			return;
		}
		offsetStepIndex = choice;
		UpdateOffsetStepButton();
	}

	void OpenStepPopup()
	{
		CncPopup::Choose("STEP SIZE", OffsetStepNames, NumOffsetSteps, offsetStepIndex, OffsetStepAllowed, OffsetStepChosen, 0);
	}

	// ---- Offset value (standard numpad) ----
	// POS: the offset that makes the current position 0 in the viewed WCS. From the work position the
	// DRO shows (so the tool offset and the babystep are taken into account as RRF does), relative to
	// the active WCS: new = active offset + work position.
	float MachinePos(int axis)
	{
		if (activeWcs >= NumWcs)
		{
			return offsets[axis][viewedWcs];			// G59.1..G59.3 active: its offsets are not known here
		}
		return offsets[axis][activeWcs] + WorkPosition((size_t)axis);
	}

	void OffsetEntered(int axis, float value)
	{
		if (!WcsEditable(viewedWcs))
		{
			RefuseJobWcs();
			return;
		}
		SerialIo::Sendf("G10 L2 P%u %c%.3f\n", (unsigned int)(viewedWcs + 1), AxisLetters[axis], (double)value);
		CncRequestWorkplaceOffsets();
		MarkUnsaved();
	}

	void OpenOffsetNumpad(size_t axis)
	{
		String<12> tag;
		tag.printf("%s %c", WcsNames[viewedWcs], AxisLetters[axis]);
		CncPopup::NumpadSpec pad;
		pad.tag = tag.c_str();								// copied by the numpad
		pad.unit = (axis == 3) ? "deg" : "mm";
		pad.value = offsets[axis][viewedWcs];
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = true;
		pad.min = -99999.0f;
		pad.max = 99999.0f;
		pad.pos = MachinePos;
		pad.onOk = OffsetEntered;
		pad.param = (int)axis;
		CncPopup::Numpad(pad);
	}

	// ---- RESET (confirmed) ----
	void DoReset(int wcs)
	{
		if (!WcsEditable((size_t)wcs))
		{
			RefuseJobWcs();
			return;
		}
		SerialIo::Sendf("G10 L2 P%d X0 Y0 Z0%s\n", wcs + 1, (numAxes > 3) ? " A0" : "");
		CncRequestWorkplaceOffsets();
		MarkUnsaved();
	}

	void AskReset()
	{
		String<40> line;
		line.printf("Set all %s offsets to 0?", WcsNames[viewedWcs]);
		CncPopup::Confirm("RESET", line.c_str(), nullptr, nullptr, nullptr, DoReset, (int)viewedWcs);
	}

	// ---- CLEAR offset step (confirmed: Z moves by the whole offset step) ----
	void DoClearOffset(int param)
	{
		UNUSED(param);
		SerialIo::Sendf("M290 R0 Z0\n");
	}

	void AskClearOffset()
	{
		if (fabsf(offsetStepZ) < 0.0005f)
		{
			Refuse("No offset step to clear.");
			return;
		}
		String<40> line;
		line.printf("Z will move %.3f mm.", (double)fabsf(offsetStepZ));
		CncPopup::Confirm("CLEAR", "Clear offset?", line.c_str(), nullptr, nullptr, DoClearOffset, 0);
	}

	// ---- APPLY OFFSET (confirmed) ----
	void DoApplyOffset(int param)
	{
		UNUSED(param);
		if (jobRunning)
		{
			Refuse("Pause the job to apply the offset step.");
			return;
		}
		// Fold the offset step (RRF babystep) into the active WCS Z on the Duet, using its own current values
		SerialIo::Sendf("G10 L2 P{move.workplaceNumber + 1} Z{move.axes[2].workplaceOffsets[move.workplaceNumber] + move.axes[2].babystep}\n");
		SerialIo::Sendf("M290 R0 Z0\n");
		CncRequestWorkplaceOffsets();
		MarkUnsaved();
	}

	void AskApplyOffset()
	{
		if (fabsf(offsetStepZ) < 0.0005f)
		{
			Refuse("No offset step to apply.");
			return;
		}
		String<40> line1, line2;
		line1.printf("Add Z offset step %+.3f", (double)offsetStepZ);
		line2.printf("to the %s Z offset", (activeWcs < NumWcs) ? WcsNames[activeWcs] : "active");
		CncPopup::Confirm("APPLY OFFSET", line1.c_str(), line2.c_str(), "and clear the offset step?", nullptr, DoApplyOffset, 0);
	}

	// ---- COPY TO (standard popup form: target + axes, overwrite confirmed) ----
	size_t copySource = 0, copyTarget = 0;
	uint16_t copyAxes = 0;
	String<32> copyLines[6];

	bool WcsHasOffsets(size_t wcs, uint16_t axesMask)
	{
		for (size_t a = 0; a < numAxes && a < MaxAxes; ++a)
		{
			if ((axesMask & (1u << a)) != 0 && offsets[a][wcs] != 0.0f)
			{
				return true;
			}
		}
		return false;
	}

	void DoCopy(int param)
	{
		UNUSED(param);
		if (!WcsEditable(copyTarget))
		{
			RefuseJobWcs();							// the target became the active WCS of a job
			return;
		}
		String<80> cmd;
		cmd.printf("G10 L2 P%u", (unsigned int)(copyTarget + 1));
		for (size_t a = 0; a < numAxes && a < MaxAxes; ++a)
		{
			if ((copyAxes & (1u << a)) != 0)
			{
				cmd.catf(" %c%.3f", AxisLetters[a], (double)offsets[a][copySource]);
			}
		}
		SerialIo::Sendf("%s\n", cmd.c_str());
		CncRequestWorkplaceOffsets();
		MarkUnsaved();
	}

	void CopyChosen(int source, const uint16_t selected[])
	{
		copySource = (size_t)source;
		copyTarget = 0;
		while (copyTarget < NumWcs && (selected[0] & (1u << copyTarget)) == 0)
		{
			++copyTarget;
		}
		copyAxes = selected[1];
		if (copyTarget >= NumWcs || copyTarget == copySource || !WcsEditable(copyTarget))
		{
			RefuseJobWcs();
			return;
		}
		if (!WcsHasOffsets(copyTarget, copyAxes))
		{
			DoCopy(0);								// empty target: nothing to lose
			return;
		}

		// Overwrite: old -> new for every copied axis
		const char *lines[6];
		size_t n = 0;
		copyLines[n].printf("%s already has offsets", WcsNames[copyTarget]);
		lines[n] = copyLines[n].c_str(); ++n;
		for (size_t a = 0; a < numAxes && a < MaxAxes; ++a)
		{
			if ((copyAxes & (1u << a)) != 0)
			{
				copyLines[n].printf("%c  %+.3f -> %+.3f", AxisLetters[a], (double)offsets[a][copyTarget], (double)offsets[a][copySource]);
				lines[n] = copyLines[n].c_str(); ++n;
			}
		}
		copyLines[n].printf("Copy %s into %s?", WcsNames[copySource], WcsNames[copyTarget]);
		lines[n] = copyLines[n].c_str(); ++n;
		CncPopup::ConfirmLines("OVERWRITE", lines, n, DoCopy, 0);
	}

	void OpenCopyTo()
	{
		static String<12> title;
		title.printf("COPY %s", WcsNames[viewedWcs]);

		uint16_t disabled = (uint16_t)(1u << viewedWcs);
		if (jobActive && activeWcs < NumWcs)
		{
			disabled |= (uint16_t)(1u << activeWcs);	// the job's WCS cannot be overwritten
		}
		uint16_t marked = 0;
		const uint16_t allAxes = (uint16_t)((1u << numAxes) - 1);
		for (size_t w = 0; w < NumWcs; ++w)
		{
			if (WcsHasOffsets(w, allAxes))
			{
				marked |= (uint16_t)(1u << w);
			}
		}
		// Preselect the next free-to-use WCS after the source
		uint16_t target = 0;
		for (size_t k = 1; k < NumWcs && target == 0; ++k)
		{
			const size_t w = (viewedWcs + k) % NumWcs;
			if ((disabled & (1u << w)) == 0)
			{
				target = (uint16_t)(1u << w);
			}
		}

		CncPopup::FormGroup groups[2];
		groups[0] = { "TO", WcsNames, NumWcs, false, target, disabled, marked };
		groups[1] = { "AXES", AxisNames, numAxes, true, allAxes, 0, 0 };
		CncPopup::Form(title.c_str(), groups, 2, CopyChosen, (int)viewedWcs);
	}

	// ---- SHIFT (standard popup form: axes, then the amount on the numpad) ----
	size_t shiftWcs = 0;
	uint16_t shiftAxes = 0;

	void ShiftEntered(int param, float amount)
	{
		UNUSED(param);
		if (!WcsEditable(shiftWcs))
		{
			RefuseJobWcs();
			return;
		}
		String<80> cmd;
		cmd.printf("G10 L2 P%u", (unsigned int)(shiftWcs + 1));
		for (size_t a = 0; a < numAxes && a < MaxAxes; ++a)
		{
			if ((shiftAxes & (1u << a)) != 0)
			{
				cmd.catf(" %c%.3f", AxisLetters[a], (double)(offsets[a][shiftWcs] + amount));
			}
		}
		SerialIo::Sendf("%s\n", cmd.c_str());
		CncRequestWorkplaceOffsets();
		MarkUnsaved();
	}

	void ShiftAxesChosen(int wcs, const uint16_t selected[])
	{
		shiftWcs = (size_t)wcs;
		shiftAxes = selected[0];
		// Unit: degrees only when A is the only picked axis
		const bool onlyA = (shiftAxes == (1u << 3));
		static String<12> tag;
		tag.printf("%s +", WcsNames[shiftWcs]);
		CncPopup::NumpadSpec pad;
		pad.tag = tag.c_str();
		pad.unit = onlyA ? "deg" : "mm";
		pad.value = 0.0f;
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = true;
		pad.min = -9999.0f;
		pad.max = 9999.0f;
		pad.pos = nullptr;
		pad.onOk = ShiftEntered;
		pad.param = 0;
		CncPopup::Numpad(pad);
	}

	void OpenShift()
	{
		static String<12> title;
		title.printf("SHIFT %s", WcsNames[viewedWcs]);
		CncPopup::FormGroup group = { "AXES", AxisNames, numAxes, true, 0, 0, 0 };	// nothing picked yet
		CncPopup::Form(title.c_str(), &group, 1, ShiftAxesChosen, (int)viewedWcs);
	}

	void CreateOffsets()
	{
		const Colour accent = Accent();

		// Labels
		AddLabel(LabelWcsY, LeftX + 2, 200, "WORK OFFSET");
		AddLabel(LabelWcsY, RightX - 6, RightW + 12, "OFFSET", TextAlignment::Centre);
		gridLabel = AddLabel(LabelGridY, LeftX + 2, 250, "");

		// G54..G59
		{
			const PixelNumber w = (LeftW - (NumWcs - 1) * WcsGap) / NumWcs;
			for (size_t i = 0; i < NumWcs; ++i)
			{
				wcsButtons[i] = AddButton(RowWcsY, LeftX + i * (w + WcsGap), w, RowWcsH, WcsNames[i], evCncWcsSelect, (int)i);
			}
		}

		// 2 x 2 offset grid: X Y / Z A
		for (size_t a = 0; a < MaxAxes; ++a)
		{
			const PixelNumber cx = LeftX + (a % 2) * (CellW + CellGap);
			const PixelNumber cy = GridY + (a / 2) * (CellH + CellGap);
			DisplayField::SetDefaultFont(glcd28x32);
			DisplayField::SetDefaultColours(accent, PageBg);
			offsetLetters[a] = new StaticTextField(cy + (CellH - 32) / 2, cx, CellLetterW, TextAlignment::Centre, AxisNames[a]);
			mgr.AddField(offsetLetters[a]);
			DisplayField::SetDefaultFont(glcd19x21);
			DisplayField::SetDefaultColours(Text, Tile);
			offsetFields[a] = new FloatField(cy + (CellH - 21) / 2, cx + CellLetterW + 6, CellW - CellLetterW - 18, TextAlignment::Right, 3);
			mgr.AddField(offsetFields[a]);
			offsetCards[a] = new ModernCard(cy, cx + CellLetterW, CellW - CellLetterW, CellH, Tile, Border, true);
			mgr.AddField(offsetCards[a]);
			offsetTouch[a] = new ModernTouchArea(cy, cx, CellW, CellH, evCncOffsetEdit, (int)a);
			mgr.AddField(offsetTouch[a]);
		}

		// Actions, 2 rows of 3
		{
			const PixelNumber w = (LeftW - 2 * 8) / 3;
			const PixelNumber y2 = ActY + ActH + ActGap;
			activateButton = AddButton(ActY, LeftX, w, ActH, "ACTIVATE", evCncWcsActivate, 0);
			copyButton = AddButton(ActY, LeftX + w + 8, w, ActH, "COPY TO", evCncWcsCopy, 0);
			saveButton = AddButton(ActY, LeftX + 2 * (w + 8), w, ActH, "SAVE", evCncWcsSave, 0);
			resetButton = AddButton(y2, LeftX, w, ActH, "RESET", evCncWcsReset, 0);
			shiftButton = AddButton(y2, LeftX + w + 8, w, ActH, "SHIFT", evCncWcsShift, 0);
			applyOffsetButton = AddButton(y2, LeftX + 2 * (w + 8), w, ActH, "APPLY |->|", evCncWcsApplyOffset, 0);

		}

		// Vertical accent separator
		mgr.AddField(new ModernCard(LabelWcsY, SepX, SeparatorH, Bottom - LabelWcsY, accent, accent, false));

		// OFFSET column (offset step)
		AddButton(StepUpY, RightX, RightW, StepBtnH, "Z+", evCncOffsetStep, 1, glcd28x32);
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(accent, Tile);
		offsetStepTotal = new FloatField(StepTotalY + (StepTotalH - 21) / 2, RightX + 4, RightW - 8, TextAlignment::Centre, 3);
		mgr.AddField(offsetStepTotal);
		AddCard(StepTotalY, RightX, RightW, StepTotalH);
		AddButton(StepDownY, RightX, RightW, StepBtnH, "Z-", evCncOffsetStep, -1, glcd28x32);
		AddLabel(StepSizeLabelY, RightX, RightW, "STEP", TextAlignment::Centre);
		offsetStepButton = AddButton(StepSizeY, RightX, RightW, StepSizeH, OffsetStepNames[offsetStepIndex], evCncOffsetStepOpen, 0);
		AddButton(StepClearY, RightX, RightW, StepClearH, "CLEAR", evCncOffsetClear, 0);
	}

	bool ProcessOffsetsTouch(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncWcsSelect:
			viewedWcs = (size_t)bp.GetIParam();
			UpdateWcsButtons();
			UpdateGridLabel();
			UpdateOffsetFields();
			UpdateLockedLook();
			return true;

		case evCncWcsActivate:
			if (jobActive || viewedWcs == activeWcs)
			{
				if (jobActive)
				{
					Refuse(CNC_LOCKED_JOB_ACTIVE);
				}
				else
				{
					String<32> msg;
					msg.printf("%s is already active.", WcsNames[viewedWcs]);
					Refuse(msg.c_str());
				}
				return true;
			}
			mgr.Press(bp, true);
			SerialIo::Sendf("%s\n", WcsNames[viewedWcs]);
			return true;

		case evCncWcsSave:
			mgr.Press(bp, true);
			SerialIo::Sendf("M500 P10\n");		// P10: tool offsets too
			unsaved = false;
			UpdateSaveDot();
			return true;

		case evCncWcsApplyOffset:
			if (jobRunning)
			{
				Refuse("Pause the job to apply the offset step.");
				return true;
			}
			AskApplyOffset();
			return true;

		case evCncWcsReset:
		case evCncOffsetEdit:
		case evCncWcsShift:
			if (!WcsEditable(viewedWcs))
			{
				RefuseJobWcs();
				return true;
			}
			if (bp.GetEvent() == evCncWcsReset)
			{
				AskReset();
			}
			else if (bp.GetEvent() == evCncWcsShift)
			{
				OpenShift();
			}
			else
			{
				OpenOffsetNumpad((size_t)bp.GetIParam());
			}
			return true;

		case evCncWcsCopy:
			OpenCopyTo();
			return true;

		case evCncOffsetStep:
			if (!OffsetStepAllowed(offsetStepIndex))
			{
				Refuse("0.5 and 1 mm steps: not while a job is running.");	// chosen while idle, the job started since
				return true;
			}
			if (MachineBusy())
			{
				IgnoreRepeats();				// live in the cut: one step per tap, holding must not add more
			}
			mgr.Press(bp, true);
			SerialIo::Sendf("M290 Z%.3f\n", (double)(OffsetSteps[offsetStepIndex] * (float)bp.GetIParam()));
			return true;

		case evCncOffsetClear:
			AskClearOffset();					// Z moves: confirm first (also live during a job)
			return true;

		case evCncOffsetStepOpen:
			OpenStepPopup();
			return true;

		default:
			return false;
		}
	}

}

namespace CncWcs
{
	void Create(DisplayField *baseRoot)
	{
		// Sub-tab strip, shared by the three sub-pages
		mgr.SetRoot(baseRoot);
		AddSubTabs(SubTabNames, NumSubTabs, subTabs);
		DisplayField * const tabsRoot = mgr.GetRoot();

		// OFFSETS
		CreateOffsets();
		subRoots[0] = mgr.GetRoot();

		// TOOLS
		mgr.SetRoot(tabsRoot);
		CncTools::Create();
		subRoots[1] = mgr.GetRoot();

		// PROBE
		mgr.SetRoot(tabsRoot);
		CncProbe::Create();
		subRoots[2] = mgr.GetRoot();

		// Initial state
		selectedSubTab = subTabs[0];
		subTabs[0]->Press(true, 0);
		UpdateWcsButtons();
		UpdateGridLabel();
		UpdateOffsetFields();
		UpdateAxisCells();
		UpdateLockedLook();

	}

	DisplayField *CurrentRoot()
	{
		return subRoots[currentSub];
	}

	bool ProcessTouch(ButtonPress bp, bool& redraw)
	{
		redraw = false;
		if (bp.GetEvent() == evCncSubTab)
		{
			const size_t sub = (size_t)bp.GetIParam();
			if (sub != currentSub)
			{
				currentSub = sub;
				Select(selectedSubTab, bp.GetButton());
				redraw = true;
			}
			return true;
		}
		switch (currentSub)
		{
		case 0:		return ProcessOffsetsTouch(bp);
		case 1:		return CncTools::ProcessTouch(bp);
		case 2:		return CncProbe::ProcessTouch(bp);
		default:	return false;
		}
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncSubTab:
		case evCncWcsSelect:
			return true;							// selection stays

		case evCncWcsActivate:
		case evCncWcsSave:
		case evCncOffsetStep:
			mgr.Press(bp, false);
			return true;

		case evCncWcsApplyOffset:
		case evCncOffsetClear:
		case evCncWcsReset:
		case evCncOffsetEdit:
		case evCncOffsetStepOpen:
		case evCncWcsCopy:
		case evCncWcsShift:
			return true;							// never pressed: they open a popup

		default:
			return CncTools::ProcessRelease(bp) || CncProbe::ProcessRelease(bp);
		}
	}

	void MarkUnsaved()
	{
		::MarkUnsaved();
	}

	void MarkSaved()
	{
		unsaved = false;
		UpdateSaveDot();
	}

	void Disconnected()
	{
		// The next offsets are an initial load again. The SAVE outline stays: the link may only have
		// timed out while the Duet kept the unsaved offsets (pressing SAVE again does no harm).
		firstOffsetTime = 0;
	}

	void NoteSavedValueChanged()
	{
		const uint32_t now = SystemTick::GetTickCount();
		if (firstOffsetTime != 0 && now - firstOffsetTime > InitialLoadTime)
		{
			::MarkUnsaved();					// changed after the initial load
		}
	}

	void SetWorkplaceOffset(size_t axis, size_t workplace, float offset)
	{
		if (axis >= MaxAxes || workplace >= NumWcs)
		{
			return;
		}
		const uint32_t now = SystemTick::GetTickCount();
		if (firstOffsetTime == 0)
		{
			firstOffsetTime = (now == 0) ? 1 : now;
		}
		if (offsets[axis][workplace] != offset)
		{
			offsets[axis][workplace] = offset;
			if (workplace == viewedWcs)
			{
				offsetFields[axis]->SetValue(offset);
			}
			if (now - firstOffsetTime > InitialLoadTime)
			{
				MarkUnsaved();					// changed after the initial load (zero, probe, DWC, ...)
			}
		}
	}

	void SetActiveWorkplace(size_t workplace)
	{
		if (workplace >= NumWcs)
		{
			workplace = NumWcs;							// G59.1..G59.3: none of the six tiles
		}
		if (workplace != activeWcs)
		{
			activeWcs = workplace;
			UpdateWcsButtons();
			UpdateLockedLook();
		}
	}

	void SetOffsetStep(size_t axis, float value)
	{
		if (axis == ZAxis)
		{
			offsetStepZ = value;
			offsetStepTotal->SetValue(value);
		}
	}

	void SetAxisVisible(size_t axis, bool visible)
	{
		if (axis == 3)
		{
			const size_t n = visible ? 4 : 3;
			if (n != numAxes)
			{
				numAxes = n;
				UpdateAxisCells();
				CncProbe::SetNumAxes(n);
			}
		}
	}

	void SetJobState(bool running, bool active)
	{
		if (running != jobRunning || active != jobActive)
		{
			jobRunning = running;
			jobActive = active;
			UpdateLockedLook();
		}
		CncTools::SetJobState(running, active);
		CncProbe::SetJobState(running, active);
	}
}

// End
