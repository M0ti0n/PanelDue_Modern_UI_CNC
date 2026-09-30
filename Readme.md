 PanelDue Modern UI CNC — Interface Guide

This page describes the current CNC user interface and navigation structure in the latest firmware source.  
The interface is organised around five main tabs — **CONTROL, WCS, JOB, MACROS, and SYSTEM** — with a dedicated **STOP** control always available from the main navigation.

---

## Table of Contents

- [Navigation](#navigation)
- [CONTROL](#control)
- [WCS](#wcs)
  - [OFFSETS](#wcs--offsets)
  - [TOOLS](#wcs--tools)
  - [PROBE](#wcs--probe)
- [JOB](#job)
  - [JOB STATUS](#job--job-status)
  - [JOB LIST](#job--job-list)
- [MACROS](#macros)
- [SYSTEM](#system)
  - [ALERT](#system--alert)
  - [CONSOLE](#system--console)
  - [SETTINGS — SYSTEM](#system--settings--system)
  - [SETTINGS — CUSTOMIZATION](#system--settings--customization)

---

# Navigation

The interface uses a persistent bottom navigation bar with the main tabs **CONTROL, WCS, JOB, MACROS, and SYSTEM**, plus a dedicated **STOP** button.  
The currently selected tab is highlighted using the configured accent colour, while STOP remains available independently of the active page.  
CONTROL and WCS use the full DRO, while JOB, MACROS, and SYSTEM use a more compact DRO where additional screen space is needed.  
Several main tabs contain their own sub-tabs so related CNC functions can remain grouped without adding more top-level navigation buttons.

---

# CONTROL

CONTROL is the main page for manual machine movement and direct operator control.  
The operator selects **X, Y, Z, or A**, chooses a movement step such as **0.01 / 0.1 / 1 / 10 / 100**, and then moves the selected axis using the `−` and `+` jog controls.  
**RAPID** uses relative `G0` moves, while **SLOW** uses relative `G1` moves at the configured jog feed; large 10 and 100-unit steps are protected so repeated taps do not queue multiple long movements.  
The page also provides spindle control, coolant/vacuum control, homing, Safe Z, XY0, and a configurable **CUSTOM 1** macro button.

### Important controls

- **Axis selection** — X, Y, Z, A
- **Step selection** — 0.01 / 0.1 / 1 / 10 / 100
- **− / +** — jog selected axis
- **RAPID** — relative rapid movement
- **SLOW** — relative feed movement using configured jog feed
- **HOME ALL / X / Y / Z / A** — homing commands with confirmation
- **SAFE Z** — runs `0:/macros/safe_z.g`
- **XY0** — runs `0:/macros/goto_xy0.g`
- **Spindle controls** — spindle state and speed
- **COOLANT / VACUUM** — auxiliary output control
- **CUSTOM 1** — user-assigned macro

During an active job the page is locked.  
When a job is paused, most manual controls become available again, but homing remains disabled.

---

# WCS

The WCS section groups coordinate-system, tool, and probing functions under three sub-tabs:

- **OFFSETS**
- **TOOLS**
- **PROBE**

---

## WCS → OFFSETS

OFFSETS manages the six standard work coordinate systems **G54–G59**.  
The active WCS is shown using the accent colour, while the WCS currently being viewed is highlighted separately so the operator can inspect or prepare another coordinate system without activating it.  
Axis offset values for X, Y, Z, and A can be entered directly using the numeric keypad, while COPY, SHIFT, RESET, ACTIVATE, and SAVE provide higher-level offset management.  
The page also includes temporary Z offset stepping using `M290`, allowing small live Z corrections to be applied and then committed into the active WCS.

### Important controls

- **G54–G59** — select WCS to view/edit
- **X / Y / Z / A offset fields** — direct numeric offset entry
- **ACTIVATE** — activate the viewed WCS
- **RESET** — set the viewed WCS offsets to zero
- **COPY TO** — copy selected axes into another WCS
- **SHIFT** — add a value to selected axes
- **SAVE** — save work/tool offsets in RRF
- **Z offset step** — temporary live Z adjustment using `M290`
- **APPLY |→|** — apply accumulated temporary Z correction to the active WCS

During a running job, the active job WCS is protected from editing or replacement.  
Other WCS entries remain available so another fixture or setup can be prepared while the current job is running.

---

## WCS → TOOLS

TOOLS displays the tools defined by RepRapFirmware, including the **tool number, tool name, and Z offset**.  
Four tools are displayed at a time, with page controls available when the machine has a longer tool list.  
Selecting a tool row makes it the target for LOAD, MEASURE, and SET Z operations, while the currently active tool is visually identified separately.  
Tool-changing and measurement operations are disabled while a job is running or paused to prevent accidental changes to the active machining setup.

### Important controls

- **Tool row** — select a tool
- **LOAD** — load/select the chosen tool
- **MEASURE** — runs `0:/sys/toolchange/measure_tool.g`
- **SET Z** — directly edit the selected tool's Z offset
- **Page arrows** — browse longer tool lists

If no tool is currently loaded, LOAD can tell RRF which manually inserted tool is in the spindle.  
If another tool is already active, LOAD performs the corresponding normal `T#` tool-change request.

---

## WCS → PROBE

PROBE provides the interface for workpiece probing and work-origin measurement.  
The operator selects the required probing position from corner, edge, centre, bore, boss, or Z-top positions, and the available parameters change depending on whether **AUTO, SEMI-AUTO, or MANUAL** probing mode is selected in SETTINGS.  
The probe status indicator shows whether probe K0 is currently **OPEN or CLOSED**, and RUN PROBE / START launches the corresponding probing macro with the selected position and parameters.  
AUTO mode provides live probing progress and measured-axis results, while SEMI-AUTO and MANUAL modes can use dedicated M291 interaction controls for positioning and confirmation.

### Probe positions

- Top-left
- Top
- Top-right
- Left
- Centre
- Right
- Bottom-left
- Bottom
- Bottom-right
- **BORE**
- **BOSS**
- **Z TOP**

### Important parameters

Depending on the selected probing mode, available parameters can include:

- **TIP Ø** — probe tip diameter
- **CLEAR D / CLEARANCE** — clearance distance
- **SEARCH** — maximum probing/search travel
- **Z DEPTH** — probing depth
- **STOCK** — stock dimension
- **SAVE TO / STORE TO** — destination WCS

### Probe macros

- `0:/sys/probe/probe_auto.g`
- `0:/sys/probe/probe_semi.g`
- `0:/sys/probe/probe_manual.g`

---

# JOB

The JOB tab is divided into:

- **JOB STATUS**
- **JOB LIST**

---

## JOB → JOB STATUS

JOB STATUS is the live machining dashboard.  
It displays the current filename, job progress, elapsed time, and estimated time remaining, along with feed and spindle override controls.  
Pause, resume, abort, coolant/vacuum, custom macros, and tool-change prompts are handled from this page so the operator can manage the running program without leaving the job view.  
Overrides return to 100% when the job finishes.

### Important controls

- **PAUSE**
- **RESUME**
- **ABORT** — confirmation required
- **FEED WORK override**
- **SPINDLE override**
- **COOLANT / VACUUM**
- **CUSTOM 2**
- **CUSTOM 3**
- **Tool-change acknowledgement**

### Feed override

Feed override uses RepRapFirmware's `M220` functionality.

Available controls include:

- `−`
- `+`
- direct percentage entry
- selectable adjustment step:
  - 1%
  - 5%
  - 10%

Typical allowed range:

- **10–200%**

### Spindle override

Spindle override modifies the commanded spindle speed relative to the programmed value.

Typical allowed range:

- **50–150%**

---

## JOB → JOB LIST

JOB LIST is the G-code file browser used to choose the next machining program.  
Folders open with one tap, `..` moves to the parent directory, and tapping a file selects it before a second confirmation starts it using `M32`.  
SEARCH filters filenames in the current folder using the shared on-screen keyboard, while page arrows are used to browse longer file lists.  
If more than one storage volume is available, the SD control can switch between available storage locations.

### Important controls

- **Folder** — open directory
- **..** — go to parent directory
- **File** — select machining file
- **Start confirmation** — starts selected file using `M32`
- **SEARCH** — filter current directory
- **Page arrows** — browse file list
- **SD** — switch storage volume where available

The file list is refreshed whenever the page is opened so it reflects the current contents of the machine storage.

---

# MACROS

MACROS provides direct access to the machine's `0:/macros` directory.  
Macros can be displayed in either a compact list or a two-column grid, with folders opened normally and page controls used for longer directories.  
SEARCH filters macros in the current directory using the shared keyboard, and tapping a macro normally opens a confirmation before executing it with `M98`.  
The same browser is reused when assigning macros to CUSTOM buttons in SETTINGS.

### Important controls

- **Macro** — select/run macro
- **Folder** — open macro folder
- **Search**
- **Page up/down**
- **List/Grid view toggle**

### Macro display-name rules

The UI automatically cleans macro names for display:

- `.g` is hidden
- numeric ordering prefixes such as `01_` can be hidden
- a leading `!` can be used as a confirmation marker

Example:

```text
01_probe_edge.g
```

can be displayed as:

```text
PROBE_EDGE
```

while:

```text
!02_safe_home.g
```

can require confirmation before execution.

Macros cannot be started while the machine is actively running a job, but they become available again when the job is paused.

---

# SYSTEM

SYSTEM is divided into:

- **ALERT**
- **CONSOLE**
- **SETTINGS**

SETTINGS itself contains separate SYSTEM and CUSTOMIZATION pages.

---

## SYSTEM → ALERT

ALERT maintains a rolling history of recent RepRapFirmware warnings and errors.  
The newest message is shown first, with each row displaying severity, message age, and shortened message text.  
Unread alerts receive an accent indicator, and tapping a row opens a larger popup containing the full RRF message.  
The current implementation stores up to approximately eleven alerts, with older messages disappearing as newer messages replace them or when the panel is restarted.

### Alert types

- **ERR** — error
- **WARN** — warning

There is currently no manual CLEAR button.

---

## SYSTEM → CONSOLE

CONSOLE provides a local G-code terminal directly on PanelDue.  
It displays commands sent from the panel together with replies received from RepRapFirmware, using different text styles for normal messages, warnings, and errors.  
Tapping the command-entry area or KEYBOARD opens the shared keyboard, and ENTER sends the command while leaving the keyboard open for repeated console work.  
Command-history controls allow the operator to recall recent entries, which is useful for diagnostics and repeated Object Model queries such as `M409`.

### Important controls

- **KEYBOARD**
- **Command entry**
- **ENTER**
- **Command history up/down**

Typical uses include:

```gcode
M409 K"move.axes[].workplaceOffsets" F"v"
```

or other diagnostic and machine-configuration commands.

---

## SYSTEM → SETTINGS → SYSTEM

The SYSTEM settings page contains options related primarily to the PanelDue display itself rather than CNC machine behaviour.  
Display appearance, sound, timeout, calibration, orientation, communication speed, and other persistent panel settings are configured here.  
The page also shows current network/RAM information and provides access to FACTORY RESET.  
Settings are stored in PanelDue's persistent memory so they survive normal restarts.

### Important settings

- **ACCENT COLOR**
- **BRIGHTNESS**
- **VOLUME**
- **INFO TIMEOUT**
- **SCREEN DIMMING**
- **TOUCH CALIBRATION**
- **MIRROR DISPLAY**
- **INVERT DISPLAY**
- **BAUD**
- **FACTORY RESET**

The current IP address and free-memory information are also displayed here.

---

## SYSTEM → SETTINGS → CUSTOMIZATION

CUSTOMIZATION contains CNC-specific interface and machine-behaviour settings.  
These values are stored as RRF globals in `0:/sys/cnc-settings.g`, allowing both PanelDue and machine macros to use the same configuration.  
The page controls probing mode, tool handling, tool-setter behaviour, coolant/vacuum naming, and assignment of the configurable CUSTOM buttons.  
Changes are written back to the Duet and persisted so the CNC interface retains its configuration after restart.

### Important settings

- **PROBE MODE**
  - AUTO
  - SEMI-AUTO
  - MANUAL

- **TOOL HOLDER**
  - ER COLLET
  - TOOL HOLDER

- **REMEMBER TOOL**
  - ON / OFF

- **TOOL SETTER**
  - ON / OFF

- **COOL / VAC**
  - COOLANT
  - VACUUM

- **CUSTOM 1**
  - assigned macro
  - editable label
  - clear assignment

- **CUSTOM 2**
  - assigned macro
  - editable label
  - clear assignment

- **CUSTOM 3**
  - assigned macro
  - editable label
  - clear assignment

### Custom-button locations

- **CUSTOM 1** — CONTROL
- **CUSTOM 2** — JOB STATUS
- **CUSTOM 3** — JOB STATUS

---

## Notes

This document reflects the current CNC UI implementation in the latest source version and may change as additional CNC functionality is added or refined.

    
## Software Compatibility 

RRF 3.5.2 onward


## Compatible Hardware

Duet3d PanelDue version:

v3-5.0

v3-7.0

v3-7.0c

5.0i

7.0i

## Firmware Downloads


### Latest builds (22092026_v2)

| PanelDue model | Download |
| --- | --- |
| **v3 5.0"** | [Download latest](last%20versions%20compiled/paneldue_MODERN_UI_v3_5_v22092026_2.bin) |
| **v3 7.0"** | [Download latest](last%20versions%20compiled/paneldue_MODERN_UI_v3_7_v22092026_2.bin) |
| **v3 7.0c** | [Download latest](last%20versions%20compiled/paneldue_MODERN_UI_v3_7_C_v22092026_2.bin) |
| **5.0i** | [Download latest](last%20versions%20compiled/paneldue_MODERN_UI_5_i_v22092026_2.bin) |
| **7.0i** | [Download latest](last%20versions%20compiled/paneldue_MODERN_UI_7_i_v22092026_2.bin) |



Instructions on how to update your panel due can be found on Duet 3D site
https://docs.duet3d.com/User_manual/RepRapFirmware/Updating_PanelDue

## Repository status

This project is a community-maintained fork. For the latest information about supported hardware, firmware compatibility, building, installation, and configuration, please check the repository documentation and open issues.
Also check Discord. Most of discussion is done there. ( https://discord.gg/mPFXxvBWT )
Firmware / Firmware-forum / NEW UI for old PANEL DUE on RRF 3.6.x


## Original project

- [Duet3D PanelDue Firmware](https://github.com/Duet3D/PanelDueFirmware)
- [Duet3D documentation](https://docs.duet3d.com/)

## License

Please refer to the original project and the license files in this repository for licensing information.
