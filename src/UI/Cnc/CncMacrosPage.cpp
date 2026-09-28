/*
 * CncMacrosPage.cpp
 *
 * MACROS page, portrait CNC UI (see CncMacrosPage.hpp):
 *
 *   MACROS                               0:/macros
 *   [ probing                   FOLDER ]   [ GRID ]     list: 11 rows
 *   [ park                             ]   [  ^   ]     grid: 2 x 5 tiles, name centred,
 *   [ safe_z                           ]   [  v   ]           tag bottom-right
 *
 * The listing is the panel's cached M20 listing of the macro folder (FileManager, up to 42
 * entries per folder), shared with nothing else in the CNC UI.
 */

#include "CncMacrosPage.hpp"
#include "CncCommon.hpp"
#include "CncWidgets.hpp"
#include "CncPopups.hpp"
#include "CncKeyboard.hpp"
#include <UI/UserInterface.hpp>
#include "Hardware/SerialIo.hpp"
#include "Library/Misc.hpp"
#include "FileManager.hpp"
#include "PanelDue.hpp"
#include <General/String.h>

using namespace CncLayout;
using namespace CncLayout::Macros;
using namespace Cnc;

namespace
{
	constexpr uint8_t ParentEntry = 0xFF;						// ".." row: parent folder
	constexpr uint8_t RootEntry = 0xFE;							// ".." row after a listing error: macro root
	constexpr size_t MaxEntries = 48;							// FileManager keeps up to 42 per folder (+ "..")

	CncFileRow *cells[NumCells];
	CncGlyphButton *toggleTile, *searchTile;
	CncArrowButton *upTile, *downTile;
	StaticTextField *leftField, *queryField, *pathField, *emptyField;

	uint8_t view[MaxEntries];
	size_t viewCount = 0;
	size_t fileTotal = 0;										// macros (not folders) in the open folder
	size_t scroll = 0;
	bool gridMode = false;
	int listError = 0;

	// Search (same behaviour as JOB LIST): filters the macros of the open folder by display name
	bool searching = false;
	bool kbMode = false;
	String<CncKeyboard::MaxText> query;
	String<40> queryText;
	String<24> countText;

	String<48> pathText;
	String<FileManager::maxPathLength> runDir;
	String<FileManager::maxPathLength> runFile;
	String<48> runName;

	// SETTINGS > CUSTOM n > MACRO: the next macro tapped is handed over instead of run
	CncMacros::PickHandler pickHandler = nullptr;
	String<FileManager::maxPathLength> pickPath;

	unsigned int PageSize()
	{
		return gridMode ? GridCols * (kbMode ? GridRowsKeyboard : GridRows)
						: (kbMode ? ListRowsKeyboard : ListRows);
	}

	bool FilterOn()
	{
		return searching && !query.IsEmpty();
	}

	// Case-insensitive position of q in the first 'len' characters of s, -1 if not found
	int MatchPos(const char *s, size_t len, const char *q)
	{
		const size_t ql = strlen(q);
		for (size_t i = 0; i + ql <= len; ++i)
		{
			size_t k = 0;
			while (k < ql && tolower((unsigned char)s[i + k]) == tolower((unsigned char)q[k]))
			{
				++k;
			}
			if (k == ql)
			{
				return (int)i;
			}
		}
		return -1;
	}

	// Display name: without "!", a leading "12_" and ".g". 'len' = characters to show.
	const char *DisplayName(const char *file, size_t& len)
	{
		const char *p = file;
		if (*p == '!')
		{
			++p;
		}
		p = SkipDigitsAndUnderscore(p);
		len = strlen(p);
		if (len > 2 && (strcasecmp(p + len - 2, ".g") == 0))
		{
			len -= 2;
		}
		return p;
	}

