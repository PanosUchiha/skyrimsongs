# Skyrim Songs

Play the lute and flute for real in Skyrim SE, AE and GOG: take out your instrument anywhere, play it with the
computer keyboard or a USB MIDI keyboard, and whoever's in earshot listens. Play well and they toss you coins;
play badly and they heckle. In an inn, a bad enough set gets you thrown out (and you're not welcome back that day).
Put your own MIDI files in a folder to play along with them, or to let them play themselves (the cheat).

Status: **first version, not yet tried in game.**

## Playing

- **Take it out:** carry a lute or a flute (the vanilla items, anything sharing their models such as Finn's Lute,
  Pantea's Flute or the Dancer's Flute, and any mod's item, misc, weapon or armour, with "lute" or "flute" in its
  name or model file name) and press **O** (`Hotkey`; change it in game on the SKSE Menu Framework page: *Change...*, then press the key). The player plays the game's own bard idle, which puts the
  instrument in their hands (Skyrim.esm `0x96F8D` lute, `0x96F8C` flute, put away with `IdleStop_Loose`
  `0x10D9EE`, the same ones Skyrim's Got Talent uses); a torch or shield in the left hand is unequipped, the camera
  goes to third person, and a silent music type is pushed over the game's music. **Esc** puts it away (while you play, the hotkey and every other key are swallowed; Esc is
  the only way out). Not while fighting, swimming, riding or sitting, or with
  a weapon out; drawing a weapon, a fight, a conversation or leaving the area stops you.
- **The keyboard:** 37 notes, like a 37-key keyboard: three rows of seven keys, each an octave of white notes,
  lowest at the bottom (`Z X C V B N M`, `A S D F G H J`, `Q W E R T Y U`), and `I` for the top C. Shift for
  sharps, Up/Down to move an octave. The lute runs C3-C6, the flute C4-C7. While you play, none of the keyboard reaches the game; the mouse
  still turns the camera.
- **A MIDI keyboard:** every MIDI input Windows knows is opened while you play (and closed after), with velocity,
  the sustain pedal and all-notes-off. Its notes sound straight from the driver's thread, not on the next frame.
- **The songbook** (**Tab**, **Esc** to close): switch instrument, go back to free play, or pick a song: five built-in public-domain
  tunes (Ode to Joy, Amazing Grace, Scarborough Fair, Drunken Sailor, Greensleeves) and every `.mid` in
  `Data\SKSE\Plugins\SkyrimSongs\Songs`.
  - **Play along:** after a three-tick count-in the tune slides along three lanes (the keyboard's rows) to a line,
    each note labelled with its key (`+` = Shift). Within 70 ms is perfect, 160 ms good; any octave counts, so a
    MIDI keyboard works at any octave. The speed can be lowered to 50%.
  - **Let it play:** the whole file plays itself on your instrument, perfectly; the crowd hears a great bard.

## The crowd

Every half second the plugin finds the people within earshot (`ListenRange`, the same interior cell; living,
not hostile, not fighting, not asleep, not children, people not animals). Each has a mood, a taste (how hard to
please) and a purse.

- **How well you're playing** (0-1): playing along, a running average of your hits and misses. Free play is judged
  on the last eight seconds: rhythm (do the gaps between notes fit a steady beat and its halves and doubles?), key
  (the share of notes in the best-fitting major or harmonic minor scale), melody (steps and small leaps score,
  big jumps don't), pace (enough notes, not mashing) and variety, and a long silence loses the room.
- **Moods** drift up or down with that; tips come faster and bigger the happier a listener is, the deeper their
  purse and the better your Speech, capped per listener per performance (`MaxTipPerListener`). Unhappy listeners
  heckle; delighted ones cheer; a good play-along ends in applause and a collection. You get Speech experience
  for what you earn.
- **Thrown out:** in an inn (an innkeeper in the room, or a location with the inn keyword) a room that stays
  hostile gets a warning from the innkeeper, then you're put out through the nearest door that leads outside and
  can't play there again until a day has passed (kept in the SKSE co-save).

## How it's built

- `src/music`: plain C++ with no game code, tested on Linux in CI (`tests/music_test.cpp`):
  - `Synth`: the instruments synthesised in real time. The lute is two Karplus-Strong strings per note (a
    course; the second 1.8 cents sharp), plucked near the bridge with a brightness that follows velocity, through
    a small body (three resonances) and a little room reverb. The flute is a breathy additive tone with a chiff,
    a scoop into the note and a vibrato that blooms on long notes; played live it's one voice that slurs.
    `tests/render_preview.cpp` renders both to WAV (a CI artifact).
  - `Midi`: standard MIDI files, format 0 and 1, tempo maps, SMPTE time, running status, RIFF `.rmi`; and the
    melody finder for play-along (the highest, busiest single-line part, reduced to one note at a time).
  - `Judge`, `Crowd`, `Songs`, `Keymap`.
- `src/plugin`: the SKSE plugin (CommonLibSSE-NG, one DLL for 1.5.97, 1.6 and later via Address Library).
  The synth streams through its own XAudio2 voice (8 ms blocks, four queued); songs are scheduled on the audio
  thread, and the play-along clock is the voice's played-sample count, so the sheet music, the judging and what
  you hear agree. Input comes from the same hook on the game's input dispatch as SkyrimYacht's; ImGui draws over
  a Present hook.
- No plugin (.esp) and no new assets beyond fonts.

## Building

GitHub Actions builds on every push: the tests on Linux, the DLL on Windows (logs to the `ci-logs` branch, the
package to `dist`).

Fonts: Barlow Condensed and IM Fell English, SIL Open Font License. GPL-3.0.
