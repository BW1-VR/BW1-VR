// Soft sounds for the player's own moves (docs/audio.md, "8. Cues for the
// moves of the player").
//
// The game has no sound for a move of the God view, because the mod makes
// those moves. A move with no sound feels as if nothing happened. So the mod
// plays a short, quiet cue: a swish for a turn, a swoosh for a zoom step, a
// whoosh with a soft landing for a teleport, and for the hands a thud, a
// ping and a tap. Half-Life: Alyx does the same: its ping at a lock plays at
// 0.15 of full volume, and one at a time.
//
// THE SOUNDS are made in code when the game starts, so the mod ships no
// sound file: noise through a band filter whose center moves, and sine
// tones that fall away. Each one starts and ends at zero.
//
// THE MOVES OF THE VIEW need no call from other files. This file sees them
// in the ears that the frame driver gives it: a turn step is a yaw of more
// than 15 degrees in one frame, a teleport is a move of more than half a
// meter of the room in one frame, and a zoom step is a change of the scale.
// A real head cannot do any of these in one frame. In the God view a cue
// also needs a new version of the view, which only a move of the player
// gives: the game can put the view at a new place by itself (a new camera
// mode, the end of a story), and that has no sound. Nothing plays while a
// story owns the view, or in the first frames after the view comes back.
//
// THE SOUNDS OF THE HANDS start where their haptic pulse starts:
// HapticsPulseHands (haptics.cpp) calls AudioCueHaptic with the kind of the
// pulse. A lock of the grab from a distance, the arrival of the object in
// the hand, and a grip that takes the world have a cue.
//
// The cues play through the mixer of the mod (audio_voices.cpp), flat, with
// a pan. They need audio_mixer=1.
//
// Ini: audio_cues (default 1, 0 is no cue), audio_cues_pct (default 100).

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "haptics_math.h"

void ModLog(const char* format, ...);
// tabletop.cpp: the game camera that the God view wants, with a version that
// goes up each time the player moves the view. False when the God view does
// not own the view.
bool TabletopGodGameCamera(float position[3], float focus[3],
                           uint64_t* version);

