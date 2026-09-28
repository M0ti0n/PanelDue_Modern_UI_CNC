/*
 * CncToolsPage.cpp
 *
 * WCS > TOOLS, portrait CNC UI (mock-up pd_cnc_wcs_tools_3axis.svg):
 *
 *   TOOLS  TOOL NAME              Z OFFSET
 *   [T0 ] [6mm 3fl flat      ] [ -42.315 ]  [ ^ ]
 *   [T1 ] [3mm ball          ] [ -38.020 ]  [   ]
 *   [T2 ] [90 deg spot drill ] [ -35.550 ]  [ v ]
 *   [T3 ] [1/4in chamfer     ] [ -40.100 ]  [   ]
 *   [   LOAD   ] [  MEASURE  ] [  SET Z  ]
 *
 * The list is whatever RRF has defined (M563 in config.g); the tool number is the index in
 * RRF's tools array. The panel keeps up to 32 tools with any tool number from T0 to T99
 * (a slot remembers its tool number); higher numbers or a 33rd tool are not listed. The loaded (active) tool has an accent-filled T# tile, the selected
 * row an accent outline on all three tiles. The arrows page through the list 4 rows at a time.
 *
 * Tapping T# or the name selects the row; tapping the Z OFFSET tile selects it and opens SET Z.
 * LOAD     no tool in the spindle (T-): "Is T# in the spindle?"  YES = T# P0 (RRF only notes
 *          it, nothing moves; also written to sys/cnc-lasttool.g), NO = T# (tool change).
 *          Another tool loaded: tool change confirmation, then T# (runs the tool change macros).
 * MEASURE  measures the loaded tool on the tool setter: M98 P"0:/sys/toolchange/measure_tool.g" K#;
 *          only for the loaded tool (the one in the spindle); confirmed. The macro saves (M500 P10).
 * SET Z    standard numpad for the selected tool's Z offset: G10 P# Z..., then M500 P10.
 * All three are refused while a job runs or is paused.
 */

#include "CncToolsPage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncPopups.hpp"
#include "CncWcsPage.hpp"
#include "CncControlPage.hpp"
#include "CncSettingsPage.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "PanelDue.hpp"

using namespace CncLayout;
using namespace CncLayout::Tools;
using namespace Cnc;

namespace
{
	constexpr size_t MaxTools = 32;					// slots: up to 32 tools listed at once
	constexpr int MaxToolNumber = 99;				// T0..T99 (fits the DRO T tiles in every layout)
	constexpr size_t NameLen = 24;

	struct ToolInfo
	{
		int8_t number;								// tool number, -1 = free slot
		bool present;								// confirmed by RRF (the "number" field arrived)
		float z;
		String<NameLen> name;
	};

	ToolInfo slots[MaxTools] = {};
	bool slotsInitialised = false;

	void InitSlots()
	{
		if (!slotsInitialised)
		{
			for (ToolInfo& t : slots)
			{
				t.number = -1;
			}
			slotsInitialised = true;
		}
	}

	// Slot of a tool number, or nullptr
	ToolInfo *Find(int tool)
	{
		InitSlots();
		for (ToolInfo& t : slots)
		{
			if (t.number == tool && tool >= 0)
			{
				return &t;
			}
		}
		return nullptr;
	}

	// Slot of a tool number, taking a free slot if needed; nullptr if out of range or full.
	// RRF sends the tool fields alphabetically, so the name can arrive before the number.
	ToolInfo *FindOrAdd(int tool)
	{
		if (tool < 0 || tool > MaxToolNumber)
		{
			return nullptr;
		}
		ToolInfo *t = Find(tool);
		if (t == nullptr)
		{
			for (ToolInfo& s : slots)
			{
				if (s.number < 0)
				{
					s.number = (int8_t)tool;
					s.present = false;
					s.z = 0.0f;
					s.name.Clear();
					return &s;
				}
			}
		}
		return t;
	}

	void FreeSlot(ToolInfo *t)
	{
		t->number = -1;
		t->present = false;
		t->z = 0.0f;
		t->name.Clear();
	}
	int activeTool = -1;
	int selectedTool = -1;
	size_t firstRow = 0;							// index into the ordered list of present tools
	bool jobRunning = false, jobActive = false;

