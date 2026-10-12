// THE BRIGHTNESS OF THE TWO EYE PICTURES, a test (eye_luma.cpp, and
// docs/backend.md, "Testing without a headset"). Pure math: no Windows, no
// game. The checks are in test/controller_math/eye_luma_checks.h.
//
// A fade that only one eye pass draws, and a frame with no picture, do not
// show in a still picture of one moment. They show in the mean brightness of
// each eye picture, frame by frame:
//  - one eye is dark and the other eye is not, for some frames, or
//  - both eyes are black for a few frames between two bright frames.
// The first is a fault of the two passes. The second is a flash of black in
// the headset.

#pragma once

#include <stdint.h>

namespace eye_luma {

// The brightness of one pixel, 0 to 255, from its blue, green and red.
inline int Luma(int blue, int green, int red) {
  return (red * 77 + green * 150 + blue * 29) >> 8;
}

// The mean brightness of a picture of 4 bytes for each pixel (blue, green,
// red, and one more), on a grid of `cells` by `cells` points. The grid
// leaves out a tenth of the picture at each edge. 0 for a picture with no
// size.
inline float MeanLuma(const uint8_t* pixels, int pitch, int width, int height,
                      int cells) {
  if (pixels == nullptr || width <= 0 || height <= 0 || cells <= 0 ||
      pitch < width * 4) {
    return 0.0f;
  }
  unsigned sum = 0;
  for (int cy = 0; cy < cells; ++cy) {
    const int y = static_cast<int>(height * (0.1f + 0.8f * (cy + 0.5f) / cells));
    const uint8_t* row = pixels + static_cast<size_t>(y) * pitch;
    for (int cx = 0; cx < cells; ++cx) {
      const int x =
          static_cast<int>(width * (0.1f + 0.8f * (cx + 0.5f) / cells));
      const uint8_t* p = row + x * 4;
      sum += static_cast<unsigned>(Luma(p[0], p[1], p[2]));
    }
  }
  return static_cast<float>(sum) / static_cast<float>(cells * cells);
}

// What one frame is.
enum Kind {
  kBright = 0,  // both eyes have a picture, and they agree
  kBlack = 1,   // both eyes are black
  kOneEye = 2   // one eye is much darker than the other eye
};

// A mean below this is black.
const float kBlackLevel = 4.0f;
// One eye is much darker: the difference is more than this, and more than
// this part of the brighter eye. The two pictures of a scene are the same
// scene from two places 7 cm apart, so their means are near to each other.
const float kEyeGap = 10.0f;
const float kEyeGapPart = 0.25f;
// A run of black frames that is not longer than this, between two frames
// that are not black, is a flash. A fade of the game to black and back is
// longer.
const int kFlashFrames = 8;

inline int KindOf(float left, float right) {
  const float more = left > right ? left : right;
  const float less = left > right ? right : left;
  if (more - less > kEyeGap && more - less > kEyeGapPart * more) {
    return kOneEye;
  }
  if (more < kBlackLevel) return kBlack;
  return kBright;
}

// A run of frames of one kind that came to its end.
struct Run {
  int kind;
  uint64_t first;   // the first frame of the run
  uint64_t frames;  // its length
  float left;       // the two means at the frame with the largest
  float right;      // difference between the eyes
  bool flash;       // a black run of kFlashFrames or less, after a frame
                    // that was not black
};

// The frames so far.
struct Track {
  bool have;        // a run is open
  int kind;
  uint64_t first;
  uint64_t frames;
  float left, right, gap;
  bool after_picture;  // the run before this one was not black
  // The sums of a window, for the report.
  uint64_t all;
  uint64_t black;
  uint64_t one_eye;
  uint64_t flashes;
  float most_gap;      // the largest difference between the eyes
  float gap_left, gap_right;
  uint64_t gap_frame;
  // The brightest frame: the largest sum of the two eyes. A flash of light
  // that both eye passes draw shows here, with the same number in each eye.
  float bright_left, bright_right;
  uint64_t bright_frame;
};

// One frame. True when a run ended at this frame: `ended` is that run, and
// the frame starts the next run.
inline bool Step(Track* t, uint64_t frame, float left, float right,
                 Run* ended) {
  const int kind = KindOf(left, right);
  const float gap = left > right ? left - right : right - left;
  ++t->all;
  if (kind == kBlack) ++t->black;
  if (kind == kOneEye) ++t->one_eye;
  if (t->all == 1 || gap > t->most_gap) {
    t->most_gap = gap;
    t->gap_left = left;
    t->gap_right = right;
    t->gap_frame = frame;
  }
  if (t->all == 1 || left + right > t->bright_left + t->bright_right) {
    t->bright_left = left;
    t->bright_right = right;
    t->bright_frame = frame;
  }
  bool out = false;
  if (t->have && kind != t->kind) {
    ended->kind = t->kind;
    ended->first = t->first;
    ended->frames = t->frames;
    ended->left = t->left;
    ended->right = t->right;
    ended->flash = t->kind == kBlack && t->after_picture &&
                   t->frames <= static_cast<uint64_t>(kFlashFrames) &&
                   kind != kBlack;
    if (ended->flash) ++t->flashes;
    t->after_picture = t->kind != kBlack;
    t->have = false;
    out = true;
  }
  if (!t->have) {
    t->have = true;
    t->kind = kind;
    t->first = frame;
    t->frames = 0;
    t->gap = -1.0f;
  }
  ++t->frames;
  if (gap > t->gap) {
    t->gap = gap;
    t->left = left;
    t->right = right;
  }
  return out;
}

// A new window of the report. The open run stays.
inline void NewWindow(Track* t) {
  t->all = t->black = t->one_eye = t->flashes = 0;
  t->most_gap = 0.0f;
  t->gap_left = t->gap_right = 0.0f;
  t->gap_frame = 0;
  t->bright_left = t->bright_right = 0.0f;
  t->bright_frame = 0;
}

}  // namespace eye_luma
