/*
 * CncCommon.cpp
 */

#include "CncCommon.hpp"
#include <UI/UserInterface.hpp>
#include "FlashData.hpp"
#include "PanelDue.hpp"
#include "Icons/Icons.hpp"
#include "CncPopups.hpp"
#include "Hardware/SerialIo.hpp"
#include "Hardware/SysTick.hpp"
#include <General/String.h>
#include <cmath>

namespace Cnc
{
	const Colour PageBg     = UTFT::fromRGB(18, 22, 28);
	const Colour Tile       = UTFT::fromRGB(28, 34, 43);
	const Colour Text       = UTFT::fromRGB(229, 232, 236);
	const Colour Muted      = UTFT::fromRGB(154, 164, 178);
	const Colour Border     = UTFT::fromRGB(59, 67, 79);
	const Colour DarkText   = UTFT::fromRGB(18, 22, 28);
	const Colour NotHomedBg = UTFT::fromRGB(128, 64, 0);
	const Colour StopRed    = UTFT::fromRGB(201, 50, 24);

	Colour Accent()
	{
		return AccentColour(nvData.GetAccentColour());
	}

	Colour AccentColour(uint8_t index)
	{
		switch (index % NumAccentColours)
		{
		case 0:  return UTFT::fromRGB(238, 112, 4);		// Orange Fire
		case 1:  return UTFT::fromRGB(255, 0, 0);		// Red Voron
		case 2:  return UTFT::fromRGB(0, 124, 247);		// Duet Blue
		case 3:  return UTFT::fromRGB(0, 255, 255);		// Cyan
		case 4:  return UTFT::fromRGB(80, 150, 56);		// RepRap Green
		case 5:  return UTFT::fromRGB(252, 209, 10);	// Yellow / Gold
		case 6:  return UTFT::fromRGB(103, 255, 0);		// Lime
		default: return UTFT::fromRGB(255, 0, 255);		// Magenta
		}
	}

	void AddCard(PixelNumber y, PixelNumber x, PixelNumber w, PixelNumber h, bool border)
	{
		mgr.AddField(new ModernCard(y, x, w, h, Tile, Border, border));
	}

