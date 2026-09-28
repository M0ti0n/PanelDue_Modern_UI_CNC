/*
 * CncSystemPage.hpp
 *
 * SYSTEM page: sub-tabs ALERT / CONSOLE / SETTINGS.
 * ALERT: rolling history (11) of the machine's Error: / Warning: messages, newest first; tap one
 * for the whole text (popup ERROR / WARNING, X). Unread ones have an accent dot.
 * CONSOLE: the last 15 lines, newest first (typed commands, the machine's replies; warnings
 * amber, errors red, age on the first line of each message), and an input line that opens the
 * shared keyboard. ENTER sends and keeps the keyboard open; up / down step through the last
 * commands. Commands are allowed at any time (RRF queues them behind a running job).
 * SETTINGS: see CncSettingsPage.hpp.
 */

#ifndef SRC_UI_CNC_CNCSYSTEMPAGE_HPP_
#define SRC_UI_CNC_CNCSYSTEMPAGE_HPP_

#include <UI/Display.hpp>

namespace CncSystem
{
	void Create(DisplayField *baseRoot);
	DisplayField *CurrentRoot();
	void OpenSettings();							// select the SETTINGS sub-tab (shown by the next page switch)

	bool ProcessTouch(ButtonPress bp, bool& redraw);
	bool ProcessRelease(ButtonPress bp);

	// A reply or message from the machine (every non-empty "resp")
	void Response(const char *text);
	bool ConsoleReply();							// a typed command is still waiting for its reply (3 s)
	bool LastReplyWasConsole();						// the reply just passed to Response() answered it

	// Keyboard open over the CONSOLE, and touches outside it (same rules as JOB LIST)
	bool KeyboardOpen();
	bool TouchOutsideKeyboard(ButtonPress bp);		// true: close + process as a normal touch

	void Spin(bool shown);							// ages of the log lines, keyboard replaced by a popup
}

#endif /* SRC_UI_CNC_CNCSYSTEMPAGE_HPP_ */
