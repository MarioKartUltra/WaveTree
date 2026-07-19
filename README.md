# WaveTree — a DWT-based synthesizer

A synthesizer whose entire premise is the **discrete wavelet transform**. Instead
of oscillators or Fourier partials, you fill a **wavelet coefficient tree**, run
the **inverse DWT** to get a periodic "wavelet wavetable", and a resampling
oscillator plays it at the MIDI pitch (continuous pitch, clean looping). VST3 /
Standalone / AU, same build pipeline as the other plugins here.

## The substrate
A Haar / Daubechies (Db2/Db4/Db6) **orthonormal, perfect-reconstruction** DWT.
Because the filters are orthonormal, the inverse is the exact adjoint of the
forward transform — `IDWT(DWT(x)) == x` to ~1e-12 (verified by `ctest`).

## Modes (how the coefficient tree is filled)
- **Bands** — a wavelet "drawbar/formant": per-octave-scale gains shaped by
  *Tilt* (spectral slope), *Formant* (which scale is emphasised), *Band Q*.
- **Cascade** — a multiplicative **multifractal cascade** on the dyadic tree:
  *Hurst* sets the fBm spectral slope, *Intermittency* the log-normal cascade
  width (multifractal breadth). This is WTMM-style multifractal analysis run
  *backwards* into synthesis.
- **Atoms** — sparse wavelet grains (*Density*), percussive/granular.

*Morph* periodically regenerates the coefficient tree and cross-fades, so Cascade
textures evolve. *Seed* makes it deterministic and note-varying. Plus ADSR, a
state-variable low-pass (*Cutoff/Resonance*), output, polyphony, and an on-screen
keyboard. The editor shows the live wavelet wavetable.

## Build (Apple Silicon)
```bash
./build.sh
```
`COPY_PLUGIN_AFTER_BUILD` installs VST3+AU to your user plug-in folders. Ad-hoc
sign + copy to the system folder for a DAW that scans it:
```bash
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/VST3/WaveTree.vst3
cp -R ~/Library/Audio/Plug-Ins/VST3/WaveTree.vst3 /Library/Audio/Plug-Ins/VST3/
codesign --force --deep -s - /Library/Audio/Plug-Ins/VST3/WaveTree.vst3
```

> **JUCE location:** the build resolves JUCE from `external/JUCE` → `~/projects/JUCE`.
> (macOS TCC protects `~/Downloads`, so the build tooling cannot read a JUCE there.)

## Test
```bash
ctest --test-dir build --output-on-failure    # DWT perfect-reconstruction + render
```

## Known limitations
- The wavetable oscillator can alias at very high notes (no mip-mapped tables
  yet); the low-pass tames it.
- `Morph`/live coefficient regeneration rebuilds the wavetable on the audio
  thread (infrequent) — fine for a POC, would move off-thread for production.
- The CWT scalogram-*vocoder* idea is deliberately out of scope here (it's a
  *continuous* WT, not the DWT this instrument is built on).
