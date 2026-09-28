/*
 * CncSystemPage.cpp
 *
 * SYSTEM page, portrait CNC UI, under the compact DRO.
 *
 * ALERT (mock-up pd_cnc_system_alert.svg): the machine's Error: / Warning: lines, newest first,
 * 11 slots on one page, [ERR|WARN] badge + age + text, accent dot while unread. Tap = popup
 * ERROR / WARNING with the whole text and X. No CLEAR: they go when they fall out or at power off.
 *
 * CONSOLE (mock-ups pd_cnc_system_console_v2_*.svg):
 *
 *   CONSOLE                                   NEWEST FIRST
 *   [ 0m10  G10 L20 P1 Z0                              ]
 *   [ 0m42  Error: G1: target position outside ...     ]   15 lines, newest message on top
 *   [ ...                                              ]
 *   [ Tap to enter G-code...                ][ KEYBOARD ]
 *   [ ALERT ][ CONSOLE ][ SETTINGS ]   (sub-tabs)
 *
 * Log colours: typed command = text, reply = muted, "Warning" = amber, "Error" = red.
 * Long replies wrap; the age is shown on the first line of each message.
 * The keyboard is the shared one (CncKeyboard): ENTER sends and stays open for the next
 * command, up / down step through the last commands, X closes it.
 *
 * SETTINGS: CncSettingsPage (two pages of its own under the same sub-tabs).
 */

#include "CncSystemPage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncKeyboard.hpp"
#include "CncPopups.hpp"
#include "CncSettingsPage.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "Hardware/SysTick.hpp"
#include "Icons/Icons.hpp"
#include "PanelDue.hpp"
#include <General/String.h>
#include <General/SafeVsnprintf.h>

using namespace CncLayout;
using namespace CncLayout::Console;
using namespace Cnc;

namespace
{
	const Colour Amber = UTFT::fromRGB(224, 168, 0);		// #E0A800

	constexpr size_t NumSubTabs = 3;
	const char * const SubTabNames[NumSubTabs] = { "ALERT", "CONSOLE", "SETTINGS" };
	constexpr size_t ConsoleSub = 1;

	DisplayField *subRoots[NumSubTabs];
	ModernTextButton *subTabs[NumSubTabs];
	ButtonBase *selectedSubTab = nullptr;
	constexpr size_t AlertSub = 0;
	constexpr size_t SettingsSub = 2;
	size_t currentSub = AlertSub;

	// ---- Log ----
	enum class Kind : uint8_t { Command, Reply, Warning, Error };
	constexpr size_t LineChars = 40;						// ~38 average characters fit the 382 px column
	constexpr uint32_t ConsoleReplyTime = 3000;				// ms: a reply this soon after a typed command belongs to it
	constexpr size_t AgeChars = 6;							// "59m59", "23h59", "9d23" + null

	struct LogLine
	{
		uint32_t time;										// tick of the message (first line only)
		Kind kind;
		bool first;											// first line of a message
		char text[LineChars + 1];
	};

	LogLine lines[NumLines];								// ring, oldest to newest
	size_t lineHead = 0;									// next slot to write
	size_t lineCount = 0;

	StaticTextField *timeFields[NumLines];
	StaticTextField *textFields[NumLines];
	char ageText[NumLines][AgeChars];
	uint32_t lastAgeUpdate = 0;

	// ---- Keyboard, history ----
	constexpr size_t NumHistory = 5;
	String<CncKeyboard::MaxText> history[NumHistory];		// 0 = newest
	size_t historyCount = 0;
	int historyPos = -1;									// -1 = the new command being typed
	String<CncKeyboard::MaxText> draft;						// text typed but not sent (kept over history and reopening)
	bool kbOpen = false;
	uint32_t lastCommandTime = 0;
	bool commandSent = false;								// waiting for the reply to a typed command
	bool consoleReplyNow = false;							// the reply just logged answered a typed command

	const LogLine& LineAt(size_t chrono)					// 0 = oldest kept line
	{
		return lines[(lineHead + NumLines - lineCount + chrono) % NumLines];
	}

