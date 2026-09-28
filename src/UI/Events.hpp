/*
 * Events.hpp
 *
 *  Created on: 6 Jan 2017
 *      Author: David
 */

#ifndef SRC_UI_EVENTS_HPP_
#define SRC_UI_EVENTS_HPP_

// Event numbers, used to say what we need to do when a field is touched
// *** MUST leave value 0 free to mean "no event"
enum Event : uint8_t
{
	evNull = 0,                        // value must match nullEvent declared in Display.hpp

	evDefaultRoot, evScreensaverRoot,

	// Page selection
	evTabControl, evTabStatus, evTabSystem, evTabMsg, evTabSetup,

	// Heater control
	evSelectHead, evSelectBed, evSelectChamber,
	evAdjustToolActiveTemp, evAdjustToolStandbyTemp,
	evAdjustBedActiveTemp, evAdjustBedStandbyTemp,
	evAdjustChamberActiveTemp, evAdjustChamberStandbyTemp,

	// Spindle control
	evAdjustActiveRPM,

	// Control functions
	evMovePopup, evExtrudePopup, evFan, evListMacros,
	evMoveAxis,
	evMoveSelectAxis,
	evExtrudeAmount, evExtrudeRate, evExtrude, evRetract,
	evHomeAxis,

	// Print functions
	evExtrusionFactor,
	evAdjustFan,
	evAdjustInt,
	evSetInt,
	evListFiles,

	evFile, evMacro, evMacroControlPage,
	evPrintFile,
	evSendCommand,
	evFactoryReset,
	evAdjustSpeed,

	evScrollFiles, evScrollMacros, evFilesUp, evMacrosUp, evChangeCard,

	evKeyboard,

	// Setup functions
	evCalTouch, evSetBaudRate, evInvertX, evInvertY, evAdjustBaudRate, evSetVolume, evAdjustVolume, evSetInfoTimeout, evAdjustInfoTimeout, evReset,

	evYes,
	evCancel,
	evDeleteFile,
	evSimulateFile,
	evPausePrint,
	evResumePrint,
	evReprint, evResimulate,
	evBabyStepPopup, evBabyStepMinus, evBabyStepPlus,

	evKey, evShift, evBackspace, evSendKeyboardCommand, evUp, evDown,

	evAdjustColours, evSetColours,
	evBrighter, evDimmer,
	evSetDimmingType,
	evSetScreensaverTimeout, evAdjustScreensaverTimeout,
	evSetBabystepAmount, evAdjustBabystepAmount,
	evSetFeedrate, evAdjustFeedrate,
	evSetHeaterCombineType,
	evSetLogLevel,

	evEmergencyStop,

	evJogZ,
	evCloseAlert, evOkAlert, evChoiceAlert, evEditAlert,

	// Subpage events for the vertical tabs
	// CONTROL subpages
	evControlTools,
	evControlMovement,
	evControlExtrusion,
	evControlMacros,
	evControlToolsPageUp,
	evControlToolsPageDown,
	evControlToolsPower,
	evControlToolsActiveTemp,
	evControlToolsStandbyTemp,
	evControlToolsHeaderTap,
	evControlToolChangeConfirm,
	evControlToolChangeCancel,
	evControlHeaterOffConfirm,
	evControlHeaterOffCancel,
	evControlMoveStep,
	evControlMoveJog,
	evControlMoveHome,
	evControlMoveBedComp,
	evModernAlertClose,
	evModernInfoClose,
	evControlExtrudeSpeed,
	evControlExtrudeDistance,
	evControlExtrudeAction,
	evControlExtrudePageUp,
	evControlExtrudePageDown,

	// STATUS subpages
	evStatusJobStatus,
	evStatusTune,
	evStatusJob,
	evStatusObjects,
	evStatusObject1,
	evStatusObject2,
	evStatusObject3,
	evStatusObject4,
	evStatusObject5,
	evStatusObject6,
	evStatusObjectPageUp,
	evStatusObjectPageDown,
	evStatusObjectSelect,
	evStatusObjectNumber,
	evStatusObjectMarker,
	evStatusObjectCancelConfirm,
	evStatusObjectCancelClose,

