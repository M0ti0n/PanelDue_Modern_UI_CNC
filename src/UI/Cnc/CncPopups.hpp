/*
 * CncPopups.hpp
 *
 * The two shared popups of the portrait CNC UI, ported from the Modern UI:
 *
 *  - Standard popup: title tile + information tile (up to 6 lines), a set of choice
 *    buttons, or a form (labelled button groups), with the red X / green check actions.
 *    Used for every confirmation (HOME, RESET, APPLY OFFSET, ...), every preset choice
 *    (offset STEP SIZE, ...) and small forms (COPY TO: target + axes).
 *  - Standard numpad: value tile with unit, tag tile, 3 x 4 digits, backspace / X / check,
 *    optional POS key. Used for every number the UI asks for. Only the tag, the unit and
 *    the allowed keys change per use.
 *
 * The standard popup is 460 x 480, centred (CncLayout::StdPopup*). The numpad sits directly
 * under the DRO accent line (CncLayout::PopupUnderDro*), so the DRO stays live while a value
 * is typed. The STOP tile stays reachable under both.
 *
 * One popup is open at a time. The caller passes a handler that runs when the check
 * mark is pressed; X just closes the popup.
 */

#ifndef SRC_UI_CNC_CNCPOPUPS_HPP_
#define SRC_UI_CNC_CNCPOPUPS_HPP_

#include <UI/Display.hpp>

namespace CncPopup
{
	typedef void (*ConfirmHandler)(int param);
	typedef void (*ChoiceHandler)(int param, size_t choice);
	typedef bool (*ChoiceAllowed)(size_t choice);			// false: choice shown muted and refused
	typedef void (*ValueHandler)(int param, float value);
	typedef float (*PosGetter)(int param);

	struct NumpadSpec
	{
		const char *tag;				// tag tile, e.g. "S", "F", "G55 X" (copied)
		const char *unit;				// unit in the value tile, e.g. "rpm", "mm/min", "mm" (static string)
		float value;					// value shown when the numpad opens
		unsigned int decimals;			// decimals of the initial value (0 = integer)
		bool allowDecimal;				// '.' key active
		bool allowMinus;				// '-' key active
		float min, max;					// accepted range; outside it the check mark is refused
		PosGetter pos;					// non-null: POS key shown, fills in pos(param)
		ValueHandler onOk;
		int param;
	};

	void Create();

	// Confirmation: title + information lines (all copied). The info tile holds up to 6 lines.
	void Confirm(const char *title, const char *line1, const char *line2, const char *line3, const char *line4,
					ConfirmHandler onOk, int param);
	void ConfirmLines(const char *title, const char * const lines[], size_t n, ConfirmHandler onOk, int param);

	// Message from the machine (M291): text word-wrapped into the info tile.
	// withCancel false: only the check mark, centred. onCancel may be null.
	void Message(const char *title, const char *text, bool withCancel, ConfirmHandler onOk, ConfirmHandler onCancel, int param,
					bool waits = false,		// waits: the machine waits for an answer (given in DWC)
					bool passive = false);	// passive: M291 S1, the check mark just closes it (not waited for)
	bool IsMessageOpen();						// an M291 message is open (not a Notice)

	// Information from the panel itself (JOB DONE, ...): like Message, only the check mark,
	// and not closed when the machine clears its message box
	void Notice(const char *title, const char *text);

	// ALERT ! (standard popup): the reason in the info tile, only X. Used for every refused action.
	void Alert(const char *text);
	void Info(const char *title, const char *text);		// same look with another title (SYSTEM > ALERT history)
	// Reply from the machine, as the Modern UI (INFO TIMEOUT): no title tile, the text, only X, closes
	// after timeoutMs. isError ("Error" replies): title "ALERT !", no timeout. The next reply replaces
	// the one shown (an error too) and a machine message that needs no answer (M291 S0/S1); nothing
	// else is replaced, unless 'force' (errors during a job), which replaces all but a machine message
	// that waits for an answer.
	void Response(const char *text, bool isError, uint32_t timeoutMs, bool force = false);
	void Spin();								// closes a reply whose time is up
	void CloseResponse();						// closes a reply popup (nothing else)
	bool IsAlertOpen();
	bool CanShowPassive();						// nothing in use: a message that needs no answer may be shown
	bool IsBlockingMessageOpen();				// a message whose answer the machine waits for

	// Preset choice: title + n choice buttons (labels must be static), 'selected' starts highlighted.
	// instant = the choices are actions (SETTINGS > CUSTOM n: MACRO / LABEL / CLEAR): a tap on a choice
	// closes the popup and runs the handler at once, the green check is not needed.
	// columns: 0 = automatic (up to 4 in one row, else 3 per row); e.g. 2 for six wide choices in 2 x 3.
	void Choose(const char *title, const char * const labels[], size_t n, size_t selected,
					ChoiceAllowed allowed, ChoiceHandler onOk, int param, bool instant = false, size_t columns = 0);
	// Colour choice (ACCENT COLOR): n swatches, 4 per row, the selected one outlined ('colours' must be static)
	void ChooseColour(const char *title, const Colour colours[], size_t n, size_t selected, ChoiceHandler onOk, int param);

	void Numpad(const NumpadSpec& pad);

	// Form: up to 2 labelled groups of choice buttons in the standard popup.
	// Single-pick group: exactly one item selected. Multi group: toggles, at least one on.
	// The check mark is refused while a group has nothing selected.
	struct FormGroup
	{
		const char *label;					// small label above the group (static)
		const char * const *items;			// item labels (static)
		size_t n;							// up to 6 items
		bool multi;
		uint16_t selected;					// bit mask, initial selection
		uint16_t disabled;					// bit mask, items that cannot be selected
		uint16_t marked;					// bit mask, items with a dot
	};
	typedef void (*FormHandler)(int param, const uint16_t selected[]);
	void Form(const char *title, const FormGroup groups[], size_t nGroups, FormHandler onOk, int param);

	// A question (e.g. "IS T3 IN THE SPINDLE?") with its answer buttons: the question is centred in a
	// tile under the title, the answers sit below it. One single-pick group, no answer selected yet.
	void FormQuestion(const char *title, const char *question, const char * const answers[], size_t n,
						FormHandler onOk, int param);

	bool IsOpen();
	void Close();

	// Popup events. Return true when the event belonged to a popup.
	bool ProcessTouch(ButtonPress bp);
	bool ProcessRelease(ButtonPress bp);
}

#endif /* SRC_UI_CNC_CNCPOPUPS_HPP_ */
