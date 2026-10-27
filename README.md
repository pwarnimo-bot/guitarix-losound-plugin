# LoSound Red Sun

An LV2 guitar plugin for Guitarix (or any LV2 host) that aims for the early-90s
Palm Desert / *Blues for the Red Sun* tone: thick fuzz pushed into a bass amp
and a bass cabinet.

```
in -> tightening HPF -> [4x oversampled: 2-stage fuzz + octave-up -> bass-amp
tone stack -> sagging power stage] -> bass 8x10 / guitar 4x12 cab sim -> level
```

## Controls

| Control  | Range        | What it does |
|----------|--------------|--------------|
| Fuzz     | 0-1          | Fuzz gain (+6 dB to +50 dB across two clipping stages) |
| Octave   | 0-1          | Blends in a rectified octave-up; best on single notes above the 12th fret, neck pickup |
| Bass     | 0-1          | ±15 dB low shelf at 100 Hz (0.5 = flat) |
| Mid      | 0-1          | ±12 dB at 500 Hz: scooped and huge vs. honky and cutting |
| Treble   | 0-1          | ±15 dB high shelf at 2.5 kHz |
| Ultra Lo | on/off       | Ampeg-style switch: extra sub, 500 Hz notch |
| Sag      | 0-1          | Power-supply sag: loud notes compress and bloom |
| Cab      | 0-1          | 0 = bass 8x10, 1 = guitar 4x12 |
| Level    | -30..+12 dB  | Output level |

Presets: **Green Machine**, **Thumb**, **Freedom Run (octave lead)**.

## Build and install

Needs `gcc`, `make`, `pkg-config` and the `lv2` package.

```sh
make            # builds redsun.lv2/redsun.so
make install    # copies the bundle to ~/.lv2
make test       # offline sanity test, writes WAVs to test/out/
make uninstall
```

In Guitarix, open the LV2/LADSPA plugin manager, tick **LoSound Red Sun**,
save, and it shows up in the rack's plugin list (restart Guitarix if it doesn't).

## Tips

- Tune down to C standard (C G C F A D) for the real thing.
- Fuzz this hot amplifies hum: put Guitarix's noise gate in front of it.
- Bypass Guitarix's own amp and cabinet modules; this plugin includes both.
  (Running into a clean amp + cab instead also works: set Cab to taste.)
- `python3 test/spectrum.py test/out/*.wav` prints octave-band spectra of the test renders.
