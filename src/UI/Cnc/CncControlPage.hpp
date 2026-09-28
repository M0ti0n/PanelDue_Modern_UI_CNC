/*
 * CncControlPage.hpp
 *
 * CONTROL page: jog (axis / step / move), actions, homing.
 */

#ifndef SRC_UI_CNC_CNCCONTROLPAGE_HPP_
#define SRC_UI_CNC_CNCCONTROLPAGE_HPP_

#include <UI/Display.hpp>
#include <ObjectModel/Spindle.hpp>

namespace CncControl
{
	// Adds the page fields on top of 'baseRoot' (DRO + nav) and returns the page root
	DisplayField *Create(DisplayField *baseRoot);

	// Touch handling. Return true when the event belongs to this page.
	bool ProcessTouch(ButtonPress bp);
	bool ProcessRelease(ButtonPress bp);

	// Machine state
	bool RefuseUnhomed();							// X, Y or Z not homed: ALERT "Home X, Y and Z first."
	void SetHomed(size_t axis, bool homed);
	bool SetAxisVisible(size_t axis, bool visible);		// true if the page layout changed
	void SetSpindleActive(int32_t rpm);
	void SetSpindleMax(int32_t rpm);
	void SetSpindleState(OM::SpindleState state);

	// Job state: running locks the whole page, running or paused locks HOME
	void SetJobState(bool running, bool active);

	// CUSTOM 1 / 2 and the COOLANT / VACUUM toggle follow the shared state in CncCommon

	// Spindle on (set speed) / standby (M5), from the DRO T tile
	void ToggleSpindle();
	void SetSpindleDisplay(IntegerField *f, IntegerField *compact);	// DRO S tiles (full, compact), show the set speed

	// Numpads opened from the DRO tiles
	void OpenSpindleNumpad();		// S tile: spindle speed (rpm)
	void OpenFeedNumpad();			// F tile: SLOW jog feed (mm/min)
	unsigned int JogFeed();			// SLOW jog feed, also used by the probe / M291 jog popup
}

#endif /* SRC_UI_CNC_CNCCONTROLPAGE_HPP_ */