	void FormatAge(char *p, uint32_t tick)
	{
		uint32_t age = (SystemTick::GetTickCount() - tick) / 1000;		// seconds
		if (age < 60 * 60)
		{
			SafeSnprintf(p, AgeChars, "%lum%02lu", (unsigned long)(age / 60), (unsigned long)(age % 60));
		}
		else if ((age /= 60) < 24 * 60)										// minutes
		{
			SafeSnprintf(p, AgeChars, "%luh%02lu", (unsigned long)(age / 60), (unsigned long)(age % 60));
		}
		else
		{
			age /= 60;														// hours
			SafeSnprintf(p, AgeChars, "%lud%02lu", (unsigned long)(age / 24), (unsigned long)(age % 24));
		}
	}

	Colour KindColour(Kind k)
	{
		switch (k)
		{
		case Kind::Command:	return Text;
		case Kind::Warning:	return Amber;
		case Kind::Error:	return StopRed;
		default:			return Muted;
		}
	}

	// Map screen rows to log lines: newest message first, each message's lines in reading order.
	// rowLine[r] = chronological index, or -1 for an empty row.
	void MapRows(int rowLine[NumLines])
	{
		for (size_t r = 0; r < NumLines; ++r)
		{
			rowLine[r] = -1;
		}
		size_t r = 0;
		int last = (int)lineCount - 1;
		while (r < NumLines && last >= 0)
		{
			int start = last;
			while (start > 0 && !LineAt((size_t)start).first)
			{
				--start;										// back to the message's first line
			}
			for (int m = start; m <= last && r < NumLines; ++m)
			{
				rowLine[r++] = m;
			}
			last = start - 1;
		}
	}

	void ShowAges()
	{
		int rowLine[NumLines];
		MapRows(rowLine);
		for (size_t r = 0; r < NumLines; ++r)
		{
			char age[AgeChars] = { 0 };
			if (rowLine[r] >= 0)
			{
				const LogLine& l = LineAt((size_t)rowLine[r]);
				if (l.first && l.time != 0)
				{
					FormatAge(age, l.time);
				}
			}
			if (strcmp(age, ageText[r]) != 0)				// redraw only what changed
			{
				memcpy(ageText[r], age, AgeChars);
				timeFields[r]->SetValue(ageText[r], true);
			}
		}
		lastAgeUpdate = SystemTick::GetTickCount();
	}

	void ShowLog()
	{
		int rowLine[NumLines];
		MapRows(rowLine);
		for (size_t r = 0; r < NumLines; ++r)
		{
			if (rowLine[r] >= 0)
			{
				const LogLine& l = LineAt((size_t)rowLine[r]);
				textFields[r]->SetColours(KindColour(l.kind), Tile);
				textFields[r]->SetValue(l.text, true);
			}
			else
			{
				textFields[r]->SetValue("", true);
			}
		}
		ShowAges();
	}

	void AddLine(Kind kind, bool first, const char *text, size_t len)
	{
		LogLine& l = lines[lineHead];
		l.kind = kind;
		l.first = first;
		l.time = first ? SystemTick::GetTickCount() : 0;
		if (first && l.time == 0)
		{
			l.time = 1;
		}
		const size_t n = (len < LineChars) ? len : LineChars;
		for (size_t i = 0; i < n; ++i)
		{
			l.text[i] = (text[i] == '\t') ? ' ' : text[i];		// no tab glyph in the font
		}
		l.text[n] = 0;
		lineHead = (lineHead + 1) % NumLines;
		if (lineCount < NumLines)
		{
			++lineCount;
		}
	}

	// UTF-8: never end a piece in the middle of a character
	size_t CharBoundary(const char *p, size_t n)
	{
		while (n > 0 && (p[n] & 0xC0) == 0x80)
		{
			--n;
		}
		return n;
	}

