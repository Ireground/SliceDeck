# SliceDeck

A simple VST3 slice sampler, modeled on Ableton's Simpler.

- **Modes:** Classic (polyphonic, with optional loop), 1-Shot (monophonic), Slice
- **Slice by:** Transient (with Sensitivity), Beat (uses the project tempo), Region (equal parts), Manual
- **Playback:** Trigger (plays the whole slice) or Gate (stops when you release the key)
- **Per slice:** volume and pitch
- **Sound:** amp envelope (ADSR), velocity, filter (low-pass, high-pass, band-pass), volume, pan, transpose, detune, reverse, snap to zero crossings
- **Playback engine:** Repitch (classic: speed changes pitch) or Stretch (time-stretch that keeps pitch, synced to the project tempo, with formant control)
- **Speed:** -400% to 400%. Negative speeds play backwards.
- **Character:** Analog (saturation and tape-style roll-off) or Digital (bit and sample-rate reduction), applied per voice
- **Freeze:** holds the playhead at a position (scrub it by automating Position) and turns the sample into a sustained tone
- **Trigger slices by:** note, velocity (soft hits play early slices, hard hits play late ones) or the Select knob (automate it to step through slices)
- **Per-slice loop:** a looped slice repeats while the key is held
- **Samples:** embedded in the project (on by default). With embedding off, REAPER saves a relative path when the file is inside the project folder.

## Import from REAPER's timeline

Select an audio item on the timeline and click **FROM ARRANGE**. The part of the clip you see on the timeline is loaded (not the whole source file). Item volume, fades, take FX and playback rate are not included; glue the item first if you want them. REAPER only.

## Mouse controls on the waveform

| Action | What it does |
|---|---|
| Click | Selects a slice and plays it |
| Double-click | Adds a slice marker (switches Slice By to Manual) |
| Double-click or right-click a marker | Removes the marker |
| Drag a marker | Moves it |
| Drag the orange S / E handles | Sets the sample start / end |
| Drag the dashed LOOP line | Sets the loop start (Classic mode with Loop on) |

In Stretch mode, Sample BPM is guessed when you load a file (assuming a whole number of beats). Correct it if the guess is wrong, and turn **Sync to tempo** on to follow the project tempo.

The first slice plays on **First Note** (C1 by default), the next one on C#1, and so on.

## Building the plugin on GitHub

1. Create a new repository on GitHub (for example `SliceDeck`).
2. Upload everything in this folder, including the `.github` folder.
3. Open the **Actions** tab. The "Build VST3" job starts on its own and takes about 10 minutes.
4. When it shows a green check mark, open the run and download **SliceDeck-VST3-Windows** at the bottom of the page.
5. Unzip it and copy the `SliceDeck.vst3` folder to `C:\Program Files\Common Files\VST3`.
6. In REAPER, go to Options > Preferences > Plug-ins > VST and click **Re-scan**.

Every time you upload changed files, GitHub builds a new version.