	// STATUS > TUNE controls
	evTuneSpeed,
	evTuneGeneralFan,
	evTuneToolFan,
	evTuneToolFlow,
	evTunePressureAdvance,
	evTunePageUp,
	evTunePageDown,
	evTuneZMinus,
	evTuneZPlus,
	evTunePopupAdjustPercent,
	evTunePopupAdjustPa,
	evTunePopupConfirm,
	evTunePopupCancel,

	// CONTROL > MACROS controls
	evControlMacroFile,
	evControlMacroPageUp,
	evControlMacroPageDown,
	evControlMacroRunConfirm,
	evControlMacroRunCancel,

	// STATUS > JOB controls
	evStatusJobFile,
	evStatusJobPageUp,
	evStatusJobPageDown,
	evStatusJobPrintConfirm,
	evStatusJobPrintCancel,
	evStatusJobDeleteOpen,
	evStatusJobDeleteConfirm,
	evStatusJobDeleteCancel,

	// STATUS > JOB STATUS controls
	evStatusJobStatusPauseResume,
	evStatusJobStatusAbort,
	evStatusJobStatusConfirm,
	evStatusJobStatusCancel,

	// SYSTEM subpages
	evSystemConsole,
	evSystemSettings,

	// SYSTEM > SETTINGS modern controls
	evSettingsVolumeOpen, evSettingsVolumeSelect, evSettingsVolumeConfirm,
	evSettingsBrightnessOpen, evSettingsBrightnessSelect, evSettingsBrightnessConfirm,
	evSettingsInfoTimeoutOpen, evSettingsInfoTimeoutSelect, evSettingsInfoTimeoutConfirm,
	evSettingsAccentOpen, evSettingsAccentSelect, evSettingsAccentConfirm,
	evSettingsAlwaysDimToggle,
	evSettingsBaudOpen, evSettingsBaudSelect, evSettingsBaudConfirm,
	evSettingsTouchOpen, evSettingsTouchConfirm,
	evSettingsHeaterCombineOpen, evSettingsHeaterCombineConfirm,
	evSettingsFactoryResetOpen, evSettingsFactoryResetConfirm,
	evSettingsPopupCancel,

	// Extrusion length numeric trigger
	evAdjustExtrudeLength,

	// Numeric pad events (for full numeric input popup)
	evNumericKey,     // integer iParam = ASCII code of digit or '.'; used while numeric pad is active
	evNumericBack,    // backspace key on numeric pad
	evNumericOk,      // confirm numeric pad value
	evNumericCancel,  // cancel numeric pad

	// Shared reusable standard popup events. Appended here so existing event values stay stable.
	evStandardPopupChoice,
	evStandardPopupConfirm,
	evStandardPopupCancel,

	// Filasnake (SYSTEM > SETTINGS easter egg). Appended so existing event values stay stable.
	evFilasnakeOpen,
	evFilasnakeDir,		// iParam = direction 0 up, 1 right, 2 down, 3 left
	evFilasnakeGo,