	void RebuildView()
	{
		viewCount = 0;
		fileTotal = 0;
		if (!FileManager::IsMacroListLoaded())
		{
			if (listError != 0)
			{
				view[viewCount++] = RootEntry;
			}
			return;
		}
		if (!FilterOn() && FileManager::IsMacroListInSubdir())
		{
			view[viewCount++] = ParentEntry;
		}
		const size_t n = min<size_t>(FileManager::GetMacroFileCount(), RootEntry);
		for (size_t i = 0; i < n && viewCount < MaxEntries; ++i)
		{
			const char * const file = FileManager::GetMacroFile(i);
			const bool folder = (file[0] == '*');
			if (!folder)
			{
				++fileTotal;
			}
			if (FilterOn())
			{
				size_t len;
				const char * const name = DisplayName(file, len);
				if (folder || MatchPos(name, len, query.c_str()) < 0)
				{
					continue;								// search: macros of this folder only
				}
			}
			view[viewCount++] = (uint8_t)i;
		}
		const unsigned int page = PageSize();
		if (scroll >= viewCount)
		{
			scroll = (viewCount == 0) ? 0 : ((viewCount - 1) / page) * page;
		}
	}

	// Cell i as a list row or a grid tile
	void PlaceCells()
	{
		for (unsigned int i = 0; i < NumCells; ++i)
		{
			CncFileRow * const c = cells[i];
			if (gridMode)
			{
				const unsigned int col = i % GridCols, row = i / GridCols;
				c->SetGeometry(AreaX + col * (GridTileW + GridGap), AreaY + row * GridPitchY, GridTileW, GridTileH);
			}
			else
			{
				c->SetGeometry(AreaX, AreaY + i * ListPitch, AreaW, ListRowH);
			}
			c->SetGridStyle(gridMode);
		}
	}

