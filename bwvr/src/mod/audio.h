// Sound in the room. See docs/audio.md.
#pragma once

#include <stdint.h>
#include <windows.h>

//
// audio_listener.cpp owns the listener and the once per frame driver.
// audio_sources.cpp steers the voices of the advisors, and the flat
// effects and sounds of the atmosphere that have a place.
// audio_rolloff.cpp makes the distances of the positional sounds room
// distances.
// audio_rolloff.cpp: room distances for the positional sounds.
// others: the two hooks are needed for the trace or for the other flat
// sounds also with the room distances off.
bool AudioRolloffInstall(bool room, int units_per_meter, bool others);
// The LHSamplePlay hook calls these around the original.
void AudioRolloffPlayBegin(const void* options);
void AudioRolloffPlayEnd();
// Once per rendered frame. active: the ears are the head and the table or
// the God view owns the view, at units_per_meter.
void AudioRolloffFrame(bool active, float units_per_meter);
// True when the room distances apply at this scale (the table scale, or 0).
bool AudioRoomActive(float units_per_meter);

// True while the room distances apply: the ears are the head, and the table
// or the God view owns the view. factor gets k of this frame.
bool AudioRoomNow(float* factor);

// audio_listener.cpp: the ears that QMixer got in this frame, when they are
// the head. False when the ears are the game camera.
bool AudioEarsPosition(float out[3]);

// audio_range.cpp: the ears decide which positional sounds start and stop
// (docs/audio.md, "6.4 The ears decide what plays"). by_ears is audio_range, and keep_loops is
// audio_loops_keep. test_tag is a sample number for a test sound tag on the
// Creature, or 0.
bool AudioRangeInstall(bool by_ears, bool keep_loops, int test_tag,
                       int test_tag_bank);
// Once per rendered frame, for the log and the test tag.
void AudioRangeFrame();
// From the LHSamplePlay hook, after the original, with its result.
void AudioRangeNotePlay(void* info);
// The test key: true gives the decision back to the game camera.
void AudioRangeSetTestOff(bool off);
// audio_range, audio_loops_keep and the two test keys, before AudioInstall.
void AudioSetRange(bool by_ears, bool keep_loops, int test_tag,
                   int test_tag_bank);

// audio_listener.cpp: an int key of bwvr.ini in the game folder, for the
// audio keys that are not in the launcher.
int AudioIni(const char* key, int fallback);

// audio_capture.cpp: the stream that QMixer gives to the wave device
// (docs/audio.md, "11. Tools and tests without a headset"). capture writes it to bwvr-audio.wav. report
// writes its level to the log each second.
bool AudioCaptureInstall(bool capture, bool report);
// The mixer of the mod adds its sound to each block here: 16 bit stereo, on
// the mixer thread of QMixer.
typedef void (*AudioRenderFn)(int16_t* stereo, int frames, int rate);
void AudioCaptureSetRender(AudioRenderFn render);
// A hook that has the QMixer session gives it here, on the game thread. The
// first call finds the DirectSound buffer that QMixer mixes into.
void AudioCaptureSession(void* session);
// The frames that went to the device so far. The clock of the capture.
long AudioCaptureFrames();
// How far ahead of the device QMixer writes the stream, in ms. A turn of
// the head is heard that much later.
// passed: how many times the device passed the place of a write (a gap).
bool AudioCaptureLead(float* mean_ms, float* most_ms, float* least_ms,
                      long* passed);
// The rate of the stream in Hz, or 0 when it is not 16 bit stereo PCM.
int AudioOutRate();
// Once per rendered frame: the log lines of the hook.
void AudioCaptureFrame();

// The same for a text key. out is empty when the key is not there.
void AudioIniText(const char* key, char* out, int size);

// Who calls LHSamplePlay, for the mixer of the mod.
enum AudioPlayKind {
  kAudioOther,
  kAudioEffect,
  kAudioAnim,
  kAudioAtmos,
  kAudioAdvisor
};

// audio_voices.cpp: the mixer of the mod (docs/audio.md, "7. The mixer of the
// mod"). on is audio_mixer. With 0, QMixer pans each sound.
bool AudioVoicesInstall(bool on);
// The LHSamplePlay hook calls these around the original.
void AudioVoicesPlayBegin(int kind, const void* options);
void AudioVoicesPlayEnd();
// Between the two: the play is a single sound of the atmosphere, and the mod
// made this place in the world for it (audio_sources.cpp).
void AudioVoicesPlayPlace(const float point[3]);
// From the polar hook of audio_rolloff.cpp: what the DLL sends in a play.
void AudioVoicesPolar(int channel, const float polar[3]);
// From the hooks of audio_rolloff.cpp: the place that QMixer got for a
// channel, and the distance mapping of the game for it.
void AudioVoicesSource(int channel, const float point[3]);
void AudioVoicesMapping(int channel, const float game[3], bool positional);
// From audio_listener.cpp: the ears that QMixer got. forward and up have
// the length 1.
void AudioVoicesListener(const float position[3], const float forward[3],
                         const float up[3]);