	// Row fields
	ModernTextButton *numButtons[NumRows];
	String<6> numText[NumRows];
	StaticTextField *nameFields[NumRows];
	ModernCard *nameCards[NumRows];
	FloatField *zFields[NumRows];
	ModernCard *zCards[NumRows];
	ModernTouchArea *rowTouch[NumRows];
	ModernTouchArea *zTouch[NumRows];
	int rowTool[NumRows];							// tool shown in each row, -1 = empty row
	bool rowSelected[NumRows];						// outline state currently drawn

	CncArrowButton *upButton, *downButton;
	ModernTextButton *loadButton, *measureButton, *setZButton;

	// Present tool numbers in ascending order; returns the count
	size_t Ordered(int out[])
	{
		InitSlots();
		size_t n = 0;
		for (const ToolInfo& t : slots)
		{
			if (t.number >= 0 && t.present)
			{
				// insertion sort by tool number
				size_t i = n++;
				while (i > 0 && out[i - 1] > t.number)
				{
					out[i] = out[i - 1];
					--i;
				}
				out[i] = t.number;
			}
		}
		return n;
	}

	bool IsTool(int t)
	{
		const ToolInfo * const s = Find(t);
		return s != nullptr && s->present;
	}

	// Only called for tools that IsTool() confirmed
	ToolInfo& Tool(int t)
	{
		return *Find(t);
	}

	void UpdateActionLook()
	{
		SetButtonLocked(loadButton, jobActive || !IsTool(selectedTool) || selectedTool == activeTool);
		SetButtonLocked(measureButton, jobActive || !IsTool(activeTool) || selectedTool != activeTool);
		SetButtonLocked(setZButton, jobActive || !IsTool(selectedTool));
	}

	// Show one row's content; 'textChanged' forces the name to be repainted
	void ShowRow(size_t r, int tool, bool textChanged)
	{
		const bool visible = (tool >= 0);
		const bool toolChanged = (tool != rowTool[r]);
		rowTool[r] = tool;

		if (visible && (toolChanged || textChanged))
		{
			numText[r].printf("T%d", tool);
			numButtons[r]->SetText(numText[r].c_str());
			nameFields[r]->SetValue(Tool(tool).name.c_str(), true);
		}
		if (visible)
		{
			zFields[r]->SetValue(Tool(tool).z);
		}

		// Cards before their text, so the text ends up on top
		mgr.Show(numButtons[r], visible);
		mgr.Show(nameCards[r], visible);
		mgr.Show(nameFields[r], visible);
		mgr.Show(zCards[r], visible);
		mgr.Show(zFields[r], visible);
		mgr.Show(rowTouch[r], visible);
		mgr.Show(zTouch[r], visible);
		if (!visible)
		{
			// Forget the outline, so the row starts unselected when it shows again
			rowSelected[r] = false;
			numButtons[r]->SetBorderColour(Border);
			nameCards[r]->SetBorderColour(Border);
			zCards[r]->SetBorderColour(Border);
			return;
		}

		mgr.Press(ButtonPress(numButtons[r], 0), tool == activeTool);

		const bool selected = (tool == selectedTool);
		if (selected != rowSelected[r])
		{
			rowSelected[r] = selected;
			const Colour c = selected ? Accent() : Border;
			numButtons[r]->SetBorderColour(c);
			nameCards[r]->SetBorderColour(c);
			zCards[r]->SetBorderColour(c);
			// The card repaint covers the text: repaint the text after it
			mgr.Redraw(nameCards[r]);
			mgr.Redraw(nameFields[r]);
			mgr.Redraw(zCards[r]);
			mgr.Redraw(zFields[r]);
		}
	}

	void UpdateRows(bool textChanged = false)
	{
		int order[MaxTools];
		const size_t n = Ordered(order);

		// Keep a valid selection: the loaded tool, else the first one
		if (!IsTool(selectedTool))
		{
			selectedTool = IsTool(activeTool) ? activeTool : (n > 0) ? order[0] : -1;
		}
		// Keep the page inside the list
		while (firstRow > 0 && firstRow >= n)
		{
			firstRow = (firstRow >= NumRows) ? firstRow - NumRows : 0;
		}

		for (size_t r = 0; r < NumRows; ++r)
		{
			const size_t idx = firstRow + r;
			ShowRow(r, (idx < n) ? order[idx] : -1, textChanged);
		}
		upButton->SetDisabled(firstRow == 0);
		downButton->SetDisabled(firstRow + NumRows >= n);
		UpdateActionLook();
	}

