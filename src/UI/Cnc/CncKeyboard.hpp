/*
 * CncKeyboard.hpp
 *
 * Shared portrait keyboard of the CNC UI (mock-ups pd_cnc_system_console_keyboard.svg and
 * pd_cnc_system_console_keyboard_sym.svg), used by JOB LIST search and SYSTEM > CONSOLE.
 *
 * The sheet spans the screen width and ends above the nav bar, so STOP stays visible:
 *
 *   [^][v][ text_                          ][ X ]
 *   [1][2][3][4][5][6][7][8][9][0]
 *   [Q][W][E][R][T][Y][U][I][O][P]          #+= page:  [ ] { } ( ) < > = +
 *     [A][S][D][F][G][H][J][K][L]                        _ : ; " ' # $ % &
 *   [SHIFT][Z][X][C][V][B][N][M][BKSP]                 ~ * / \ ^ ! ? @
 *   [#+=][.][-][     SPACE     ][ ENTER ]              ABC . - SPACE ENTER
 *
 * Letters are upper case; SHIFT (sticky) switches them to lower case. Only these keys act
 * differently per user, through the client callbacks:
 *   up / down    CONSOLE: command history; JOB LIST: scroll the results
 *   X            close without using the text
 *   ENTER        use the text; closes the keyboard unless the client keeps it open (CONSOLE)
 * Every change of the text is reported (JOB LIST filters as you type).
 */

#ifndef SRC_UI_CNC_CNCKEYBOARD_HPP_
#define SRC_UI_CNC_CNCKEYBOARD_HPP_

#include <UI/Display.hpp>

namespace CncKeyboard
{
	constexpr size_t MaxText = 64;

	struct Client
	{
		void (*changed)(const char *text);		// text edited (may be null)
		void (*enter)(const char *text);		// ENTER (the keyboard has closed, unless keepOpenOnEnter)
		void (*cancel)();						// X: the keyboard has closed
		void (*arrow)(int dir);					// -1 up, +1 down (may be null: arrows muted)
		bool keepOpenOnEnter;					// CONSOLE: send and type the next command
	};

	void Create();
	void Open(const char *text, const Client& client);	// 'client' must stay valid (static)
	void Close(bool redraw = true);				// closes without calling the client; redraw false:
												// the caller repaints the whole screen anyway
	bool IsOpen();								// false as well when another popup replaced it
	bool IsOpenFor(const Client *c);			// open, and for this client (pages share the keyboard)
	void SetArrowsEnabled(bool up, bool down);
	const char *GetText();
	void SetText(const char *t);				// replace the text (history, clear after sending)

	// Keyboard events. Return true when the event belonged to the keyboard.
	bool ProcessTouch(ButtonPress bp);
	bool ProcessRelease(ButtonPress bp);
}

#endif /* SRC_UI_CNC_CNCKEYBOARD_HPP_ */
