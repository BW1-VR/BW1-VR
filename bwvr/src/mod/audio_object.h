// The object of a sound: is it a Base of the game?
//
// The game gives each sound an object (LH_SamplePlayOptions+0x24). The mod
// asks that object for its place with Base::Get3DSoundPos, vtable offset
// 0x10 (thiscall, one argument, ret 4). The object is not always a Base.
// PHandFX::DoRemoveFromHandVisual (0x0068CE90) plays sample 0x77 with the
// PHandFX of the Hand as the object, when a miracle is shaken out of the
// Hand. PHandFX derives from PSysHandFX only, and slot 0x10 of its vtable is
// PHandFX::DrawHandFX: no argument and a plain ret. The call left 4 bytes on
// the stack, and the stack check of the caller ended the game (0xC0000409 in
// the mod's DLL, desktop run mirB of 2026-10-04).
//
// So the mod calls slot 0x10 only when the class of the object, by the RTTI
// of its vtable, is Base or derives from it.

#pragma once

#include <windows.h>
#include <stdint.h>
#include <string.h>

// vt: the vtable of the object, in the .rdata of the game. The locator of
// the class is at vt - 4, its hierarchy at +0x10 of the locator, and the
// hierarchy has the count of the base classes at +8 and their array at +0xC.
// Each entry starts with the type, and the name of a type is at +8.
inline bool AudioVtableIsBase(uintptr_t vt) {
  const uintptr_t* locator_slot = reinterpret_cast<const uintptr_t*>(vt - 4);
  if (IsBadReadPtr(locator_slot, 4)) return false;
  const uint8_t* locator = reinterpret_cast<const uint8_t*>(*locator_slot);
  if (locator == nullptr || IsBadReadPtr(locator, 0x14)) return false;
  const uint8_t* hierarchy =
      *reinterpret_cast<const uint8_t* const*>(locator + 0x10);
  if (hierarchy == nullptr || IsBadReadPtr(hierarchy, 0x10)) return false;
  const uint32_t count = *reinterpret_cast<const uint32_t*>(hierarchy + 8);
  const uint8_t* const* bases =
      *reinterpret_cast<const uint8_t* const* const*>(hierarchy + 0xC);
  if (count == 0 || count > 64 || bases == nullptr ||
      IsBadReadPtr(bases, count * sizeof(void*))) {
    return false;
  }
  static const char kBase[] = ".?AVBase@@";
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* base = bases[i];
    if (base == nullptr || IsBadReadPtr(base, 4)) continue;
    const uint8_t* type = *reinterpret_cast<const uint8_t* const*>(base);
    if (type == nullptr || IsBadReadPtr(type + 8, sizeof(kBase))) continue;
    if (memcmp(type + 8, kBase, sizeof(kBase)) == 0) return true;
  }
  return false;
}
