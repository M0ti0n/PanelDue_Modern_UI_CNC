/*
 * CncSettingsPage.cpp
 *
 * SYSTEM > SETTINGS, portrait CNC UI (see CncSettingsPage.hpp).
 *
 * Page 1 SYSTEM copies the Modern UI SETTINGS page (UserInterface.cpp, OpenSettings*Popup and the
 * evStandardPopupConfirm cases): the same popups, choices and titles, every change saved to the
 * panel's flash straight away (no SAVE button). ACCENT COLOR restarts the panel, as there.
 *
 * Page 2 CUSTOMIZATION is the machine's: its values are globals in 0:/sys/cnc-settings.g.
 *   PROBE MODE     AUTO / SEMI-AUTO / MANUAL     global.cncProbeMode 0 / 1 / 2
 *   TOOL HOLDER    ER COLLET / TOOL HOLDER       global.cncMeasureAfterChange true / false
 *   REMEMBER TOOL  toggle                        global.cncRememberTool
 *   TOOL SETTER    toggle                        global.cncToolSetter
 *   CUSTOM n       MACRO / LABEL / CLEAR         global.cncCustom<n>Macro, global.cncCustom<n>Label (n = 1..3:
 *                                                1 on CONTROL, 2 and 3 on JOB STATUS)
 *   COOL / VAC     COOLANT / VACUUM              global.cncAuxVacuum
 * On a change the panel first sets the changed globals on the machine at once ("set global.x = v",
 * or "global x = v" when the machine does not have it yet), so what the machine reports back is the
 * new value whatever happens to the file. Then it rewrites the whole file with echo, one line every
 * WriteInterval ms so the machine's serial input is never flooded, and renames it over the old one:
 * the file only matters at the next power-on (config.g runs it). It is NOT run again after the write:
 * if the write or the rename had failed, running the old file would put the old values back.
 * Values from the machine are ignored while a write is going on and shortly after it.
 */

#include "CncSettingsPage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncPopups.hpp"
#include "CncKeyboard.hpp"
#include "CncProbePage.hpp"
#include "CncMacrosPage.hpp"
#include "CncSystemPage.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "Hardware/SysTick.hpp"
#include "Hardware/Mem.hpp"
#include "Hardware/Reset.hpp"
#include "FlashData.hpp"
#include "PanelDue.hpp"
#include <General/String.h>
#include <General/SafeVsnprintf.h>
#include "Library/Misc.hpp"
#include "Icons/Icons.hpp"
#include <cstdlib>

using namespace CncLayout;
using namespace CncLayout::Settings;
using namespace Cnc;

namespace
{
	// Printer UI events the CNC UI does not create otherwise (event_t is full), routed by page
	constexpr Event evCncMachineSetting = evSettingsHeaterCombineOpen;	// page 2 tile, iParam = MachineItem
	constexpr Event evCncSettingsPage = evSettingsVolumeSelect;			// page arrows, iParam -1 / +1

	const Colour ButtonText = UTFT::fromRGB(36, 36, 36);

	constexpr size_t NumPages = 2;
	DisplayField *pageRoots[NumPages];
	size_t currentPage = 0;

	// ---- Page 1: panel ----
	enum PanelItem : uint8_t { PAccent, PBrightness, PVolume, PInfoTimeout, PDimming, PTouch, PMirror, PInvert, PBaud, NumPanelTiles };
	const char * const PanelLabels[NumPanelTiles] =
		{ "ACCENT COLOR", "BRIGHTNESS", "VOLUME", "INFO TIMEOUT", "SCREEN DIMMING", "TOUCH CALIBR.", "MIRROR DISPLAY", "INVERT DISPLAY", "BAUD" };
	const Event PanelEvents[NumPanelTiles] =
		{ evSettingsAccentOpen, evSettingsBrightnessOpen, evSettingsVolumeOpen, evSettingsInfoTimeoutOpen, evSettingsAlwaysDimToggle,
		  evSettingsTouchOpen, evInvertX, evInvertY, evSettingsBaudOpen };
	constexpr unsigned int RamTile = NumPanelTiles;			// tile 9: RAM monitor

	ModernTextButton *dimmingTile;
	StaticTextField *ramField;
	String<16> ramText;
	uint32_t lastRam = 0xFFFFFFFF, lastRamUpdate = 0;

	StaticTextField *ipField;
	String<20> ipText;