	void ShowCells()
	{
		if (toggleTile == nullptr)
		{
			return;												// machine data before the page exists
		}
		const unsigned int page = PageSize();
		const bool loaded = FileManager::IsMacroListLoaded();
		const bool onlyParent = (viewCount == 1 && view[0] == ParentEntry && !kbMode);		// empty subfolder
		const bool message = (viewCount == 0) || onlyParent || (!loaded && listError != 0);
		if (!message && emptyField->IsVisible())
		{
			mgr.Show(emptyField, false);						// before the cells: hiding clears its area
		}

		for (unsigned int i = 0; i < NumCells; ++i)
		{
			CncFileRow * const c = cells[i];
			const size_t vi = scroll + i;
			const bool vis = (i < page && vi < viewCount);
			if (vis)
			{
				const uint8_t e = view[vi];
				if (e == ParentEntry || e == RootEntry)
				{
					c->SetEntry("..", (e == ParentEntry) ? "UP" : "BACK");
				}
				else
				{
					const char * const file = FileManager::GetMacroFile(e);
					if (file[0] == '*')
					{
						c->SetEntry(file + 1, "FOLDER");
					}
					else
					{
						size_t len;
						const char * const name = DisplayName(file, len);
						const int m = FilterOn() ? MatchPos(name, len, query.c_str()) : -1;
						c->SetEntry(name, nullptr, (m < 0) ? 0 : (size_t)m, (m < 0) ? 0 : query.strlen(), len);
					}
				}
			}
			if (vis || c->IsVisible())
			{
				mgr.Show(c, vis);							// never paint a hidden cell (grid cell 10 lies over the nav bar)
			}
		}

		const bool upOk = scroll > 0, downOk = scroll + page < viewCount;
		upTile->SetDisabled(!upOk);
		downTile->SetDisabled(!downOk);
		mgr.Show(upTile, !kbMode);
		mgr.Show(downTile, !kbMode);
		mgr.Show(toggleTile, !kbMode);
		toggleTile->SetGlyph(gridMode ? CncGlyph::List : CncGlyph::Grid);	// the view the tile switches to
		searchTile->SetActive(searching);
		if (kbMode)
		{
			CncKeyboard::SetArrowsEnabled(upOk, downOk);
		}

		// Label row: MACROS + path, or SEARCH "text" + counts
		if (searching)
		{
			if (pathField->GetMinX() != CountX)
			{
				mgr.Show(pathField, false);				// it moves: clear its old (wider) area first
				pathField->SetPositionAndWidth(CountX, CountW);
			}
			leftField->SetValue("SEARCH");
			lcd.setFont(glcd19x21);
			size_t n = query.strlen();
			while (n > 0 && DisplayField::GetTextWidth(query.c_str(), 9999, n) > QueryTextW)
			{
				--n;
			}
			queryText.copy("\"");
			queryText.catn(query.c_str(), n);
			if (n < query.strlen())
			{
				queryText.cat("..");
			}
			queryText.cat('"');
			queryField->SetValue(queryText.c_str(), true);
			mgr.Show(queryField, true);
			countText.printf("%u of %u", (unsigned int)(FilterOn() ? viewCount : fileTotal), (unsigned int)fileTotal);
			pathField->SetValue(countText.c_str(), true);
			mgr.Show(pathField, true);
		}
		else
		{
			leftField->SetColours((pickHandler != nullptr) ? Accent() : Muted, PageBg);
			leftField->SetValue((pickHandler != nullptr) ? "SELECT" : "MACROS", true);
			mgr.Show(queryField, false);
			if (pathField->GetMinX() != PathX)
			{
				mgr.Show(pathField, false);
				pathField->SetPositionAndWidth(PathX, PathW);
			}
			lcd.setFont(glcd19x21);
			const char *path = FileManager::GetMacrosDir();
			if (path[0] == 0)
			{
				path = FileManager::GetMacrosRootDir();		// before the first listing arrives
			}
			const size_t pathLen = strlen(path);
			const char *p = (pathLen > 40) ? path + pathLen - 40 : path;	// at most ~40 characters fit anyway
			for (;;)
			{
				pathText.copy((p != path) ? ".." : "");
				pathText.cat(p);
				if (*p == 0 || DisplayField::GetTextWidth(pathText.c_str(), 9999, pathText.strlen()) <= PathW)
				{
					break;
				}
				++p;
			}
			pathField->SetValue(pathText.c_str(), true);
			mgr.Show(pathField, true);
		}

		if (message)
		{
			// grid while typing: only tile row 0 is above the sheet, so the message goes there
			const PixelNumber ey = !gridMode ? EmptyY : kbMode ? EmptyGridKbY : EmptyGridY;
			if (emptyField->IsVisible() && emptyField->GetMinY() != ey)
			{
				mgr.Show(emptyField, false);				// moving: clear the old place first
			}
			emptyField->SetPosition(AreaX, ey);
			emptyField->SetValue(!loaded ? ((listError != 0) ? "Cannot read this folder." : "Loading...")
								: FilterOn() ? "No macros match" : "No macros", true);
			mgr.Show(emptyField, true);
		}
	}

	void FolderChanged()
	{
		scroll = 0;
		listError = 0;
		if (kbMode)
		{
			CncKeyboard::Close();
			kbMode = false;
		}
		searching = false;
		query.Clear();
	}

	// ---- Search keyboard ----
	void KbChanged(const char *text)
	{
		query.copy(text);
		scroll = 0;
		RebuildView();
		ShowCells();
	}

	void KbEnter(const char *text)
	{
		query.copy(text);
		kbMode = false;
		if (query.IsEmpty())
		{
			searching = false;
		}
		scroll = 0;
		RebuildView();
		ShowCells();
	}

	void KbCancel()
	{
		kbMode = false;
		searching = false;
		query.Clear();
		scroll = 0;
		RebuildView();
		ShowCells();
	}

	void KbArrow(int dir)
	{
		const unsigned int page = PageSize();
		if (dir < 0 && scroll > 0)
		{
			scroll = (scroll > page) ? scroll - page : 0;
		}
		else if (dir > 0 && scroll + page < viewCount)
		{
			scroll += page;
		}
		ShowCells();
	}

	const CncKeyboard::Client kbClient = { KbChanged, KbEnter, KbCancel, KbArrow, false };

	void OpenSearch()
	{
		searching = true;
		kbMode = true;
		scroll = 0;
		RebuildView();
		ShowCells();									// hides what the sheet covers
		CncKeyboard::Open(query.c_str(), kbClient);
		ShowCells();									// keyboard arrows
	}