	// Page so that the given tool is visible
	void ScrollTo(int tool)
	{
		int order[MaxTools];
		const size_t n = Ordered(order);
		for (size_t i = 0; i < n; ++i)
		{
			if (order[i] == tool)
			{
				firstRow = (i / NumRows) * NumRows;
				return;
			}
		}
	}

	// ---- LOAD ----
	// Tool change: T# runs the tool change macros (manual prompt or ATC)
	void DoLoad(int tool)
	{
		if (jobActive || !IsTool(tool))
		{
			Refuse(jobActive ? CNC_LOCKED_JOB_ACTIVE : "The tool list changed. Try again.");
			return;
		}
		SerialIo::Sendf("T%d\n", tool);
	}

	// No tool in the spindle (T-): the answer to "Is T# in the spindle?"
	void LoadAnswer(int tool, const uint16_t selected[])
	{
		if (jobActive || !IsTool(tool) || IsTool(activeTool))
		{
			// a tool was loaded elsewhere (DWC) while the question was open: T# P0 would skip its tool change
			Refuse(jobActive ? CNC_LOCKED_JOB_ACTIVE : "The tool list changed. Try again.");
			return;
		}
		if (selected[0] & 1u)
		{
			// YES: it is already in. RRF only notes it (P0: no tool change macros, nothing moves).
			// The last-tool file is what config.g restores at power-on when REMEMBER TOOL is on.
			SerialIo::Sendf("T%d P0\n", tool);
			SerialIo::Sendf("echo >\"0:/sys/cnc-lasttool.g\" \"T%d P0\"\n", tool);
		}
		else
		{
			SerialIo::Sendf("T%d\n", tool);			// NO: tool change
		}
	}

	void AskLoad()
	{
		static String<16> title;
		title.printf("LOAD T%d", selectedTool);
		if (!IsTool(activeTool))
		{
			static String<28> question;
			static const char * const answers[2] = { "YES", "NO" };
			question.printf("IS T%d IN THE SPINDLE?", selectedTool);
			CncPopup::FormGroup group = { question.c_str(), answers, 2, false, 0, 0, 0 };	// no default answer
			CncPopup::Form(title.c_str(), &group, 1, LoadAnswer, selectedTool);
			return;
		}

		String<24> line1;
		line1.printf("Change T%d -> T%d?", activeTool, selectedTool);
		const char *name = Tool(selectedTool).name.c_str();
		if (name[0] != 0)
		{
			CncPopup::Confirm(title.c_str(), line1.c_str(), name, "Runs the tool change.", nullptr, DoLoad, selectedTool);
		}
		else
		{
			CncPopup::Confirm(title.c_str(), line1.c_str(), "Runs the tool change.", nullptr, nullptr, DoLoad, selectedTool);
		}
	}

	// ---- MEASURE (confirmed) ----
	void DoMeasure(int tool)
	{
		if (jobActive || tool != activeTool)
		{
			Refuse(jobActive ? CNC_LOCKED_JOB_ACTIVE : "The loaded tool changed. Try again.");
			return;
		}
		SerialIo::Sendf("M98 P\"0:/sys/toolchange/measure_tool.g\" K%d\n", tool);
	}

	void AskMeasure()
	{
		String<16> title;
		String<24> line1;
		title.printf("MEASURE T%d", activeTool);
		line1.printf("Measure T%d length", activeTool);
		CncPopup::Confirm(title.c_str(), line1.c_str(), "on the tool setter?", nullptr, nullptr, DoMeasure, activeTool);
	}

	// ---- SET Z (standard numpad) ----
	void ZEntered(int tool, float value)
	{
		if (jobActive || !IsTool(tool))
		{
			Refuse(jobActive ? CNC_LOCKED_JOB_ACTIVE : "The tool list changed. Try again.");
			return;
		}
		SerialIo::Sendf("G10 P%d Z%.3f\nM500 P10\n", tool, (double)value);	// saved right away
		CncWcs::MarkSaved();						// M500 saved the work offsets too
	}

