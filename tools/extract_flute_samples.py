#!/usr/bin/env python3
"""Cuts single flute notes out of solo flute recordings (e.g. the game's bard flute tracks, decoded to WAV)
for SkyrimSongs' sampled flute: finds notes held steady for half a second or more (YIN pitch tracking), keeps
the longest clean one for each pitch, tunes it exactly to its note, trims it to start at its attack, levels it,
and writes <outdir>/<midi note>.wav (48 kHz mono 16-bit). The plugin loads every <note>.wav it finds in
Data\\SKSE\\Plugins\\SkyrimSongs\\Samples\\Flute and plays the nearest one, retuned, for each note.

The recordings are game assets: the samples are for your own install, not for redistribution.

Usage: extract_flute_samples.py <outdir> <recording.wav> [...]   (decode .xwm first: ffmpeg -i x.xwm x.wav)
"""
import sys, os
import numpy as np
import scipy.io.wavfile as wf
from scipy.signal import resample_poly

SR = 48000
HOP = 480  # 10 ms


def load(path):
    sr, x = wf.read(path)
    x = x.astype(np.float32)
    if x.dtype.kind != 'f' or np.abs(x).max() > 2:
        x /= 32768.0
    if x.ndim > 1:
        x = x.mean(axis=1)
    if sr != SR:
        g = np.gcd(sr, SR)
        x = resample_poly(x, SR // g, sr // g).astype(np.float32)
    return x


def yin(x, win=2048, fmin=180, fmax=2600):
    tau_min, tau_max = int(SR / fmax), int(SR / fmin)
    frames = []
    for s in range(0, len(x) - win - tau_max, HOP):
        f = x[s:s + win + tau_max]
        e = float(np.sqrt(np.mean(f[:win] ** 2)))
        if e < 0.01:
            frames.append((0.0, 0.0, e))
            continue
        a = f[:win]
        d = np.array([np.sum((a - f[t:t + win]) ** 2) for t in range(tau_max + 1)])
        cm = np.ones_like(d)
        cs = np.cumsum(d[1:])
        cm[1:] = d[1:] * np.arange(1, len(d)) / np.maximum(cs, 1e-12)
        t = tau_min + int(np.argmin(cm[tau_min:]))
        conf = 1.0 - cm[t]
        tt = float(t)
        if 1 <= t < len(cm) - 1:
            p, q, r = cm[t - 1], cm[t], cm[t + 1]
            tt = t + 0.5 * (p - r) / (p - 2 * q + r + 1e-12)
        frames.append((SR / tt, conf, e))
    return frames


def notes(x, frames):
    out = []
    i = 0
    while i < len(frames):
        f, c, _ = frames[i]
        if f <= 0 or c < 0.85:
            i += 1
            continue
        m = 69 + 12 * np.log2(f / 440)
        j, mid = i, []
        while j < len(frames) and frames[j][0] > 0 and frames[j][1] > 0.8 and abs(69 + 12 * np.log2(frames[j][0] / 440) - m) < 0.35:
            mid.append(69 + 12 * np.log2(frames[j][0] / 440))
            j += 1
        dur = (j - i) * HOP / SR
        if dur >= 0.5 and np.std(mid) * 100 < 12:
            out.append(dict(start=i * HOP, end=j * HOP + 2048, midi=float(np.median(mid)), dur=dur, sd=float(np.std(mid) * 100)))
        i = max(j, i + 1)
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    best = {}
    for path in sys.argv[2:]:
        x = load(path)
        for n in notes(x, yin(x)):
            note = int(round(n['midi']))
            score = n['dur'] - n['sd'] / 20
            if note not in best or best[note][0] < score:
                best[note] = (score, x, n, path)
    for note, (_, x, n, path) in sorted(best.items()):
        a = max(0, n['start'] - int(0.03 * SR))  # a little before: the attack
        seg = x[a:n['end']].copy()
        # tune exactly: resample by the measured offset
        ratio = 2 ** ((n['midi'] - note) / 12)  # > 1: sharp, so stretch it
        idx = np.arange(0, len(seg) - 1, ratio)
        seg = np.interp(idx, np.arange(len(seg)), seg).astype(np.float32)
        seg -= seg.mean()
        # level: the sustained part to -16 dBFS RMS; fade the very ends
        body = seg[len(seg) // 4: 3 * len(seg) // 4]
        seg *= 0.158 / max(1e-6, float(np.sqrt(np.mean(body ** 2))))
        fade = int(0.005 * SR)
        seg[:fade] *= np.linspace(0, 1, fade)
        seg[-fade * 4:] *= np.linspace(1, 0, fade * 4)
        seg = np.clip(seg, -1, 1)
        wf.write(os.path.join(outdir, f'{note}.wav'), SR, (seg * 32000).astype(np.int16))
        print(f'{note}: {len(seg) / SR:.2f} s from {os.path.basename(path)} (was {(n["midi"] - note) * 100:+.0f} cents, wobble {n["sd"]:.0f} cents)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
