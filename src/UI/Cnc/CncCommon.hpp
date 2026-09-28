/*
 * CncCommon.hpp
 *
 * Shared colours and small helpers for the portrait CNC UI pages.
 */

#ifndef SRC_UI_CNC_CNCCOMMON_HPP_
#define SRC_UI_CNC_CNCCOMMON_HPP_

#include <UI/Display.hpp>
#include <UI/Cnc/CncLayout.hpp>
#include <UI/UserInterfaceConstants.hpp>		// declares glcd28x32 (DEFAULT_FONT, large)
#include <ObjectModel/PrinterStatus.hpp>

extern const uint8_t glcd19x21[];			// small font

namespace Cnc
{
	// Modern UI palette
	extern const Colour PageBg;			// #12161c
	extern const Colour Tile;			// #1c222b
	extern const Colour Text;			// #e5e8ec
	extern const Colour Muted;			// #9aa4b2
	extern const Colour Border;			// #3b434f
	extern const Colour DarkText;		// #12161c, text on accent fill
	extern const Colour NotHomedBg;		// #804000, Modern UI dark not-homed colour
	extern const Colour StopRed;		// #C93218

	Colour Accent();					// accent colour chosen in settings
	constexpr uint8_t NumAccentColours = 8;
	Colour AccentColour(uint8_t index);	// the Modern UI accent colours 0..7

	// Field helpers. Window::AddField prepends and the list is drawn from the root,
	// so text must be added before the card behind it.
	void AddCard(PixelNumber y, PixelNumber x, PixelNumber w, PixelNumber h, bool border = true);
	StaticTextField *AddLabel(PixelNumber y, PixelNumber x, PixelNumber w, const char *text,
								TextAlignment align = TextAlignment::Left);

	// Standard buttons: tile + neutral border, accent fill with dark text when pressed/selected
	ModernTextButton *AddButton(PixelNumber y, PixelNumber x, PixelNumber w, PixelNumber h,
								const char *text, event_t e, int param, const uint8_t *font = glcd19x21);

	// Keep exactly one button of a group selected (pressed look)
	void Select(ButtonBase *&selected, ButtonBase *b);

	// Job in progress: from the start of a file job until it ends, including the tool change
	// (and macro) states inside it. Set by the UI on every status change.
	void SetJobInProgress(bool b);
	bool JobInProgress();

	// True while a job runs (not when paused): machine-moving controls are locked then
	bool MachineBusy();

	// True while a job runs or is paused: setup controls (zero, WCS changes) are locked then
	bool JobActive();
	bool JobActive(OM::PrinterStatus status);

	// Switch to a nav page (0 CONTROL, 1 WCS, 2 JOB, 3 MACROS, 4 SYSTEM) and redraw it
	void GoToPage(unsigned int page);

	// Refuse an action: ALERT ! popup with the reason, nothing sent
	void Refuse(const char *reason);

	// Common reasons
	#define CNC_LOCKED_JOB			"Locked while a job is running."
	#define CNC_LOCKED_JOB_ACTIVE	"Locked while a job is running or paused." 

	// Sub-tab strip above the nav bar. Adds n tabs to the current root; returns them in 'tabs'.
	void AddSubTabs(const char * const labels[], unsigned int n, ModernTextButton *tabs[]);

	// Locked look for a setup button: muted text, no response (caller refuses the touch)
	void SetButtonLocked(ModernTextButton *b, bool locked);

	// Toggle look (COOLANT / VACUUM, ...): on = accent outline and accent text, off = normal
	void SetToggleLook(ModernTextButton *b, bool on);

	// Aux toggle (COOLANT or VACUUM, label chosen in SETTINGS), shared by CONTROL and JOB.
	// Runs 0:/macros/aux_on.g / aux_off.g. Listeners are told when the state changes.
	bool AuxOn();
	void ToggleAux();
	const char *AuxLabel();
	void SetAuxLabel(bool vacuum);
	void AddAuxListener(void (*listener)());

	// Custom macro buttons: slots 0/1 = CUSTOM 1/2 on CONTROL, 2/3 = CUSTOM 3/4 on JOB STATUS.
	// Assigned in SETTINGS; a slot without label or macro is hidden. A macro whose file name
	// starts with '!' asks for confirmation first.
	constexpr size_t NumCustomMacros = 4;
	void SetCustomMacro(size_t slot, const char *label, const char *macro);
	bool CustomUsed(size_t slot);
	const char *CustomLabel(size_t slot);
	const char *CustomMacro(size_t slot);
	void RunCustomMacro(size_t slot);				// with the '!' confirmation
	void AddCustomListener(void (*listener)());

	// Last machine position reported by RRF (used by the numpad POS key)
	void SetMachinePosition(size_t axis, float value);
	float MachinePosition(size_t axis);
	// Large jogs (10 mm and more): allowed only once the last one has finished (the machine position
	// has settled), so they cannot queue up. 'lastSent' is the caller's tick of its last large jog.
	bool GuardedJogAllowed(uint32_t& lastSent);

	// Last work position (DRO, user coordinates) reported by RRF
	void SetWorkPosition(size_t axis, float value);
	float WorkPosition(size_t axis);

	// The finger now on the screen acts once: its repeats are ignored until it is lifted
	void IgnoreRepeats();
}

#endif /* SRC_UI_CNC_CNCCOMMON_HPP_ */