	// True when the line starting at p (up to the line break) contains 'word'
	bool LineContains(const char *p, const char *word)
	{
		const size_t wl = strlen(word);
		for (; *p != 0 && *p != '\n' && *p != '\r'; ++p)
		{
			if (strncmp(p, word, wl) == 0)
			{
				return true;
			}
		}
		return false;
	}

	// Kind of one line: typed command, or a reply line with "Error:" / "Warning:" in it
	Kind LineKind(Kind kind, const char *p)
	{
		if (kind == Kind::Command)
		{
			return kind;
		}
		return LineContains(p, "Error:") ? Kind::Error
				: LineContains(p, "Warning:") ? Kind::Warning
					: Kind::Reply;
	}

	// Split a message into lines that fit the text column (line breaks start a new line).
	// A message never takes more than the whole log: its last line then ends in "..".
	void AddMessage(Kind kind, const char *text)
	{
		lcd.setFont(glcd19x21);
		bool first = true;
		size_t added = 0;
		Kind lineKind = kind;
		const char *p = text;
		while (*p != 0)
		{
			bool newLine = first;
			while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			{
				newLine = newLine || (*p == '\n');
				++p;
			}
			if (*p == 0)
			{
				break;
			}
			if (newLine)
			{
				lineKind = LineKind(kind, p);				// "Error..." / "Warning..." at the start of a line
			}
			size_t len = 0;
			while (p[len] != 0 && p[len] != '\n' && p[len] != '\r')
			{
				++len;
			}
			// Longest part that fits (binary search), then back to a space if there is one
			size_t lo = 0, hi = min<size_t>(len, LineChars) + 1;			// lo fits, hi does not
			while (hi - lo > 1)
			{
				const size_t mid = (lo + hi) / 2;
				const size_t cb = CharBoundary(p, mid);
				if (cb == 0 || DisplayField::GetTextWidth(p, 9999, cb) <= TextW)
				{
					lo = mid;
				}
				else
				{
					hi = mid;
				}
			}
			size_t take = CharBoundary(p, lo);
			if (take == 0)
			{
				take = 1;
				while (take < len && (p[take] & 0xC0) == 0x80)
				{
					++take;									// one whole character at least
				}
			}
			if (take < len)
			{
				size_t s = take;
				while (s > take / 2 && p[s] != ' ')
				{
					--s;
				}
				if (s > take / 2)
				{
					take = s;										// split at the space
				}
			}
			// More text after this piece (on this line or a following one)?
			const char *rest = p + take;
			while (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n')
			{
				++rest;
			}
			if (added + 1 == NumLines && *rest != 0)
			{
				// Last line the log can hold: cut it so ".." fits, and mark that the message goes on
				char last[LineChars + 1];
				const PixelNumber dotsW = DisplayField::GetTextWidth("..", 9999, 2);
				size_t n = CharBoundary(p, min<size_t>(take, LineChars - 2));
				while (n > 0 && DisplayField::GetTextWidth(p, 9999, n) + dotsW > TextW)
				{
					n = CharBoundary(p, n - 1);
				}
				memcpy(last, p, n);
				memcpy(last + n, "..", 3);
				AddLine(lineKind, first, last, n + 2);
				break;
			}
			AddLine(lineKind, first, p, take);
			++added;
			first = false;
			p += take;
			if (added == NumLines)
			{
				break;
			}
		}
		ShowLog();
	}

	// ---- Keyboard ----
	void UpdateArrows()
	{
		CncKeyboard::SetArrowsEnabled(historyPos + 1 < (int)historyCount, historyPos >= 0);
	}

	void KbEnter(const char *text)
	{
		// skip leading spaces; an empty line sends nothing
		while (*text == ' ')
		{
			++text;
		}
		if (*text == 0)
		{
			return;
		}
		String<CncKeyboard::MaxText> cmd;
		cmd.copy(text);
		SerialIo::Sendf("%s\n", cmd.c_str());
		AddMessage(Kind::Command, cmd.c_str());
		lastCommandTime = SystemTick::GetTickCount();
		commandSent = true;
		draft.Clear();

		// history: newest first, no immediate repeat
		if (historyCount == 0 || !history[0].Equals(cmd.c_str()))
		{
			for (size_t i = NumHistory - 1; i > 0; --i)
			{
				history[i].copy(history[i - 1].c_str());
			}
			history[0].copy(cmd.c_str());
			if (historyCount < NumHistory)
			{
				++historyCount;
			}
		}
		historyPos = -1;
		CncKeyboard::SetText("");
		UpdateArrows();
	}

	void KbChanged(const char *text)
	{
		if (historyPos < 0)
		{
			draft.copy(text);							// what is being typed, not a recalled command
		}
	}

	void KbCancel()
	{
		kbOpen = false;
		if (historyPos < 0)
		{
			draft.copy(CncKeyboard::GetText());			// X keeps the unsent text for next time
		}
		historyPos = -1;
	}

	void KbArrow(int dir)
	{
		if (dir < 0 && historyPos + 1 < (int)historyCount)
		{
			if (historyPos < 0)
			{
				draft.copy(CncKeyboard::GetText());		// back with down arrow
			}
			++historyPos;
			CncKeyboard::SetText(history[historyPos].c_str());
		}
		else if (dir > 0 && historyPos >= 0)
		{
			--historyPos;
			CncKeyboard::SetText((historyPos >= 0) ? history[historyPos].c_str() : draft.c_str());
		}
		UpdateArrows();
	}

	const CncKeyboard::Client kbClient = { KbChanged, KbEnter, KbCancel, KbArrow, true };

	void OpenKeyboard()
	{
		historyPos = -1;

		// Row 6 reaches 4 px above the sheet: blank it now, the sheet stops it being drawn
		for (size_t r = LinesAboveKeyboard; r < NumLines; ++r)
		{
			textFields[r]->SetValue("", true);
			mgr.Redraw(textFields[r]);
			ageText[r][0] = 0;
			timeFields[r]->SetValue(ageText[r], true);
			mgr.Redraw(timeFields[r]);
		}
		CncKeyboard::Open(draft.c_str(), kbClient);
		ShowLog();										// rows under the sheet come back when it closes
		kbOpen = true;
		UpdateArrows();
	}

	// ---- Build ----
	void CreateConsole()
	{
		AddLabel(LabelY, LabelLeftX, LabelW, "CONSOLE");
		AddLabel(LabelY, LabelRightX, LabelW, "NEWEST FIRST", TextAlignment::Right);

		// Log lines, then the card behind them (AddField prepends)
		DisplayField::SetDefaultFont(glcd19x21);
		for (size_t r = 0; r < NumLines; ++r)
		{
			ageText[r][0] = 0;
			DisplayField::SetDefaultColours(Muted, Tile);
			timeFields[r] = new StaticTextField(LineY(r), TimeX, TimeW, TextAlignment::Right, ageText[r]);
			mgr.AddField(timeFields[r]);
			DisplayField::SetDefaultColours(Text, Tile);
			textFields[r] = new StaticTextField(LineY(r), TextX, TextW, TextAlignment::Left, "");
			mgr.AddField(textFields[r]);
		}
		AddCard(LogY, LogX, LogW, LogH);

		// Input line and keyboard button: both open the keyboard
		const Colour accent = Accent();
		DisplayField::SetDefaultColours(Muted, Tile, Border, Tile, accent, accent, IconPaletteDark);
		mgr.AddField(new ModernTextButton(InputY, InputX, InputW, InputH, "Tap to enter G-code...", evKeyboard, 0, glcd19x21, true, TextAlignment::Left));
		DisplayField::SetDefaultColours(Text, Tile, Border, Tile, accent, accent, IconPaletteDark);
		mgr.AddField(new ModernIconButton(InputY, KbButtonX, KbButtonW, InputH, IconKeyboard, evKeyboard, 0, true));
	}

	// ---------------------------------------------------------------------
	// ALERT: the machine's Error: / Warning: messages, newest first, 11 slots, one page.
	// Kept until they fall out of the list or the panel is switched off (no CLEAR button).
	// Tap a row: popup ERROR / WARNING with the whole text and X; the row is then read.
	// The same message again right after itself only refreshes the newest row (no flooding).
	// ---------------------------------------------------------------------
	namespace AL = CncLayout::Alerts;
	constexpr size_t AlertTextLen = 100;						// fits the 6-line popup info tile

	struct AlertEntry
	{
		uint32_t time;
		bool error;
		bool unread;
		char text[AlertTextLen + 1];
	};

	AlertEntry alerts[AL::NumRows];							// ring, oldest to newest
	size_t alertHead = 0, alertCount = 0;
	CncAlertRow *alertRows[AL::NumRows];
	StaticTextField *alertAges[AL::NumRows];
	StaticTextField *alertCountField, *alertEmptyField;
	char alertAgeText[AL::NumRows][AgeChars];
	String<32> alertCountText;
	uint32_t lastAlertAgeUpdate = 0;

	AlertEntry& AlertAt(size_t row)							// row 0 = newest
	{
		return alerts[(alertHead + AL::NumRows - 1 - row) % AL::NumRows];
	}

	void ShowAlertAges(bool force)
	{
		for (size_t r = 0; r < AL::NumRows; ++r)
		{
			char age[AgeChars] = { 0 };
			if (r < alertCount)
			{
				FormatAge(age, AlertAt(r).time);
			}
			if (force || strcmp(age, alertAgeText[r]) != 0)
			{
				memcpy(alertAgeText[r], age, AgeChars);
				alertAges[r]->SetValue(alertAgeText[r], true);
			}
		}
		lastAlertAgeUpdate = SystemTick::GetTickCount();
	}

	void ShowAlerts()
	{
		if (alertEmptyField == nullptr)
		{
			return;
		}
		for (size_t r = 0; r < AL::NumRows; ++r)
		{
			const bool vis = r < alertCount;
			if (vis)
			{
				const AlertEntry& a = AlertAt(r);
				alertRows[r]->SetEntry(a.text, a.error, a.unread);
			}
			if (vis || alertRows[r]->IsVisible())
			{
				mgr.Show(alertRows[r], vis);
			}
			if (vis || alertAges[r]->IsVisible())
			{
				mgr.Show(alertAges[r], vis);
			}
		}
		ShowAlertAges(true);								// the rows were repainted under the ages
		alertCountText.printf("%u / %u   NEWEST FIRST", (unsigned int)alertCount, (unsigned int)AL::NumRows);
		alertCountField->SetValue(alertCountText.c_str(), true);
		if (alertCount == 0 || alertEmptyField->IsVisible())
		{
			mgr.Show(alertEmptyField, alertCount == 0);
		}
	}

	// One line of a reply (up to the line break); 'p' points after "Error:" / "Warning:".
	// Returns true when the list changed (the caller redraws once per reply).
	bool AddAlert(bool error, const char *p, size_t len, bool& onlyNewest)
	{
		// Normalise first (tabs to spaces, trimmed, whole UTF-8 characters) so repeats compare equal
		char buf[AlertTextLen + 1];
		while (len > 0 && (*p == ' ' || *p == '\t'))
		{
			++p;
			--len;
		}
		while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
		{
			--len;
		}
		size_t n = CharBoundary(p, min<size_t>(len, AlertTextLen));
		if (n == 0)
		{
			memcpy(buf, "(no text)", 10);				// the machine said Error: / Warning: and nothing else
			n = 9;
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				buf[i] = (p[i] == '\t') ? ' ' : p[i];
			}
			buf[n] = 0;
		}

		if (alertCount != 0)
		{
			AlertEntry& last = AlertAt(0);
			if (last.error == error && strcmp(last.text, buf) == 0)
			{
				last.time = SystemTick::GetTickCount();		// the same again: refresh it
				last.unread = true;
				return true;							// onlyNewest stays as it is
			}
		}
		AlertEntry& a = alerts[alertHead];
		a.time = SystemTick::GetTickCount();
		a.error = error;
		a.unread = true;
		memcpy(a.text, buf, n + 1);
		alertHead = (alertHead + 1) % AL::NumRows;
		if (alertCount < AL::NumRows)
		{
			++alertCount;
		}
		onlyNewest = false;
		return true;
	}

