// THE RING (docs/controllers.md, "The ring").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/radial_menu_math.h"

namespace ring_checks {

inline void RingChecks() {
  using namespace radial_math;
  printf("\nThe ring\n");

  // THE ITEMS. Each one has a word for the ini, a word for the ring and a
  // name for the card, and no two share the word of the ini.
  {
    bool words = true, unique = true, fit = true;
    for (int i = 1; i < kItems; ++i) {
      const ItemInfo& a = kItemInfo[i];
      if (a.id == nullptr || a.id[0] == 0 || a.word[0] == 0 || a.name[0] == 0) {
        words = false;
      }
      // A word of 10 letters fits the badge, and a name of 23 fits the card.
      if (wcslen(a.word) > 10 || wcslen(a.name) > 23) fit = false;
      for (int k = i + 1; k < kItems; ++k) {
        if (strcmp(a.id, kItemInfo[k].id) == 0) unique = false;
      }
    }
    Check(words, "each item has its three words", words, 1);
    Check(unique, "no two items share the word of the ini", unique, 1);
    Check(fit, "each word fits its badge and each name fits the card", fit, 1);
    Check(kItemInfo[kItemTemple].presses == 2,
          "the Temple key goes out two times: the first press only pulls the "
          "camera back", kItemInfo[kItemTemple].presses, 2);
    Check(kItemInfo[kItemTemple].code == 0x11 &&
              kItemInfo[kItemEnter].code == 0x14 &&
              kItemInfo[kItemCreature].code == 0x12 &&
              kItemInfo[kItemLand].code == 0x13,
          "the Temple, the way in, the Creature and the land are the actions "
          "0x11, 0x14, 0x12 and 0x13", 1, 1);
    Check(kItemInfo[kItemRoomCreature].code == 0x15 &&
              kItemInfo[kItemRoomChallenge].code == 0x16 &&
              kItemInfo[kItemRoomSave].code == 0x17 &&
              kItemInfo[kItemRoomOptions].code == 0x18 &&
              kItemInfo[kItemRoomLibrary].code == 0x19,
          "the five rooms are the actions 0x15 to 0x19", 1, 1);
    Check(kItemInfo[kItemTown].code == 0x0F && kItemInfo[kItemTown].mods == 0 &&
              kItemInfo[kItemMarkGo].code == 0x0F &&
              kItemInfo[kItemMarkGo].mods == kModCtrl,
          "the next town is Tab, and the next bookmark is Ctrl and Tab", 1, 1);
    Check(kItemInfo[kItemSlower].code == 2 &&
              kItemInfo[kItemSlower].mods == kModAlt &&
              kItemInfo[kItemFaster].code == 3 &&
              kItemInfo[kItemFaster].mods == kModAlt,
          "the game speed is Alt with the keys 1 and 2", 1, 1);
    Check(kItemInfo[kItemPause].code == 0x19 && kItemInfo[kItemLeave].code == 1,
          "the pause is P and the way out of the Temple is Esc", 1, 1);
  }

  // THE LISTS OF THE INI.
  {
    int items[kSlots];
    char unknown[16];
    int bad = ParseItems(kDefaultPage1, items, unknown, sizeof(unknown));
    Check(bad == 0, "each word of the first page as made is known", bad, 0);
    Check(items[0] == kItemTemple && items[1] == kItemCreature &&
              items[4] == kItemMore && items[7] == kItemEnter,
          "the first page has the Temple at the top, More at the bottom and "
          "the way in at the upper left", 1, 1);
    bad = ParseItems(kDefaultPage2, items, unknown, sizeof(unknown));
    Check(bad == 0 && items[4] == kItemMore,
          "the second page is known too, with More in the same slot", bad, 0);
    bad = ParseItems(kDefaultTemple, items, unknown, sizeof(unknown));
    Check(bad == 0 && items[4] == kItemLeave && items[7] == kItemNone,
          "the ring of the Temple has the way out at the bottom", bad, 0);
    bad = ParseItems(" pause , ,help,nonsense,,", items, unknown,
                     sizeof(unknown));
    Check(bad == 1 && strcmp(unknown, "nonsense") == 0,
          "a word that is not known is counted and named", bad, 1);
    Check(items[0] == kItemPause && items[1] == kItemNone &&
              items[2] == kItemHelp && items[3] == kItemNone &&
              items[5] == kItemNone && items[7] == kItemNone,
          "blanks are ignored, and an empty word, an unknown word and the "
          "slots past the end are empty", 1, 1);
    bad = ParseItems("a,b,c,d,e,f,g,h,temple", items, unknown, sizeof(unknown));
    Check(bad == 8 && items[7] == kItemNone,
          "a ninth word is not read", bad, 8);
    bad = ParseItems(nullptr, items, unknown, sizeof(unknown));
    Check(bad == 0 && !PageHasItems(items), "no list gives an empty page", bad,
          0);
    ParseItems("more", items, unknown, sizeof(unknown));
    Check(!PageHasItems(items), "a page with only More counts as empty", 0, 0);
  }

  // THE DIRECTION.
  {
    Check(SlotOf(0.0f, 1.0f) == 0, "up is slot 0", SlotOf(0.0f, 1.0f), 0);
    Check(SlotOf(1.0f, 1.0f) == 1, "up and to the right is slot 1",
          SlotOf(1.0f, 1.0f), 1);
    Check(SlotOf(1.0f, 0.0f) == 2, "to the right is slot 2",
          SlotOf(1.0f, 0.0f), 2);
    Check(SlotOf(0.0f, -1.0f) == 4, "down is slot 4", SlotOf(0.0f, -1.0f), 4);
    Check(SlotOf(-1.0f, 0.0f) == 6, "to the left is slot 6",
          SlotOf(-1.0f, 0.0f), 6);
    Check(SlotOf(-1.0f, 1.0f) == 7, "up and to the left is slot 7",
          SlotOf(-1.0f, 1.0f), 7);
    bool all = true;
    for (int deg = 0; deg < 360; ++deg) {
      const float t = deg * kPi / 180.0f;
      const int want = ((deg + 22) / 45) % 8;
      if (deg % 45 == 22 || deg % 45 == 23) continue;  // on a border
      if (SlotOf(sinf(t), cosf(t)) != want) all = false;
    }
    Check(all, "each degree of the circle gives the slot of its 45 degrees",
          all, 1);
    Near("the middle of slot 2 is 0 degrees from slot 2",
         DegreesFromSlot(1.0f, 0.0f, 2), 0.0, 1e-3);
    Near("and 90 degrees from slot 0", DegreesFromSlot(1.0f, 0.0f, 0), 90.0,
         1e-3);
    Near("slot 7 is 45 degrees from slot 0, across the top",
         DegreesFromSlot(-1.0f, 1.0f, 0), 45.0, 1e-3);
  }

  // THE PICK.
  {
    Check(Pick(0.0f, 0.04f, -1, 0.05f, 0.035f, 6.0f) == -1,
          "a hand 4 cm from the middle picks nothing", 0, 0);
    Check(Pick(0.0f, 0.05f, -1, 0.05f, 0.035f, 6.0f) == 0,
          "a hand 5 cm up picks the top item", 0, 0);
    Check(Pick(0.0f, 0.04f, 0, 0.05f, 0.035f, 6.0f) == 0,
          "the pick stays while the hand comes back to 4 cm", 0, 0);
    Check(Pick(0.0f, 0.03f, 0, 0.05f, 0.035f, 6.0f) == -1,
          "and it goes at 3 cm", 0, 0);
    // 26 degrees from the top is past the border of slot 0 (22.5), and inside
    // the margin (28.5).
    const float a = 26.0f * kPi / 180.0f;
    Check(Pick(0.1f * sinf(a), 0.1f * cosf(a), -1, 0.05f, 0.035f, 6.0f) == 1,
          "with no pick, 26 degrees from the top is the next slot", 0, 0);
    Check(Pick(0.1f * sinf(a), 0.1f * cosf(a), 0, 0.05f, 0.035f, 6.0f) == 0,
          "with the top picked, 26 degrees still is the top", 0, 0);
    const float b = 30.0f * kPi / 180.0f;
    Check(Pick(0.1f * sinf(b), 0.1f * cosf(b), 0, 0.05f, 0.035f, 6.0f) == 1,
          "and 30 degrees is the next slot", 0, 0);
    // A hand that shakes 3 degrees about a border never flips the pick.
    int flips = 0, pick = 0;
    for (int i = 0; i < 200; ++i) {
      const float t = (22.5f + ((i & 1) ? 3.0f : -3.0f)) * kPi / 180.0f;
      const int next = Pick(0.08f * sinf(t), 0.08f * cosf(t), pick, 0.05f,
                            0.035f, 6.0f);
      if (next != pick) ++flips;
      pick = next;
    }
    Check(flips == 0, "a hand that shakes 3 degrees at a border does not flip "
                      "the choice", flips, 0);
  }

  // THE PLANE. The ring faces the head, and the hand moves over it.
  {
    const float hand[3] = {-0.20f, 1.00f, -0.40f};
    const float head[3] = {0.0f, 1.50f, 0.0f};
    Plane plane;
    PlaneAt(hand, head, &plane);
    const float z[3] = {0.0f, 0.0f, 1.0f};
    float front[3];
    controller_math::QuatRotate(plane.orientation, z, front);
    const float to_head[3] = {head[0] - hand[0], head[1] - hand[1],
                              head[2] - hand[2]};
    const float length = sqrtf(to_head[0] * to_head[0] +
                               to_head[1] * to_head[1] +
                               to_head[2] * to_head[2]);
    Near("the front of the ring points at the head",
         (front[0] * to_head[0] + front[1] * to_head[1] +
          front[2] * to_head[2]) / length, 1.0, 1e-5);
    Near("the ring has no roll: its right is level", plane.right[1], 0.0, 1e-6);
    Check(plane.up[1] > 0.5f, "its up is up", plane.up[1], 1);
    float on[2];
    OnPlane(plane, hand, on);
    Near("the hand starts in the middle (x)", on[0], 0.0, 1e-6);
    Near("the hand starts in the middle (y)", on[1], 0.0, 1e-6);
    const float moved[3] = {hand[0] + plane.right[0] * 0.07f,
                            hand[1] + plane.right[1] * 0.07f,
                            hand[2] + plane.right[2] * 0.07f};
    OnPlane(plane, moved, on);
    Near("7 cm along the right of the ring is 7 cm to the right", on[0], 0.07,
         1e-6);
    Near("and 0 up", on[1], 0.0, 1e-6);
    // The player sees +x of the ring on the right: with the head behind the
    // hand (the hand ahead, at -z), right is +x of the room.
    const float hand2[3] = {0.0f, 1.2f, -0.5f};
    const float head2[3] = {0.0f, 1.2f, 0.0f};
    PlaneAt(hand2, head2, &plane);
    Near("for a hand straight ahead, the right of the ring is +x of the room",
         plane.right[0], 1.0, 1e-5);
    const float toward[3] = {0.0f, 1.2f, -0.4f};
    OnPlane(plane, toward, on);
    Near("a move toward the head does not move the hand on the ring (x)",
         on[0], 0.0, 1e-6);
    Near("a move toward the head does not move the hand on the ring (y)",
         on[1], 0.0, 1e-6);
    float x = 0.0f, y = 0.0f;
    SlotPixel(0, 512.0f, 352.0f, &x, &y);
    Near("the top item is above the middle of the picture (x)", x, 512.0, 1e-3);
    Near("the top item is above the middle of the picture (y)", y, 160.0, 1e-3);
    SlotPixel(2, 512.0f, 352.0f, &x, &y);
    Near("the right item is right of the middle (x)", x, 864.0, 1e-3);
    Near("the right item is right of the middle (y)", y, 512.0, 1e-3);
  }

  // THE RING WITH THE BUTTON AND THE HAND.
  int pages[kPages][kSlots];
  char unknown[16];
  ParseItems(kDefaultPage1, pages[0], unknown, sizeof(unknown));
  ParseItems(kDefaultPage2, pages[1], unknown, sizeof(unknown));
  const Settings set = DefaultSettings();
  {
    State st;
    Reset(&st);
    Events ev;
    Input in;
    memset(&in, 0, sizeof(in));
    in.can_open = true;
    in.have_hand = true;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.open == kClosed && !ev.opened, "nothing opens with no input",
          st.open, 0);
    in.button = true;
    in.time = 1.0;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.opened && st.open == kOpenButton && st.slot == -1,
          "the button opens the ring, with the choice in the middle",
          st.open, kOpenButton);
    in.time = 1.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(!ev.opened && !ev.moved && st.open == kOpenButton,
          "a held button keeps it open, and nothing moves", st.open,
          kOpenButton);
    in.hand[0] = 0.07f;  // to the right: the next town
    in.time = 1.2;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.moved && st.slot == 2, "the hand 7 cm to the right picks slot 2",
          st.slot, 2);
    in.button = false;
    in.time = 1.3;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.closed && ev.chosen == 2 && ev.chosen_page == 0 &&
              pages[0][ev.chosen] == kItemTown,
          "the release chooses it: the next town", ev.chosen, 2);
    Check(st.open == kClosed, "and the ring is closed", st.open, kClosed);

    // A release in the middle chooses nothing.
    in.button = true;
    in.hand[0] = 0.0f;
    in.time = 2.0;
    Step(&st, set, in, pages, 2, &ev);
    in.hand[1] = 0.02f;
    in.time = 2.1;
    Step(&st, set, in, pages, 2, &ev);
    in.button = false;
    in.time = 2.2;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.closed && ev.chosen == -1,
          "a release with the hand 2 cm from the middle chooses nothing",
          ev.chosen, -1);

    // The hand goes to an item and back to the middle.
    in.button = true;
    in.hand[0] = in.hand[1] = 0.0f;
    in.time = 3.0;
    Step(&st, set, in, pages, 2, &ev);
    in.hand[1] = 0.08f;
    in.time = 3.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.slot == 0, "the hand 8 cm up picks the Temple", st.slot, 0);
    in.hand[1] = 0.01f;
    in.time = 3.2;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.moved && st.slot == -1, "back in the middle, the pick goes",
          st.slot, -1);
    in.button = false;
    in.time = 3.3;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.chosen == -1, "and the release chooses nothing", ev.chosen, -1);

    // More: a release on it chooses nothing, and a dwell changes the page.
    in.button = true;
    in.hand[0] = in.hand[1] = 0.0f;
    in.time = 4.0;
    Step(&st, set, in, pages, 2, &ev);
    in.hand[1] = -0.08f;
    in.time = 4.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.slot == 4 && pages[0][4] == kItemMore, "the hand down picks More",
          st.slot, 4);
    in.time = 4.3;
    Step(&st, set, in, pages, 2, &ev);
    Check(!ev.paged && st.page == 0, "0.2 s on More does not change the page",
          st.page, 0);
    in.time = 4.1 + set.dwell + 0.01;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.paged && st.page == 1, "the dwell on More changes the page",
          st.page, 1);
    Check(st.slot == -1 && st.dead_slot == 4,
          "and the slot of More is not live while the hand stays in it",
          st.dead_slot, 4);
    in.time = 6.0;
    Step(&st, set, in, pages, 2, &ev);
    Check(!ev.paged && st.page == 1,
          "so the page does not change back by itself", st.page, 1);
    in.hand[0] = 0.08f;  // to the right on page 2: back
    in.hand[1] = 0.0f;
    in.time = 6.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.slot == 2 && st.dead_slot == -1,
          "the hand goes to the right, and that item is live", st.slot, 2);
    in.button = false;
    in.time = 6.2;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.chosen == 2 && ev.chosen_page == 1 &&
              pages[ev.chosen_page][ev.chosen] == kItemMarkBack,
          "and the release chooses the item of page 2", ev.chosen_page, 1);

    // The next open starts on page 1 again.
    in.button = true;
    in.hand[0] = 0.0f;
    in.time = 7.0;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.opened && st.page == 0, "the next open starts on page 1", st.page,
          0);
    in.hand[1] = -0.08f;
    in.time = 7.1;
    Step(&st, set, in, pages, 2, &ev);
    in.button = false;
    in.time = 7.2;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.closed && ev.chosen == -1,
          "a release on More before the dwell chooses nothing", ev.chosen, -1);

    // The ring cannot open.
    in.can_open = false;
    in.button = true;
    in.hand[1] = 0.0f;
    in.time = 8.0;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.blocked && st.open == kClosed,
          "a press while the ring cannot open is refused", ev.blocked, 1);
    in.can_open = true;
    in.time = 8.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(!ev.opened, "and the same press does not open it later", ev.opened,
          0);
    in.button = false;
    Step(&st, set, in, pages, 2, &ev);
    in.button = true;
    in.time = 8.3;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.opened, "a new press opens it", ev.opened, 1);
    in.hand[1] = 0.08f;
    in.time = 8.4;
    Step(&st, set, in, pages, 2, &ev);
    in.can_open = false;  // for example the hand lost its pose
    in.time = 8.5;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.closed && ev.chosen == -1 && st.open == kClosed,
          "a ring that cannot stay open closes with no choice", ev.chosen, -1);
    in.can_open = true;
    in.button = false;
    Step(&st, set, in, pages, 2, &ev);

    // An empty slot cannot be picked.
    int one[kPages][kSlots];
    ParseItems("temple", one[0], unknown, sizeof(unknown));
    ParseItems("", one[1], unknown, sizeof(unknown));
    in.button = true;
    in.hand[0] = in.hand[1] = 0.0f;
    in.time = 9.0;
    Step(&st, set, in, one, 1, &ev);
    in.hand[0] = 0.08f;
    in.time = 9.1;
    Step(&st, set, in, one, 1, &ev);
    Check(st.slot == -1, "the hand over an empty slot picks nothing", st.slot,
          -1);
    in.button = false;
    Step(&st, set, in, one, 1, &ev);
    Check(ev.chosen == -1, "and the release there chooses nothing", ev.chosen,
          -1);
  }

  // THE RING WITH THE STICK AND THE TRIGGER.
  {
    State st;
    Reset(&st);
    Events ev;
    Input in;
    memset(&in, 0, sizeof(in));
    in.can_open = true;
    in.stick[1] = 0.5f;
    in.time = 1.0;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.open == kClosed, "a stick at 0.5 does not open the ring", st.open,
          kClosed);
    in.stick[1] = 1.0f;
    in.time = 1.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.opened && st.open == kOpenStick && st.slot == 0 && ev.moved,
          "a stick pushed up opens the ring with the top item picked",
          st.slot, 0);
    in.stick[0] = 1.0f;
    in.stick[1] = 0.0f;
    in.time = 1.2;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.moved && st.slot == 2, "the stick to the right picks slot 2",
          st.slot, 2);
    in.trigger = true;
    in.time = 1.3;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.chosen == 2 && ev.closed, "the trigger chooses it", ev.chosen, 2);
    in.time = 1.4;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.open == kClosed && !ev.opened,
          "the ring stays closed while the stick is still out", st.open,
          kClosed);
    in.trigger = false;
    in.stick[0] = 0.0f;
    in.time = 1.5;
    Step(&st, set, in, pages, 2, &ev);
    in.stick[0] = -1.0f;
    in.time = 1.6;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.opened && st.slot == 6,
          "after the stick came back, a new push opens the ring", st.slot, 6);
    in.stick[0] = 0.0f;
    in.time = 1.7;
    Step(&st, set, in, pages, 2, &ev);
    Check(!ev.closed && st.open == kOpenStick && st.slot == -1,
          "a stick that comes to the middle does not close the ring at once",
          st.open, kOpenStick);
    in.stick[0] = 1.0f;
    in.time = 1.9;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.open == kOpenStick && st.slot == 2,
          "so the stick can go through the middle to the other side", st.slot,
          2);
    in.stick[0] = 0.0f;
    in.time = 2.0;
    Step(&st, set, in, pages, 2, &ev);
    in.trigger = true;
    in.time = 2.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.chosen == -1 && st.open == kOpenStick,
          "the trigger with the stick in the middle chooses nothing",
          ev.chosen, -1);
    in.trigger = false;
    in.time = 2.0 + set.stick_rest + 0.01;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.closed && ev.chosen == -1 && st.open == kClosed,
          "a stick that rests in the middle for half a second closes the ring "
          "with no choice", ev.chosen, -1);

    // The trigger on More changes the page at once.
    in.stick[1] = -1.0f;
    in.time = 3.0;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.slot == 4, "the stick down picks More", st.slot, 4);
    in.trigger = true;
    in.time = 3.05;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.paged && st.page == 1 && st.open == kOpenStick,
          "the trigger on More changes the page, and the ring stays open",
          st.page, 1);
    in.trigger = false;
    in.stick[1] = 0.0f;  // through the middle to the top
    in.time = 3.1;
    Step(&st, set, in, pages, 2, &ev);
    in.stick[1] = 1.0f;
    in.time = 3.2;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.page == 1 && st.slot == 0 && pages[1][0] == kItemMarkSet,
          "the stick goes through the middle to the top item of page 2",
          st.slot, 0);
    in.stick[0] = 1.0f;
    in.time = 3.25;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.slot == 1 && pages[1][1] == kItemMarkGo,
          "the stick up and to the right picks the next item of page 2",
          st.slot, 1);
    in.trigger = true;
    in.time = 3.3;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.chosen == 1 && ev.chosen_page == 1, "and the trigger chooses it",
          ev.chosen_page, 1);
    in.trigger = false;
    in.stick[0] = in.stick[1] = 0.0f;
    Step(&st, set, in, pages, 2, &ev);

    // A trigger that is down before the ring opens is not a choice.
    in.trigger = true;
    in.time = 4.0;
    Step(&st, set, in, pages, 2, &ev);
    in.stick[1] = 1.0f;
    in.time = 4.1;
    Step(&st, set, in, pages, 2, &ev);
    Check(ev.opened && ev.chosen == -1 && st.open == kOpenStick,
          "a trigger that was down before the ring opened chooses nothing",
          ev.chosen, -1);
    in.trigger = false;
    in.stick[1] = 0.0f;
    Step(&st, set, in, pages, 2, &ev);
    in.time = 4.9;
    Step(&st, set, in, pages, 2, &ev);
    Check(st.open == kClosed, "the ring is closed again", st.open, kClosed);

    // Each way can be off.
    Settings only_button = set;
    only_button.stick = false;
    in.stick[1] = 1.0f;
    in.time = 5.0;
    Step(&st, only_button, in, pages, 2, &ev);
    Check(st.open == kClosed, "with the stick way off, the stick opens nothing",
          st.open, kClosed);
    in.stick[1] = 0.0f;
    Step(&st, only_button, in, pages, 2, &ev);
    Settings only_stick = set;
    only_stick.button = false;
    in.button = true;
    in.time = 5.2;
    Step(&st, only_stick, in, pages, 2, &ev);
    Check(st.open == kClosed && !ev.blocked,
          "with the button way off, the button opens nothing", st.open,
          kClosed);
  }

  // THE GAME SIDE.
  {
    Check(LeashAction(1, 2) == 0x1F && LeashAction(2, 3) == 0x1F &&
              LeashAction(3, 1) == 0x1F,
          "action 0x1F steps the leash 1, 2, 3, 1", 1, 1);
    Check(LeashAction(1, 3) == 0x20 && LeashAction(2, 1) == 0x20 &&
              LeashAction(3, 2) == 0x20,
          "action 0x20 steps it the other way", 1, 1);
    Check(LeashAction(2, 2) == -1 && LeashAction(0, 2) == -1 &&
              LeashAction(2, 4) == -1,
          "the same kind, and a kind that is not 1 to 3, need no key", 1, 1);
    // One step of the game's own rule reaches each kind from each kind.
    bool reach = true;
    for (int now = 1; now <= 3; ++now) {
      for (int want = 1; want <= 3; ++want) {
        if (now == want) continue;
        const int action = LeashAction(now, want);
        const int after = action == 0x1F ? (now == 3 ? 1 : now + 1)
                                         : (now == 1 ? 3 : now - 1);
        if (after != want) reach = false;
      }
    }
    Check(reach, "one key brings each kind of leash to each other kind", reach,
          1);
    bool set_marks[kMarks] = {true, true, false, true, false, false, false};
    Check(MarkToSet(set_marks, -1) == 2, "the first free bookmark is taken",
          MarkToSet(set_marks, -1), 2);
    for (int i = 0; i < kMarks; ++i) set_marks[i] = true;
    Check(MarkToSet(set_marks, -1) == 0,
          "with all 7 set and none by the mod, the first one is taken",
          MarkToSet(set_marks, -1), 0);
    Check(MarkToSet(set_marks, 3) == 4 && MarkToSet(set_marks, 6) == 0,
          "then the one after the last one that the mod set", 1, 1);
    Check(ModsOfKey(0x1D) == kModCtrl && ModsOfKey(0x9D) == kModCtrl &&
              ModsOfKey(0x2A) == kModShift && ModsOfKey(0x36) == kModShift &&
              ModsOfKey(0x38) == kModAlt && ModsOfKey(0xB8) == kModAlt &&
              ModsOfKey(0) == 0,
          "the modifier key of an action gives the modifier word of the game",
          1, 1);
    Check(!ActionIsOff(0x11, true, true) && ActionIsOff(0x11, true, false) &&
              ActionIsOff(0x11, false, true),
          "the Temple key is off while either tutorial switch is off", 1, 1);
    Check(!ActionIsOff(0x14, false, false) && !ActionIsOff(0x1F, false, false) &&
              !ActionIsOff(0x00, false, false),
          "the way into the Temple, the leash and the help are never off", 1,
          1);
    Check(ActionIsOff(0x03, false, true) && !ActionIsOff(0x05, false, true) &&
              ActionIsOff(0x06, false, true) && ActionIsOff(0x13, false, true),
          "with the moves off, the actions 3, 4 and 6 to 0x13 are off", 1, 1);
  }
}

}  // namespace ring_checks
