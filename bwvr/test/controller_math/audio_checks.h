// SOUND IN THE ROOM (docs/audio.md, "Tools and tests without a headset").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <vector>

#include "../../src/mod/audio_math.h"
#include "../../src/mod/audio_spatial.h"

// The level of a band of a signal in dB, from the power at 16 frequencies
// of the band (Goertzel).
inline double AudioBand(const std::vector<float>& x, double rate, double lo,
                        double hi) {
  double total = 0.0;
  const int kPoints = 16;
  for (int p = 0; p < kPoints; ++p) {
    const double f = lo * pow(hi / lo, (p + 0.5) / kPoints);
    const double w = 2.0 * 3.14159265358979 * f / rate;
    const double c = 2.0 * cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
      const double s0 = x[i] + c * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    total += s1 * s1 + s2 * s2 - c * s1 * s2;
  }
  return 10.0 * log10(total / kPoints / (x.size() * x.size() * 0.25) + 1e-20);
}

inline double AudioRms(const std::vector<float>& x, size_t from) {
  double sum = 0.0;
  for (size_t i = from; i < x.size(); ++i) sum += x[i] * x[i];
  return sqrt(sum / (x.size() - from));
}

// White noise, the same in each run.
inline std::vector<float> AudioNoise(int count, unsigned seed) {
  std::vector<float> x(count);
  for (int i = 0; i < count; ++i) {
    seed = seed * 1664525u + 1013904223u;
    x[i] = static_cast<float>(static_cast<int>(seed)) / 2147483648.0f;
  }
  return x;
}

// One second of noise from one place, through the model, for each ear.
inline void AudioRenderPlace(float azimuth_deg, float elevation_deg,
                             float meters, float part,
                             std::vector<float>* left,
                             std::vector<float>* right) {
  namespace sp = audio_spatial;
  const float rate = 22050.0f;
  const std::vector<float> in = AudioNoise(22050, 777u);
  left->assign(in.size(), 0.0f);
  right->assign(in.size(), 0.0f);
  const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  const float ears[3] = {0, 0, 0};
  const float az = azimuth_deg / 57.29578f, el = elevation_deg / 57.29578f;
  const float point[3] = {sinf(az) * cosf(el) * meters, sinf(el) * meters,
                          cosf(az) * cosf(el) * meters};
  float distance = 0.0f;
  const sp::Aim aim = sp::AimOf(basis, ears, point, &distance);
  const float focus = sp::NearFocus(distance);
  sp::Target t;
  t.lateral = aim.lateral * focus;
  t.elevation = aim.elevation;
  t.back = aim.back * focus;
  t.gain = 1.0f;
  t.corner = sp::DistanceCorner(part);
  t.diffuse = sp::DiffuseShare(part);
  t.height = 1.0f;
  sp::Voice voice;
  sp::Reset(&voice);
  sp::Process(&voice, t, rate, in.data(), static_cast<int>(in.size()),
              left->data(), right->data());
}

inline void AudioPrintEars(const char* name, const std::vector<float>& l,
                           const std::vector<float>& r) {
  static const double kEdges[7] = {177, 354, 707, 1414, 2828, 5657, 11000};
  printf("  %-22s L", name);
  for (int b = 0; b < 6; ++b) {
    printf(" %6.1f", AudioBand(l, 22050.0, kEdges[b], kEdges[b + 1]));
  }
  printf("   R");
  for (int b = 0; b < 6; ++b) {
    printf(" %6.1f", AudioBand(r, 22050.0, kEdges[b], kEdges[b + 1]));
  }
  printf("\n");
}

