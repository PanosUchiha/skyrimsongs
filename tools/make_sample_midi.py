#!/usr/bin/env python3
"""Writes assets/songs/Greensleeves (lute arrangement).mid: a sample MIDI file for the songbook, shipped in
Data\\SKSE\\Plugins\\SkyrimSongs\\Songs. Greensleeves is a 16th-century tune (public domain); this is a plain
arrangement: the tune over a bass note on each bar, in 3/4. Format 1: a tempo track, the tune, the bass.

Usage: python3 tools/make_sample_midi.py
"""
import os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPQ = 480
TUNE = ("A4:1 | C5:2 D5:1 | E5:1.5 F5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:2 A4:1 | A4:1.5 G#4:.5 A4:1 |"
        "B4:2 G#4:1 | E4:2 A4:1 | C5:2 D5:1 | E5:1.5 F5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:1.5 B4:.5 A4:1 |"
        "G#4:1.5 F#4:.5 G#4:1 | A4:3 | r:3 | G5:3 | G5:1.5 F#5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:2 A4:1 |"
        "A4:1.5 G#4:.5 A4:1 | B4:2 G#4:1 | E4:3 | G5:3 | G5:1.5 F#5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 |"
        "C5:1.5 B4:.5 A4:1 | G#4:1.5 F#4:.5 G#4:1 | A4:3")
# one bass note per bar (after the one-beat pickup): A minor, with C and G major and E for the cadences
BASS = "A2 G2 G2 A2 A2 E2 E2 A2 G2 G2 A2 E2 A2 A2 C3 C3 G2 G2 A2 E2 E2 C3 C3 G2 G2 A2 E2 A2".split()
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def pitch(n):
    pc = NAMES[n[0]]
    i = 1
    while n[i] in "#b":
        pc += 1 if n[i] == "#" else -1
        i += 1
    return (int(n[i:]) + 1) * 12 + pc


def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def track(events):
    """events: (tick, bytes) -> an MTrk chunk"""
    data, last = b"", 0
    for tick, ev in sorted(events, key=lambda e: (e[0], e[1][0] & 0xF0 != 0x80)):
        data += vlq(tick - last) + ev
        last = tick
    data += vlq(0) + b"\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + data


def main():
    tune, t = [], 0
    for tok in TUNE.replace("|", " ").split():
        n, length = tok.split(":")
        ticks = int(float(length) * TPQ)
        if n != "r":
            p = pitch(n)
            tune += [(t, bytes([0x90, p, 96])), (t + int(ticks * 0.92), bytes([0x80, p, 0]))]
        t += ticks
    bass, bar = [], TPQ  # after the pickup
    for b in BASS:
        p = pitch(b)
        bass += [(bar, bytes([0x91, p, 70])), (bar + 3 * TPQ - 20, bytes([0x81, p, 0]))]
        bar += 3 * TPQ
    tempo = [(0, b"\xFF\x51\x03" + struct.pack(">I", 600000)[1:]), (0, b"\xFF\x58\x04\x03\x02\x18\x08")]
    name = lambda s: (0, b"\xFF\x03" + vlq(len(s)) + s.encode())
    head = b"MThd" + struct.pack(">IHHH", 6, 1, 3, TPQ)
    out = head + track(tempo + [name("Greensleeves")]) + track(tune + [name("Tune")]) + track(bass + [name("Bass")])
    os.makedirs(os.path.join(ROOT, "assets", "songs"), exist_ok=True)
    path = os.path.join(ROOT, "assets", "songs", "Greensleeves (lute arrangement).mid")
    open(path, "wb").write(out)
    print(f"wrote {path} ({len(out)} bytes)")


if __name__ == "__main__":
    main()
