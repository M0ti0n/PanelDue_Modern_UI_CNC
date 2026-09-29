/*
 * CncJobPage.hpp
 *
 * JOB page: sub-tabs JOB STATUS / JOB LIST.
 * JOB STATUS: file, progress, times, per-tool FEED WORK and SPINDLE overrides,
 * COOLANT / VACUUM, CUSTOM 2 / 3, tool change row, PAUSE / RESUME and ABORT.
 * JOB LIST: files of the open folder (cached M20 listing), folders, paging, SEARCH with the
 * shared keyboard, SD card switch, RUN (M32) after the standard confirmation.
 */

#ifndef SRC_UI_CNC_CNCJOBPAGE_HPP_
#define SRC_UI_CNC_CNCJOBPAGE_HPP_

#include <UI/Display.hpp>
#include <ObjectModel/PrinterStatus.hpp>

namespace CncJob
{
	// Build the sub-pages on top of 'baseRoot' (compact DRO + nav)
	void Create(DisplayField *baseRoot);

	// Root of the sub-page to show when the JOB tab is selected
	DisplayField *CurrentRoot();
	void ShowStatusTab();								// select JOB STATUS (job start)
	bool StatusTabSelected();
	void PageShown();									// the JOB page was selected (JOB LIST: new listing)

	// JOB LIST data
	void FilesChanged();								// FileManager: listing loaded / requested
	void SetListError(int err);							// M20 error of the last listing
	void SetVolumeMounted(size_t volume, bool mounted);	// SD tile

	// JOB LIST keyboard: touches outside the sheet (rows above it, SEARCH, nav bar).
	// Process: the keyboard has been closed, handle the touch as a normal one.
	enum class OutsideTouch : uint8_t { Ignored, Used, Process };
	bool KeyboardOpen();
	OutsideTouch TouchOutsideKeyboard(ButtonPress bp);

	// Touch handling. Return true when the event belongs to this page.
	// 'redraw' is set when the whole page must be redrawn (sub-tab changed).
	bool ProcessTouch(ButtonPress bp, bool& redraw);
	bool ProcessRelease(ButtonPress bp);

	// Machine status, called before the status changes. Returns true when a job has just started
	// (the UI switches to JOB STATUS then).
	bool StatusChanged(OM::PrinterStatus oldStatus, OM::PrinterStatus newStatus);

	// Machine data
	void SetFileName(const char *path);
	void SetProgress(unsigned int percent);
	void SetDuration(uint32_t seconds);
	void SetTimeLeft(unsigned int seconds);
	void SetRequestedSpeed(int mmPerMin);				// FEED WORK actual
	void SetSpeedFactor(int percent);					// move.speedFactor (M220)
	void SetSpindleActive(int32_t rpm);					// set speed, negative = reverse
	void SetSpindleCurrent(int32_t rpm);				// SPINDLE actual
	void SetSpindleMax(int32_t rpm);					// emulated override never asks for more
	void SetSpindleMin(int32_t rpm);					// ... or less
	void SetSpindleState(bool running, bool reverse);	// spindles[0].state
	void EmergencyStop();								// STOP tile: the job ends as ABORTED
	void SetTool(int tool);								// current tool, -1 = none
	void SetNextTool(int tool);							// state.nextTool, -1 = none

	// Tool change prompt (M291 whose title starts with TOOL, during a job): shown inline
	bool ShowToolPrompt(const char *text, uint32_t seq);
	void CloseToolPrompt();								// answered elsewhere
	bool ToolPromptOpen();

	// Error reply from the machine: ALERT ! during a job
	void Error(const char *text);

	void Spin();										// main loop: blink, abort timeout
}

#endif /* SRC_UI_CNC_CNCJOBPAGE_HPP_ */