// Once per rendered frame.
void AudioVoicesFrame();
// The test key: false gives the sounds with a place back to QMixer.
void AudioVoicesAudible(bool audible);
// A flat cue through the mixer: mono, 22050 Hz, pan -1 to 1.
bool AudioVoicesCue(const int16_t* data, int frames, float gain, float pan);
// audio_rolloff.cpp, the test key: true gives a room factor that is the same
// at each zoom.
void AudioRolloffSetLinear(bool linear);

// audio_cues.cpp: soft sounds for the player's own moves (docs/audio.md,
// "8. Cues for the moves of the player").
enum AudioCueKind {
  kAudioCueTurnLeft,
  kAudioCueTurnRight,
  kAudioCueZoomIn,
  kAudioCueZoomOut,
  kAudioCueTeleport,
  kAudioCueGrab,      // a hand takes the land
  kAudioCueLock,      // the grab from a distance has its target
  kAudioCueArrive,    // an object arrives in the hand
  kAudioCueRingMove,    // the choice of the ring moves to an item
  kAudioCueRingChoose,  // the player chooses an item of the ring
  kAudioCueCount
};
void AudioCuesInstall();
// Plays one cue. pan is -1 (left) to 1 (right). The moves of the view need
// no call: AudioCuesFrame sees them.
void AudioCue(int kind, float pan);
// From HapticsPulseHands (haptics.cpp): the cue that goes with a haptic
// pulse of this kind (haptics_math::Kind), if it has one.
void AudioCueHaptic(int kind);
void AudioCuesFrame(bool have_ears, const float basis[9],
                    const float position[3], bool owned, float scale);

// audio_trace.cpp: names for the log. They work with the trace off. The
// class comes from the RTTI of an object of the game, or null.
const char* AudioClassName(uintptr_t object);
// The place of an object of the game as its sound code sees it
// (Get3DSoundPos), or false.
bool AudioObjectPosition(uintptr_t object, float out[3]);
const char* AudioBankName(uintptr_t bank);

// audio_trace.cpp logs the calls into QMixer, for tests only.
#pragma once

#include <stdint.h>
#include <windows.h>

// The audio system object of the game, or null. The game keeps it at
// GAudio + 0x14, and GAudio is at 0x00CD3B20.
void* AudioSystem();
// A hook that received the audio system as its this pointer gives it here. It
// is the fallback when the game global is empty.
void AudioNoteSystem(void* system);

// The rendered head, for the ears. True only when the head camera drew a frame
// a moment ago and the test key has not put the ears back on the game camera.
// The basis columns are right, up and forward. See HeadCameraRenderBasis.
bool AudioHeadPose(float basis_out[9], float position_out[3]);

// The audio DLL and the QMixer module, or null before they are loaded.
HMODULE AudioAudioDll();
HMODULE AudioMixerDll();

// audio_room, audio_units_per_meter and audio_flat_places, before
// AudioInstall.
void AudioSetRoom(bool room, int units_per_meter, bool places);
void AudioInstall(bool head, bool advisors, bool trace);
// Once per rendered frame, from the FinishFrame hook. It latches itself.
void AudioFrame();
// Test key. Ctrl Shift F12.
void AudioCycleMode();

// audio_sources.cpp
// advisors: steer the advisors. flat: steer the flat effects and the
// sounds of the atmosphere that have a place. hook: the LHSamplePlay
// hook is needed for the trace or the room distances, also with both off.
bool AudioSourcesInstall(bool advisors, bool flat, bool hook);
// active: the head is fresh. basis and position are the head.
void AudioSourcesFrame(bool active, const float basis[9],
                       const float position[3]);
// From the polar hook of audio_rolloff.cpp, before the original call. It may
// change the azimuth and the elevation of the call.
void AudioSourcesPolar(int channel, float polar[3]);

// audio_trace.cpp
bool AudioTraceInstall();
void AudioTraceFrame();
// The play trace. The LHSamplePlay hook calls Begin before the original and
// End after it. stack points at the return address of the hook.
bool AudioTraceOn();
void AudioTracePlayBegin(void* system, const void* options, uintptr_t caller,
                         const uintptr_t* stack);
void AudioTracePlayEnd(void* system, void* info);
// From the hooks of audio_rolloff.cpp, after the original call.
void AudioTraceNotePolar(int channel, const void* polar);
void AudioTraceNoteDistance(int channel, const void* mapping);
void AudioTraceNoteSource(int channel, const void* position, DWORD result);
