// The ring: the pure math. See docs/controllers.md, "The ring", and
// docs/controls-audit.md. radial_menu.cpp uses it, and test/controller_math
// checks it (ring_checks.h). Nothing here reads a global or touches the game.
//
// THE RING is a menu of 8 slots around the hand that does not point. It holds
// the functions of the plain game that have no controller button: the Temple,
// the Creature, the kinds of the leash, the bookmarks, the pause. Slot 0 is at
// the top, and the slots go clockwise as the player sees them.
//
// TWO WAYS TO USE IT, and each one is complete with one hand:
//
//   The button   Hold the ring button. The ring opens at the hand and stays
//                at that place of the room. Move the hand toward an item and
//                let go of the button. With the hand in the middle, the
//                release chooses nothing.
//   The stick    Push the stick of the same hand. The ring opens, and the
//                stick points at an item. Pull the trigger of that hand to
//                choose. Let the stick rest in the middle to close the ring
//                with no choice. The ring waits half a second for that, so
//                the stick can go through the middle to an item on the
//                other side.
//
// The thumb holds the button, so it cannot push the stick at the same time.
// That is why the button goes with the hand and the stick goes with the
// trigger.

#pragma once

#include <math.h>
#include <string.h>

#include "controller_math.h"

namespace radial_math {

const float kPi = 3.14159265f;
const int kSlots = 8;
const int kPages = 2;

// --- the items -------------------------------------------------------------------

enum Item {
  kItemNone,
  kItemTemple,
  kItemEnter,
  kItemCreature,
  kItemTown,
  kItemLand,
  kItemLeashNext,
  kItemLeashPrevious,
  kItemLeashAggression,
  kItemLeashLearning,
  kItemLeashCompassion,
  kItemMarkSet,
  kItemMarkGo,
  kItemMarkBack,
  kItemPause,
  kItemSlower,
  kItemFaster,
  kItemHelp,
  kItemNames,
  kItemDetails,
  kItemSave,
  kItemLoad,
  kItemPlayground,
  kItemRoomCreature,
  kItemRoomChallenge,
  kItemRoomSave,
  kItemRoomOptions,
  kItemRoomLibrary,
  kItemHall,
  kItemLeave,
  kItemMore,
  kItems
};

// How an item acts. Each one goes through the key path of the game
// (docs/controls-audit.md, section 1).
enum How {
  kHowNone,
  kHowAction,   // an action of the control map: the key that the player gave it
  kHowKey,      // a fixed key of GGame::ProcessKey, with its modifiers
  kHowLeash,    // a kind of the leash: the action that steps to that kind
  kHowMarkSet,  // Ctrl and the number of a free bookmark
  kHowPage,     // the other page of the ring
};

// The modifier word of the key buffer of the game.
const int kModShift = 0x10;
const int kModCtrl = 0x20;
const int kModAlt = 0x40;

struct ItemInfo {
  const char* id;        // the word of bwvr.ini
  const wchar_t* word;   // on the ring
  const wchar_t* name;   // on the card in the middle
  int how;
  int code;              // the action, the key, or the kind of the leash
  int mods;              // for a key
  int presses;           // the Temple key acts on its second press
};

const ItemInfo kItemInfo[kItems] = {
    {"none", L"", L"", kHowNone, 0, 0, 0},
    {"temple", L"Temple", L"Go to the Temple", kHowAction, 0x11, 0, 2},
    {"enter", L"Inside", L"Go into the Temple", kHowAction, 0x14, 0, 1},
    {"creature", L"Creature", L"Go to the Creature", kHowAction, 0x12, 0, 1},
    {"town", L"Town", L"Go to the next town", kHowKey, 0x0F, 0, 1},
    {"land", L"Land", L"The middle of the land", kHowAction, 0x13, 0, 1},
    {"leash_next", L"Leash +", L"The next leash", kHowAction, 0x20, 0, 1},
    {"leash_previous", L"Leash -", L"The leash before", kHowAction, 0x1F, 0,
     1},
    {"leash_aggression", L"Aggression", L"Leash of Aggression", kHowLeash, 1,
     0, 1},
    {"leash_learning", L"Learning", L"Leash of Learning", kHowLeash, 2, 0, 1},
    {"leash_compassion", L"Compassion", L"Leash of Compassion", kHowLeash, 3,
     0, 1},
    {"mark_set", L"Set mark", L"Set a bookmark here", kHowMarkSet, 0, 0, 1},
    {"mark_go", L"Next mark", L"Go to the next bookmark", kHowKey, 0x0F,
     kModCtrl, 1},
    {"mark_back", L"Back", L"Go back", kHowKey, 0x0E, 0, 1},
    {"pause", L"Pause", L"Pause the game", kHowKey, 0x19, 0, 1},
    {"slower", L"Slower", L"Make the game slower", kHowKey, 0x02, kModAlt, 1},
    {"faster", L"Faster", L"Make the game faster", kHowKey, 0x03, kModAlt, 1},
    {"help", L"Help", L"Help of the game", kHowAction, 0x00, 0, 1},
    {"names", L"Names", L"Villager names", kHowAction, 0x1B, 0, 1},
    {"details", L"Details", L"Villager details", kHowAction, 0x1C, 0, 1},
    {"save", L"Save", L"Quick save", kHowAction, 0x1D, 0, 1},
    {"load", L"Load", L"Quick load", kHowAction, 0x1E, 0, 1},
    {"playground", L"Playground", L"The Gods' Playground", kHowKey, 0x3C, 0,
     1},
    {"room_creature", L"Creature", L"The Creature Room", kHowAction, 0x15, 0,
     1},
    {"room_challenge", L"Challenge", L"The Challenge Room", kHowAction, 0x16,
     0, 1},
    {"room_save", L"Save", L"The Save Game Room", kHowAction, 0x17, 0, 1},
    {"room_options", L"Options", L"The Options Room", kHowAction, 0x18, 0, 1},
    {"room_library", L"Library", L"The Library", kHowAction, 0x19, 0, 1},
    {"hall", L"Hall", L"The hall of the Temple", kHowAction, 0x14, 0, 1},
    {"leave", L"Leave", L"Leave the Temple", kHowKey, 0x01, 0, 1},
    {"more", L"More", L"More", kHowPage, 0, 0, 0},
};

// The rings as made. The order is how often a player needs an item, from the
// top and clockwise, with "More" at the bottom of each page.
const char* const kDefaultPage1 =
    "temple,creature,town,leash_learning,more,leash_compassion,"
    "leash_aggression,enter";
const char* const kDefaultPage2 =
    "mark_set,mark_go,mark_back,pause,more,help,names,save";
// In the Temple: the rooms and the way out.
const char* const kDefaultTemple =
    "hall,room_creature,room_challenge,room_save,leave,room_options,"
    "room_library,none";

inline int ItemOfId(const char* id, size_t length) {
  for (int i = 0; i < kItems; ++i) {
    if (strlen(kItemInfo[i].id) == length &&
        strncmp(kItemInfo[i].id, id, length) == 0) {
      return i;
    }
  }
  return -1;
}

// A list of item words with commas, as bwvr.ini has it. A slot with no word
// and a slot past the end of the list are empty. The answer is the number of
// words that are not known: each of those slots is empty too. `unknown` gets
// the first such word.
inline int ParseItems(const char* text, int out[kSlots], char* unknown,
                      size_t unknown_size) {
  for (int i = 0; i < kSlots; ++i) out[i] = kItemNone;
  if (unknown != nullptr && unknown_size > 0) unknown[0] = 0;
  int bad = 0;
  const char* p = text != nullptr ? text : "";
  for (int slot = 0; slot < kSlots && *p != 0; ++slot) {
    while (*p == ' ' || *p == '\t') ++p;
    const char* end = p;
    while (*end != 0 && *end != ',') ++end;
    const char* last = end;
    while (last > p && (last[-1] == ' ' || last[-1] == '\t')) --last;
    const size_t length = static_cast<size_t>(last - p);
    if (length > 0) {
      const int item = ItemOfId(p, length);
      if (item >= 0) {
        out[slot] = item;
      } else {
        if (bad == 0 && unknown != nullptr && unknown_size > 0) {
          const size_t n = length < unknown_size - 1 ? length
                                                     : unknown_size - 1;
          memcpy(unknown, p, n);
          unknown[n] = 0;
        }
        ++bad;
      }
    }
    p = *end == ',' ? end + 1 : end;
  }
  return bad;
}

inline bool PageHasItems(const int items[kSlots]) {
  for (int i = 0; i < kSlots; ++i) {
    if (items[i] != kItemNone && items[i] != kItemMore) return true;
  }
  return false;
}

// --- the direction ---------------------------------------------------------------

// The slot of a direction on the plane of the ring: x to the right and y up,
// as the player sees the ring.
inline int SlotOf(float x, float y) {
  const float turn = atan2f(x, y);  // 0 is up, and clockwise is positive
  int slot = static_cast<int>(floorf(turn / (kPi / 4.0f) + 0.5f));
  slot %= kSlots;
  if (slot < 0) slot += kSlots;
  return slot;
}

// The angle from the middle of a slot to a direction, in degrees, 0 to 180.
inline float DegreesFromSlot(float x, float y, int slot) {
  float turn = atan2f(x, y) - slot * (kPi / 4.0f);
  while (turn > kPi) turn -= 2.0f * kPi;
  while (turn < -kPi) turn += 2.0f * kPi;
  return fabsf(turn) * 57.29578f;
}

// THE PICK. A point (the hand on the plane of the ring, or the stick) picks
// the slot of its direction when it is `in` or more from the middle. The pick
// goes back to the middle (-1) only when the point is nearer than `out`, and
// it goes to the next slot only when the direction is `margin_deg` past the
// border of the two. So a hand that shakes at a border does not flip the
// choice.
inline int Pick(float x, float y, int before, float in, float out,
                float margin_deg) {
  const float r = sqrtf(x * x + y * y);
  if (before < 0 || before >= kSlots) return r >= in ? SlotOf(x, y) : -1;
  if (r < out) return -1;
  if (DegreesFromSlot(x, y, before) <= 22.5f + margin_deg) return before;
  return SlotOf(x, y);
}

// --- the plane -------------------------------------------------------------------

// The ring in the LOCAL space of OpenXR (right handed, meters, y up). It is a
// quad that shows its +z side. Its middle is the place of the hand at the
// open, and it faces the head of that moment, with no roll. It does not move
// after that: the hand moves over it.
struct Plane {
  float center[3];
  float orientation[4];  // x y z w
  float right[3];        // +x of the quad: to the right, as the player sees it
  float up[3];           // +y of the quad
};

inline void PlaneAt(const float hand[3], const float head[3], Plane* out) {
  for (int i = 0; i < 3; ++i) out->center[i] = hand[i];
  controller_math::FacePose(hand, head, out->orientation);
  const float x[3] = {1.0f, 0.0f, 0.0f};
  const float y[3] = {0.0f, 1.0f, 0.0f};
  controller_math::QuatRotate(out->orientation, x, out->right);
  controller_math::QuatRotate(out->orientation, y, out->up);
}

// The hand on the plane: meters to the right and up from the middle.
inline void OnPlane(const Plane& plane, const float hand[3], float out[2]) {
  const float d[3] = {hand[0] - plane.center[0], hand[1] - plane.center[1],
                      hand[2] - plane.center[2]};
  out[0] = d[0] * plane.right[0] + d[1] * plane.right[1] +
           d[2] * plane.right[2];
  out[1] = d[0] * plane.up[0] + d[1] * plane.up[1] + d[2] * plane.up[2];
}

// The middle of a slot in the picture of the ring, in pixels, with y down.
inline void SlotPixel(int slot, float center, float radius, float* x,
                      float* y) {
  const float turn = slot * (kPi / 4.0f);
  *x = center + radius * sinf(turn);
  *y = center - radius * cosf(turn);
}

// --- the ring, step by step --------------------------------------------------------

struct Settings {
  bool button;        // the button opens the ring, and the hand picks
  bool stick;         // the stick opens the ring and picks, the trigger chooses
  float hand_in;      // meters from the middle to pick a slot
  float hand_out;     // and to go back to the middle
  float stick_in;     // the same for the stick, 0 to 1
  float stick_out;
  float margin_deg;
  float dwell;        // seconds on "More" before the page changes
  float stick_rest;   // seconds of the stick in the middle before it closes
};

inline Settings DefaultSettings() {
  Settings s;
  s.button = true;
  s.stick = true;
  s.hand_in = 0.05f;
  s.hand_out = 0.035f;
  s.stick_in = 0.6f;
  s.stick_out = 0.4f;
  s.margin_deg = 6.0f;
  s.dwell = 0.45f;
  s.stick_rest = 0.5f;
  return s;
}

struct Input {
  bool can_open;     // the hand has a pose, and nothing blocks the ring
  bool button;       // the ring button is down
  bool trigger;      // the trigger of the same hand is down
  float stick[2];    // x right, y forward
  bool have_hand;    // hand[] is known
  float hand[2];     // the hand on the plane of the ring, meters
  double time;       // seconds
};

enum Open { kClosed, kOpenButton, kOpenStick };

struct State {
  int open;
  int page;
  int slot;            // the choice now, -1 for the middle
  int dead_slot;       // after a change of page: not live until the pick leaves it
  double slot_since;
  double rest_since;   // the stick came to the middle then, or 0
  bool button_before;
  bool trigger_before;
  bool stick_back;     // the stick must go back to the middle before it opens
};

struct Events {
  bool opened;
  bool closed;     // with or without a choice
  bool moved;      // the choice changed
  bool paged;      // the page changed
  bool blocked;    // the button went down and the ring could not open
  int chosen;      // the slot that was chosen, or -1
  int chosen_page;
};

inline void Reset(State* state) {
  memset(state, 0, sizeof(*state));
  state->slot = -1;
  state->dead_slot = -1;
}

// One frame of the ring. `items` are the pages of the ring now, and `pages`
// is 1 or 2. An empty slot cannot be picked.
inline void Step(State* st, const Settings& set, const Input& in,
                 const int (*items)[kSlots], int pages, Events* ev) {
  memset(ev, 0, sizeof(*ev));
  ev->chosen = -1;
  const bool button_edge = in.button && !st->button_before;
  const bool trigger_edge = in.trigger && !st->trigger_before;
  st->button_before = in.button;
  st->trigger_before = in.trigger;
  const float stick_r =
      sqrtf(in.stick[0] * in.stick[0] + in.stick[1] * in.stick[1]);
  if (stick_r < set.stick_out) st->stick_back = false;
  if (pages < 1) pages = 1;
  if (pages > kPages) pages = kPages;
  if (st->page >= pages) st->page = 0;

  if (st->open == kClosed) {
    if (set.button && button_edge) {
      if (!in.can_open) {
        ev->blocked = true;
        return;
      }
      st->open = kOpenButton;
    } else if (set.stick && !st->stick_back && stick_r >= set.stick_in &&
               in.can_open && !in.button) {
      st->open = kOpenStick;
    } else {
      return;
    }
    st->page = 0;
    st->slot = -1;
    st->dead_slot = -1;
    st->slot_since = in.time;
    st->rest_since = 0.0;
    ev->opened = true;
  }

  // The ring is open.
  const auto close = [&]() {
    st->open = kClosed;
    st->slot = -1;
    st->dead_slot = -1;
    ev->closed = true;
  };
  if (!in.can_open) {
    close();
    return;
  }
  int pick = st->slot;
  if (st->open == kOpenButton) {
    if (in.have_hand) {
      pick = Pick(in.hand[0], in.hand[1], st->slot >= 0 ? st->slot
                                                         : st->dead_slot,
                  set.hand_in, set.hand_out, set.margin_deg);
    }
  } else {
    pick = Pick(in.stick[0], in.stick[1],
                st->slot >= 0 ? st->slot : st->dead_slot, set.stick_in,
                set.stick_out, set.margin_deg);
    if (stick_r < set.stick_out) {
      // The stick is in the middle. It can be on its way to an item on the
      // other side, so the ring waits. A stick that rests there closes the
      // ring with no choice.
      if (st->rest_since == 0.0) st->rest_since = in.time;
      if (in.time - st->rest_since >= set.stick_rest) {
        close();
        return;
      }
    } else {
      st->rest_since = 0.0;
    }
  }
  // After a change of page the hand is still in the slot of "More". That
  // slot does nothing until the pick leaves it.
  if (st->dead_slot >= 0) {
    if (pick == st->dead_slot) {
      pick = -1;
    } else {
      st->dead_slot = -1;
    }
  }
  if (pick >= 0 && items[st->page][pick] == kItemNone) pick = -1;
  if (pick != st->slot) {
    st->slot = pick;
    st->slot_since = in.time;
    ev->moved = true;
  }
  const bool on_more = st->slot >= 0 && items[st->page][st->slot] == kItemMore;
  const auto turn_page = [&]() {
    st->dead_slot = st->slot;
    st->slot = -1;
    st->page = (st->page + 1) % pages;
    st->slot_since = in.time;
    ev->paged = true;
    ev->moved = true;
  };

  if (st->open == kOpenButton) {
    if (!in.button) {
      // The release chooses. On "More" and in the middle it chooses nothing.
      if (st->slot >= 0 && !on_more) {
        ev->chosen = st->slot;
        ev->chosen_page = st->page;
      }
      close();
      return;
    }
  } else if (trigger_edge && st->slot >= 0) {
    if (on_more) {
      if (pages > 1) turn_page();
      return;
    }
    ev->chosen = st->slot;
    ev->chosen_page = st->page;
    // The stick is still out. It must come back before the ring opens again.
    st->stick_back = true;
    close();
    return;
  }
  if (on_more && pages > 1 && in.time - st->slot_since >= set.dwell) {
    turn_page();
  }
}

// --- the game side ---------------------------------------------------------------

// THE KIND OF THE LEASH. The game has two actions that step the kind
// (GGame::DoAction 0x0063F940): action 0x1F goes 1, 2, 3, 1 and action 0x20
// goes 1, 3, 2, 1. The kinds are 1 Aggression, 2 Learning, 3 Compassion. The
// answer is the action that brings `now` to `want` in one step, or -1 when
// the leash has that kind already. A kind that is not 1 to 3 gives -1 too.
inline int LeashAction(int now, int want) {
  if (now < 1 || now > 3 || want < 1 || want > 3 || now == want) return -1;
  const int next = now == 3 ? 1 : now + 1;
  return next == want ? 0x1F : 0x20;
}

// THE BOOKMARK TO SET. The game has 7 bookmarks for the keys 1 to 7. The mod
// takes the first one that is not set. When all 7 are set, it takes the one
// after the last one that it set, so the oldest of its own goes first.
const int kMarks = 7;

inline int MarkToSet(const bool set[kMarks], int last) {
  for (int i = 0; i < kMarks; ++i) {
    if (!set[i]) return i;
  }
  if (last < 0 || last >= kMarks) return 0;
  return (last + 1) % kMarks;
}

// The modifier word of the key buffer for the modifier key of an action of
// the control map (a scan code): Shift, Ctrl or Alt, left or right.
inline int ModsOfKey(int key) {
  if (key == 0x2A || key == 0x36) return kModShift;
  if (key == 0x1D || key == 0x9D) return kModCtrl;
  if (key == 0x38 || key == 0xB8) return kModAlt;
  return 0;
}

// The game turns actions of the control map off for the tutorial
// (fn_0046F750). `moves` is ControlMap+0x652C and `jumps` is +0x6530.
inline bool ActionIsOff(int action, bool moves, bool jumps) {
  if (!moves) {
    return action == 3 || action == 4 || (action >= 6 && action <= 0x13);
  }
  if (!jumps) return action >= 0x11 && action <= 0x13;
  return false;
}

}  // namespace radial_math