	void OpenSetZ()
	{
		String<12> tag;
		tag.printf("T%d Z", selectedTool);
		CncPopup::NumpadSpec pad;
		pad.tag = tag.c_str();
		pad.unit = "mm";
		pad.value = Tool(selectedTool).z;
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = true;
		pad.min = -999.0f;
		pad.max = 999.0f;
		pad.pos = nullptr;
		pad.onOk = ZEntered;
		pad.param = selectedTool;
		CncPopup::Numpad(pad);
	}
}

namespace CncTools
{
	void Create()
	{
		// Labels
		AddLabel(LabelY, NumX + 2, NumW, "TOOLS");
		AddLabel(LabelY, NameX + 2, NameW, "TOOL NAME");
		AddLabel(LabelY, ZX + 2, ZW, "Z OFFSET");

		// Rows. AddField prepends: text before its card, so the card is drawn first.
		for (size_t r = 0; r < NumRows; ++r)
		{
			const PixelNumber y = RowY(r);
			numButtons[r] = AddButton(y, NumX, NumW, RowH, "", evCncToolRow, (int)r, glcd19x21);

			DisplayField::SetDefaultFont(glcd19x21);
			DisplayField::SetDefaultColours(Text, Tile);
			nameFields[r] = new StaticTextField(y + (RowH - 21) / 2, NameX + 12, NameW - 20, TextAlignment::Left, "");
			mgr.AddField(nameFields[r]);
			nameCards[r] = new ModernCard(y, NameX, NameW, RowH, Tile, Border, true);
			mgr.AddField(nameCards[r]);

			zFields[r] = new FloatField(y + (RowH - 21) / 2, ZX + 8, ZW - 20, TextAlignment::Right, 3);
			mgr.AddField(zFields[r]);
			zCards[r] = new ModernCard(y, ZX, ZW, RowH, Tile, Border, true);
			mgr.AddField(zCards[r]);

			rowTouch[r] = new ModernTouchArea(y, NameX, NameW, RowH, evCncToolRow, (int)r);
			mgr.AddField(rowTouch[r]);
			zTouch[r] = new ModernTouchArea(y, ZX, ZW, RowH, evCncToolZ, (int)r);
			mgr.AddField(zTouch[r]);

			rowTool[r] = -2;						// forces the first ShowRow to fill the row
			rowSelected[r] = false;
		}

		// Page arrows, each two rows tall
		upButton = new CncArrowButton(RowY(0), ArrowX, ArrowW, ArrowH, true, evCncToolScroll, -1);
		mgr.AddField(upButton);
		downButton = new CncArrowButton(RowY(2), ArrowX, ArrowW, ArrowH, false, evCncToolScroll, 1);
		mgr.AddField(downButton);

		// Actions
		loadButton = AddButton(ActY, Margin, ActW, ActH, "LOAD", evCncToolLoad, 0);
		measureButton = AddButton(ActY, Margin + ActW + ActGap, ActW, ActH, "MEASURE", evCncToolMeasure, 0);
		setZButton = AddButton(ActY, Margin + 2 * (ActW + ActGap), ActW, ActH, "SET Z", evCncToolSetZ, 0);

		UpdateRows(true);
	}

