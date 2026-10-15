# WaveTree II

A synthesizer built on the **discrete wavelet transform**. Instead of oscillators or Fourier
partials you fill a **wavelet coefficient tree**; the **inverse DWT** turns it into a periodic
wavetable, and a resampling oscillator plays that at the MIDI pitch. VST3, AU and standalone on
macOS. Version 2 ("WaveTree II", beta) installs beside version 1 under its own identity.

## How it works

Twelve wavelet families (Haar, Daubechies, Symlets, Coiflets and three biorthogonal pairs), all
perfect-reconstruction: `IDWT(DWT(x)) == x` to 1e-12, checked by the tests.

Three ways to fill the tree:

- **Bands**: per-octave gains shaped by Tilt (spectral slope), Formant (which scale is emphasised)
  and Band Q.
- **Cascade**: a multiplicative multifractal cascade on the dyadic tree. Hurst sets the fBm slope,
  Intermittency the log-normal cascade width. The editor shows the resulting singularity spectrum
  D(h), as a CHORD of the summed moments and as a FIT parabola. This is WTMM-style multifractal
  analysis run backwards, into synthesis.
- **Atoms**: sparse wavelet grains, set by Density.

Tilt applies in every mode. The tree can also be painted: drag on a node to set its level on one dB
scale for the whole tree, or drag out of the home row to paint one node per band. Painted levels
remember the slope they were painted at and follow Tilt and Hurst afterwards.

Also: Morph (regenerate the tree on an interval and cross-fade), seeds that are fixed, per key or a
fresh roll each strike, ADSR, a state-variable low-pass, anti-aliased playback, polyphony with
legato and a constant-time glide, two LFOs with eight shapes and a mod matrix, and every time
control in milliseconds or in beat divisions that follow the host tempo. The standalone has an
on-screen keyboard.

Morph regenerates the table on the audio thread. The work is infrequent and bounded, but a very
short Morph interval at high polyphony can load that thread.

## Install a release (no build needed)

Download `WaveTree-II-2.0.0-macOS.zip` from the [Releases](https://github.com/MarioKartUltra/WaveTree/releases)
page and unzip it; that makes a folder `WaveTree-II-2.0.0-macOS`. Copy the two plug-in
bundles into the user plug-in folders, by dragging them in the Finder or with the two `cp` lines.
Then clear the quarantine flag: the bundles are signed ad hoc rather than notarised, so macOS marks
them after the download and the DAW will not load them until the mark is gone.

```sh
cd ~/Downloads/WaveTree-II-2.0.0-macOS
cp -R "WaveTree II.vst3"      ~/Library/Audio/Plug-Ins/VST3/
cp -R "WaveTree II.component" ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"WaveTree II.vst3" \
                               ~/Library/Audio/Plug-Ins/Components/"WaveTree II.component" "WaveTree II.app"
```

Then rescan in the DAW (Ableton Live: Settings → Plug-Ins → Rescan; Logic and GarageBand pick up
the AU after a restart). `WaveTree II.app` is the standalone version with an on-screen keyboard. macOS 11 or later,
Apple Silicon or Intel.

## Build from source (macOS)

Xcode command-line tools and CMake 3.22 or later (`brew install cmake ninja`).

```sh
git clone https://github.com/MarioKartUltra/WaveTree.git
cd WaveTree
./build.sh                 # arm64
./build.sh --universal     # arm64 + x86_64
```

The first configure fetches JUCE 8.0.7 and the patched foleys_gui_magic (network, once). To build
against checkouts you already have, pass `-DJUCE_DIR=... -DFOLEYS_DIR=...` or symlink them as
`external/JUCE` and `external/foleys_gui_magic`.

Artefacts land in `build/WaveTree_artefacts/Release/`. By default the VST3 and AU are also copied into `~/Library/Audio/Plug-Ins`; pass
`-DWAVETREE_INSTALL_AFTER_BUILD=OFF` to keep them in the build directory. `-DWAVETREE_GUI_EDITOR=ON`
opens foleys' WYSIWYG layout editor beside the plugin, for development only.

```sh
ctest --test-dir build --output-on-failure    # the offline DSP checks
auval -v aumu Wtr2 Wvtr                      # validate the installed AU
```

## Licence

WaveTree II is free software under the GNU Affero General Public License, version 3 or later
(`LICENSE`). Third-party components and their licences are listed in `NOTICE`; JUCE is used under
its AGPLv3 option.
