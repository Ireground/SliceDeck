# SliceDeck Lite

The original, simpler version of SliceDeck, kept as its own plugin (it has its own name and ID, so it can be installed next to the full SliceDeck).

A simple VST3 slice sampler, modeled on Ableton's Simpler.

- **Modes:** Classic (polyphonic, with optional loop), 1-Shot (monophonic), Slice
- **Slice by:** Transient (with Sensitivity), Beat (uses the project tempo), Region (equal parts), Manual
- **Playback:** Trigger (plays the whole slice) or Gate (stops when you release the key)
- **Per slice:** volume and pitch
- **Sound:** amp envelope (ADSR), velocity, filter (low-pass, high-pass, band-pass), volume, pan, transpose, detune, reverse, snap to zero crossings
- **Samples:** embedded in the project (on by default). With embedding off, REAPER saves a relative path when the file is inside the project folder.

## Random Play

In Slice mode, Random Play picks a number of slices at random (**SLICES**, 7 by default) and loops them, one slice per step (**RATE**, 1/16 by default), locked to REAPER's transport.

- **RANDOM PLAY** turns it on. It plays whenever REAPER plays.
- **RANDOMIZE** picks a new set. Changing **SLICES** also picks a new set.
- The same set keeps looping until you randomize again, and it's saved with the project.
- If you ask for more slices than the sample has, every slice is used before any repeats.
- **DRAG** (MIDI CLIP) drags the current loop into REAPER as a MIDI item: the whole loop, or 1/2/4/8 bars (click the length pill to change it). With Random Play off, that item plays the same pattern through SliceDeck Lite.

## Mouse controls on the waveform

| Action | What it does |
|---|---|
| Click | Selects a slice and plays it |
| Double-click | Adds a slice marker (switches Slice By to Manual) |
| Double-click or right-click a marker | Removes the marker |
| Drag a marker | Moves it |
| Drag the orange S / E handles | Sets the sample start / end |
| Drag the dashed LOOP line | Sets the loop start (Classic mode with Loop on) |

The first slice plays on **First Note** (C1 by default), the next one on C#1, and so on.

## Building the plugin on GitHub

1. Create a new repository on GitHub (for example `SliceDeck`).
2. Upload everything in this folder, including the `.github` folder.
3. Open the **Actions** tab. The "Build VST3" job starts on its own and takes about 10 minutes.
4. When it shows a green check mark, open the run and download **SliceDeck-VST3-Windows** at the bottom of the page.
5. Unzip it and copy the `SliceDeck.vst3` folder to `C:\Program Files\Common Files\VST3`.
6. In REAPER, go to Options > Preferences > Plug-ins > VST and click **Re-scan**.

Every time you upload changed files, GitHub builds a new version.
