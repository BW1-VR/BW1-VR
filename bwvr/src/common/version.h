// The name and the version of the Black & White VR Mod.
//
// Change the version here only. The launcher, the mod, the OpenXR instance,
// both version resources and the installer read these values.
//
// Keep preprocessor lines only in this file, because rc.exe reads it too.
// rc.exe does not understand C++, so no namespace, no constexpr and no
// string joining beyond plain literals.
#ifndef BWVR_VERSION_H
#define BWVR_VERSION_H

// Exactly this, everywhere a player can see it.
#define BWVR_PRODUCT_NAME "Black & White VR Mod"

#define BWVR_VERSION_MAJOR 1
#define BWVR_VERSION_MINOR 1
#define BWVR_VERSION_PATCH 0
#define BWVR_VERSION_TEXT "1.1.0"

#define BWVR_PUBLISHER "Buck"
#define BWVR_COPYRIGHT "Copyright (C) 2026 Buck"
#define BWVR_SUPPORT_URL "https://ko-fi.com/buckzor"

#endif  // BWVR_VERSION_H
