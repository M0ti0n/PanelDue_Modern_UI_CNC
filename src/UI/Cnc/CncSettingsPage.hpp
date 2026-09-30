/*
 * CncSettingsPage.hpp
 *
 * SYSTEM > SETTINGS (mock-ups pd_cnc_settings_p1_system.svg / pd_cnc_settings_p2_custom.svg):
 *
 *   [ IP        192.168.1.42 ][   FACTORY RESET   ]
 *   SYSTEM                               PAGE 1 / 2
 *   [ ACCENT COLOR ][ BRIGHTNESS     ]      page 1: the panel (Modern UI settings, same popups,
 *   [ VOLUME       ][ INFO TIMEOUT   ]              saved to the panel's flash on every change)
 *   [ SCREEN DIMM. ][ TOUCH CALIBR.  ]
 *   [ MIRROR DISP. ][ INVERT DISPLAY ]
 *   [ BAUD         ][ RAM 10564      ]
 *          [  ^  ]   [  v  ]
 *
 *   page 2 CUSTOMIZATION: the machine (PROBE MODE, TOOL HOLDER, REMEMBER TOOL, TOOL SETTER,
 *   CUSTOM 1-4, COOL / VAC). Kept on the Duet as globals in 0:/sys/cnc-settings.g, which the
 *   panel rewrites on every change (echo, one line at a time) and then runs; read back from
 *   the machine's globals at connect and whenever they change. The macros use the same globals.
 *
 * Toggles (SCREEN DIMMING, REMEMBER TOOL, TOOL SETTER): accent outline + accent text when on.
 */

#ifndef SRC_UI_CNC_CNCSETTINGSPAGE_HPP_
#define SRC_UI_CNC_CNCSETTINGSPAGE_HPP_

#include <UI/Display.hpp>

namespace CncSettings
{
	void Create();									// on the SYSTEM sub-tab root (mgr root set by the caller)
	DisplayField *CurrentRoot();					// page 1 or 2

	bool ProcessTouch(ButtonPress bp, bool& redraw);	// redraw: page changed
	bool ProcessRelease(ButtonPress bp);

	void SetIP(const char *ip);						// network.interfaces[0].actualIP
	void GlobalsArriving();							// start of the machine's global reply
	void UpdateGlobal(const char *name, const char *data);	// global.cnc* from the machine
	void GlobalsDone(bool complete);				// end of that reply (false: cut short)
	bool ToolSetter();								// TOOL SETTER on (MEASURE is refused without)
	void OpenJogFeedPopup(bool fromControl);		// SLOW JOG FEEDRATE presets (also the F tile on CONTROL)

	// LABEL keyboard (CUSTOM n): touches outside the sheet
	bool KeyboardOpen();
	bool TouchOutsideKeyboard(ButtonPress bp);		// true: closed, process as a normal touch

	void Spin(bool shown);							// RAM value, writing cnc-settings.g, keyboard closed
}

#endif /* SRC_UI_CNC_CNCSETTINGSPAGE_HPP_ */