	// The keyboard went without ENTER / X (another popup, STOP, page change): keep the filter
	void KeyboardGone(bool alignPage)
	{
		if (kbMode)
		{
			kbMode = false;
			if (query.IsEmpty())
			{
				searching = false;
			}
			if (alignPage)
			{
				scroll = (scroll / PageSize()) * PageSize();	// back on the full page grid
			}
			RebuildView();
			ShowCells();
		}
	}

	// ---- RUN MACRO ----
	void DoRun(int param)
	{
		UNUSED(param);
		if (MachineBusy())
		{
			Refuse(CNC_LOCKED_JOB);
			return;
		}
		SerialIo::Sendf("M98 P");
		SerialIo::SendFilename(CondStripDrive(runDir.c_str()), runFile.c_str());
		SerialIo::Sendf("\n");
	}

	void AskRun(const char *file)
	{
		if (MachineBusy())
		{
			Refuse(CNC_LOCKED_JOB);									// allowed again while paused
			return;
		}
		runDir.copy(FileManager::GetMacrosDir());
		runFile.copy(file);
		size_t len;
		const char * const name = DisplayName(file, len);
		runName.copy(name);
		runName.Truncate(min<size_t>(len, runName.strlen()));
		CncPopup::Confirm("RUN MACRO", "Do you want to run this macro?", runName.c_str(), nullptr, nullptr, DoRun, 0);
	}
}

namespace CncMacros
{
	DisplayField *Create(DisplayField *baseRoot)
	{
		mgr.SetRoot(baseRoot);
		const Colour accent = Accent();

		// Label row
		DisplayField::SetDefaultFont(glcd19x21);
		DisplayField::SetDefaultColours(Muted, PageBg);
		leftField = new StaticTextField(LabelY, LabelLeftX, LabelLeftW, TextAlignment::Left, "MACROS");
		mgr.AddField(leftField);
		DisplayField::SetDefaultColours(accent, PageBg);
		queryField = new StaticTextField(LabelY, QueryX, QueryW, TextAlignment::Left, "");
		queryField->Show(false);
		mgr.AddField(queryField);
		DisplayField::SetDefaultColours(Muted, PageBg);
		pathField = new StaticTextField(LabelY, PathX, PathW, TextAlignment::Right, "");
		mgr.AddField(pathField);

		// Cells (list rows or grid tiles), message
		for (unsigned int i = 0; i < NumCells; ++i)
		{
			cells[i] = new CncFileRow(AreaY + i * ListPitch, AreaX, AreaW, ListRowH, accent, evMacro, (int)i);
			cells[i]->Show(false);
			mgr.AddField(cells[i]);
		}
		DisplayField::SetDefaultColours(Muted, PageBg);
		emptyField = new StaticTextField(EmptyY, AreaX, AreaW, TextAlignment::Centre, "");
		emptyField->Show(false);
		mgr.AddField(emptyField);

		// Side column: SEARCH, up, down, LIST / GRID
		searchTile = new CncGlyphButton(SearchY, SideX, SideW, SearchH, CncGlyph::Search, accent, evKeyboard, 0);
		mgr.AddField(searchTile);
		toggleTile = new CncGlyphButton(ToggleY, SideX, SideW, ToggleH, CncGlyph::Grid, accent, evMacroControlPage, 0);
		toggleTile->SetActive(true);						// accent outline and glyph, as in the mock-ups
		mgr.AddField(toggleTile);
		upTile = new CncArrowButton(UpY, SideX, SideW, ArrowH, true, evScrollMacros, -1);
		mgr.AddField(upTile);
		downTile = new CncArrowButton(DownY, SideX, SideW, ArrowH, false, evScrollMacros, 1);
		mgr.AddField(downTile);

		PlaceCells();
		DisplayField::SetDefaultFont(DEFAULT_FONT);
		return mgr.GetRoot();
	}

	void StartPick(PickHandler picked)
	{
		pickHandler = picked;
	}

