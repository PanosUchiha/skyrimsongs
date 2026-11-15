#!/usr/bin/env python3
"""Writes the songs shipped in Data\\SKSE\\Plugins\\SkyrimSongs\\Songs (assets/songs/*.mid).

Every tune here is either long out of copyright (traditional folk tunes, a 19th-century hymn tune, Beethoven) or
written for this mod. Each is arranged the way a bard would play it alone: the tune on top, a bass note on the
downbeat and the chord filled in on the off-beats (oom-pah-pah in 3/4, a lilt in 6/8), played through twice.

Format 1 MIDI: a tempo/meter track, then the tune, the bass and the chords on tracks of their own (so the
songbook's "Simple" finds the tune).

Usage: python3 tools/make_songs.py
"""
import os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPQ = 480
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

# title, meter (beats per bar, beat unit 4 or 8), bpm (quarter notes), pickup (beats), tune, one chord per bar
SONGS = [
    {
        "title": "Scarborough Fair",
        "meter": (3, 4), "bpm": 96, "pickup": 0,
        "tune": "D4:2 D4:1 | A4:2 A4:1 | E4:1.5 F4:.5 E4:1 | D4:3 | r:1 A4:1 C5:1 | D5:2 C5:1 | A4:1 B4:1 G4:1 | A4:3 |"
                "r:1 D5:1 D5:1 | D5:2 C5:1 | A4:2 A4:1 | G4:1 F4:1 E4:1 | C4:3 | D4:2 A4:1 | G4:2 F4:1 | E4:1 D4:1 C4:1 | D4:3",
        "chords": "Dm Dm C Dm F Dm G Am Dm F F C C Dm C C Dm",
    },
    {
        "title": "Greensleeves (lute arrangement)",
        "meter": (3, 4), "bpm": 100, "pickup": 1,
        "tune": "A4:1 | C5:2 D5:1 | E5:1.5 F5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:2 A4:1 | A4:1.5 G#4:.5 A4:1 |"
                "B4:2 G#4:1 | E4:2 A4:1 | C5:2 D5:1 | E5:1.5 F5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:1.5 B4:.5 A4:1 |"
                "G#4:1.5 F#4:.5 G#4:1 | A4:3 | r:3 | G5:3 | G5:1.5 F#5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 | C5:2 A4:1 |"
                "A4:1.5 G#4:.5 A4:1 | B4:2 G#4:1 | E4:3 | G5:3 | G5:1.5 F#5:.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:.5 B4:1 |"
                "C5:1.5 B4:.5 A4:1 | G#4:1.5 F#4:.5 G#4:1 | A4:3",
        "chords": "Am C G Em Am E E Am Am C G Em Am E Am Am C C G Em Am E E Am C C G Em Am E Am",
    },
    {
        "title": "Drunken Sailor",
        "meter": (2, 4), "bpm": 112, "pickup": 0,
        "tune": "A4:.5 A4:.25 A4:.25 A4:.5 A4:.25 A4:.25 | A4:.5 D4:.5 F4:.5 A4:.5 | G4:.5 G4:.25 G4:.25 G4:.5 G4:.25 G4:.25 |"
                "G4:.5 C4:.5 E4:.5 G4:.5 | A4:.5 A4:.25 A4:.25 A4:.5 A4:.25 A4:.25 | A4:.5 B4:.5 C5:.5 D5:.5 |"
                "C5:.5 A4:.5 G4:.5 E4:.5 | D4:1 D4:1 |"
                "A4:1.5 A4:.5 | A4:.5 D4:.5 F4:.5 A4:.5 | G4:1.5 G4:.5 | G4:.5 C4:.5 E4:.5 G4:.5 | A4:1.5 A4:.5 |"
                "A4:.5 B4:.5 C5:.5 D5:.5 | C5:.5 A4:.5 G4:.5 E4:.5 | D4:1 D4:1",
        "chords": "Dm Dm C C Dm Dm C Dm Dm Dm C C Dm Dm C Dm",
    },
    {
        "title": "Amazing Grace",
        "meter": (3, 4), "bpm": 80, "pickup": 1,
        "tune": "D4:1 | G4:2 B4:.5 G4:.5 | B4:2 A4:1 | G4:2 E4:1 | D4:2 D4:1 | G4:2 B4:.5 G4:.5 | B4:2 A4:1 | D5:3 |"
                "D5:2 B4:1 | D5:1.5 B4:.5 G4:1 | B4:2 A4:1 | G4:2 E4:1 | D4:2 D4:1 | G4:2 B4:.5 G4:.5 | B4:2 A4:1 | G4:3",
        "chords": "G G C G G G D G G D C G G D G",
    },
    {
        "title": "Ode to Joy",
        "meter": (4, 4), "bpm": 104, "pickup": 0,
        "tune": "E4:1 E4:1 F4:1 G4:1 | G4:1 F4:1 E4:1 D4:1 | C4:1 C4:1 D4:1 E4:1 | E4:1.5 D4:.5 D4:2 |"
                "E4:1 E4:1 F4:1 G4:1 | G4:1 F4:1 E4:1 D4:1 | C4:1 C4:1 D4:1 E4:1 | D4:1.5 C4:.5 C4:2 |"
                "D4:1 D4:1 E4:1 C4:1 | D4:1 E4:.5 F4:.5 E4:1 C4:1 | D4:1 E4:.5 F4:.5 E4:1 D4:1 | C4:1 D4:1 G3:2 |"
                "E4:1 E4:1 F4:1 G4:1 | G4:1 F4:1 E4:1 D4:1 | C4:1 C4:1 D4:1 E4:1 | D4:1.5 C4:.5 C4:2",
        "chords": "C G C G C G C C G C G G C G C C",
    },
    {
        "title": "Auld Lang Syne",
        "meter": (4, 4), "bpm": 84, "pickup": 1,
        "tune": "C4:1 | F4:1.5 F4:.5 F4:1 A4:1 | G4:1.5 F4:.5 G4:1 A4:.5 G4:.5 | F4:1.5 F4:.5 A4:1 C5:1 | D5:3 D5:1 |"
                "C5:1.5 A4:.5 A4:1 F4:1 | G4:1.5 F4:.5 G4:1 A4:.5 G4:.5 | F4:1.5 D4:.5 D4:1 C4:1 | F4:3 D5:1 |"
                "C5:1.5 A4:.5 A4:1 F4:1 | G4:1.5 F4:.5 G4:1 D5:1 | C5:1.5 A4:.5 A4:1 C5:1 | D5:3 F5:1 |"
                "C5:1.5 A4:.5 A4:1 F4:1 | G4:1.5 F4:.5 G4:1 A4:.5 G4:.5 | F4:1.5 D4:.5 D4:1 C4:1 | F4:4",
        "chords": "F C F Bb F C Bb F F C F Bb F C Bb F",
    },
    # written for SkyrimSongs
    {
        "title": "The Whiterun Jig",
        "meter": (6, 8), "bpm": 112, "pickup": 0,
        "tune": "F#4:.5 A4:.5 A4:.5 D5:.5 A4:.5 F#4:.5 | G4:.5 B4:.5 B4:.5 D5:.5 B4:.5 G4:.5 |"
                "F#4:.5 A4:.5 A4:.5 B4:.5 A4:.5 F#4:.5 | E4:.5 F#4:.5 G4:.5 A4:1.5 |"
                "F#4:.5 A4:.5 A4:.5 D5:.5 A4:.5 F#4:.5 | G4:.5 B4:.5 D5:.5 E5:.5 D5:.5 B4:.5 |"
                "A4:.5 F#4:.5 D4:.5 E4:.5 F#4:.5 E4:.5 | D4:1.5 D4:1.5 |"
                "D5:.5 E5:.5 F#5:.5 E5:.5 D5:.5 C#5:.5 | B4:.5 D5:.5 B4:.5 G4:1.5 |"
                "A4:.5 B4:.5 A4:.5 F#4:.5 A4:.5 D5:.5 | E5:1.5 C#5:1.5 |"
                "D5:.5 E5:.5 F#5:.5 G5:.5 F#5:.5 E5:.5 | D5:.5 B4:.5 G4:.5 B4:.5 D5:.5 B4:.5 |"
                "A4:.5 F#4:.5 E4:.5 C#4:.5 D4:.5 E4:.5 | D4:3",
        "chords": "D G D A D G A D D G D A D G A D",
    },
    {
        "title": "A Night at the Sleeping Giant",
        "meter": (3, 4), "bpm": 88, "pickup": 0,
        "tune": "A4:2 C5:1 | B4:2 G4:1 | A4:1.5 G4:.5 F4:1 | E4:3 | A4:1 B4:1 C5:1 | D5:2 B4:1 | C5:1 A4:1 F4:1 | E4:2 G#4:1 |"
                "A4:2 E5:1 | F5:2 E5:1 | E5:1 D5:1 C5:1 | D5:3 | C5:2 A4:1 | B4:1.5 A4:.5 G4:1 | G#4:2 B4:1 | A4:3",
        "chords": "Am G F E Am G F E Am F C G F G E Am",
    },
]