	// Choices, as the Modern UI popups
	const char * const VolumeLabels[] = { "0", "1", "2", "3", "4", "5" };
	const char * const BrightnessLabels[] = { "0%", "20%", "40%", "60%", "80%", "100%" };
	const int BrightnessValues[] = { 0, 20, 40, 60, 80, 100 };
	const char * const InfoTimeoutLabels[] = { "0s", "3s", "6s", "10s" };
	const int InfoTimeoutValues[] = { 0, 3, 6, 10 };
	const char * const BaudLabels[] = { "9600", "19200", "38400", "57600", "115200" };
	const int BaudValues[] = { 9600, 19200, 38400, 57600, 115200 };
	Colour swatchColours[NumAccentColours];

	// ---- Page 2: machine ----
	enum MachineItem : uint8_t { MProbeMode, MToolHolder, MRememberTool, MToolSetter, MCustom1, MCustom2, MCustom3, MAux, NumMachineTiles };
	const char * const MachineLabels[NumMachineTiles] =
		{ "PROBE MODE", "TOOL HOLDER", "REMEMBER TOOL", "TOOL SETTER", "CUSTOM 1", "CUSTOM 2", "CUSTOM 3", "COOL / VAC" };
	ModernTextButton *rememberTile, *setterTile;

	const char * const ProbeModeLabels[] = { "AUTO", "SEMI-AUTO", "MANUAL" };
	const char * const ToolHolderLabels[] = { "ER COLLET", "TOOL HOLDER" };
	const char * const AuxLabels[] = { "COOLANT", "VACUUM" };
	const char * const CustomLabels[] = { "MACRO", "LABEL", "CLEAR" };
	const char * const CustomTitles[NumCustomMacros] = { "CUSTOM 1", "CUSTOM 2", "CUSTOM 3" };
	enum CustomAction : uint8_t { CMacro, CLabel, CClear };

	constexpr size_t MaxLabel = 12;
	constexpr size_t MaxMacroPath = 63;
	struct Machine
	{
		uint8_t probeMode = 0;							// CncProbe::Mode
		bool measureAfterChange = true;					// ER COLLET
		bool rememberTool = false;
		bool toolSetter = true;
		bool auxVacuum = false;
		String<MaxLabel> labels[NumCustomMacros];
		String<MaxMacroPath> macros[NumCustomMacros];
	} machine,
	  incoming;											// a global reply being received: taken over when complete
	bool receiving = false;
	uint32_t presentMask = 0;							// bit per Global: the machine has this global
	uint32_t incomingPresent = 0;						// same, for the reply being received
	bool verifyReadback = false;						// the next complete reply must equal what we wrote

	// ---- Writing sys/cnc-settings.g ----
	enum class Global : uint8_t
	{
		ProbeMode, MeasureAfterChange, RememberTool, ToolSetter, AuxVacuum,
		Custom1Label, Custom1Macro, Custom2Label, Custom2Macro, Custom3Label, Custom3Macro,
		Count
	};
	constexpr size_t NumGlobals = (size_t)Global::Count;
	const char * const GlobalNames[NumGlobals] =
	{
		"cncProbeMode", "cncMeasureAfterChange", "cncRememberTool", "cncToolSetter", "cncAuxVacuum",
		"cncCustom1Label", "cncCustom1Macro", "cncCustom2Label", "cncCustom2Macro",
		"cncCustom3Label", "cncCustom3Macro"
	};
	constexpr uint32_t Bit(Global g) { return 1u << (unsigned)g; }
	// Label and macro of CUSTOM slot + 1 (they are next to each other in the enum)
	constexpr uint32_t SlotBits(size_t slot) { return 3u << ((unsigned)Global::Custom1Label + 2 * (unsigned)slot); }
	constexpr const char *SettingsFile = "0:/sys/cnc-settings.g";
	constexpr const char *TempFile = "0:/sys/cnc-settings.tmp";	// written first, renamed when complete
	const char * const HeaderLines[] =
	{
		"; CNC panel settings (SYSTEM > SETTINGS > CUSTOMIZATION), loaded by config.g at power-on.",
		"; Written by the panel on every change. config.g declares the globals first (global.cnc*).",
	};
	constexpr size_t NumHeaderLines = ARRAY_SIZE(HeaderLines);
	constexpr size_t LinesPerGlobal = 1;				// set global.x = v (config.g declares the globals)
	constexpr size_t NumFileLines = NumHeaderLines + NumGlobals * LinesPerGlobal;
	constexpr uint32_t WriteInterval = 100;				// ms between lines (the machine appends each one to the SD card)
	constexpr uint32_t IgnoreAfterWrite = 3000;			// ms after the last line: replies may still show older values

	bool writing = false;
	size_t writeLine = 0;								// next line; NumFileLines = the M98 is next
	uint32_t lastWriteTime = 0, writeDoneTime = 0;
	bool ignoring = false;								// machine values ignored until IgnoreAfterWrite after the M98
	bool globalsKnown = false;							// the machine's settings have arrived since connecting

