// THE DEBUG PAGE OF THE RING (docs/controllers.md, "The debug page").
// main.cpp includes this file after the checks of the ring.
#pragma once

#include "../../src/mod/debug_menu_math.h"

namespace debug_menu_checks {

inline void DebugMenuChecks() {
  using namespace radial_math;
  using namespace debug_math;
  printf("\nThe debug page of the ring\n");

  // THE LISTS. Seven or fewer fit one page. A longer list has six on each
  // page, with "More" in slot 3, and no entry is on slot 4 ("Back").
  {
    Check(ListPages(0) == 1 && ListPages(7) == 1 && ListPages(8) == 2 &&
              ListPages(12) == 2 && ListPages(13) == 3,
          "a list of 7 has one page, 8 and 12 have two, 13 has three",
          ListPages(13), 3);
    bool no_back = true, unique = true;
    for (int n = 1; n <= 30; ++n) {
      bool used[8][kSlots] = {};
      for (int i = 0; i < n; ++i) {
        int page = 0, slot = 0;
        EntryPlace(n, i, &page, &slot);
        if (slot == kBackSlot || (n > 7 && slot == kMoreSlot)) no_back = false;
        if (page < 8) {
          if (used[page][slot]) unique = false;
          used[page][slot] = true;
        }
      }
    }
    Check(no_back, "no entry takes the slot of Back or of More", no_back, 1);
    Check(unique, "no two entries share a slot", unique, 1);
  }

  // THE SLOT OF THE ITEM "debug". The page 2 as made is full: "debug" takes
  // slot 5 (the help of the game). The ring of the Temple has an empty slot.
  {
    int page[kSlots], temple[kSlots];
    char unknown[16];
    ParseItems(kDefaultPage2, page, unknown, sizeof(unknown));
    ParseItems(kDefaultTemple, temple, unknown, sizeof(unknown));
    Check(DebugSlot(page) == 5 && page[5] == kItemHelp,
          "on the full page 2 the item debug takes slot 5, the help",
          DebugSlot(page), 5);
    Check(DebugSlot(temple) == 7, "in the Temple it takes the empty slot 7",
          DebugSlot(temple), 7);
    int more[kSlots];
    for (int i = 0; i < kSlots; ++i) more[i] = kItemPause;
    more[5] = kItemMore;
    Check(DebugSlot(more) == 6, "it never takes the slot of More",
          DebugSlot(more), 6);
    int words[kSlots];
    Check(ParseItems("temple,debug,back", words, unknown, sizeof(unknown)) ==
                  0 &&
              words[1] == kItemDebug && words[2] == kItemBack,
          "the ini knows the words debug and back", words[1], kItemDebug);
  }

  // THE PAGES. Two pages of the ini, and 20 switches: 9 in group 0, 0 in
  // group 1, 2 in group 2, and 3 that cannot change now.
  Pages p;
  memset(&p, 0, sizeof(p));
  char unknown[16];
  ParseItems(kDefaultPage1, p.items[0], unknown, sizeof(unknown));
  ParseItems(kDefaultPage2, p.items[1], unknown, sizeof(unknown));
  p.count = 2;
  p.items[1][5] = kItemDebug;
  IniLinks(&p);
  Check(p.links[0][4] == 1 && p.links[1][4] == 0 && p.links[1][5] == -1,
        "on the pages of the ini More goes to the other page", p.links[0][4],
        1);
  int group_of[20];
  for (int i = 0; i < 20; ++i) group_of[i] = -1;
  for (int i = 0; i < 9; ++i) group_of[i] = 0;
  group_of[10] = 2;
  group_of[15] = 2;
  int group_page[3];
  {
    const bool ok = AddDebugPages(&p, 1, 5, group_of, 20, 3, group_page);
    // The groups: one page. Group 0: two pages. Group 2: one page.
    Check(ok && p.count == 6, "the debug pages are pages 3 to 6", p.count, 6);
    Check(p.links[1][5] == 2, "the item debug goes to the page of the groups",
          p.links[1][5], 2);
    Check(p.items[2][0] == kItemGroupBase + 0 &&
              p.items[2][1] == kItemGroupBase + 2 &&
              p.items[2][2] == kItemNone,
          "the page of the groups has the groups with a switch, in order",
          p.items[2][1], kItemGroupBase + 2);
    Check(group_page[0] == 3 && group_page[1] == -1 && group_page[2] == 5 &&
              p.links[2][0] == 3 && p.links[2][1] == 5,
          "each group goes to its first page, and a group with no switch is "
          "not there", group_page[2], 5);
    Check(p.items[2][kBackSlot] == kItemBack && p.links[2][kBackSlot] == 1,
          "Back on the page of the groups goes to the page with debug",
          p.links[2][kBackSlot], 1);
    Check(p.items[3][0] == kItemSwitchBase + 0 &&
              p.items[3][7] == kItemSwitchBase + 5 &&
              p.items[4][0] == kItemSwitchBase + 6 &&
              p.items[4][2] == kItemSwitchBase + 8 &&
              p.items[4][5] == kItemNone,
          "the 9 switches of group 0 are 6 on its first page and 3 on its "
          "second", p.items[4][2], kItemSwitchBase + 8);
    Check(p.items[3][kMoreSlot] == kItemMore && p.links[3][kMoreSlot] == 4 &&
              p.links[4][kMoreSlot] == 3,
          "More goes to the next page of the group, and back to its first",
          p.links[4][kMoreSlot], 3);
    Check(p.links[3][kBackSlot] == 2 && p.links[5][kBackSlot] == 2,
          "Back on a page of a group goes to the page of the groups",
          p.links[5][kBackSlot], 2);
    Check(p.items[5][0] == kItemSwitchBase + 10 &&
              p.items[5][1] == kItemSwitchBase + 15 &&
              p.items[5][kMoreSlot] == kItemNone,
          "a group of 2 has one page with no More", p.items[5][1],
          kItemSwitchBase + 15);
    bool hidden = true;
    for (int page = 2; page < p.count; ++page) {
      for (int s = 0; s < kSlots; ++s) {
        const int item = p.items[page][s];
        if (item == kItemSwitchBase + 9 || item == kItemSwitchBase + 19) {
          hidden = false;
        }
      }
    }
    Check(hidden, "a switch that cannot change now is not on a page", hidden,
          1);
  }
  // Again, with all 20 in one group: the pages are made the same way.
  {
    p.count = 2;
    for (int i = 0; i < 20; ++i) group_of[i] = 1;
    AddDebugPages(&p, 1, 5, group_of, 20, 3, group_page);
    Check(p.count == 2 + 1 + 4 && group_page[1] == 3,
          "a second build replaces the debug pages of the first", p.count, 7);
    int too_many[400];
    for (int i = 0; i < 400; ++i) too_many[i] = i % 3;
    p.count = 2;
    const bool ok = AddDebugPages(&p, 1, 5, too_many, 400, 3, group_page);
    Check(!ok && p.count == 2, "pages that do not fit change nothing",
          p.count, 2);
  }

  // THE WORDS OF A VALUE.
  {
    wchar_t word[16];
    Check(ValueWord(L"off|on", 1, word, 16) == 2 && wcscmp(word, L"on") == 0,
          "value 1 of off|on is on", 1, 1);
    Check(ValueWord(L"off|on", 0, word, 16) == 3 && wcscmp(word, L"off") == 0,
          "value 0 of off|on is off", 1, 1);
    Check(ValueWord(L"off|on", 2, word, 16) == 0 && word[0] == 0,
          "a value past the words has no word: the page shows the number", 1,
          1);
    Check(ValueWord(L"", 0, word, 16) == 0 && ValueWord(L"a|b", -1, word, 16) == 0,
          "no words, and a value below 0, give no word", 1, 1);
  }

  // THE RING WITH THE LINKS. Open, go to the item debug on page 2, wait on
  // it, go to the first group, and choose the first switch.
  {
    p.count = 2;
    for (int i = 0; i < 20; ++i) group_of[i] = i < 9 ? 0 : -1;
    AddDebugPages(&p, 1, 5, group_of, 20, 3, group_page);
    const Settings set = DefaultSettings();
    State st;
    Reset(&st);
    Events ev;
    Input in;
    memset(&in, 0, sizeof(in));
    in.can_open = true;
    in.have_hand = true;
    st.open_page = 1;
    in.button = true;
    in.time = 1.0;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(ev.opened && st.page == 1, "the ring opens on the page the caller "
          "gives", st.page, 1);
    // Slot 5 is down and to the left.
    in.hand[0] = -0.05f;
    in.hand[1] = -0.05f;
    in.time = 1.1;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(st.slot == 5 && p.items[1][5] == kItemDebug,
          "the hand picks the item debug", st.slot, 5);
    in.time = 1.6;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(ev.paged && st.page == 2, "after the dwell the ring shows the page "
          "of the groups", st.page, 2);
    // Up: the first group.
    in.hand[0] = 0.0f;
    in.hand[1] = 0.07f;
    in.time = 1.7;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    in.time = 2.2;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(ev.paged && st.page == 3, "a group goes to its first page after "
          "the dwell", st.page, 3);
    Check(st.slot == -1 && st.dead_slot == 0,
          "the slot of the group is dead on the new page", st.dead_slot, 0);
    in.time = 2.3;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(st.slot == -1, "so the switch under the hand is not picked yet",
          st.slot, -1);
    in.hand[1] = 0.0f;
    in.time = 2.4;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    in.hand[1] = 0.07f;
    in.time = 2.5;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(st.slot == 0, "after the middle the hand picks the first switch",
          st.slot, 0);
    in.button = false;
    in.time = 2.6;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(ev.chosen == 0 && ev.chosen_page == 3 &&
              p.items[3][0] == kItemSwitchBase + 0,
          "the release chooses the switch", ev.chosen, 0);

    // The stick: the trigger on Back goes to the page of the groups at once.
    st.open_page = 3;
    in.stick[0] = 0.0f;
    in.stick[1] = -0.9f;
    in.time = 3.0;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(ev.opened && st.open == kOpenStick && st.page == 3 &&
              st.slot == kBackSlot,
          "the stick opens the ring on the page of the group and points at "
          "Back", st.slot, kBackSlot);
    in.trigger = true;
    in.time = 3.1;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(ev.paged && st.page == 2 && ev.chosen == -1,
          "the trigger on Back goes to the page of the groups, with no "
          "choice", st.page, 2);
    in.trigger = false;
    in.time = 3.2;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    in.trigger = true;
    in.stick[1] = 0.0f;
    in.stick[0] = 0.0f;
    in.time = 3.3;
    Step(&st, set, in, p.items, p.links, p.count, &ev);
    Check(st.page == 2 && ev.chosen == -1,
          "a trigger with the stick in the middle chooses nothing", st.page,
          2);
    in.trigger = false;
  }
}

}  // namespace debug_menu_checks