	bool ProcessTouch(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncToolRow:
			{
				const size_t r = (size_t)bp.GetIParam();
				if (r < NumRows && rowTool[r] >= 0 && rowTool[r] != selectedTool)
				{
					selectedTool = rowTool[r];
					UpdateRows();
				}
			}
			return true;

		case evCncToolZ:
			{
				const size_t r = (size_t)bp.GetIParam();
				if (r >= NumRows || rowTool[r] < 0)
				{
					return true;
				}
				if (rowTool[r] != selectedTool)
				{
					selectedTool = rowTool[r];
					UpdateRows();
				}
				if (jobActive)
				{
					Refuse(CNC_LOCKED_JOB_ACTIVE);		// selected, but no editing during a job
					return true;
				}
				OpenSetZ();
			}
			return true;

		case evCncToolScroll:
			{
				int order[MaxTools];
				const size_t n = Ordered(order);
				if (bp.GetIParam() < 0 && firstRow > 0)
				{
					firstRow = (firstRow >= NumRows) ? firstRow - NumRows : 0;
				}
				else if (bp.GetIParam() > 0 && firstRow + NumRows < n)
				{
					firstRow += NumRows;
				}
				else
				{
					return true;					// already at the end: the arrow is greyed
				}
				UpdateRows(true);
			}
			return true;

		case evCncToolLoad:
			if (jobActive)
			{
				Refuse(CNC_LOCKED_JOB_ACTIVE);
				return true;
			}
			if (CncControl::RefuseUnhomed())
			{
				return true;						// the tool change macros move in machine coordinates
			}
			if (!IsTool(selectedTool))
			{
				Refuse("Select a tool first.");
				return true;
			}
			if (selectedTool == activeTool)
			{
				String<32> msg;
				msg.printf("T%d is already loaded.", selectedTool);
				Refuse(msg.c_str());
				return true;
			}
			AskLoad();
			return true;

		case evCncToolMeasure:
			if (jobActive)
			{
				Refuse(CNC_LOCKED_JOB_ACTIVE);
				return true;
			}
			if (CncControl::RefuseUnhomed())
			{
				return true;						// measure_tool.g moves to the setter with G53
			}
			if (!IsTool(activeTool))
			{
				Refuse("No tool is loaded.\nLOAD a tool first.");
				return true;
			}
			if (!CncSettings::ToolSetter())
			{
				Refuse("No tool setter.\nTOOL SETTER is off in SETTINGS.");
				return true;
			}
			if (selectedTool != activeTool)
			{
				String<48> msg;
				msg.printf("Only the loaded tool (T%d) can be measured.", activeTool);
				Refuse(msg.c_str());
				return true;
			}
			AskMeasure();
			return true;

		case evCncToolSetZ:
			if (jobActive || !IsTool(selectedTool))
			{
				Refuse(jobActive ? CNC_LOCKED_JOB_ACTIVE : "Select a tool first.");
				return true;
			}
			OpenSetZ();
			return true;

		default:
			return false;
		}
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncToolRow:
		case evCncToolZ:
		case evCncToolScroll:
		case evCncToolLoad:
		case evCncToolMeasure:
		case evCncToolSetZ:
			return true;						// never pressed: selection, paging or a popup
		default:
			return false;
		}
	}

	void SetPresent(size_t tool, bool present)
	{
		if (present)
		{
			ToolInfo * const t = FindOrAdd((int)tool);
			if (t != nullptr && !t->present)
			{
				t->present = true;
				UpdateRows(true);
			}
		}
		else
		{
			ToolInfo * const t = Find((int)tool);
			if (t != nullptr)
			{
				const bool wasShown = t->present;
				FreeSlot(t);
				if (wasShown)
				{
					UpdateRows(true);
				}
			}
		}
	}

	void SetName(size_t tool, const char *name)
	{
		ToolInfo * const t = FindOrAdd((int)tool);
		if (t != nullptr)
		{
			String<NameLen> truncated;					// long names are stored cut to NameLen
			truncated.copy(name);
			if (!t->name.Equals(truncated.c_str()))
			{
				t->name.copy(truncated.c_str());
				if (t->present)
				{
					UpdateRows(true);
				}
			}
		}
	}

	void RemoveFrom(size_t firstTool)
	{
		InitSlots();
		bool changed = false;
		for (ToolInfo& t : slots)
		{
			if (t.number >= 0 && (size_t)t.number >= firstTool)
			{
				changed = changed || t.present;
				FreeSlot(&t);
			}
		}
		if (changed)
		{
			UpdateRows(true);
		}
	}

	void SetZOffset(size_t tool, float offset)
	{
		ToolInfo * const t = FindOrAdd((int)tool);
		if (t != nullptr && t->z != offset)
		{
			t->z = offset;							// measured (measure_tool.g saves itself), SET Z (saved), DWC ...
			if (t->present)
			{
				UpdateRows();
			}
		}
	}

	void SetActiveTool(int tool)
	{
		if (tool != activeTool)
		{
			const bool followSelection = (selectedTool == activeTool);
			activeTool = tool;
			if (followSelection && IsTool(tool))
			{
				selectedTool = tool;				// a tool change moves the selection along
				ScrollTo(tool);
				UpdateRows(true);
			}
			else
			{
				UpdateRows();
			}
		}
	}

	void SetJobState(bool running, bool active)
	{
		if (running != jobRunning || active != jobActive)
		{
			jobRunning = running;
			jobActive = active;
			UpdateActionLook();
		}
	}
}

// End
