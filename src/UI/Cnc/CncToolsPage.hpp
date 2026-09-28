/*
 * CncToolsPage.hpp
 *
 * WCS > TOOLS sub-page: the tool list defined in RRF (M563 in config.g), with name and
 * Z length offset, plus LOAD / MEASURE / SET Z.
 */

#ifndef SRC_UI_CNC_CNCTOOLSPAGE_HPP_
#define SRC_UI_CNC_CNCTOOLSPAGE_HPP_

#include <UI/Display.hpp>

namespace CncTools
{
	// Adds the page fields to the current root (sub-tab strip + DRO + nav)
	void Create();

	bool ProcessTouch(ButtonPress bp);
	bool ProcessRelease(ButtonPress bp);

	// Tool data from RRF (tool number = index in RRF's tools array)
	void SetPresent(size_t tool, bool present);
	void SetName(size_t tool, const char *name);
	void RemoveFrom(size_t firstTool);
	void SetZOffset(size_t tool, float offset);
	void SetActiveTool(int tool);						// -1 = none

	void SetJobState(bool running, bool active);
}

#endif /* SRC_UI_CNC_CNCTOOLSPAGE_HPP_ */