	// ---- CUSTOM n ----
	size_t customSlot = 0;
	bool kbOpen = false;
	void LabelChanged(const char *text);
	void LabelEntered(const char *text);
	const CncKeyboard::Client kbClient = { LabelChanged, LabelEntered, nullptr, nullptr, false };
	String<CncKeyboard::MaxText> labelEdit;

	// -----------------------------------------------------------------
	// Helpers
	// -----------------------------------------------------------------
	bool Connected()
	{
		return GetStatus() != OM::PrinterStatus::connecting;
	}

	// The machine reads the panel's lines now (not busy running something the aux channel waits for)
	bool MachineListening()
	{
		const OM::PrinterStatus st = GetStatus();
		return st == OM::PrinterStatus::idle || st == OM::PrinterStatus::printing
			|| st == OM::PrinterStatus::paused || st == OM::PrinterStatus::off;
	}

	// Panel restarts and baud changes would cut the file short
	bool RefuseWhileWriting()
	{
		if (writing)
		{
			Refuse("The machine settings are being saved.\nTry again in a few seconds.");
			return true;
		}
		return false;
	}

	// Index of the choice nearest to 'value' (a value set elsewhere may not be one of the choices)
	size_t Nearest(const int values[], size_t n, int value)
	{
		size_t best = 0;
		for (size_t i = 1; i < n; ++i)
		{
			if (abs(values[i] - value) < abs(values[best] - value))
			{
				best = i;
			}
		}
		return best;
	}

	void ShowToggles()
	{
		SetToggleLook(dimmingTile, nvData.GetDisplayDimmerType() != DisplayDimmerType::never);
		SetToggleLook(rememberTile, machine.rememberTool);
		SetToggleLook(setterTile, machine.toolSetter);
	}

	void ShowRam(bool force)
	{
		const uint32_t ram = GetFreeMemory();
		if (force || ram != lastRam)
		{
			lastRam = ram;
			ramText.printf("RAM %lu", (unsigned long)ram);
			ramField->SetValue(ramText.c_str(), true);
		}
	}

	// Labels are written into a G-code string: no quote characters, no checksum or comment start
	bool LabelCharOk(char c)
	{
		return c >= ' ' && c != '"' && c != '\'' && c != '*' && c != ';';
	}

	// Default label: the macro's display name in capitals, cut to 12
	void DefaultLabel(size_t slot, const char *name)
	{
		machine.labels[slot].Clear();
		for (const char *p = name; *p != 0 && machine.labels[slot].strlen() < MaxLabel; ++p)
		{
			if (LabelCharOk(*p))
			{
				machine.labels[slot].cat((char)toupper((unsigned char)*p));
			}
		}
		if (machine.labels[slot].IsEmpty())
		{
			machine.labels[slot].copy(CustomTitles[slot]);	// nothing usable in the name (e.g. "01_.g")
		}
	}

	// Push the machine values to the pages that use them
	void ApplyMachine()
	{
		CncProbe::SetMode((CncProbe::Mode)machine.probeMode);
		if (strcmp(AuxLabel(), machine.auxVacuum ? "VACUUM" : "COOLANT") != 0)
		{
			SetAuxLabel(machine.auxVacuum);
		}
		for (size_t i = 0; i < NumCustomMacros; ++i)
		{
			const bool used = !machine.labels[i].IsEmpty() && !machine.macros[i].IsEmpty();
			if (used != CustomUsed(i)
				|| (used && (strcmp(CustomLabel(i), machine.labels[i].c_str()) != 0 || strcmp(CustomMacro(i), machine.macros[i].c_str()) != 0)))
			{
				SetCustomMacro(i, machine.labels[i].c_str(), machine.macros[i].c_str());
			}
		}
		ShowToggles();
	}

	// Value of a global as it goes into the file
	void FormatValue(Global g, String<80>& out)
	{
		switch (g)
		{
		case Global::ProbeMode:				out.printf("%u", (unsigned int)machine.probeMode); break;
		case Global::MeasureAfterChange:	out.copy(machine.measureAfterChange ? "true" : "false"); break;
		case Global::RememberTool:			out.copy(machine.rememberTool ? "true" : "false"); break;
		case Global::ToolSetter:			out.copy(machine.toolSetter ? "true" : "false"); break;
		case Global::AuxVacuum:				out.copy(machine.auxVacuum ? "true" : "false"); break;
		default:
			{
				const size_t k = (size_t)g - (size_t)Global::Custom1Label;
				const size_t slot = k / 2;
				out.copy("\"");
				for (const char *p = (k % 2 == 0) ? machine.labels[slot].c_str() : machine.macros[slot].c_str(); *p != 0; ++p)
				{
					if (*p == '"')
					{
						out.cat('"');						// set from DWC: a quote in a string is doubled
					}
					out.cat(*p);
				}
				out.cat('"');
			}
			break;
		}
	}

