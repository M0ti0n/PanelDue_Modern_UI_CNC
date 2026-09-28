/*
 * CncProbePage.hpp
 *
 * WCS > PROBE sub-page, the AUTO probe popup and the jog popup that shows M291 prompts
 * with axis controls (probe macros in SEMI-AUTO / MANUAL, and any other macro).
 */

#ifndef SRC_UI_CNC_CNCPROBEPAGE_HPP_
#define SRC_UI_CNC_CNCPROBEPAGE_HPP_

#include <UI/Display.hpp>

namespace CncProbe
{
	enum class Mode : uint8_t { Auto, Semi, Manual };

	// Adds the page fields to the current root (sub-tab strip + DRO + nav)
	void Create();

	// Page touches (only while the PROBE sub-page is shown)
	bool ProcessTouch(ButtonPress bp);
	bool ProcessRelease(ButtonPress bp);

	// Popup touches (the jog popup can open on any page)
	bool ProcessPopupTouch(ButtonPress bp);
	bool ProcessPopupRelease(ButtonPress bp);

	// Machine data
	void SetProbeValue(int value);						// probe K0 reading; >= 500 = closed (triggered)
	void SetJobState(bool running, bool active);
	void SetNumAxes(size_t n);

	// Probe mode, chosen in SYSTEM > SETTINGS (AUTO until SETTINGS exists)
	void SetMode(Mode m);

	// M291 routing. Returns true when the probe UI took the message.
	bool ShowJogPrompt(const char *title, const char *text, uint32_t controls, bool withCancel, uint32_t seq);
	bool AutoProbeRunning();
	bool JogPromptOpen();
	void AutoMessage(const char *title, const char *text);	// info message from the auto probe macro
	void ClosePopups();									// emergency stop
	void CloseJogPrompt();								// the M291 prompt was answered elsewhere (DWC)
	void StatusChanged(bool idle, bool busy);			// machine status (called before it changes)
	void Spin();										// main loop: auto popup timeout
	void Error(const char *text);						// error reply while the auto probe runs
}

#endif /* SRC_UI_CNC_CNCPROBEPAGE_HPP_ */