	void CancelPick()
	{
		pickHandler = nullptr;
	}

	void PageShown()
	{
		listError = 0;
		FileManager::DisplayControlMacrosPage();			// M20 of the open macro folder; its hook redraws
	}

	void FilesChanged()
	{
		for (CncFileRow *c : cells)
		{
			if (c != nullptr && c->IsVisible())
			{
				c->SetChanged();							// new listing in the same buffer
			}
		}
		RebuildView();
		ShowCells();
	}

	void SetListError(int err)
	{
		listError = err;
	}

	bool ProcessTouch(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evMacro:
			{
				const size_t vi = scroll + (size_t)bp.GetIParam();
				if ((unsigned int)bp.GetIParam() >= PageSize() || vi >= viewCount)
				{
					return true;
				}
				const uint8_t e = view[vi];
				if (e == ParentEntry)
				{
					FolderChanged();
					FileManager::RequestControlMacrosParentDir();
				}
				else if (e == RootEntry)
				{
					FolderChanged();
					FileManager::RequestControlMacrosRoot();		// back to 0:/macros after a listing error
				}
				else
				{
					const char * const file = FileManager::GetMacroFile(e);
					if (file[0] == '*')
					{
						FolderChanged();
						FileManager::RequestControlMacrosSubdir(file + 1);
					}
					else if (pickHandler != nullptr)
					{
						const PickHandler h = pickHandler;		// the handler leaves the page
						pickHandler = nullptr;
						pickPath.copy(FileManager::GetMacrosDir());
						if (pickPath.IsEmpty() || pickPath[pickPath.strlen() - 1] != '/')
						{
							pickPath.cat('/');
						}
						pickPath.cat(file);
						size_t len;
						const char * const name = DisplayName(file, len);
						runName.copy(name);
						runName.Truncate(min<size_t>(len, runName.strlen()));
						h(pickPath.c_str(), runName.c_str());
					}
					else
					{
						AskRun(file);
					}
				}
			}
			return true;

		case evScrollMacros:
			if ((bp.GetIParam() < 0) ? scroll > 0 : scroll + PageSize() < viewCount)
			{
				mgr.Press(bp, true);
				KbArrow(bp.GetIParam());
			}
			return true;

		case evKeyboard:
			OpenSearch();									// new search, or edit the kept one
			return true;

		case evMacroControlPage:							// LIST <-> GRID
			gridMode = !gridMode;
			for (CncFileRow *c : cells)
			{
				if (c->IsVisible())
				{
					mgr.Show(c, false);						// clear the old layout first
				}
			}
			if (emptyField->IsVisible())
			{
				mgr.Show(emptyField, false);				// it moves with the view
			}
			scroll = (scroll / PageSize()) * PageSize();
			PlaceCells();
			RebuildView();
			ShowCells();
			return true;

		default:
			return false;
		}
	}

	bool ProcessRelease(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evScrollMacros:
			mgr.Press(bp, false);
			return true;

		case evMacro:
		case evMacroControlPage:
		case evKeyboard:
			return true;

		default:
			return false;
		}
	}

	bool KeyboardOpen()
	{
		return kbMode && CncKeyboard::IsOpenFor(&kbClient);
	}

	OutsideTouch TouchOutsideKeyboard(ButtonPress bp)
	{
		switch ((Event)bp.GetEvent())
		{
		case evKeyboard:								// SEARCH while typing: leave search
			CncKeyboard::Close();
			KbCancel();
			return OutsideTouch::Used;

		case evMacro:									// a macro / folder: keep the filter, then act on it
			CncKeyboard::Close();
			KeyboardGone(false);						// the scroll stays, so the cell index still fits
			return OutsideTouch::Process;

		case evCncNav:									// another page: keep the filter
			CncKeyboard::Close();
			KeyboardGone(true);
			return OutsideTouch::Process;

		default:
			return OutsideTouch::Ignored;
		}
	}

	void Spin()
	{
		if (kbMode && !CncKeyboard::IsOpenFor(&kbClient))
		{
			KeyboardGone(true);
		}
	}
}

// End