	// Send one line of the file: echo >"file" "<line>" (quotes in the line doubled)
	void SendFileLine(size_t n)
	{
		String<120> line;
		if (n < NumHeaderLines)
		{
			line.copy(HeaderLines[n]);
		}
		else
		{
			const size_t k = n - NumHeaderLines;
			const Global g = (Global)(k / LinesPerGlobal);
			const char * const name = GlobalNames[(size_t)g];
			String<80> value;
			FormatValue(g, value);
			line.printf("set global.%s = %s", name, value.c_str());
		}
		String<240> cmd;
		cmd.printf("echo %s\"%s\" \"", (n == 0) ? ">" : ">>", TempFile);
		for (const char *p = line.c_str(); *p != 0; ++p)
		{
			if (*p == '"')
			{
				cmd.cat('"');								// a quote inside a G-code string is doubled
			}
			cmd.cat(*p);
		}
		cmd.cat('"');
		SerialIo::Sendf("%s\n", cmd.c_str());
	}

	// Set the changed globals on the machine now. A global it does not have yet is created.
	void SendLive(uint32_t changed)
	{
		for (size_t g = 0; g < NumGlobals; ++g)
		{
			if ((changed & (1u << g)) != 0)
			{
				String<80> value;
				FormatValue((Global)g, value);
				if ((presentMask & (1u << g)) != 0)
				{
					SerialIo::Sendf("set global.%s = %s\n", GlobalNames[g], value.c_str());
				}
				else
				{
					SerialIo::Sendf("global %s = %s\n", GlobalNames[g], value.c_str());
					presentMask |= (1u << g);				// so the next change sets it
				}
			}
		}
	}

	// A machine setting changed ('changed' = the globals): apply it here, set it on the machine,
	// and (re)write the file from the first line
	void MachineChanged(uint32_t changed)
	{
		if (!Connected() || !globalsKnown)
		{
			// The link went down while a popup, the keyboard or the MACROS pick was open:
			// writing now could overwrite changes made meanwhile. Take the machine's values again.
			Refuse("No connection to the machine.");
			CncRequestGlobals();
			return;
		}
		ApplyMachine();
		SendLive(changed);
		verifyReadback = false;						// checked again after this write
		writing = true;
		writeLine = 0;
	}

	void SpinWriter()
	{
		const uint32_t now = SystemTick::GetTickCount();
		if (ignoring && !writing && now - writeDoneTime >= IgnoreAfterWrite)
		{
			ignoring = false;
			verifyReadback = true;						// the machine must now report what we set
			CncRequestGlobals();						// what the machine has now
		}
		if (!writing || now - lastWriteTime < WriteInterval)
		{
			return;
		}
		if (!Connected() || !globalsKnown)
		{
			// Connection lost during the write: drop it. After reconnecting the machine's values are
			// shown again (they may have been changed in DWC, or it may be another machine).
			writing = false;
			verifyReadback = false;
			return;
		}
		if (!MachineListening())
		{
			return;										// its aux input would fill up: wait
		}
		lastWriteTime = now;
		if (writeLine < NumFileLines)
		{
			SendFileLine(writeLine++);
		}
		else
		{
			// Complete: replace the real file (a write cut short never touches it). The file is not run:
			// the machine already has the values (SendLive), and config.g runs the file at power-on.
			SerialIo::Sendf("M471 S\"%s\" T\"%s\" D1\n", TempFile, SettingsFile);
			writing = false;
			writeDoneTime = now;
			ignoring = true;
		}
	}

	// The values the panel shows / wrote against the values the machine reports
	bool SameSettings(const Machine& a, const Machine& b)
	{
		if (a.probeMode != b.probeMode || a.measureAfterChange != b.measureAfterChange || a.rememberTool != b.rememberTool
			|| a.toolSetter != b.toolSetter || a.auxVacuum != b.auxVacuum)
		{
			return false;
		}
		for (size_t i = 0; i < NumCustomMacros; ++i)
		{
			if (strcmp(a.labels[i].c_str(), b.labels[i].c_str()) != 0 || strcmp(a.macros[i].c_str(), b.macros[i].c_str()) != 0)
			{
				return false;
			}
		}
		return true;
	}

	bool ParseBool(const char *data, bool& b)
	{
		if (strcasecmp(data, "true") == 0) { b = true; return true; }
		if (strcasecmp(data, "false") == 0) { b = false; return true; }
		return false;
	}

