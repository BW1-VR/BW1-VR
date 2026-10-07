#!/usr/bin/env python3
"""Measure what went to each ear in a desktop run.

A desktop run has no listener. With audio_capture=1 the mod writes the stream
that QMixer gives to the wave device, with the sound of the mod's own mixer
in it, to bwvr-audio.wav (16 bit stereo). This script measures that file, so
a change of the sound has a proof in numbers. See docs/audio.md, section 14.

Usage:
    python audio_measure.py run.wav
    python audio_measure.py run.wav --seg below:12.0:16.0 --seg above:20:24
    python audio_measure.py run.wav --log bwvr.log
    python audio_measure.py run.wav --log bwvr.log --compare level below
    python audio_measure.py run.wav --track 0.5

--log reads the lines "audio test   : step N <name>, capture frame F" of the
mod's log. Each step is a segment that ends at the next step.

For each segment the script prints:
    rms, peak     the level of each ear, in dB below full scale
    ild           left minus right, in dB
    itd           the delay of the right ear against the left ear, in
                  microseconds, from the cross correlation below 1.5 kHz
                  (more than 0: the sound is at the left)
    corr          the correlation of the ears at that delay (1 is mono)
    centroid      the center of the spectrum of each ear, in Hz
    bands         the level of each octave band, for each ear, in dB
    click         the largest step between two samples, against the rms

numpy is required.
"""

import argparse
import re
import sys

import numpy as np

BANDS = [63, 125, 250, 500, 1000, 2000, 4000, 8000]


