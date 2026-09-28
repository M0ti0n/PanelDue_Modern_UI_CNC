/*
 * CncMacrosPage.hpp
 *
 * MACROS page (mock-ups pd_cnc_macros_list.svg / pd_cnc_macros_grid.svg), under the compact DRO,
 * no sub-tabs. The open folder of 0:/macros as a list (11 rows) or a grid (2 x 5 tiles). Side
 * column as JOB LIST: SEARCH, up / down (page), and the LIST / GRID toggle in the SD slot.
 * SEARCH works as on JOB LIST (shared keyboard, live filter on the macro names of the open folder,
 * ENTER keeps the filter, X clears it); the view stays list or grid while typing (3 rows or 2 tiles).
 * Tap a folder to open it, ".." goes back up. Tap a macro: standard popup "RUN MACRO" / "Do you want to run this macro?" +
 * name, then M98 P"<path>". Refused while a job runs (allowed while paused, like CONTROL).
 * Names are shown without a leading "!", a leading "12_" number and the ".g" extension.
 */

#ifndef SRC_UI_CNC_CNCMACROSPAGE_HPP_
#define SRC_UI_CNC_CNCMACROSPAGE_HPP_

#include <UI/Display.hpp>

namespace CncMacros
{
	DisplayField *Create(DisplayField *baseRoot);	// returns the page root
	void PageShown();								// new listing of the open folder

	void FilesChanged();							// FileManager: macro listing loaded / requested
	void SetListError(int err);

	bool ProcessTouch(ButtonPress bp);
	bool ProcessRelease(ButtonPress bp);

	// Search keyboard: touches outside the sheet (same rules as JOB LIST)
	enum class OutsideTouch : uint8_t { Ignored, Used, Process };
	bool KeyboardOpen();
	OutsideTouch TouchOutsideKeyboard(ButtonPress bp);
	void Spin();									// keyboard replaced by a popup / closed by a page change

	// SETTINGS > CUSTOM n > MACRO: while picking, the label reads SELECT (accent) and tapping a
	// macro hands its path and display name to 'picked' instead of asking to run it. Folders,
	// search and ".." work as usual. Leaving the page cancels it.
	typedef void (*PickHandler)(const char *path, const char *name);
	void StartPick(PickHandler picked);				// call before the page is shown
	void CancelPick();
}

#endif /* SRC_UI_CNC_CNCMACROSPAGE_HPP_ */