CHORDS = {"": [0, 4, 7], "m": [0, 3, 7], "7": [0, 4, 7, 10]}


def pitch(n):
    pc = NAMES[n[0]]
    i = 1
    while n[i] in "#b":
        pc += 1 if n[i] == "#" else -1
        i += 1
    return (int(n[i:]) + 1) * 12 + pc


def chord(name):
    """'Am' -> (bass pitch in octave 2, chord tones around C3-B3)"""
    root = NAMES[name[0]]
    i = 1
    if i < len(name) and name[i] in "#b":
        root += 1 if name[i] == "#" else -1
        i += 1
    tones = CHORDS[name[i:]]
    bass = 36 + root % 12            # C2..B2
    if bass < 40:
        bass += 12                   # E2..D#3: low enough to be a bass, high enough to sing on a lute
    voiced = sorted(52 + (root + t - 52) % 12 for t in tones)  # E3..D#4: the middle of the instrument
    return bass, voiced


def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def track(name, events):
    data, last = b"\xFF\x03" + vlq(len(name)) + name.encode(), 0
    data = vlq(0) + data
    for tick, ev in sorted(events, key=lambda e: (e[0], e[1][0] & 0xF0 != 0x80)):
        data += vlq(tick - last) + ev
        last = tick
    data += vlq(0) + b"\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + data