void AudioChecks() {
  namespace am = audio_math;
  printf("\nSound in the room: the QMixer distance gain\n");
  Near("inside min, full level", am::QMixerDistanceGain(40, 50, 1000, 0.3f),
       1.0, 1e-6);
  Near("at min, full level", am::QMixerDistanceGain(50, 50, 1000, 0.3f), 1.0,
       1e-6);
  Near("past max, silent", am::QMixerDistanceGain(1001, 50, 1000, 0.3f), 0.0,
       1e-6);
  Near("rolloff 1 is min / d", am::QMixerDistanceGain(200, 50, 1000, 1.0f),
       0.25, 1e-6);
  Near("rolloff 0.3 is min / (min + 0.3 (d - min))",
       am::QMixerDistanceGain(250, 50, 1000, 0.3f), 50.0 / (50.0 + 60.0),
       1e-6);
  Near("rolloff 0 is full level to max",
       am::QMixerDistanceGain(900, 50, 1000, 0.0f), 1.0, 1e-6);

  printf("Sound in the room: the room factor\n");
  Near("at the reference scale nothing changes",
       am::RoomFactor(100.0f, 100.0f, 0.05f, 100.0f), 1.0, 1e-6);
  Near("zoomed out to 4000 units a meter, k is 40",
       am::RoomFactor(4000.0f, 100.0f, 0.05f, 100.0f), 40.0, 1e-4);
  Near("at the life size scale, k is 0.075",
       am::RoomFactor(7.5f, 100.0f, 0.05f, 100.0f), 0.075, 1e-6);
  Near("the lowest limit holds",
       am::RoomFactor(1.0f, 100.0f, 0.05f, 100.0f), 0.05, 1e-6);
  Near("the highest limit holds",
       am::RoomFactor(20000.0f, 100.0f, 0.05f, 100.0f), 100.0, 1e-4);
  Near("no scale gives 1", am::RoomFactor(0.0f, 100.0f, 0.05f, 100.0f), 1.0,
       1e-6);

  printf("Sound in the room: one room distance, every zoom\n");
  {
    const float game[3] = {50.0f, 1500.0f, 0.3f};
    const float scales[4] = {7.5f, 100.0f, 1000.0f, 4000.0f};
    const float meters[3] = {0.3f, 2.0f, 12.0f};
    for (int m = 0; m < 3; ++m) {
      // The gain of the game's mapping at meters * 100 units.
      const double want = am::QMixerDistanceGain(meters[m] * 100.0f, game[0],
                                                 game[1], game[2]);
      for (int i = 0; i < 4; ++i) {
        const float k = am::RoomFactor(scales[i], 100.0f, 0.05f, 100.0f);
        float sent[3];
        am::ScaleMapping(game, k, sent);
        const double got = am::QMixerDistanceGain(meters[m] * scales[i],
                                                  sent[0], sent[1], sent[2]);
        char what[160];
        sprintf_s(what, "%.1f m at %.1f units a meter sounds as at 100",
                  meters[m], scales[i]);
        Near(what, got, want, 1e-5);
      }
    }
    float sent[3];
    am::ScaleMapping(game, 40.0f, sent);
    Near("the rolloff itself does not change", sent[2], 0.3, 1e-6);
    Near("min times k", sent[0], 2000.0, 1e-3);
    Near("max times k", sent[1], 60000.0, 1e-2);
  }

  printf("Sound in the room: when to send the mapping again\n");
  Check(!am::FactorChanged(10.0f, 10.05f, 0.01f), "a change of 0.5% waits",
        10.05, 10.0);
  Check(am::FactorChanged(10.0f, 10.2f, 0.01f), "a change of 2% goes out",
        10.2, 10.0);
  Check(am::FactorChanged(0.0f, 1.0f, 0.01f), "nothing sent yet goes out", 1,
        0);

  printf("Sound in the room: the azimuth in the frame of the ears\n");
  {
    // Columns right, up and forward, for a head that faces +z.
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float ears[3] = {100.0f, 50.0f, 200.0f};
    const float ahead[3] = {100.0f, 20.0f, 300.0f};
    const float right[3] = {150.0f, 50.0f, 200.0f};
    const float left[3] = {50.0f, 80.0f, 200.0f};
    const float behind[3] = {100.0f, 50.0f, 150.0f};
    const float above[3] = {100.0f, 90.0f, 200.01f};
    float a = -1.0f, d = 0.0f;
    Check(am::Azimuth(basis, ears, ahead, 0.05f, &a, &d), "ahead has an angle",
          a, 0);
    Near("ahead is 0", a, 0.0, 1e-3);
    Near("and the distance counts the height too", d,
         sqrt(100.0 * 100.0 + 30.0 * 30.0), 1e-3);
    am::Azimuth(basis, ears, right, 0.05f, &a, &d);
    Near("right is 90", a, 90.0, 1e-3);
    am::Azimuth(basis, ears, left, 0.05f, &a, &d);
    Near("left is 270", a, 270.0, 1e-3);
    am::Azimuth(basis, ears, behind, 0.05f, &a, &d);
    Near("behind is 180", a, 180.0, 1e-3);
    a = 12.0f;
    Check(!am::Azimuth(basis, ears, above, 0.05f, &a, &d),
          "straight above has no stable angle", a, 12.0);
    // The head turns 90 degrees to the right: its forward is +x.
    const float turned[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    am::Azimuth(turned, ears, right, 0.05f, &a, &d);
    Near("turned right, the point on the right is ahead", a, 0.0, 1e-3);
    am::Azimuth(turned, ears, ahead, 0.05f, &a, &d);
    Near("and the old ahead is on the left", a, 270.0, 1e-3);

    printf("Sound in the room: the elevation\n");
    const float up45[3] = {100.0f, 150.0f, 300.0f};
    Near("45 degrees up", am::Elevation(basis, ears, up45), 45.0, 1e-3);
    const float down[3] = {130.0f, 10.0f, 200.0f};
    Near("the land at the waist, to the right, is below",
         am::Elevation(basis, ears, down), -53.1301, 1e-3);
  }

  printf("Sound in the room: a sound of the atmosphere gets a place\n");
  {
    // The head faces +x: right is -z, up is +y, forward is +x.
    const float basis[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    const float ears[3] = {1000.0f, 60.0f, 2000.0f};
    // The DLL put a bird 8 units to the right of the ears and 4 up.
    const float local[3] = {8.0f, 4.0f, 0.0f};
    float p[3];
    Check(am::PointFromEars(basis, ears, local, 300.0f, p),
          "a direction gives a point", 0, 0);
    Near("300 units from the ears (3 m at 100 units a meter)",
         sqrt((p[0] - ears[0]) * (p[0] - ears[0]) +
              (p[1] - ears[1]) * (p[1] - ears[1]) +
              (p[2] - ears[2]) * (p[2] - ears[2])),
         300.0, 1e-2);
    float a = -1.0f, d = 0.0f;
    am::Azimuth(basis, ears, p, 0.05f, &a, &d);
    Near("at the start it is where the DLL put it: to the right", a, 90.0,
         1e-3);
    Near("and up by atan(4 / 8)", am::Elevation(basis, ears, p),
         atan(0.5) * 57.29578, 1e-3);
    // The head turns 90 degrees to the right. The bird stays in the world,
    // so now it is ahead.
    const float turned[9] = {-1, 0, 0, 0, 1, 0, 0, 0, -1};
    am::Azimuth(turned, ears, p, 0.05f, &a, &d);
    Near("the head turns right, the bird is now ahead", a, 0.0, 1e-3);
    const float none[3] = {0.0f, 0.0f, 0.0f};
    Check(!am::PointFromEars(basis, ears, none, 300.0f, p),
          "no direction gives no point", 0, 0);
  }

  printf("Sound in the room: the ears decide what plays\n");
  {
    // The God view at 100 units a meter. The game camera is 400 units from
    // the land under the waist, 50 degrees up, behind the player. The head
    // is 60 units over that land. A portal is 3 m ahead on the land.
    const float focus[3] = {1000.0f, 0.0f, 1000.0f};
    const float camera[3] = {1000.0f, 400.0f * 0.7660f,
                             1000.0f - 400.0f * 0.6428f};
    const float ears[3] = {1000.0f, 60.0f, 1000.0f};
    const float portal[3] = {focus[0], 0.0f, focus[2] + 300.0f};
    const float max = 500.0f;
    const float k = am::RoomFactor(100.0f, 40.0f, 0.05f, 100.0f);
    const float game = am::RangeDistance(camera, portal, 1.0f);
    const float room = am::RangeDistance(ears, portal, k);
    Near("the game camera is 636 units from the portal", game, 635.8, 0.5);
    Check(am::RangeStops(game, max) && !am::RangeStarts(game, max),
          "the game camera stops the loop and does not start it", game, max);
    Near("the head is 3.06 m from it, which sounds as 122 units", room, 122.4,
         0.2);
    Check(am::RangeStarts(room, max) && !am::RangeStops(room, max),
          "the head starts the loop and does not stop it", room, max);
    // The two tests give the range that QMixer uses, at every zoom.
    const float scales[4] = {7.5f, 100.0f, 1000.0f, 4000.0f};
    for (int i = 0; i < 4; ++i) {
      const float ki = am::RoomFactor(scales[i], 40.0f, 0.05f, 100.0f);
      float mapping[3];
      const float gamemap[3] = {100.0f, max, 4.0f};
      am::ScaleMapping(gamemap, ki, mapping);
      const float e[3] = {0.0f, 0.0f, 0.0f};
      const float in[3] = {0.0f, 0.0f, max * ki * 0.99f};
      const float past[3] = {0.0f, 0.0f, max * ki * 1.01f};
      char what[160];
      sprintf_s(what, "at %.1f units a meter a sound that QMixer hears plays",
                scales[i]);
      Check(am::RangeStarts(am::RangeDistance(e, in, ki), max) &&
                !am::RangeStops(am::RangeDistance(e, in, ki), max) &&
                am::QMixerDistanceGain(in[2], mapping[0], mapping[1],
                                       mapping[2]) > 0.0f,
            what, in[2], mapping[1]);
      sprintf_s(what, "at %.1f units a meter a sound past the range stops",
                scales[i]);
      Check(!am::RangeStarts(am::RangeDistance(e, past, ki), max) &&
                am::RangeStops(am::RangeDistance(e, past, ki), max) &&
                am::QMixerDistanceGain(past[2], mapping[0], mapping[1],
                                       mapping[2]) == 0.0f,
            what, past[2], mapping[1]);
      sprintf_s(what, "at %.1f units a meter the range is 12.5 m of the room",
                scales[i]);
      Near(what, max * ki / scales[i], 12.5, 1e-3);
    }
    Check(am::RangeStops(max, max) && am::RangeStarts(max, max),
          "at the maximum itself a sound can start, and it stops", max, max);
    Near("no factor leaves the world distance",
         am::RangeDistance(ears, portal, 0.0f), 306.0, 0.1);
  }

  printf("Sound in the room: a loop out of range keeps its slot\n");
  Check(am::SlotReserve(16) == 8, "of 16 slots, 8 are the reserve",
        am::SlotReserve(16), 8);
  Check(am::SlotReserve(6) == 4, "the reserve is 4 at least",
        am::SlotReserve(6), 4);
  Check(am::KeepSilentLoop(8, 16), "with 8 of 16 free the loop stays", 8, 16);
  Check(!am::KeepSilentLoop(7, 16), "with 7 of 16 free the loop stops", 7,
        16);
  Check(!am::KeepSilentLoop(0, 0), "with no count of the slots it stops", 0,
        0);

  printf("Sound in the room: the zoom changes how far a sound carries\n");
  Near("with the power 1 the factor is the room factor",
       am::ZoomFactor(1000.0f, 40.0f, 100.0f, 1.0f, 0.05f, 100.0f),
       am::RoomFactor(1000.0f, 40.0f, 0.05f, 100.0f), 1e-4);
  Near("at the start scale the two are the same with each power",
       am::ZoomFactor(100.0f, 40.0f, 100.0f, 0.5f, 0.05f, 100.0f), 2.5, 1e-5);
  {
    const float k = am::ZoomFactor(4000.0f, 40.0f, 100.0f, 0.5f, 0.05f,
                                   100.0f);
    Near("at 4000 units a meter the factor is 15.8, not 100", k, 15.811,
         0.001);
    Near("a sound with a maximum of 300 units carries 1.2 m of the room",
         300.0 * k / 4000.0, 1.186, 0.001);
    Near("and one with a maximum of 2000 units carries 7.9 m",
         2000.0 * k / 4000.0, 7.906, 0.001);
    const float k1 = am::ZoomFactor(100.0f, 40.0f, 100.0f, 0.5f, 0.05f,
                                    100.0f);
    Near("at 100 units a meter the first carries 7.5 m, as before",
         300.0 * k1 / 100.0, 7.5, 1e-4);
    const float k0 = am::ZoomFactor(7.5f, 40.0f, 100.0f, 0.5f, 0.05f, 100.0f);
    Near("at the life size scale it carries 27 m (40 m in the game)",
         300.0 * k0 / 7.5, 27.386, 0.001);
  }
  Near("no scale gives 1",
       am::ZoomFactor(0.0f, 40.0f, 100.0f, 0.5f, 0.05f, 100.0f), 1.0, 1e-6);

  namespace sp = audio_spatial;
  const double kDeg = 3.14159265358979 / 180.0;
  printf("The mixer of the mod: the delay and the shadow of the head\n");
  Near("a source at the right: the left ear is 0.656 ms late",
       sp::InterauralDelay(static_cast<float>(90 * kDeg)) * 1000.0, 0.656,
       0.002);
  Near("a source ahead: no delay", sp::InterauralDelay(0.0f), 0.0, 1e-9);
  Near("a source at the left: the same delay, the other way",
       sp::InterauralDelay(static_cast<float>(-90 * kDeg)) * 1000.0, -0.656,
       0.002);
  Near("at the ear the high frequencies double", sp::ShadowHigh(0.0f), 2.0,
       1e-5);
  Near("150 degrees from the ear they are 0.1",
       sp::ShadowHigh(static_cast<float>(150 * kDeg)), 0.1, 1e-5);
  {
    float b0, b1, a1;
    sp::ShadowFilter(0.1f, 22050.0f, &b0, &b1, &a1);
    Near("the shadow filter passes 0 Hz at 1", (b0 + b1) / (1.0 + a1), 1.0,
         1e-5);
    Near("and half the rate at the high gain", (b0 - b1) / (1.0 - a1), 0.1,
         1e-5);
  }
  printf("The mixer of the mod: the outer ear\n");
  Near("the notch of the second echo for a source straight above, Hz",
       1.0 / sp::PinnaDelay(1, 0.0f, static_cast<float>(90 * kDeg)), 11025.0,
       1.0);
  Near("and for a source straight below, Hz",
       1.0 / sp::PinnaDelay(1, 0.0f, static_cast<float>(-90 * kDeg)), 4900.0,
       1.0);
  {
    double sum = 0.0;
    for (int e = 0; e < sp::kPinnaEchoes; ++e) sum += sp::kPinnaReflection[e];
    Near("the echoes add nothing at 0 Hz", sum, 0.0, 1e-6);
  }

  printf("The mixer of the mod: the two ears, bands 250 Hz to 8 kHz in dB\n");
  std::vector<float> l, r, l2, r2;
  AudioRenderPlace(0, -60, 1.0f, 0.0f, &l, &r);
  AudioPrintEars("ahead, 60 deg below", l, r);
  const double below_4k = AudioBand(l, 22050.0, 2828, 5657);
  const double below_8k = AudioBand(l, 22050.0, 5657, 11000);
  const double below_h = AudioBand(l, 22050.0, 6500, 9000);
  const double below_1k = AudioBand(l, 22050.0, 707, 1414);
  AudioRenderPlace(0, 0, 1.0f, 0.0f, &l, &r);
  AudioPrintEars("ahead, level", l, r);
  const double level_4k = AudioBand(l, 22050.0, 2828, 5657);
  const double level_8k = AudioBand(l, 22050.0, 5657, 11000);
  const double level_h = AudioBand(l, 22050.0, 6500, 9000);
  const double level_1k = AudioBand(l, 22050.0, 707, 1414);
  Near("a source ahead is the same in the two ears, dB", AudioRms(l, 2000),
       AudioRms(r, 2000), 1e-6);
  AudioRenderPlace(0, 60, 1.0f, 0.0f, &l, &r);
  AudioPrintEars("ahead, 60 deg above", l, r);
  const double above_4k = AudioBand(l, 22050.0, 2828, 5657);
  const double above_8k = AudioBand(l, 22050.0, 5657, 11000);
  const double above_h = AudioBand(l, 22050.0, 6500, 9000);
  printf("  4 kHz band: below %.1f, level %.1f, above %.1f dB. 8 kHz band: "
         "below %.1f, level %.1f, above %.1f dB. 6.5 to 9 kHz: below %.1f, "
         "level %.1f, above %.1f dB\n", below_4k, level_4k, above_4k,
         below_8k, level_8k, above_8k, below_h, level_h, above_h);
  Check(level_8k - below_8k > 5.0,
        "a source at the level of the ears has 5 dB more in the 8 kHz band "
        "than one 60 degrees below", level_8k - below_8k, 5.0);
  Check(above_8k - below_8k > 6.0,
        "and a source 60 degrees above has 6 dB more there than one below",
        above_8k - below_8k, 6.0);
  Check(above_8k - level_8k > 2.0,
        "and 2 dB more than one at the level", above_8k - level_8k, 2.0);
  Check(level_h - below_h > 5.0,
        "from 6.5 to 9 kHz one at the level has 5 dB more than one below",
        level_h - below_h, 5.0);
  (void)above_h;
  Check(above_4k - below_4k > 2.0,
        "the 4 kHz band also has more for a source above, dB",
        above_4k - below_4k, 2.0);
  Near("the height band is +7 dB straight above",
       sp::HeightDb(static_cast<float>(90 * kDeg), 1.0f), 7.0, 1e-4);
  Near("and -9 dB straight below",
       sp::HeightDb(static_cast<float>(-90 * kDeg), 1.0f), -9.0, 1e-4);
  Near("and 0 at the level of the ears", sp::HeightDb(0.0f, 1.0f), 0.0, 1e-6);
  {
    float c[5];
    sp::ShelfFilter(0.0f, sp::kHeightHz, 22050.0f, c);
    Near("a shelf of 0 dB changes nothing: b0", c[0], 1.0, 1e-6);
    Near("b1 is a1", c[1], c[3], 1e-6);
    Near("b2 is a2", c[2], c[4], 1e-6);
    sp::ShelfFilter(6.0f, sp::kHeightHz, 22050.0f, c);
    Near("a shelf of 6 dB passes 0 Hz at 1",
         (c[0] + c[1] + c[2]) / (1.0 + c[3] + c[4]), 1.0, 1e-4);
    Near("and half the rate at 6 dB",
         20.0 * log10((c[0] - c[1] + c[2]) / (1.0 - c[3] + c[4])), 6.0,
         1e-3);
  }
  Check(level_1k - below_1k < 2.5 && level_1k - below_1k > -2.5,
        "the 1 kHz band changes little with the height, dB",
        level_1k - below_1k, 0.0);

  AudioRenderPlace(90, 0, 1.0f, 0.0f, &l, &r);
  AudioPrintEars("at the right, level", l, r);
  const double ild_far = 20.0 * log10(AudioRms(r, 2000) / AudioRms(l, 2000));
  Check(ild_far > 6.0, "a source at the right is louder in the right ear, dB",
        ild_far, 6.0);
  {
    // The lag of the left ear, from the best match of the two ears.
    int best = 0;
    double most = -1e30;
    for (int lag = -24; lag <= 24; ++lag) {
      double sum = 0.0;
      for (size_t i = 4000; i < 12000; ++i) sum += r[i] * l[i + lag];
      if (sum > most) {
        most = sum;
        best = lag;
      }
    }
    Near("and the left ear gets it 14.5 samples later (0.66 ms at 22050 Hz)",
         best, 14.5, 1.01);
  }
  AudioRenderPlace(90, 0, 0.2f, 0.0f, &l2, &r2);
  AudioPrintEars("at the right, 20 cm", l2, r2);
  const double ild_near =
      20.0 * log10(AudioRms(r2, 2000) / AudioRms(l2, 2000));
  Check(ild_near < ild_far * 0.6 && ild_near > 0.5,
        "at 20 cm the difference between the ears is less than 0.6 of it, dB",
        ild_near, ild_far * 0.6);
  Near("the near field keeps 0.2 of the angle at 20 cm", sp::NearFocus(0.2f),
       0.2, 1e-6);
  Near("and all of it from 50 cm", sp::NearFocus(0.5f), 1.0, 1e-6);
  AudioRenderPlace(180, 0, 1.0f, 0.0f, &l2, &r2);
  AudioPrintEars("behind, level", l2, r2);
  AudioRenderPlace(0, 0, 1.0f, 0.0f, &l, &r);
  Check(AudioBand(l, 22050.0, 2828, 5657) - AudioBand(l2, 22050.0, 2828, 5657)
            > 1.5,
        "a source behind has less in the 4 kHz band than one ahead, dB",
        AudioBand(l, 22050.0, 2828, 5657) -
            AudioBand(l2, 22050.0, 2828, 5657), 1.5);

  printf("The mixer of the mod: distance\n");
  Near("the low pass is open at the minimum distance, Hz",
       sp::DistanceCorner(0.0f), 12000.0, 0.1);
  Near("5 kHz half way", sp::DistanceCorner(0.5f), 5018.4, 0.5);
  Near("1.2 kHz at the maximum distance", sp::DistanceCorner(1.0f), 1200.0,
       0.1);
  Near("the part of the range, half way", sp::RangePart(200.0f, 100.0f, 300.0f),
       0.5, 1e-6);
  Near("inside the minimum it is 0", sp::RangePart(50.0f, 100.0f, 300.0f), 0.0,
       1e-6);
  AudioRenderPlace(0, 0, 1.0f, 0.9f, &l2, &r2);
  AudioPrintEars("ahead, far (part 0.9)", l2, r2);
  Check(AudioBand(l, 22050.0, 2828, 5657) - AudioBand(l2, 22050.0, 2828, 5657)
            > 7.0,
        "a far sound loses more than 7 dB in the 4 kHz band",
        AudioBand(l, 22050.0, 2828, 5657) -
            AudioBand(l2, 22050.0, 2828, 5657), 7.0);
  Check(AudioBand(l, 22050.0, 177, 354) - AudioBand(l2, 22050.0, 177, 354) <
            2.0,
        "and less than 2 dB in the 250 Hz band",
        AudioBand(l, 22050.0, 177, 354) - AudioBand(l2, 22050.0, 177, 354),
        2.0);

  printf("The mixer of the mod: no click\n");
  {
    // A sine of 200 Hz. The voice starts from silence, and a level that
    // drops to 0 goes there in small steps.
    const float rate = 22050.0f;
    std::vector<float> in(8000), a(8000, 0.0f), b(8000, 0.0f);
    for (size_t i = 0; i < in.size(); ++i) {
      in[i] = sinf(2.0f * sp::kPi * 200.0f * i / rate);
    }
    sp::Voice voice;
    sp::Reset(&voice);
    sp::Target t = {0.0f, 0.0f, 0.0f, 1.0f, 12000.0f, 0.0f, 1.0f};
    sp::Process(&voice, t, rate, in.data(), 4000, a.data(), b.data());
    t.gain = 0.0f;
    sp::Process(&voice, t, rate, in.data() + 4000, 4000, a.data() + 4000,
                b.data() + 4000);
    double steady = 0.0, largest = 0.0;
    for (size_t i = 3000; i < 3999; ++i) {
      steady = fmax(steady, fabs(a[i + 1] - a[i]));
    }
    for (size_t i = 0; i + 1 < a.size(); ++i) {
      largest = fmax(largest, fabs(a[i + 1] - a[i]));
    }
    Near("the first sample of a voice is silent", a[0], 0.0, 1e-3);
    Check(fabs(a[60]) > 0.0 && fabs(a[400]) > 0.5,
          "and after 1.5 ms the voice has its full level, so an attack "
          "stays sharp", fabs(a[400]), 0.5);
    Check(largest < steady * 1.3,
          "no step at the start or the stop is larger than a step of the "
          "sound itself", largest, steady * 1.3);
    Near("80 ms after the stop the voice is silent", a[4000 + 1764], 0.0,
         0.01);
  }

  printf("The mixer of the mod: the sum stays inside 16 bits\n");
  Near("a sample under the knee does not change", sp::SoftLimit(12000.0f),
       12000.0, 1e-3);
  Near("nor a negative one", sp::SoftLimit(-25999.0f), -25999.0, 1e-3);
  Near("at the knee the curve has no step", sp::SoftLimit(26000.5f), 26000.5,
       0.01);
  Near("a sum of 40000 comes to 30562", sp::SoftLimit(40000.0f), 30561.9,
       0.5);
  Check(sp::SoftLimit(1e9f) <= 32767.0f && sp::SoftLimit(-1e9f) >= -32767.0f,
        "no sum goes past full level", sp::SoftLimit(1e9f), 32767.0);
  Check(sp::SoftLimit(30000.0f) < sp::SoftLimit(31000.0f),
        "a louder sum is still louder", sp::SoftLimit(31000.0f),
        sp::SoftLimit(30000.0f));

  printf("The mixer of the mod: the bed of the atmosphere turns\n");
  {
    const float rate = 22050.0f;
    const int n = 22050;
    const std::vector<float> wl = AudioNoise(n, 11u), wr = AudioNoise(n, 22u);
    std::vector<float> in(2 * n);
    for (int i = 0; i < n; ++i) {
      in[2 * i] = wl[i];
      in[2 * i + 1] = wr[i];
    }
    double rms_l[4], rms_r[4];
    for (int q = 0; q < 4; ++q) {
      sp::Bed bed;
      sp::ResetBed(&bed);
      std::vector<float> ol(n, 0.0f), orr(n, 0.0f);
      sp::ProcessBed(&bed, static_cast<float>(q * 90 * kDeg), 1.0f, rate,
                     in.data(), n, ol.data(), orr.data());
      rms_l[q] = AudioRms(ol, 4000);
      rms_r[q] = AudioRms(orr, 4000);
      if (q == 0) {
        Near("yaw 0: the left ear gets the left channel", ol[9000], wl[9000],
             1e-3);
        Near("and the right ear the right channel", orr[9000], wr[9000],
             1e-3);
      }
      if (q == 2) {
        Near("yaw 180: the left ear gets the right channel", ol[9000],
             wr[9000], 1e-3);
        Near("and the right ear the left channel", orr[9000], wl[9000], 1e-3);
      }
      if (q == 1) {
        double dot = 0.0, pl = 0.0, pr = 0.0;
        for (int i = 4000; i < n; ++i) {
          dot += ol[i] * orr[i];
          pl += ol[i] * ol[i];
          pr += orr[i] * orr[i];
        }
        Check(fabs(dot / sqrt(pl * pr)) < 0.2,
              "yaw 90: the two ears are not the same signal, so the bed "
              "keeps its width", dot / sqrt(pl * pr), 0.2);
      }
    }
    for (int q = 1; q < 4; ++q) {
      char what[120];
      sprintf_s(what, "yaw %d: the level of the left ear is as at yaw 0, dB",
                q * 90);
      Near(what, 20.0 * log10(rms_l[q] / rms_l[0]), 0.0, 0.5);
      sprintf_s(what, "yaw %d: the level of the right ear is as at yaw 0, dB",
                q * 90);
      Near(what, 20.0 * log10(rms_r[q] / rms_r[0]), 0.0, 0.5);
    }
    const float turned[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    Near("a head that faces +x has a yaw of 90 degrees",
         sp::HeadYaw(turned) / kDeg, 90.0, 1e-3);
  }

  printf("The release of a voice, and the wait of a stop\n");
  {
    using namespace audio_math;
    Near("a voice has 80 ms from the flush of its channel to the free slot",
         ReleaseLength(ReleaseStart(false)), 0.080, 1e-5);
    Near("with the wait of the stop skipped it has 100 ms",
         ReleaseLength(ReleaseStart(true)), 0.100, 1e-5);
    Near("the slot is free at the same time after the volume 0: the wait "
         "and 80 ms, or no wait and 100 ms",
         ReleaseLength(ReleaseStart(true)),
         kStopWaitSeconds + ReleaseLength(ReleaseStart(false)), 1e-5);
    Near("the wait of the audio DLL is 20 ms", kStopWaitSeconds, 0.020, 1e-6);
  }
}