	// Every line of a reply that contains "Error:" (checked first, as CONSOLE colours it) or "Warning:"
	void CollectAlerts(const char *text)
	{
		bool changed = false, onlyNewest = true;
		const char *line = text;
		while (*line != 0)
		{
			size_t len = 0;
			while (line[len] != 0 && line[len] != '\n' && line[len] != '\r')
			{
				++len;
			}
			bool found = false;
			for (int pass = 0; pass < 2 && !found; ++pass)
			{
				const char * const tag = (pass == 0) ? "Error:" : "Warning:";
				const size_t tl = strlen(tag);
				for (size_t i = 0; i + tl <= len; ++i)
				{
					if (strncmp(line + i, tag, tl) == 0)
					{
						changed |= AddAlert(pass == 0, line + i + tl, len - i - tl, onlyNewest);
						found = true;
						break;
					}
				}
			}
			line += len;
			while (*line == '\n' || *line == '\r')
			{
				++line;
			}
		}
		if (!changed)
		{
			return;
		}
		if (onlyNewest && alertRows[0] != nullptr)
		{
			// a repeat of the newest: only row 0 and its age
			const AlertEntry& a = AlertAt(0);
			alertRows[0]->SetEntry(a.text, a.error, a.unread);
			mgr.Show(alertRows[0], true);
			alertAgeText[0][0] = 0;
			ShowAlertAges(false);
			alertAges[0]->SetValue(alertAgeText[0], true);
		}
		else
		{
			ShowAlerts();								// once per reply
		}
	}