	// CNC portrait UI. Appended so existing event values stay stable.
	evCncNav,			// iParam = master tab index 0..4 (CONTROL, WCS, JOB, MACROS, SYSTEM)
	evCncAxis,			// CONTROL: select jog axis, iParam = axis index
	evCncStep,			// CONTROL: select step distance, iParam = step index
	evCncMove,			// CONTROL: jog by one step, iParam = -1 or +1
	evCncFeedMode,		// CONTROL: RAPID | SLOW toggle
	evCncSafeZ,			// CONTROL: run safe_z.g
	evCncXY0,			// CONTROL: run goto_xy0.g
	evCncCustom,		// CONTROL: custom macro button, iParam = slot 0 / 1
	evCncAux,			// CONTROL: aux (coolant) on/off toggle
	evCncHome,			// CONTROL: home, iParam = axis index, or -1 for all
	evCncZero,			// DRO zero button, iParam = axis index
	evCncSubTab,		// sub-tab strip, iParam = sub-tab index
	evCncWcsSelect,		// WCS > OFFSETS: view G54..G59, iParam = 0..5
	evCncWcsActivate,	// WCS > OFFSETS: make the viewed WCS active
	evCncWcsSave,		// WCS > OFFSETS: M500
	evCncWcsApplyOffset,	// WCS > OFFSETS: fold the offset step (RRF babystep) into the active WCS Z
	evCncOffsetStep,		// offset step Z+/Z- (M290), iParam = +1 / -1
	evCncOffsetClear,		// offset step back to 0
	evCncOffsetStepOpen,	// open the offset step size popup
	evCncNumPos,		// standard numpad POS key: fill in the current position
	evCncFormItem,		// standard popup form button, iParam = group * 16 + item
	evCncDroSpindle,	// DRO S tile: spindle speed numpad
	evCncDroTool,		// DRO T tile: spindle on (set speed) / standby (M5)
	evCncDroFeed,		// DRO F tile: jog feed numpad
	evCncOffsetEdit,	// WCS > OFFSETS: edit an offset cell, iParam = axis index
	evCncWcsReset,		// WCS > OFFSETS: set the viewed WCS offsets to 0
	evCncWcsCopy,		// WCS > OFFSETS: COPY TO popup
	evCncWcsShift,		// WCS > OFFSETS: SHIFT (axes, then amount on the numpad)
	evCncToolRow,		// WCS > TOOLS: select a row, iParam = row 0..3
	evCncToolZ,			// WCS > TOOLS: Z OFFSET tile, select the row + SET Z numpad, iParam = row
	evCncToolScroll,	// WCS > TOOLS: scroll, iParam = -1 up / +1 down
	evCncToolLoad,		// WCS > TOOLS: load the selected tool (T#)
	evCncToolMeasure,	// WCS > TOOLS: measure the loaded tool on the tool setter
	evCncToolSetZ,		// WCS > TOOLS: numpad for the selected tool's Z offset
	evCncProbeOrigin,	// WCS > PROBE: pick an origin, iParam = CncOrigin
	evCncProbeParam,	// WCS > PROBE: parameter tile, iParam = parameter id
	evCncProbeRun,		// WCS > PROBE: RUN PROBE / START
	evCncProbeJogAxis,	// probe / M291 jog popup: axis, iParam = axis index
	evCncProbeJogStep,	// probe / M291 jog popup: step, iParam = step index
	evCncProbeJogMove,	// probe / M291 jog popup: move, iParam = -1 / +1
	evCncProbeJogOk,	// probe / M291 jog popup: PROBE / READ / OK (M292 P0)
	evCncProbeJogCancel,// probe / M291 jog popup: X (M292 P1)
	evCncJobOvrMinus,	// JOB STATUS: override - one step, iParam = 0 feed / 1 spindle
	evCncJobOvrPlus,	// JOB STATUS: override + one step, iParam = 0 feed / 1 spindle
	evCncJobOvrValue,	// JOB STATUS: override value tile, numpad, iParam = 0 feed / 1 spindle
	evCncJobOvrStep,	// JOB STATUS: override step 1/5/10 %, iParam = row * 4 + step index
	evCncJobAux,		// JOB STATUS: COOLANT / VACUUM toggle
	evCncJobCustom,		// JOB STATUS: CUSTOM 3 / 4, iParam = slot 2 / 3
	evCncJobToolOk,		// JOB STATUS: OK in the inline tool change prompt (M292 P0)
	evCncJobPause,		// JOB STATUS: PAUSE / RESUME
	evCncJobAbort,		// JOB STATUS: ABORT
	// Only 256 events fit in event_t: JOB LIST and the keyboard reuse printer UI events that the
	// CNC UI never creates otherwise (evFile row, evScrollFiles, evKeyboard SEARCH, evChangeCard SD,
	// evKey, evShift, evBackspace, evUp arrows, evSendKeyboardCommand enter).
	evCncKbMode,		// keyboard: #+= / ABC
	evCncKbClose,		// keyboard: red X

};

#endif /* SRC_UI_EVENTS_HPP_ */