	// -----------------------------------------------------------------
	// Page 1 actions (Modern UI)
	// -----------------------------------------------------------------
	void VolumeChosen(int, size_t choice)
	{
		nvData.SetVolume((uint8_t)choice);
		SaveSettings();
		TouchBeep();
	}

	void BrightnessChosen(int, size_t choice)
	{
		SetBrightness(BrightnessValues[choice]);
		SaveSettings();
	}

	void InfoTimeoutChosen(int, size_t choice)
	{
		nvData.SetInfoTimeout((uint8_t)InfoTimeoutValues[choice]);
		SaveSettings();
	}

	void BaudChosen(int, size_t choice)
	{
		SetBaudRate((uint32_t)BaudValues[choice]);
		SaveSettings();
	}

	void AccentChosen(int, size_t choice)
	{
		nvData.SetAccentColour((uint8_t)choice);
		SaveSettings();
		Reset();											// the panel restarts with the new colour
	}

	// Calibration holds the panel until the 4 spots are touched: no STOP, no polling. Not during a job.
	bool RefuseDuringJob()
	{
		if (JobActive())
		{
			Refuse(CNC_LOCKED_JOB_ACTIVE);
			return true;
		}
		return false;
	}

	void DoCalibrateTouch(int)
	{
		if (RefuseDuringJob())
		{
			return;
		}
		CncCalibrateTouch();
		SaveSettings();
	}

	void DoFactoryReset(int)
	{
		FactoryReset();
	}

	bool PanelTouch(Event ev)
	{
		switch (ev)
		{
		case evSettingsAccentOpen:
			if (RefuseWhileWriting())
			{
				return true;
			}
			CncPopup::ChooseColour("ACCENT COLOR", swatchColours, NumAccentColours, nvData.GetAccentColour(), AccentChosen, 0);
			return true;

		case evSettingsBrightnessOpen:
			CncPopup::Choose("BRIGHTNESS", BrightnessLabels, ARRAY_SIZE(BrightnessLabels),
								Nearest(BrightnessValues, ARRAY_SIZE(BrightnessValues), nvData.GetBrightness()), nullptr, BrightnessChosen, 0);
			return true;

		case evSettingsVolumeOpen:
			CncPopup::Choose("VOLUME", VolumeLabels, ARRAY_SIZE(VolumeLabels), min<size_t>(nvData.GetVolume(), ARRAY_SIZE(VolumeLabels) - 1),
								nullptr, VolumeChosen, 0);
			return true;

		case evSettingsInfoTimeoutOpen:
			CncPopup::Choose("INFO TIMEOUT", InfoTimeoutLabels, ARRAY_SIZE(InfoTimeoutLabels),
								Nearest(InfoTimeoutValues, ARRAY_SIZE(InfoTimeoutValues), nvData.infoTimeout), nullptr, InfoTimeoutChosen, 0);
			return true;

		case evSettingsAlwaysDimToggle:
			// On = dim when idle: a CNC keeps full brightness during a job (the loop in PanelDue.cpp)
			nvData.SetDisplayDimmerType((nvData.GetDisplayDimmerType() != DisplayDimmerType::never) ? DisplayDimmerType::never : DisplayDimmerType::onIdle);
			SaveSettings();
			ShowToggles();
			if (nvData.GetDisplayDimmerType() != DisplayDimmerType::never)
			{
				DimDisplayNow();							// show the effect now (when idle); the next touch only wakes it
			}
			return true;

		case evSettingsTouchOpen:
			if (RefuseDuringJob())
			{
				return true;
			}
			CncPopup::Confirm("TOUCH CALIBRATION", "Do you want to perform", "touch calibration?", nullptr, nullptr, DoCalibrateTouch, 0);
			return true;

		case evInvertX:
			if (RefuseDuringJob())
			{
				return true;
			}
			CncFlipDisplay(false);							// MIRROR DISPLAY, then touch calibration
			SaveSettings();
			return true;

		case evInvertY:
			if (RefuseDuringJob())
			{
				return true;
			}
			CncFlipDisplay(true);							// INVERT DISPLAY, then touch calibration
			SaveSettings();
			return true;

		case evSettingsBaudOpen:
			if (RefuseWhileWriting())
			{
				return true;
			}
			CncPopup::Choose("BAUD LINK", BaudLabels, ARRAY_SIZE(BaudLabels),
								Nearest(BaudValues, ARRAY_SIZE(BaudValues), (int)nvData.GetBaudRate()), nullptr, BaudChosen, 0);
			return true;

		case evSettingsFactoryResetOpen:
			if (RefuseWhileWriting())
			{
				return true;
			}
			CncPopup::Confirm("ALERT !", "FACTORY RESET will return", "Display to default values", "Continue?", nullptr, DoFactoryReset, 0);
			return true;

		default:
			return false;
		}
	}

