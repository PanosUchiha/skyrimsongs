TinySoundFont dependency
========================

The piano sampler uses TinySoundFont (MIT license), a single-header SF2 playback engine.
Download the unmodified header from:
https://raw.githubusercontent.com/schellingb/TinySoundFont/main/tsf.h

Save it beside this file as:
src/music/third_party/tsf.h

Do not define TSF_IMPLEMENTATION anywhere else. Synth.cpp defines it once.
The TinySoundFont header contains its own copyright and license notice; keep that notice intact.

Upstream project and license:
https://github.com/schellingb/TinySoundFont
https://github.com/schellingb/TinySoundFont/blob/main/LICENSE
