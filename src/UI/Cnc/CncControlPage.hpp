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

	// Hardware dial (rotary encoder). clicks = turn since the last call (+ right, - left), pressed = the
	// button was pressed. It is asleep until a press wakes it. Awake, a turn jogs like the - / + buttons of
	// the screen showing (CONTROL: chosen axis x STEP, RAPID or SLOW; jog prompt: its axis x STEP, SLOW JOG F
	// feed). A press while awake ends it on CONTROL. In the jog prompt a press wakes it, and holding the button
	// for 1 second while awake answers OK (a short press does nothing, turning cancels the hold).
	// pressed = button went down since the last call, down = button is held now. Call it every interval.
	void Wheel(int clicks, bool pressed, bool down);

	// Called from the UI spin loop. context = what the dial would act on: 0 nothing (other page, other popup),
	// 1 CONTROL, 2 the jog prompt. A change of context puts the dial to sleep. Also the idle timeout: 5 s without
	// a touch jog, axis / step tap or dial use clears the chosen axis, sets the step back to the default and
	// puts the dial to sleep.
	void Spin(int context);

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
	void OpenFeedPopup();			// F tile: the SLOW JOG FEEDRATE presets (same popup as SETTINGS > SLOW JOG F)
	void SelectSlow();				// RAPID / SLOW toggle to SLOW
	unsigned int JogFeed();			// SLOW jog feed, also used by the probe / M291 jog popup
	void SetJogFeed(unsigned int feed);	// the machine's global.cncJogFeed arrived / was confirmed (SETTINGS > SLOW JOG F)
}

#endif /* SRC_UI_CNC_CNCCONTROLPAGE_HPP_ */
