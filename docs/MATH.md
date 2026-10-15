# WaveTree II — the math (terse notes)

## 1. DWT / IDWT (Source/dsp/Dwt.h, Wavelets.h)

Periodic (circular) multi-level DWT on a frame of N = 2048 samples, L = 10
levels. Analysis filters (h, g): Haar, Daubechies 2/4/6/10, Symlets 4/8,
Coiflets 2/4 (orthonormal) and the biorthogonal pairs 2.2 (CDF 5/3), 4.4 (CDF
9/7) and 6.8 (tables from PyWavelets 1.8; h stored as correlation taps, i.e.
dec_lo reversed). One level maps a signal x of length n to

    a[k] = Σ_m h[m] x[(2k + m) mod n]        (approximation)
    d[k] = Σ_m g[m] x[(2k + m) mod n]        (detail),   k = 0..n/2−1

applied recursively to the approximation L times. Coefficient layout is packed:
band 0 = approx at depth L (length N/2^L = 2), band b = detail at depth
L−b+1 (length N/2^(L−b+1)), coarse → fine, so band sizes are 2, 2, 4, …, 1024.

The inverse is the transposed scatter-add with the synthesis filters: for the
orthonormal families those are h, g themselves (the exact adjoint), for the
biorthogonal ones the dual filters h̃, g̃ (PyWavelets' rec_lo, rec_hi); either
way IDWT(DWT(x)) = x to machine precision (≤ 3e-12 for all twelve, checked by
CTest; the discrete Meyer "dmey" is left out because its truncated taps only
reconstruct to 5e-3). Sound = IDWT of a synthetically filled coefficient tree,
looped as a wavetable. Why the choice matters for painting: Daubechies filters
are asymmetric (a painted node smears to one side in time), Symlets and
Coiflets are nearly symmetric, the biorthogonal ones exactly (linear phase).

## 2. Tree fills (Source/dsp/CoeffGen.h)

**Bands** — deterministic "wavelet drawbar": detail band b gets constant
magnitude with alternating sign,

    c[b,k] = ± 2^(−(b−1)·tilt′) · exp(−(b − b_f)² / 2w²)

i.e. a spectral slope (Tilt) times a Gaussian band emphasis centred at the
Formant band b_f with width w (Band Q).

**Cascade** — multiplicative multifractal cascade (WTMM run backwards). On the
dyadic tree, each node inherits its parent's log-multiplier and adds a fresh
Gaussian:

    λ[b,k]  = λ[b−1, ⌊k/2⌋] + σ ξ,    ξ ~ N(0,1)
    c[b,k]  = ± 2^(−(b−1)·s) · exp(λ[b,k])

with s = 0.35 + 1.6·Hurst (fBm-like spectral slope) and σ = 0.9·Intermittency
(log-normal cascade width). σ = 0 gives a monofractal (pure slope); σ > 0
makes tree-correlated bursts — multifractal intermittency.

**Atoms** — sparse grains: in each band, ⌈density·n_b⌉ random positions get
amplitude ±2^(−0.7(b−1)) · (0.3 + u), u ~ U[0,1); all other nodes are zero.

## 3. Tree edit overlay (Source/dsp/TreeEdit.h)

The interactive tree editor changes the generated coefficients before the
IDWT. A band has a gain (the drawbar in the gutter). A node is either painted
to a level or left to the generator:

    painted:   c′[b,k] = sgn(c[b,k]) · m(ℓ) · G_band[b]
    otherwise: c′[b,k] = c[b,k] · G_node[off_b + k] · G_band[b],   G ∈ [0, 2]

The level ℓ ∈ [0, 1] is the height drawn in the row, on ONE decibel scale for
the whole tree, tied to P = max |c| over the generated tree:

    m(ℓ) = P · 10^((T − (1 − ℓ)·R) / 20),   T = 20·log10(2),  R = 10·T ≈ 60.2 dB
    m(ℓ) = 0 for ℓ < 0.03

so the top of a row is 2P, the generated peak sits at ℓ = 0.9, and the bottom
is silence. The picture therefore shows the tree's decay: with the default
Cascade slope (≈ 8 dB per band) the finest rows sit at or under the floor.
What is drawn is what is heard, in any row. A level sets the magnitude
outright, which is what lets a node the generator left at or near zero be
painted in; a multiplier cannot do that. A node at exactly zero has no sign
and takes (−1)^k. P is measured before the edits, so painting never moves the
scale. G_node is what painting wrote before levels existed; projects saved
then still load and sound the same.

**Painted nodes follow the slope knobs.** Each painted node also stores the
tree slope s₀ at the moment it was painted (CoeffGen.h `treeSlope`: Bands
2.2·tilt, Cascade s_H + tilt offset, Atoms 0.7 + tilt offset, tilt offset =
2.2·(tilt − ½)). Played at slope s, it moves with the tree's own decay at its
band b (TreeEdit.h `paintedMagnitude`, b_P = the band holding the peak):

    m = m(ℓ) · 2^(−(b − b_P)·(s − s₀))

so nothing moves while the slope is where it was painted, the peak's band
never moves, and a steeper Tilt lowers painted fine nodes exactly as much as
it lowers the generated ones (the bars move too). Saved as `L,i,ℓ,s₀`; an
older `L,i,ℓ` loads with s₀ = the slope the project opens with. Tilt is
available in all three modes.

(2026-10-02 a per-row scale P_b = P·2^(−(b−b_P)s) was tried; every row then
showed its natural level near the top, the decay vanished from the picture
and the user rejected it. The stored slope keeps what that was for.)

**Painting a bone.** A stroke that climbs into the FINER rows above its own
(past 40 % of the next row) becomes a bone: from the node it left its row at,
exactly one node per finer row, each the child of the node below it nearest
the pointer (children of node k in band b are k·r … k·r + r − 1 in band b + 1,
r = 1 from A to the coarsest detail, 2 above), so the stroke follows a branch
of the dyadic tree — a WTMM maximum line. Every row the bone reaches holds
only that node until the button is released. While the pointer is in one of
those rows, that row's node follows the pointer's height, exactly as a plain
stroke would: a climb through a row leaves its node at the top, stopping half
way leaves it at half, and coming back down adjusts the same node again. The
start node keeps whatever level it had when the stroke left its row. Rows a
fast drag skipped take the level of the node below them. A stroke never
leaves its row DOWNWARD: below the row the level is 0, so a drag from a fine
row to the bottom of the view mutes every node it sweeps. Erase strokes
clear along the same rules.

G values live in lock-free atomics (UI writes, audio reads at rebuild time);
every edit bumps a version counter and the synth schedules ONE crossfaded
table rebuild per audio block (round-robin over voices).

## 4. Anti-aliasing mip chain (Source/dsp/WaveletVoice.h)

Band-limiting is done in the wavelet domain: mip m is the IDWT of the tree
with the m finest detail bands zeroed, so mip m contains ≤ N/2^(m+1) nonzero
"octaves". The oscillator reads mip

    m = ⌈log₂(inc) + 11⌉,  inc = f₀·bend/ sr · … (table-increment per sample)

clamped to [0, 7], i.e. one extra zeroed octave per octave of transposition —
the wavelet-domain equivalent of a mip-mapped wavetable. All mips share the
mip-0 peak normalization so levels match across the switch.

## 5. Multifractal D(h) spectrum (Source/dsp/Mfa.h)

Wavelet-moment (Legendre) estimate over the detail bands:

    S(q, j) = (1/n_j) Σ_k |d_{j,k}|^q
    ζ(q)    = − slope of log₂ S(q, j) vs j     (least squares, j = b−1)
    h(q)    = dζ/dq (central differences)
    D(h(q)) = 1 + q·h(q) − ζ(q)

For the log-normal cascade the closed form is exact for the ensemble: with
|d_j| = 2^{−j s} e^{λ_j}, λ_j a Gaussian walk of variance jσ² (CoeffGen.h),
E|d_j|^q ∝ 2^{−jqs} e^{q²jσ²/2}, so

    ζ(q) = q·s − q²σ²/(2 ln 2),   h(q) = s − qσ²/ln 2,
    D(h) = 1 − (h − s)² · ln 2 / (2σ²),     s = 0.35 + 1.6·Hurst + 2.2·(Tilt − ½),  σ = 0.9·Intermittency

(s is the slope the voice's tree was generated with, `generatedSlope()`, so FIT
moves with Tilt and with LFO modulation of Hurst/Tilt; until 2026-10-04 it
omitted the Tilt term and stayed at the Tilt-50 % position.)

a parabola centred on s that collapses to the point (s, 1) at σ = 0. The view's
FIT chip draws it dashed (Cascade mode only; Bands/Atoms/painted trees have no
closed form, the numeric estimate is what they get). The numeric curve is a
finite-sample estimate of this (one tree, few coefficients in coarse bands),
so it wobbles at large |q|; the two agree to a few hundredths of h.

Chord (CHORD chip): every sounding voice's tree is quantified together by
ADDING the moments per band, S(q, j) = mean over all coefficients of all
voices at depth j — the spectrum of the chord's total coefficient measure.
Spectra are not additive, moments are: for voices with equal Hurst and
Intermittency the chord's arch is the single voice's (pitch shifts j by a
constant and leaves slopes alone), for different ones ζ_chord(q) → min_v ζ_v(q)
with depth, whose Legendre transform is the concave hull of the voices' arches
(the FIT overlay then draws one parabola per voice). What this ignores:
interference — the mixed signal's coefficients are c₁ + c₂ and |c₁ + c₂| ≠
|c₁| + |c₂| where voices overlap in time-scale; the exact answer would be a
CWT/WTMM of the audio, not something closed-form or cheap.

## 6. Seed policy (acv2/dsp/SeedPolicy.h)

- Fixed: every note uses the global seed → identical table per key.
- Per-Key: seed = splitmix-hash(global, midiNote) → each key a reproducible
  character.
- Random-on-strike: fresh roll (0..9999) per note-on; the roll is published to
  the UI so it can be locked (Seed := roll, policy := Fixed).

All randomness is splitmix64/xorshift-derived from these seeds — nothing on
the audio path reads clocks or global RNGs (determinism invariant).

## 7. Envelope / filter / output

ADSR is linear-segment in amplitude. The tone filter is a TPT (topology-
preserving transform) SVF low-pass: g = tan(π f_c / sr), k = 2 − 1.9·reso,
solved zero-delay per sample. Output chain per voice: env · velocity → SVF →
DC blocker (y[n] = x[n] − x[n−1] + 0.999 y[n−1]); the mix bus applies
tanh soft-limiting and an isfinite guard.

## 7b. Keyboard: note stack, legato, glide (Source/dsp/WaveletSynth.h)

The synth keeps the keyboard's held keys as a stack, oldest first (32 deep).
A key with no free voice (none idle, none releasing) takes the voice of the
OLDEST held key. When a key is released, its voice does not simply release:
if some held key has lost its voice, the voice goes back to the most recent
such key — hold A, press B (A is cut), let go of B, A sounds again without a
new key press. With one voice this is the classic mono "last-note priority
with return"; with n voices it is the same rule across the stolen keys.

`legato` decides what a takeover does to the voice. Off: the voice is
retriggered (envelope from zero, a new table from the key's seed — the one
the key was struck with, so a Random roll comes back as it was). On: pitch
changes, envelope and table stay (`legatoTo`); only in Per-Key seed mode
does the table move to the new key's seed, as a 30 ms crossfade, because
there the key defines the tree.

`glide` (ms or beats, 0 = off) is a constant-time portamento: whatever the
interval, the pitch reaches the new key after the glide time, moving by a
constant ratio per sample (a straight line in pitch):

    f[n+1] = f[n] · (f_target / f_start)^(1/N),   N = glide · sr

The mip level is re-picked every 64 samples on the way. A fresh voice starts
from the previous key's pitch; a retriggered or legato voice from where it
is (so a retrigger mid-slide stays smooth). Pitch bend multiplies on top.

## 8. Tempo sync + LFO modulation (JUCE layer only)

Every time parameter follows the frozen three-param convention (`<id>Sync`,
`<id>Ms`, `<id>Div` over the 21-division table, 4 bars down to 1/128 with
triplet and dotted forms). In beats the knob does not walk the table (which
interleaves the three forms, so turning up from 1/16 met 1/8T before 1/8): it
walks one FAMILY — all, straight 1/n, triplets T or dotted . — in duration
order, chosen by the chip next to ms/beat and remembered as a state property
`<id>Family`. The parameter can hold any division; the knob shows the nearest
member of its family. LFO A/B are evaluated at
block rate; modulation is applied as bounded offsets on the APVTS values
BEFORE they enter WaveTreeParams (destinations: Hurst, Intermittency, Tilt,
Density, Output — each with a ±100% depth per LFO). The DSP core never sees
the host clock or the mod matrix.