	void CreateAlerts()
	{
		const Colour accent = Accent();
		AddLabel(AL::LabelY, AL::LabelLeftX, AL::LabelLeftW, "ALERTS");
		alertCountField = AddLabel(AL::LabelY, AL::LabelRightX, AL::LabelRightW, "", TextAlignment::Right);
		DisplayField::SetDefaultFont(glcd19x21);
		for (size_t r = 0; r < AL::NumRows; ++r)
		{
			alertAgeText[r][0] = 0;
			DisplayField::SetDefaultColours(Muted, Tile);
			alertAges[r] = new StaticTextField(AL::RowY(r) + (AL::RowH - 21) / 2, AL::AgeX, AL::AgeW, TextAlignment::Right, alertAgeText[r]);
			alertAges[r]->Show(false);
			mgr.AddField(alertAges[r]);						// before the row: drawn on top of it
			alertRows[r] = new CncAlertRow(AL::RowY(r), AL::RowX, AL::RowW, AL::RowH, accent, evTabMsg, (int)r);
			alertRows[r]->Show(false);
			mgr.AddField(alertRows[r]);
		}
		DisplayField::SetDefaultColours(Muted, PageBg);
		alertEmptyField = new StaticTextField(AL::EmptyY, AL::RowX, AL::RowW, TextAlignment::Centre, "No alerts");
		mgr.AddField(alertEmptyField);
	}
}