def note(events, ch, start, length, p, vel):
    events.append((start, bytes([0x90 | ch, p, vel])))
    events.append((start + max(1, length), bytes([0x80 | ch, p, 0])))


def build(song):
    beats_per_bar = song["meter"][0] * (4 / song["meter"][1])  # in quarter notes
    bar_t = int(beats_per_bar * TPQ)
    pickup_t = int(song["pickup"] * TPQ)
    toks = song["tune"].replace("|", " ").split()
    tune_len = int(sum(float(t.split(":")[1]) for t in toks) * TPQ)
    chords = song["chords"].split()
    bars = (tune_len - pickup_t) // bar_t
    assert bars == len(chords) and (tune_len - pickup_t) % bar_t == 0, f"{song['title']}: {bars} bars, {len(chords)} chords"

    melody, bass, harm = [], [], []
    passes = 2
    body = tune_len - pickup_t
    for k in range(passes):
        base = pickup_t + k * body  # where bar 1 falls this time through
        t = 0
        for tok in toks:
            n, length = tok.split(":")
            ticks = int(float(length) * TPQ)
            if n != "r" and not (k > 0 and t < pickup_t):  # the pickup only before the first time through
                note(melody, 0, base + t - pickup_t, int(ticks * 0.94), pitch(n), 100)
            t += ticks
        for b, name in enumerate(chords):
            start = base + b * bar_t
            root, tones = chord(name)
            last = k == passes - 1 and b == len(chords) - 1
            num, den = song["meter"]
            if last:
                note(bass, 1, start, bar_t - 10, root, 76)
                for p in tones:
                    note(harm, 2, start, bar_t - 10, p, 54)
            elif (num, den) == (6, 8):
                note(bass, 1, start, int(1.4 * TPQ), root, 76)
                for p in tones:
                    note(harm, 2, start + int(1.5 * TPQ), int(1.3 * TPQ), p, 50)
            elif num == 3:
                note(bass, 1, start, int(0.95 * TPQ), root, 76)
                for beat in (1, 2):
                    for p in tones:
                        note(harm, 2, start + beat * TPQ, int(0.8 * TPQ), p, 46)
            elif num == 4:
                note(bass, 1, start, int(1.9 * TPQ), root, 76)
                note(bass, 1, start + 2 * TPQ, int(1.9 * TPQ), root + 7 if root + 7 < 60 else root - 5, 66)
                for beat in (1, 3):
                    for p in tones:
                        note(harm, 2, start + beat * TPQ, int(0.8 * TPQ), p, 46)
            else:  # 2/4
                note(bass, 1, start, int(0.95 * TPQ), root, 76)
                for p in tones:
                    note(harm, 2, start + TPQ, int(0.8 * TPQ), p, 46)

    num, den = song["meter"]
    tempo = struct.pack(">I", int(60_000_000 / song["bpm"]))[1:]
    meta = [(0, b"\xFF\x51\x03" + tempo), (0, b"\xFF\x58\x04" + bytes([num, {4: 2, 8: 3}[den], 24, 8]))]
    head = b"MThd" + struct.pack(">IHHH", 6, 1, 4, TPQ)
    return head + track(song["title"], meta) + track("Tune", melody) + track("Bass", bass) + track("Chords", harm)


def main():
    out_dir = os.path.join(ROOT, "assets", "songs")
    os.makedirs(out_dir, exist_ok=True)
    for song in SONGS:
        data = build(song)
        path = os.path.join(out_dir, song["title"] + ".mid")
        open(path, "wb").write(data)
        print(f"wrote {os.path.relpath(path, ROOT)} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
