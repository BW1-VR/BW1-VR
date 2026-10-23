// The debug page of the ring: the switches of the test keys, as items that
// the player chooses with the hand while he plays. See debug_menu.cpp,
// docs/controllers.md, "The debug page", and docs/backend.md, "Keys for
// v1.0".
//
// A SWITCH has two functions in the file of its area:
//
//   int  <Name>(void)               the value now, or kDebugCannot when the
//                                   switch cannot change now (its part is
//                                   off, or it is for the other renderer)
//   void <Name>(const char* by)     the change of the test key: the same
//                                   step, the same pulse and the same log
//                                   line. `by` names what asked for it: the
//                                   keys, or "the debug page"
//
// The test key calls the same change function. One line of the table in
// debug_menu.cpp (kSwitches) puts a switch on the page.

#pragma once

// The value of a switch that cannot change now.
const int kDebugCannot = -1000;

// True with test_keys=1 or debug_menu=1. A part that a switch needs, and
// that the mod puts in only for a test, is in with either key. The keys of
// the keyboard still need test_keys=1.
bool TestSwitchesOn();

// True with debug_menu=1: the ring has the item "debug".
bool DebugMenuOn();

// The words of the cause of a change from the page.
const char* const kDebugBy = "the debug page";

// --- for the ring (radial_menu.cpp) ----------------------------------------------

// The groups of the switches, for the first page: View, Picture, Render ...
int DebugGroupCount();
const wchar_t* DebugGroupName(int group);

// The switches of the table, by their line.
int DebugSwitchCount();
int DebugSwitchGroup(int i);
const wchar_t* DebugSwitchLabel(int i);   // the badge, at most 14 letters
const wchar_t* DebugSwitchName(int i);    // the card in the middle
const char* DebugSwitchKey(int i);        // the ini key, or ""
// The value now, or kDebugCannot.
int DebugSwitchValue(int i);
// The words of a value: "on", or the number, or "-" for kDebugCannot.
void DebugSwitchValueText(int i, int value, wchar_t* out, int size);
// The player chose switch i on the page: its change function runs, and the
// log has the switch and its new value. False when it cannot change now.
bool DebugSwitchChoose(int i);

// One log line at the start with debug_menu=1.
void DebugMenuLogStart();