namespace CncSystem
{
	void Create(DisplayField *baseRoot)
	{
		mgr.SetRoot(baseRoot);
		AddSubTabs(SubTabNames, NumSubTabs, subTabs);
		DisplayField * const tabsRoot = mgr.GetRoot();

		CreateAlerts();
		subRoots[AlertSub] = mgr.GetRoot();

		mgr.SetRoot(tabsRoot);
		CreateConsole();
		subRoots[ConsoleSub] = mgr.GetRoot();

		mgr.SetRoot(tabsRoot);
		CncSettings::Create();
		subRoots[SettingsSub] = nullptr;				// CncSettings::CurrentRoot()

		selectedSubTab = subTabs[AlertSub];
		subTabs[AlertSub]->Press(true, 0);
		ShowLog();
		ShowAlerts();
	}

	DisplayField *CurrentRoot()
	{
		return (currentSub == SettingsSub) ? CncSettings::CurrentRoot() : subRoots[currentSub];
	}

	void OpenSettings()
	{
		if (currentSub != SettingsSub)
		{
			currentSub = SettingsSub;
			Select(selectedSubTab, subTabs[SettingsSub]);
		}
	}

	bool ProcessTouch(ButtonPress bp, bool& redraw)
	{
		redraw = false;
		switch ((Event)bp.GetEvent())
		{
		case evCncSubTab:
			{
				const size_t sub = (size_t)bp.GetIParam();
				if (sub != currentSub && sub < NumSubTabs)
				{
					currentSub = sub;
					Select(selectedSubTab, bp.GetButton());
					redraw = true;
				}
			}
			return true;

		case evKeyboard:
			if (currentSub == ConsoleSub)
			{
				OpenKeyboard();
			}
			return true;

		case evTabMsg:										// ALERT row: whole text
			{
				const size_t r = (size_t)bp.GetIParam();
				if (currentSub == AlertSub && r < alertCount)
				{
					AlertEntry& a = AlertAt(r);
					if (a.unread)
					{
						a.unread = false;
						alertRows[r]->SetEntry(a.text, a.error, false);
						alertAges[r]->SetValue(alertAgeText[r], true);	// the row repaint covers the age
					}
					CncPopup::Info(a.error ? "ERROR" : "WARNING", a.text);
				}
			}
			return true;

		default:
			return currentSub == SettingsSub && CncSettings::ProcessTouch(bp, redraw);
		}
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evCncSubTab:
		case evKeyboard:
		case evTabMsg:
			return true;								// selection stays / keyboard or popup opened

		default:
			return CncSettings::ProcessRelease(bp);
		}
	}

	void Response(const char *text)
	{
		AddMessage(Kind::Reply, text);					// colour per line (Error: / Warning:)
		CollectAlerts(text);							// ALERT history
		if (ConsoleReply())
		{
			consoleReplyNow = true;						// this reply answers the typed command
			commandSent = false;						// later replies are the job's again
		}
		else
		{
			consoleReplyNow = false;
		}
	}

	bool ConsoleReply()
	{
		return commandSent && SystemTick::GetTickCount() - lastCommandTime < ConsoleReplyTime;
	}

	bool LastReplyWasConsole()
	{
		return consoleReplyNow;
	}

	bool KeyboardOpen()
	{
		return (kbOpen && CncKeyboard::IsOpenFor(&kbClient)) || CncSettings::KeyboardOpen();
	}

	bool TouchOutsideKeyboard(ButtonPress bp)
	{
		if (CncSettings::KeyboardOpen())
		{
			return CncSettings::TouchOutsideKeyboard(bp);	// CUSTOM n LABEL
		}
		if (bp.GetEvent() == evCncNav)
		{
			if (historyPos < 0)
			{
				draft.copy(CncKeyboard::GetText());
			}
			CncKeyboard::Close();
			kbOpen = false;
			historyPos = -1;
			return true;								// then switch page as usual
		}
		return false;									// log lines, labels: nothing to do
	}

	void Spin(bool shown)
	{
		CncSettings::Spin(shown && currentSub == SettingsSub);	// writes cnc-settings.g even when not shown
		if (kbOpen && !CncKeyboard::IsOpenFor(&kbClient))
		{
			kbOpen = false;								// closed by STOP, a page change or another popup
			if (historyPos < 0)
			{
				draft.copy(CncKeyboard::GetText());		// keep the unsent text for next time
			}
			historyPos = -1;
			ShowLog();									// rows under the sheet (ages may be stale)
		}
		if (shown && currentSub == ConsoleSub && SystemTick::GetTickCount() - lastAgeUpdate >= 1000)
		{
			ShowAges();
		}
		if (shown && currentSub == AlertSub && SystemTick::GetTickCount() - lastAlertAgeUpdate >= 1000)
		{
			ShowAlertAges(false);
		}
	}
}

// End