def load(path):
    raw = open(path, "rb").read()
    if raw[:4] != b"RIFF" or len(raw) < 44:
        sys.exit("not a WAV file from the mod: " + path)
    rate = int.from_bytes(raw[24:28], "little")
    channels = int.from_bytes(raw[22:24], "little")
    bits = int.from_bytes(raw[34:36], "little")
    if channels != 2 or bits != 16:
        sys.exit("the capture must be 16 bit stereo")
    # The game can end before the mod writes the last header. So all bytes
    # after the header are data, whatever the header says.
    body = raw[44:]
    body = body[: len(body) // 4 * 4]
    data = np.frombuffer(body, dtype="<i2").reshape(-1, 2).astype(np.float64)
    return rate, data / 32768.0


def db(value):
    return 20.0 * np.log10(max(float(value), 1e-9))


def spectrum(x, rate, size=4096):
    """The mean power spectrum of x (Welch, Hann window, half overlap)."""
    if len(x) < size:
        size = max(256, 1 << int(np.log2(max(len(x), 256))))
        x = np.pad(x, (0, max(0, size - len(x))))
    window = np.hanning(size)
    step = size // 2
    total = np.zeros(size // 2 + 1)
    count = 0
    for start in range(0, len(x) - size + 1, step):
        frame = x[start:start + size] * window
        total += np.abs(np.fft.rfft(frame)) ** 2
        count += 1
    total /= max(count, 1) * np.sum(window ** 2)
    return np.fft.rfftfreq(size, 1.0 / rate), total


def band_levels(x, rate):
    freqs, power = spectrum(x, rate)
    out = []
    for center in BANDS:
        lo, hi = center / np.sqrt(2.0), center * np.sqrt(2.0)
        if lo >= rate / 2.0:
            out.append(float("nan"))
            continue
        pick = (freqs >= lo) & (freqs < hi)
        out.append(10.0 * np.log10(max(2.0 * np.sum(power[pick]), 1e-18)))
    centroid = float(np.sum(freqs * power) / max(np.sum(power), 1e-18))
    return out, centroid


def lowpass(x, rate, cutoff):
    spec = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(len(x), 1.0 / rate)
    spec[freqs > cutoff] = 0.0
    return np.fft.irfft(spec, len(x))


def itd(left, right, rate):
    """The delay of right against left, in microseconds, and the correlation."""
    n = min(len(left), int(rate * 8))
    a = lowpass(left[:n], rate, 1500.0)
    b = lowpass(right[:n], rate, 1500.0)
    na, nb = np.sqrt(np.sum(a * a)), np.sqrt(np.sum(b * b))
    if na < 1e-9 or nb < 1e-9:
        return 0.0, 0.0
    # 8 times oversampled lags up to 1 ms.
    up = 8
    spec_a = np.fft.rfft(a)
    spec_b = np.fft.rfft(b)
    cross = np.fft.irfft(np.conj(spec_a) * spec_b, n * up) * up
    span = int(0.001 * rate * up)
    lags = np.concatenate([np.arange(0, span + 1), np.arange(-span, 0)])
    values = np.concatenate([cross[:span + 1], cross[-span:]])
    best = int(np.argmax(values))
    return lags[best] / (rate * up) * 1e6, float(values[best] / (na * nb))


def measure(name, data, rate, start, end):
    part = data[int(start * rate):int(end * rate)]
    if len(part) < 256:
        return None
    left, right = part[:, 0], part[:, 1]
    rms_l, rms_r = np.sqrt(np.mean(left ** 2)), np.sqrt(np.mean(right ** 2))
    bands_l, cen_l = band_levels(left, rate)
    bands_r, cen_r = band_levels(right, rate)
    delay, corr = itd(left, right, rate)
    step = np.max(np.abs(np.diff(part, axis=0)), axis=0)
    return {
        "name": name, "start": start, "end": end,
        "rms": (db(rms_l), db(rms_r)),
        "peak": (db(np.max(np.abs(left))), db(np.max(np.abs(right)))),
        "ild": db(rms_l) - db(rms_r), "itd": delay, "corr": corr,
        "centroid": (cen_l, cen_r), "bands": (bands_l, bands_r),
        "click": (float(step[0] / max(rms_l, 1e-9)),
                  float(step[1] / max(rms_r, 1e-9))),
    }


def show(m):
    print("%s  %.2f to %.2f s" % (m["name"], m["start"], m["end"]))
    print("  rms      L %6.1f  R %6.1f dB   peak L %6.1f  R %6.1f dB"
          % (m["rms"] + m["peak"]))
    print("  ild %+5.1f dB   itd %+5.0f us   corr %.2f   centroid L %5.0f  "
          "R %5.0f Hz   click L %.1f  R %.1f"
          % (m["ild"], m["itd"], m["corr"], m["centroid"][0],
             m["centroid"][1], m["click"][0], m["click"][1]))
    print("  band   " + "".join("%7d" % b for b in BANDS))
    print("  L      " + "".join("%7.1f" % v for v in m["bands"][0]))
    print("  R      " + "".join("%7.1f" % v for v in m["bands"][1]))


def segments_from_log(path, rate, total):
    steps = []
    pattern = re.compile(r"audio test\s*: step \d+ (.*?), capture frame (\d+)")
    for line in open(path, encoding="latin-1"):
        found = pattern.search(line)
        if found:
            steps.append((found.group(1).strip(), int(found.group(2)) / rate))
    out = []
    for index, (name, start) in enumerate(steps):
        end = steps[index + 1][1] if index + 1 < len(steps) else total
        out.append((name, start, end))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("wav")
    parser.add_argument("--seg", action="append", default=[],
                        help="name:start:end, in seconds")
    parser.add_argument("--log", help="the bwvr.log of the run")
    parser.add_argument("--skip", type=float, default=0.3,
                        help="seconds to drop at the start of a log step")
    parser.add_argument("--compare", nargs=2, metavar=("A", "B"),
                        help="print B minus A for each band and each ear")
    parser.add_argument("--track", type=float,
                        help="print the level of each ear for each window "
                             "of this many seconds")
    args = parser.parse_args()

    rate, data = load(args.wav)
    total = len(data) / rate
    print("%s: %.2f s, %d Hz" % (args.wav, total, rate))
    if total <= 0:
        sys.exit("the capture is empty")

    if args.track:
        size = int(args.track * rate)
        for start in range(0, len(data) - size + 1, size):
            part = data[start:start + size]
            rms = np.sqrt(np.mean(part ** 2, axis=0))
            step = np.max(np.abs(np.diff(part, axis=0)), axis=0)
            print("  %7.2f s  L %6.1f  R %6.1f dB  step L %.3f  R %.3f"
                  % (start / rate, db(rms[0]), db(rms[1]), step[0], step[1]))
        return

    segments = []
    for text in args.seg:
        name, start, end = text.split(":")
        segments.append((name, float(start), float(end)))
    if args.log:
        for name, start, end in segments_from_log(args.log, rate, total):
            segments.append((name, start + args.skip, end))
    if not segments:
        segments.append(("all", 0.0, total))

    results = {}
    for name, start, end in segments:
        m = measure(name, data, rate, start, min(end, total))
        if m is None:
            print("%s: too short" % name)
            continue
        results[name] = m
        show(m)

    if args.compare:
        a, b = results.get(args.compare[0]), results.get(args.compare[1])
        if a is None or b is None:
            sys.exit("no segment with that name")
        print("%s minus %s" % (b["name"], a["name"]))
        print("  band   " + "".join("%7d" % x for x in BANDS))
        for ear, label in ((0, "L"), (1, "R")):
            diff = [y - x for x, y in zip(a["bands"][ear], b["bands"][ear])]
            print("  %s      " % label + "".join("%7.1f" % v for v in diff))
        print("  rms    L %+5.1f  R %+5.1f dB   ild %+5.1f dB   itd %+5.0f us"
              % (b["rms"][0] - a["rms"][0], b["rms"][1] - a["rms"][1],
                 b["ild"] - a["ild"], b["itd"] - a["itd"]))


if __name__ == "__main__":
    main()
