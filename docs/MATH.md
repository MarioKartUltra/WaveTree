# WaveTree II — the math (terse notes)

## 1. DWT / IDWT (Source/dsp/Dwt.h, Wavelets.h)

Periodic (circular) multi-level orthonormal DWT on a frame of N = 2048 samples,
L = 10 levels. Analysis filters (h, g) are orthonormal Daubechies pairs
(Haar/Db2/Db4/Db6); one level maps a signal x of length n to

    a[k] = Σ_m h[m] x[(2k + m) mod n]        (approximation)
    d[k] = Σ_m g[m] x[(2k + m) mod n]        (detail),   k = 0..n/2−1

applied recursively to the approximation L times. Coefficient layout is packed:
band 0 = approx at depth L (length N/2^L = 2), band b = detail at depth
L−b+1 (length N/2^(L−b+1)), coarse → fine, so band sizes are 2, 2, 4, …, 1024.

The inverse is the exact adjoint (transpose) of the orthonormal analysis, so
IDWT(DWT(x)) = x to machine precision (~1e-12, checked by CTest). Sound = IDWT
of a synthetically filled coefficient tree, looped as a wavetable.

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

The level ℓ ∈ [0, 1] is the height drawn in the row. It is read on one decibel
scale for the whole tree, tied to P = max |c| over the generated tree:

    m(ℓ) = P · 10^((T − (1 − ℓ)·R) / 20),   T = 20·log10(2),  R = 10·T ≈ 60.2 dB
    m(ℓ) = 0 for ℓ < 0.03

so the top of a row is 2P, the generated peak sits at ℓ = 0.9, and the bottom
is silence. A level sets the magnitude outright, which is what lets a node the
generator left at or near zero be painted in; a multiplier cannot do that. A
node at exactly zero has no sign and takes (−1)^k. P is measured before the
edits, so painting never moves the scale. G_node is what painting wrote before
levels existed; projects saved then still load and sound the same.

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

For the log-normal cascade, ζ(q) is parabolic: ζ(q) = q·s − (σ² ln2 / 2)·q²
up to constants, so the D(h) arch is a parabola whose width grows with
Intermittency σ and collapses to the point (s, 1) at σ = 0 — which is exactly
what the DhSpectrum view shows.

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

## 8. Tempo sync + LFO modulation (JUCE layer only)

Every time parameter follows the frozen three-param convention (`<id>Sync`,
`<id>Ms`, `<id>Div` over the 16-division table). LFO A/B are evaluated at
block rate; modulation is applied as bounded offsets on the APVTS values
BEFORE they enter WaveTreeParams (destinations: Hurst, Intermittency, Tilt,
Density, Output — each with a ±100% depth per LFO). The DSP core never sees
the host clock or the mod matrix.