namespace {

const float kPi = 3.14159265f;
const float kRate = 22050.0f;
const int kMaxFrames = 8192;   // 0.37 s

struct Sound {
  int16_t data[kMaxFrames];
  int frames;
};
Sound g_sounds[kAudioCueCount];

bool g_on = false;
float g_level = 1.0f;       // audio_cues_pct / 100
float g_zoom_step = 1.5f;   // the ratio of one zoom step of the God view
uint32_t g_seed = 2463534242u;

// The view of the frame before.
bool g_have_last = false;
float g_last_position[3];
float g_last_yaw = 0.0f;
float g_last_scale = 0.0f;
uint64_t g_last_version = 0;
int g_owned_frames = 0;
DWORD g_last_cue_tick[kAudioCueCount];
uint64_t g_played[kAudioCueCount];
uint64_t g_reported = 0;
DWORD g_last_report = 0;

const char* const kNames[kAudioCueCount] = {
    "turn left", "turn right", "zoom in", "zoom out", "teleport",
    "grab", "lock", "arrive", "ring move", "ring choose"};

float Noise() {
  g_seed ^= g_seed << 13;
  g_seed ^= g_seed >> 17;
  g_seed ^= g_seed << 5;
  return static_cast<float>(static_cast<int32_t>(g_seed)) / 2147483648.0f;
}

// Adds noise through a band filter (a state variable filter) whose center
// goes from f0 to f1. The level rises and falls as sin squared.
void AddSwoosh(Sound* s, int start, float seconds, float f0, float f1,
               float q, float level) {
  const int n = static_cast<int>(seconds * kRate);
  float low = 0.0f, band = 0.0f;
  for (int i = 0; i < n && start + i < kMaxFrames; ++i) {
    const float t = static_cast<float>(i) / n;
    const float f = f0 * powf(f1 / f0, t);
    const float w = 2.0f * sinf(kPi * f / kRate);
    const float high = Noise() - low - q * band;
    band += w * high;
    low += w * band;
    const float envelope = sinf(kPi * t) * sinf(kPi * t);
    const float v = s->data[start + i] + band * envelope * level;
    s->data[start + i] = static_cast<int16_t>(v);
  }
  if (start + n > s->frames) s->frames = start + n < kMaxFrames ? start + n
                                                                : kMaxFrames;
}

// Adds a sine that rises in 3 ms and then falls with the time constant
// `fall`.
void AddTone(Sound* s, int start, float seconds, float hz, float fall,
             float level) {
  const int n = static_cast<int>(seconds * kRate);
  for (int i = 0; i < n && start + i < kMaxFrames; ++i) {
    const float t = static_cast<float>(i) / kRate;
    const float rise = t < 0.003f ? t / 0.003f : 1.0f;
    // The last 5 ms go to zero, so the end makes no step.
    const float left = static_cast<float>(n - i) / kRate;
    const float end = left < 0.005f ? left / 0.005f : 1.0f;
    const float v = s->data[start + i] +
                    sinf(2.0f * kPi * hz * t) * expf(-t / fall) * rise * end *
                        level;
    s->data[start + i] = static_cast<int16_t>(v);
  }
  if (start + n > s->frames) s->frames = start + n < kMaxFrames ? start + n
                                                                : kMaxFrames;
}

void Build() {
  memset(g_sounds, 0, sizeof(g_sounds));
  // A turn: a short swish.
  AddSwoosh(&g_sounds[kAudioCueTurnLeft], 0, 0.10f, 1700.0f, 800.0f, 0.9f,
            1500.0f);
  AddSwoosh(&g_sounds[kAudioCueTurnRight], 0, 0.10f, 800.0f, 1700.0f, 0.9f,
            1500.0f);
  // A zoom step: the pitch goes up to go in, and down to go out.
  AddSwoosh(&g_sounds[kAudioCueZoomIn], 0, 0.15f, 450.0f, 1300.0f, 0.8f,
            1500.0f);
  AddSwoosh(&g_sounds[kAudioCueZoomOut], 0, 0.15f, 1300.0f, 450.0f, 0.8f,
            1500.0f);
  // A teleport: a whoosh, then a soft landing.
  AddSwoosh(&g_sounds[kAudioCueTeleport], 0, 0.24f, 350.0f, 2000.0f, 0.7f,
            1800.0f);
  AddTone(&g_sounds[kAudioCueTeleport], static_cast<int>(0.19f * kRate),
          0.12f, 72.0f, 0.035f, 3800.0f);
  // A grab of the land: a soft thud.
  AddTone(&g_sounds[kAudioCueGrab], 0, 0.10f, 105.0f, 0.028f, 3200.0f);
  AddSwoosh(&g_sounds[kAudioCueGrab], 0, 0.03f, 500.0f, 300.0f, 1.0f, 500.0f);
  // A lock of the grab from a distance: a small ping.
  AddTone(&g_sounds[kAudioCueLock], 0, 0.20f, 1320.0f, 0.045f, 2100.0f);
  AddTone(&g_sounds[kAudioCueLock], 0, 0.20f, 2640.0f, 0.030f, 600.0f);
  // An object arrives in the hand: a tap.
  AddTone(&g_sounds[kAudioCueArrive], 0, 0.08f, 235.0f, 0.018f, 3000.0f);
  AddSwoosh(&g_sounds[kAudioCueArrive], 0, 0.02f, 1500.0f, 900.0f, 1.0f,
            600.0f);
  // The choice of the ring moves to an item: a small, dry tick.
  AddTone(&g_sounds[kAudioCueRingMove], 0, 0.04f, 880.0f, 0.008f, 1500.0f);
  // The player chooses an item: two soft notes, the second one higher.
  AddTone(&g_sounds[kAudioCueRingChoose], 0, 0.10f, 660.0f, 0.030f, 1800.0f);
  AddTone(&g_sounds[kAudioCueRingChoose], static_cast<int>(0.06f * kRate),
          0.14f, 990.0f, 0.040f, 1800.0f);
}

}  // namespace

void AudioCuesInstall() {
  g_on = AudioIni("audio_cues", 1) != 0;
  int pct = AudioIni("audio_cues_pct", 100);
  if (pct < 0) pct = 0;
  if (pct > 300) pct = 300;
  g_level = pct / 100.0f;
  g_zoom_step = 1.0f + AudioIni("god_zoom_step_pct", 50) / 100.0f;
  if (!g_on) {
    ModLog("audio cues   : audio_cues=0, the moves of the view have no sound");
    return;
  }
  Build();
  ModLog("audio cues   : soft sounds for a turn, a zoom step, a teleport, a "
         "grab, a lock, an arrival and the ring, at %d%% (audio_cues_pct)",
         pct);
}