	// -----------------------------------------------------------------
	// Page 2 actions (machine)
	// -----------------------------------------------------------------
	void ProbeModeChosen(int, size_t choice)
	{
		machine.probeMode = (uint8_t)choice;
		MachineChanged(Bit(Global::ProbeMode));
	}

	void ToolHolderChosen(int, size_t choice)
	{
		machine.measureAfterChange = (choice == 0);		// ER collet: the length changes with every tool
		MachineChanged(Bit(Global::MeasureAfterChange));
	}

	void AuxChosen(int, size_t choice)
	{
		machine.auxVacuum = (choice == 1);
		MachineChanged(Bit(Global::AuxVacuum));
	}

	void MacroPicked(const char *path, const char *name)
	{
		if (strlen(path) > MaxMacroPath)
		{
			Refuse("The macro path is too long.");
		}
		else
		{
			const bool wasUsed = !machine.macros[customSlot].IsEmpty();
			machine.macros[customSlot].copy(path);
			if (!wasUsed || machine.labels[customSlot].IsEmpty())
			{
				DefaultLabel(customSlot, name);			// a new button is named after its macro
			}
			MachineChanged(SlotBits(customSlot));
		}
		CncSystem::OpenSettings();						// back to SETTINGS page 2
		GoToPage(4);
	}

	void LabelChanged(const char *text)
	{
		// Only the characters a label may have, at most 12
		labelEdit.Clear();
		for (const char *p = text; *p != 0 && labelEdit.strlen() < MaxLabel; ++p)
		{
			if (LabelCharOk(*p))
			{
				labelEdit.cat(*p);
			}
		}
		if (strcmp(labelEdit.c_str(), text) != 0)
		{
			CncKeyboard::SetText(labelEdit.c_str());
		}
	}

	void LabelEntered(const char *text)
	{
		kbOpen = false;
		const char *p = text;
		while (*p == ' ')
		{
			++p;
		}
		machine.labels[customSlot].copy(p);
		while (!machine.labels[customSlot].IsEmpty() && machine.labels[customSlot][machine.labels[customSlot].strlen() - 1] == ' ')
		{
			machine.labels[customSlot].Truncate(machine.labels[customSlot].strlen() - 1);
		}
		if (machine.labels[customSlot].IsEmpty())
		{
			// Empty: the macro's name again
			const char *path = machine.macros[customSlot].c_str();
			const char *file = path;
			for (const char *q = path; *q != 0; ++q)
			{
				if (*q == '/' || *q == ':')
				{
					file = q + 1;
				}
			}
			if (*file == '!')
			{
				++file;
			}
			file = SkipDigitsAndUnderscore(file);
			String<MaxMacroPath> name;
			name.copy(file);
			const size_t len = name.strlen();
			if (len > 2 && strcasecmp(name.c_str() + len - 2, ".g") == 0)
			{
				name.Truncate(len - 2);
			}
			DefaultLabel(customSlot, name.c_str());
		}
		MachineChanged(SlotBits(customSlot));
	}

	bool CustomAllowed(size_t choice)
	{
		return choice == CMacro || !machine.macros[customSlot].IsEmpty();	// LABEL / CLEAR need a macro
	}

	void CustomChosen(int slot, size_t choice)
	{
		customSlot = (size_t)slot;
		switch (choice)
		{
		case CMacro:
			CncMacros::StartPick(MacroPicked);			// the next macro tapped on MACROS
			GoToPage(3);
			break;

		case CLabel:
			kbOpen = true;
			CncKeyboard::Open(machine.labels[customSlot].c_str(), kbClient);
			break;

		default:
			machine.labels[customSlot].Clear();
			machine.macros[customSlot].Clear();
			MachineChanged(SlotBits(customSlot));
			break;
		}
	}

	bool MachineTouch(ButtonPress bp)
	{
		const size_t item = (size_t)bp.GetIParam();
		if (!Connected())
		{
			Refuse("No connection to the machine.");
			return true;
		}
		if (!globalsKnown)
		{
			Refuse("Waiting for the machine settings.");	// a change now would overwrite them with defaults
			return true;
		}
		switch (item)
		{
		case MProbeMode:
			CncPopup::Choose("PROBE MODE", ProbeModeLabels, ARRAY_SIZE(ProbeModeLabels), machine.probeMode, nullptr, ProbeModeChosen, 0);
			break;

		case MToolHolder:
			CncPopup::Choose("TOOL HOLDER", ToolHolderLabels, ARRAY_SIZE(ToolHolderLabels), machine.measureAfterChange ? 0 : 1,
								nullptr, ToolHolderChosen, 0);
			break;

		case MRememberTool:
			machine.rememberTool = !machine.rememberTool;
			MachineChanged(Bit(Global::RememberTool));
			break;

		case MToolSetter:
			machine.toolSetter = !machine.toolSetter;
			MachineChanged(Bit(Global::ToolSetter));
			break;

		case MAux:
			CncPopup::Choose("COOL / VAC", AuxLabels, ARRAY_SIZE(AuxLabels), machine.auxVacuum ? 1 : 0, nullptr, AuxChosen, 0);
			break;

		default:
			if (item >= MCustom1 && item <= MCustom3)
			{
				customSlot = item - MCustom1;
				CncPopup::Choose(CustomTitles[customSlot], CustomLabels, ARRAY_SIZE(CustomLabels), CMacro, CustomAllowed, CustomChosen, (int)customSlot, true);
			}
			break;
		}
		return true;
	}