	StaticTextField *AddLabel(PixelNumber y, PixelNumber x, PixelNumber w, const char *text, TextAlignment align)
	{
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, PageBg);
		StaticTextField * const f = new StaticTextField(y, x, w, align, text);
		mgr.AddField(f);
		return f;
	}

	ModernTextButton *AddButton(PixelNumber y, PixelNumber x, PixelNumber w, PixelNumber h,
								const char *text, event_t e, int param, const uint8_t *font)
	{
		const Colour accent = Accent();
		DisplayField::SetDefaultColours(Text, Tile, Border, Tile, accent, accent, IconPaletteDark);
		ModernTextButton * const b = new ModernTextButton(y, x, w, h, text, e, param, font, true);
		mgr.AddField(b);
		return b;
	}

	void Select(ButtonBase *&selected, ButtonBase *b)
	{
		if (selected != nullptr && selected != b)
		{
			mgr.Press(ButtonPress(selected, 0), false);
		}
		selected = b;
		mgr.Press(ButtonPress(b, 0), true);
	}

	static bool jobInProgress = false;

	void SetJobInProgress(bool b) { jobInProgress = b; }
	bool JobInProgress() { return jobInProgress; }

	static bool IsRunning(OM::PrinterStatus status)
	{
		switch (status)
		{
		case OM::PrinterStatus::toolChange:
		case OM::PrinterStatus::busy:
		case OM::PrinterStatus::connecting:
			return jobInProgress;				// a tool change (or macro, or a lost connection) inside a job still counts as the job

		case OM::PrinterStatus::printing:
		case OM::PrinterStatus::pausing:
		case OM::PrinterStatus::resuming:
		case OM::PrinterStatus::simulating:
		case OM::PrinterStatus::cancelling:
			return true;
		default:
			return false;
		}
	}

	bool MachineBusy()
	{
		return IsRunning(GetStatus());
	}

	bool JobActive(OM::PrinterStatus status)
	{
		return IsRunning(status) || status == OM::PrinterStatus::paused;
	}

	bool JobActive()
	{
		return JobActive(GetStatus());
	}

	void AddSubTabs(const char * const labels[], unsigned int n, ModernTextButton *tabs[])
	{
		using namespace CncLayout;
		const Colour accent = Accent();
		DisplayField::SetDefaultColours(Text, Tile, Tile, Tile, accent, accent, IconPaletteDark);
		for (unsigned int i = 0; i < n; ++i)
		{
			tabs[i] = new ModernTextButton(SubTabY, SubTabX(i, n), SubTabW(i, n), SubTabH, labels[i], evCncSubTab, (int)i, glcd19x21, false);
			mgr.AddField(tabs[i]);
		}
		mgr.AddField(new ModernCard(SubTabY, Margin, ContentW, SubTabH, Tile, Tile, false));	// strip behind the tabs
	}

	void SetButtonLocked(ModernTextButton *b, bool locked)
	{
		b->SetColours(locked ? Border : Text, Tile);
	}

	void SetToggleLook(ModernTextButton *b, bool on)
	{
		const Colour accent = Accent();
		b->SetBorderColour(on ? accent : Border);
		b->SetColours(on ? accent : Text, Tile);
	}

	// ---- Aux toggle ----
	namespace
	{
		bool auxOn = false;
		bool auxVacuum = false;
		void (*auxListeners[3])() = { nullptr, nullptr, nullptr };

		String<12> customLabels[NumCustomMacros];
		String<64> customMacros[NumCustomMacros];
		void (*customListeners[3])() = { nullptr, nullptr, nullptr };

		void AddListener(void (*list[3])(), void (*fn)())
		{
			for (size_t i = 0; i < 3; ++i)
			{
				if (list[i] == nullptr)
				{
					list[i] = fn;
					return;
				}
			}
		}

		void Notify(void (*list[3])())
		{
			for (size_t i = 0; i < 3; ++i)
			{
				if (list[i] != nullptr)
				{
					list[i]();
				}
			}
		}

		// File name part of a macro path ("0:/macros/!park.g" -> "!park.g")
		const char *MacroFileName(size_t slot)
		{
			const char * const path = customMacros[slot].c_str();
			const char *name = path;
			for (const char *p = path; *p != 0; ++p)
			{
				if (*p == '/' || *p == ':')
				{
					name = p + 1;
				}
			}
			return name;
		}

		void SendCustom(int slot)
		{
			if (slot == 0 && MachineBusy())
			{
				Refuse(CNC_LOCKED_JOB);			// CONTROL's CUSTOM 1: a job started while the question was open
				return;
			}
			SerialIo::Sendf("M98 P\"%s\"\n", customMacros[slot].c_str());
		}
	}

	bool AuxOn() { return auxOn; }

	void ToggleAux()
	{
		auxOn = !auxOn;
		SerialIo::Sendf(auxOn ? "M98 P\"0:/macros/aux_on.g\"\n" : "M98 P\"0:/macros/aux_off.g\"\n");
		Notify(auxListeners);
	}

	const char *AuxLabel() { return auxVacuum ? "VACUUM" : "COOLANT"; }

	void SetAuxLabel(bool vacuum)
	{
		auxVacuum = vacuum;
		Notify(auxListeners);
	}

	void AddAuxListener(void (*listener)()) { AddListener(auxListeners, listener); }

	// ---- Custom macros ----
	void SetCustomMacro(size_t slot, const char *label, const char *macro)
	{
		if (slot >= NumCustomMacros)
		{
			return;
		}
		const bool used = (label != nullptr && label[0] != 0 && macro != nullptr && macro[0] != 0);
		customLabels[slot].copy(used ? label : "");
		customMacros[slot].copy(used ? macro : "");
		Notify(customListeners);
	}

	bool CustomUsed(size_t slot) { return slot < NumCustomMacros && !customMacros[slot].IsEmpty(); }
	const char *CustomLabel(size_t slot) { return (slot < NumCustomMacros) ? customLabels[slot].c_str() : ""; }
	const char *CustomMacro(size_t slot) { return (slot < NumCustomMacros) ? customMacros[slot].c_str() : ""; }

	void RunCustomMacro(size_t slot)
	{
		if (!CustomUsed(slot))
		{
			return;
		}
		if (MacroFileName(slot)[0] == '!')
		{
			String<40> line;
			line.printf("Run %s?", MacroFileName(slot));
			CncPopup::Confirm(customLabels[slot].c_str(), line.c_str(), nullptr, nullptr, nullptr, SendCustom, (int)slot);
		}
		else
		{
			SendCustom((int)slot);
		}
	}

	void AddCustomListener(void (*listener)()) { AddListener(customListeners, listener); }

	namespace
	{
		constexpr size_t MaxStoredAxes = 4;
		float machinePositions[MaxStoredAxes] = {};
		float workPositions[MaxStoredAxes] = {};
	}

	static uint32_t lastPositionChange = 0;

	void SetMachinePosition(size_t axis, float value)
	{
		if (axis < MaxStoredAxes)
		{
			if (fabsf(machinePositions[axis] - value) > 0.0005f)
			{
				lastPositionChange = SystemTick::GetTickCount();	// the machine is moving
			}
			machinePositions[axis] = value;
		}
	}

	bool GuardedJogAllowed(uint32_t& lastSent, bool ignoreBusy)
	{
		constexpr uint32_t GuardTime = 700;				// ms after sending: covers the status poll delay
		constexpr uint32_t SettleTime = 1200;			// ms without a position change (2 polls): the move is over
		const uint32_t now = SystemTick::GetTickCount();
		if ((lastSent != 0 && now - lastSent < GuardTime)
			|| (!ignoreBusy && GetStatus() == OM::PrinterStatus::busy)
			|| (lastPositionChange != 0 && now - lastPositionChange < SettleTime))	// also when paused (RRF reports paused)
		{
			return false;								// also a move from another page (CONTROL / probe jog) still running
		}
		lastSent = (now == 0) ? 1 : now;
		return true;
	}

	float MachinePosition(size_t axis)
	{
		return (axis < MaxStoredAxes) ? machinePositions[axis] : 0.0f;
	}

	void SetWorkPosition(size_t axis, float value)
	{
		if (axis < MaxStoredAxes)
		{
			workPositions[axis] = value;
		}
	}

	float WorkPosition(size_t axis)
	{
		return (axis < MaxStoredAxes) ? workPositions[axis] : 0.0f;
	}

	void Refuse(const char *reason)
	{
		CncPopup::Alert(reason);
	}
}

// End