void AudioCue(int kind, float pan) {
  if (!g_on || kind < 0 || kind >= kAudioCueCount) return;
  // One at a time: a cue of a kind waits 60 ms for the next one.
  const DWORD now = GetTickCount();
  if (now - g_last_cue_tick[kind] < 60) return;
  g_last_cue_tick[kind] = now;
  const Sound& s = g_sounds[kind];
  if (!AudioVoicesCue(s.data, s.frames, g_level, pan)) return;
  ++g_played[kind];
  // A desktop run cuts its capture at these lines (tools/audio_measure.py).
  if (AudioTraceOn()) {
    ModLog("audio test   : step %llu cue %s, capture frame %ld",
           g_played[kind], kNames[kind], AudioCaptureFrames());
  }
}

// From HapticsPulseHands: a pulse of this kind goes out now.
void AudioCueHaptic(int kind) {
  static DWORD last_arrive = 0;
  switch (kind) {
    case haptics_math::kLock:
      AudioCue(kAudioCueLock, 0.0f);
      break;
    case haptics_math::kArrive: {
      // The pulse comes in each frame of the end of the glide. One arrival
      // has one tap.
      const DWORD now = GetTickCount();
      const bool first = now - last_arrive > 400;
      last_arrive = now;
      if (first) AudioCue(kAudioCueArrive, 0.0f);
      break;
    }
    case haptics_math::kWorld:
      AudioCue(kAudioCueGrab, 0.0f);
      break;
    default:
      break;
  }
}

// Once per rendered frame, with the ears of the frame. owned: the table or
// the God view owns the view, at `scale` units a meter.
void AudioCuesFrame(bool have_ears, const float basis[9],
                    const float position[3], bool owned, float scale) {
  if (!g_on) return;
  const DWORD now = GetTickCount();
  if (g_last_report == 0) g_last_report = now;
  if (now - g_last_report >= 30000) {
    g_last_report = now;
    uint64_t sum = 0;
    for (int i = 0; i < kAudioCueCount; ++i) sum += g_played[i];
    if (sum != g_reported) {
      g_reported = sum;
      ModLog("audio cues   : played %llu turn, %llu zoom, %llu teleport, "
             "%llu grab, %llu lock, %llu arrival and %llu ring cues",
             g_played[kAudioCueTurnLeft] + g_played[kAudioCueTurnRight],
             g_played[kAudioCueZoomIn] + g_played[kAudioCueZoomOut],
             g_played[kAudioCueTeleport], g_played[kAudioCueGrab],
             g_played[kAudioCueLock], g_played[kAudioCueArrive],
             g_played[kAudioCueRingMove] + g_played[kAudioCueRingChoose]);
    }
  }
  if (!have_ears || !owned || !(scale > 0.0f)) {
    g_have_last = false;
    g_owned_frames = 0;
    return;
  }
  const float yaw = atan2f(basis[2], basis[8]);
  // The view came back a moment ago (a story ended, a land loaded): its
  // first frames are no move of the player.
  bool settled = ++g_owned_frames > 45 && g_have_last;
  float unused_a[3], unused_b[3];
  uint64_t version = 0;
  if (TabletopGodGameCamera(unused_a, unused_b, &version)) {
    if (version == g_last_version) settled = false;
    g_last_version = version;
  }
  if (settled) {
    float turn = yaw - g_last_yaw;
    while (turn > kPi) turn -= 2.0f * kPi;
    while (turn < -kPi) turn += 2.0f * kPi;
    const float d[3] = {position[0] - g_last_position[0],
                        position[1] - g_last_position[1],
                        position[2] - g_last_position[2]};
    const float moved =
        sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / scale;
    // A zoom step changes the scale by one fixed ratio. Two hands that
    // scale the land change it a little in each frame, and that has no cue.
    const float ratio = scale / g_last_scale;
    const float change = ratio > 1.0f ? ratio : 1.0f / ratio;
    const float step = g_zoom_step - 1.0f;
    if (change > 1.0f + 0.6f * step && change < 1.0f + 1.4f * step) {
      AudioCue(ratio > 1.0f ? kAudioCueZoomOut : kAudioCueZoomIn, 0.0f);
    } else if (change > 1.02f) {
      // The land scales in the hands.
    } else if (moved > 0.5f) {
      AudioCue(kAudioCueTeleport, 0.0f);
    } else if (turn > 0.26f) {
      AudioCue(kAudioCueTurnRight, 0.5f);
    } else if (turn < -0.26f) {
      AudioCue(kAudioCueTurnLeft, -0.5f);
    }
  }
  memcpy(g_last_position, position, sizeof(g_last_position));
  g_last_yaw = yaw;
  g_last_scale = scale;
  g_have_last = true;
}