	// -----------------------------------------------------------------
	// Building
	// -----------------------------------------------------------------
	ModernTextButton *AddTile(unsigned int i, const char *label, event_t e, int param)
	{
		return AddButton(TileY(i), TileX(i), ColW, TileH, label, e, param);
	}

	void AddPageCommon(size_t page, const char *section, const char *pageText)
	{
		// Section label + page number
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, PageBg);
		mgr.AddField(new StaticTextField(SectionY, SectionLeftX, SectionW, TextAlignment::Left, section));
		mgr.AddField(new StaticTextField(SectionY, SectionRightX, SectionW, TextAlignment::Right, pageText));

		// Page arrows (the one at the end muted)
		CncArrowButton * const up = new CncArrowButton(ArrowY, UpX, ArrowW, ArrowH, true, evCncSettingsPage, -1);
		up->SetDisabled(page == 0);
		mgr.AddField(up);
		CncArrowButton * const down = new CncArrowButton(ArrowY, DownX, ArrowW, ArrowH, false, evCncSettingsPage, 1);
		down->SetDisabled(page + 1 == NumPages);
		mgr.AddField(down);
	}

	// IP + FACTORY RESET, shared by both pages (their lists branch off after these)
	void AddTopRow()
	{
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Text, Tile);
		ipField = new StaticTextField(TopY + (TopH - 21) / 2, ColX(0) + ColW - 12 - IpTextW, IpTextW, TextAlignment::Right, ipText.c_str());
		mgr.AddField(ipField);
		DisplayField::SetDefaultColours(Muted, Tile);
		mgr.AddField(new StaticTextField(TopY + (TopH - 21) / 2, ColX(0) + IpLabelDx, IpLabelW, TextAlignment::Left, "IP"));
		AddCard(TopY, ColX(0), ColW, TopH);
		DisplayField::SetDefaultColours(ButtonText, StopRed, StopRed, StopRed, StopRed, StopRed, IconPaletteDark);
		mgr.AddField(new ModernTextButton(TopY, ColX(1), ColW, TopH, "FACTORY RESET", evSettingsFactoryResetOpen, 0, glcd19x21, true));
	}
}

namespace CncSettings
{
	void Create()
	{
		ipText.copy("-");
		for (size_t i = 0; i < NumAccentColours; ++i)
		{
			swatchColours[i] = AccentColour((uint8_t)i);
		}
		AddTopRow();
		DisplayField * const baseRoot = mgr.GetRoot();

		// Page 1: SYSTEM (panel)
		AddPageCommon(0, "SYSTEM", "PAGE 1 / 2");
		for (unsigned int i = 0; i < NumPanelTiles; ++i)
		{
			ModernTextButton * const b = AddTile(i, PanelLabels[i], PanelEvents[i], 0);
			if (i == PDimming)
			{
				dimmingTile = b;
			}
		}
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Text, Tile);
		ramField = new StaticTextField(TileY(RamTile) + (TileH - 21) / 2, TileX(RamTile) + 4, ColW - 8, TextAlignment::Centre, "");
		mgr.AddField(ramField);
		AddCard(TileY(RamTile), TileX(RamTile), ColW, TileH);
		pageRoots[0] = mgr.GetRoot();

		// Page 2: CUSTOMIZATION (machine)
		mgr.SetRoot(baseRoot);
		AddPageCommon(1, "CUSTOMIZATION", "PAGE 2 / 2");
		for (unsigned int i = 0; i < NumMachineTiles; ++i)
		{
			ModernTextButton * const b = AddTile(i, MachineLabels[i], evCncMachineSetting, (int)i);
			if (i == MRememberTool)
			{
				rememberTile = b;
			}
			else if (i == MToolSetter)
			{
				setterTile = b;
			}
		}
		pageRoots[1] = mgr.GetRoot();

