/*
 * CncWcsPage.hpp
 *
 * WCS page: sub-tabs OFFSETS / TOOLS / PROBE.
 * OFFSETS (work offsets, offset step), TOOLS (CncToolsPage). PROBE comes later.
 */

#ifndef SRC_UI_CNC_CNCWCSPAGE_HPP_
#define SRC_UI_CNC_CNCWCSPAGE_HPP_

#include <UI/Display.hpp>

namespace CncWcs
{
	// Build all sub-pages on top of 'baseRoot' (DRO + nav)
	void Create(DisplayField *baseRoot);

	// Root of the sub-page to show when the WCS tab is selected
	DisplayField *CurrentRoot();

	// Touch handling. Return true when the event belongs to this page (incl. its popups).
	// 'redraw' is set when the whole page must be redrawn (sub-tab changed).
	bool ProcessTouch(ButtonPress bp, bool& redraw);
	bool ProcessRelease(ButtonPress bp);

	// Machine data
	void SetWorkplaceOffset(size_t axis, size_t workplace, float offset);
	void SetActiveWorkplace(size_t workplace);			// 0 = G54 ... 5 = G59
	void SetOffsetStep(size_t axis, float value);
	void SetAxisVisible(size_t axis, bool visible);
	void SetJobState(bool running, bool active);		// running: job moving; active: running or paused

	// SAVE outline (M500 P10 also saves tool offsets): an edit made on the panel, or a saved
	// value that changed on the Duet (ignored during the initial load after connecting)
	void MarkUnsaved();
	void NoteSavedValueChanged();
	void Disconnected();							// connection lost: the next offsets are an initial load
	void MarkSaved();							// M500 was sent by another page
}

#endif /* SRC_UI_CNC_CNCWCSPAGE_HPP_ */
