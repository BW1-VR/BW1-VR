// The debug page of the ring: the pure math of its pages. See debug_menu.cpp
// and docs/controllers.md, "The debug page". radial_menu.cpp uses it, and
// test/controller_math checks it (debug_menu_checks.h). Nothing here reads a
// global or touches the game.
//
// THE PAGES. The item "debug" goes to the first page of the groups. A group
// goes to the first page of its switches. Each page has "Back" in the bottom
// slot: on a page of a group it goes to the page of the groups that holds
// that group, and on a page of the groups it goes to the page with the item
// "debug". A list of 7 or fewer has one page. A longer list has 6 on each
// page, and "More" in slot 3 goes to its next page, and from its last page
// to its first page.

#pragma once

#include "radial_menu_math.h"

namespace debug_math {

using radial_math::kItemBack;
using radial_math::kItemGroupBase;
using radial_math::kItemMore;
using radial_math::kItemNone;
using radial_math::kItemSwitchBase;
using radial_math::kMaxPages;
using radial_math::kSlots;

const int kBackSlot = 4;
const int kMoreSlot = 3;
// The slots of the entries of a list, from the top and clockwise.
const int kOnePageSlots[7] = {0, 1, 2, 3, 5, 6, 7};
const int kManyPageSlots[6] = {0, 1, 2, 5, 6, 7};

// The pages of a list of n entries.
inline int ListPages(int n) {
  if (n <= 7) return 1;
  return (n + 5) / 6;
}

// The page (from the first page of the list) and the slot of entry i.
inline void EntryPlace(int n, int i, int* page, int* slot) {
  if (n <= 7) {
    *page = 0;
    *slot = kOnePageSlots[i];
    return;
  }
  *page = i / 6;
  *slot = kManyPageSlots[i % 6];
}

// All pages of one ring: the items and the page of each link item (-1 for
// an item that is not a link).
struct Pages {
  int items[kMaxPages][kSlots];
  int links[kMaxPages][kSlots];
  int count;
};

inline void ClearPage(Pages* p, int page) {
  for (int s = 0; s < kSlots; ++s) {
    p->items[page][s] = kItemNone;
    p->links[page][s] = -1;
  }
}

// The links of the pages of the ini: "More" goes to the next page, and from
// the last page to the first.
inline void IniLinks(Pages* p) {
  for (int page = 0; page < p->count; ++page) {
    for (int s = 0; s < kSlots; ++s) {
      p->links[page][s] =
          p->items[page][s] == kItemMore && p->count > 1
              ? (page + 1) % p->count
              : -1;
    }
  }
}

// THE DEBUG PAGES, after the pages that p has. `home` and `home_slot` are the
// page and the slot of the item "debug". group_of[i] is the group of switch
// i (0 to groups - 1), or -1 for a switch that is not on the page now. The
// groups with no switch on the page are not there. group_page[g] gets the
// first page of group g, or -1. The answer is false when the pages do not
// fit in kMaxPages: then p does not change.
inline bool AddDebugPages(Pages* p, int home, int home_slot,
                          const int* group_of, int switches, int groups,
                          int* group_page) {
  int count[64] = {};
  if (groups < 0 || groups > 64) return false;
  for (int i = 0; i < switches; ++i) {
    if (group_of[i] >= 0 && group_of[i] < groups) ++count[group_of[i]];
  }
  int shown = 0;
  for (int g = 0; g < groups; ++g) {
    if (count[g] > 0) ++shown;
  }
  const int base = p->count;
  const int group_pages = ListPages(shown);
  int need = group_pages;
  for (int g = 0; g < groups; ++g) {
    if (count[g] > 0) need += ListPages(count[g]);
  }
  if (base + need > kMaxPages || home < 0 || home >= base || home_slot < 0 ||
      home_slot >= kSlots) {
    return false;
  }
  // The first page of each group, after the pages of the groups.
  int next = base + group_pages;
  int first[64];
  for (int g = 0; g < groups; ++g) {
    first[g] = -1;
    if (count[g] > 0) {
      first[g] = next;
      next += ListPages(count[g]);
    }
    if (group_page != nullptr) group_page[g] = first[g];
  }
  for (int page = base; page < next; ++page) ClearPage(p, page);
  // The pages of the groups.
  int holder[64];
  int k = 0;
  for (int g = 0; g < groups; ++g) {
    holder[g] = -1;
    if (count[g] == 0) continue;
    int page = 0, slot = 0;
    EntryPlace(shown, k, &page, &slot);
    holder[g] = base + page;
    p->items[base + page][slot] = kItemGroupBase + g;
    p->links[base + page][slot] = first[g];
    ++k;
  }
  for (int page = 0; page < group_pages; ++page) {
    p->items[base + page][kBackSlot] = kItemBack;
    p->links[base + page][kBackSlot] = home;
    if (group_pages > 1) {
      p->items[base + page][kMoreSlot] = kItemMore;
      p->links[base + page][kMoreSlot] = base + (page + 1) % group_pages;
    }
  }
  // The pages of each group.
  for (int g = 0; g < groups; ++g) {
    if (count[g] == 0) continue;
    const int n = count[g];
    const int pages = ListPages(n);
    int j = 0;
    for (int i = 0; i < switches; ++i) {
      if (group_of[i] != g) continue;
      int page = 0, slot = 0;
      EntryPlace(n, j, &page, &slot);
      p->items[first[g] + page][slot] = kItemSwitchBase + i;
      ++j;
    }
    for (int page = 0; page < pages; ++page) {
      p->items[first[g] + page][kBackSlot] = kItemBack;
      p->links[first[g] + page][kBackSlot] = holder[g];
      if (pages > 1) {
        p->items[first[g] + page][kMoreSlot] = kItemMore;
        p->links[first[g] + page][kMoreSlot] = first[g] + (page + 1) % pages;
      }
    }
  }
  p->links[home][home_slot] = base;
  p->count = next;
  return true;
}

// The word of a value in a list of words with "|" between them, from the
// value 0. The answer is the length of the word, and 0 when the list has no
// word for that value: then the page shows the number.
inline int ValueWord(const wchar_t* words, int value, wchar_t* out,
                     int size) {
  if (size > 0) out[0] = 0;
  if (words == nullptr || value < 0 || size <= 0) return 0;
  int index = 0;
  const wchar_t* p = words;
  while (*p != 0 && index < value) {
    if (*p == L'|') ++index;
    ++p;
  }
  if (index != value) return 0;
  int n = 0;
  while (p[n] != 0 && p[n] != L'|' && n + 1 < size) {
    out[n] = p[n];
    ++n;
  }
  out[n] = 0;
  return n;
}

}  // namespace debug_math