		DisplayField::SetDefaultFont(DEFAULT_FONT);
		ShowRam(true);
		ApplyMachine();
	}

	DisplayField *CurrentRoot()
	{
		return pageRoots[currentPage];
	}

	bool ProcessTouch(ButtonPress bp, bool& redraw)
	{
		redraw = false;
		const Event ev = (Event)bp.GetEvent();
		if (ev == evCncSettingsPage)
		{
			const size_t page = (bp.GetIParam() < 0) ? 0 : 1;
			if (page != currentPage)
			{
				currentPage = page;
				redraw = true;
			}
			return true;
		}
		if (ev == evCncMachineSetting)
		{
			return MachineTouch(bp);
		}
		return PanelTouch(ev);
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncSettingsPage:
		case evCncMachineSetting:
		case evSettingsAccentOpen:
		case evSettingsBrightnessOpen:
		case evSettingsVolumeOpen:
		case evSettingsInfoTimeoutOpen:
		case evSettingsAlwaysDimToggle:
		case evSettingsTouchOpen:
		case evInvertX:
		case evInvertY:
		case evSettingsBaudOpen:
		case evSettingsFactoryResetOpen:
			return true;								// popups / toggles: no pressed look to undo

		default:
			return false;
		}
	}

	void SetIP(const char *ip)
	{
		const char * const shown = (ip[0] != 0) ? ip : "-";
		if (strcmp(shown, ipText.c_str()) != 0)
		{
			ipText.copy(shown);
			ipField->SetValue(ipText.c_str(), true);
		}
	}

	void GlobalsArriving()
	{
		incoming = Machine();							// a global the machine does not have means its default
		incomingPresent = 0;
		receiving = true;
	}

	void GlobalsDone(bool complete)
	{
		if (!receiving)
		{
			return;
		}
		receiving = false;
		if (!complete)
		{
			return;										// cut short: the seq is asked for again
		}
		globalsKnown = true;
		if (!writing && !ignoring)						// otherwise ours are newer
		{
			if (verifyReadback)
			{
				verifyReadback = false;
				if (!SameSettings(machine, incoming))
				{
					// What we set is not what the machine has: it refused a "set global" (see SYSTEM > ALERT)
					Refuse("The machine did not keep\nthe setting. See SYSTEM > ALERT.");
				}
			}
			machine = incoming;
			presentMask = incomingPresent;
			ApplyMachine();
		}
	}

	void UpdateGlobal(const char *name, const char *data)
	{
		if (!receiving)
		{
			return;
		}
		size_t g = 0;
		while (g < NumGlobals && strcasecmp(name, GlobalNames[g]) != 0)
		{
			++g;
		}
		if (g < NumGlobals)
		{
			incomingPresent |= (1u << g);
		}
		bool b;
		switch ((Global)g)
		{
		case Global::ProbeMode:
			{
				const int m = atoi(data);
				if (m >= 0 && m <= 2)
				{
					incoming.probeMode = (uint8_t)m;
				}
			}
			break;
		case Global::MeasureAfterChange:	if (ParseBool(data, b)) { incoming.measureAfterChange = b; } break;
		case Global::RememberTool:			if (ParseBool(data, b)) { incoming.rememberTool = b; } break;
		case Global::ToolSetter:			if (ParseBool(data, b)) { incoming.toolSetter = b; } break;
		case Global::AuxVacuum:				if (ParseBool(data, b)) { incoming.auxVacuum = b; } break;
		case Global::Count:					return;			// not one of ours
		default:
			{
				const size_t k = g - (size_t)Global::Custom1Label;
				const size_t slot = k / 2;
				if (k % 2 == 0)
				{
					incoming.labels[slot].copy(data);
				}
				else
				{
					incoming.macros[slot].copy(data);
				}
			}
			break;
		}
	}

	bool ToolSetter()
	{
		return machine.toolSetter;
	}

	bool KeyboardOpen()
	{
		return kbOpen && CncKeyboard::IsOpenFor(&kbClient);
	}

	bool TouchOutsideKeyboard(ButtonPress bp)
	{
		if (bp.GetEvent() == evCncNav)
		{
			CncKeyboard::Close();						// label not changed
			kbOpen = false;
			return true;
		}
		return false;
	}

	void Spin(bool shown)
	{
		if (!Connected())
		{
			globalsKnown = false;						// ask again after (re)connecting: maybe another machine
		}
		SpinWriter();
		if (kbOpen && !CncKeyboard::IsOpenFor(&kbClient))
		{
			kbOpen = false;								// closed by X, STOP, a page change or another popup
		}
		const uint32_t now = SystemTick::GetTickCount();
		if (shown && currentPage == 0 && now - lastRamUpdate >= 1000)
		{
			lastRamUpdate = now;
			ShowRam(false);
		}
	}
}

// End
