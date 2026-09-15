# Star Guitar — frequency bands

Reference for `src/builtin/star_guitar_engine.cpp`'s band split and what
drives each visual layer. Prototype scope only; revisit if the algorithm
design changes.

## What lives where (general audio-engineering knowledge)

| Range | Typical source | Rhythmic usefulness |
| --- | --- | --- |
| <150Hz | Kick/bass drum fundamental, sub-bass | Strong, clean on-beat pulse ("쿵") |
| 150Hz-2.5kHz | Bass guitar harmonics, vocals, guitars, keys, snare body (~150-400Hz) | Dense and near-continuous in most mixes — a poor onset trigger, useful only as a "how busy is the song" level |
| 2.5-6kHz | Snare/clap snap, vocal sibilance, pick attack | The most reliable backbeat ("짝") cue — a snare's identity is this broadband snap, not a narrow band |
| >6kHz | Hi-hats, cymbals, shakers, shimmer/air | Short, frequent transients; the only band that should represent "sky" material |

Two corrections from the first version of this engine:

- A snare/clap is **not** a mid-band signal. Its identity is a broadband
  transient combining body resonance (~150-400Hz) and wire/clap noise
  (mostly >2kHz) — closer to "presence" than "mid." Using the old
  170Hz-2100Hz mid band as a snare proxy mostly picked up whatever vocal or
  harmonic instrument was playing instead.
- A single "high" band (>2100Hz) conflated snare snap with hi-hat/cymbal
  content, so the near-layer cymbal cue and the "짝" cue could fire on the
  same material. Splitting presence (2.5-6kHz) from air (>6kHz) separates
  them.

## Engine band split (4 bands, 3 cascaded one-pole filters)

```
low       = lowFilter_                (cutoff 150Hz)
mid       = midFilter_ - lowFilter_   (150Hz-2.5kHz)
presence  = presenceFilter_ - midFilter_  (2.5-6kHz)
air       = mono - presenceFilter_    (>6kHz)
```

3 bands were considered but rejected: a single "high" band above ~2.1kHz
cannot separate snare snap from hi-hat/cymbal content, which was the direct
cause of the near-layer and "짝" triggers overlapping. 5 bands (splitting out
a dedicated snare-body band around 150-400Hz) was also considered, but the
150-400Hz zone overlaps too heavily with kick/bass fundamentals and toms to
cleanly separate with a simple filter cutoff — not worth the added
complexity for a prototype.

## Peak detection: relative, not absolute

A band "peaks" when its current level rises sharply **relative to its own
recent baseline** — not when it crosses some fixed number. This matters: a
loud, bass-heavy song and a quiet, sparse one should both read their kicks as
peaks, and a busy bassline holding a consistently high level shouldn't read
as one long peak just because it's loud. `detectPeak()` in
`star_guitar_engine.cpp` implements this once, shared by all three bands that
spawn anything:

```
relativeRise = (level - baseline) / (baseline + floor)
peaks when relativeRise > threshold AND level > floorLevel AND cooldown expired
```

`baseline` is a slow (multi-second) follow of the band's own level, captured
*before* the current frame updates it, so the comparison is always against
where the band already was. `level > floorLevel` additionally gates out pure
noise when the band is near silent (a tiny absolute level can still produce a
large *relative* jump that isn't musically meaningful).

A confirmed peak carries its `magnitude` (the band's own level at that
instant) forward into two things, uniformly across every spawn source:

- **Size.** `sizeFromMagnitude()` maps magnitude to how big the spawned
  object grows — a harder hit spawns a visibly bigger building/pole/marker.
- **Type/layer.** Which band peaked selects what spawns and where (see the
  table below) — this was already true in the previous version; what's new
  is that size now also comes from the same peak instead of being fixed.

## Visual mapping

| Band | Drives |
| --- | --- |
| low | Far layer (building, or a water tower for a strong-enough peak, "쿵") + mid layer's primary pulse (pole/tree, in both algorithm modes) + the tempo tracker |
| mid | No peak detector at all (see below) — only `songEnergy_` and the reactive mode's ambient mid-layer cadence |
| presence | Mid layer's secondary marker ("짝") |
| air | Near layer's cymbal/hi-hat marker **and** the sky layer (heavily weighted toward stars, occasional bird/plane) |

The sky layer is intentionally restricted to the air band only — never mid —
per the "하늘은 반드시 고역대" requirement: a song with no hi-hat/cymbal/shimmer
content simply never spawns anything in the sky, rather than falling back to
a generic timer or a denser band.

Mid is the one band with no peak detector, on purpose: it's dense and
near-continuously present in most mixes (vocals, guitars, keys, the bass
line's own harmonics), so a relative-rise test on it would fire almost
constantly rather than marking anything distinctive. This isn't an arbitrary
exception — every other band gets the identical treatment; mid specifically
fails the "peaks are meaningful" premise the whole mechanism depends on.

Every spawn — peak-triggered or an ambient filler spawned on a quiet-passage
timer — animates in with the same grow-from-ground motion (`growEase()` in
`buildFrame()`). The only thing that distinguishes a real hit from filler is
size: peak spawns use `sizeFromMagnitude()`, filler spawns use a fixed,
smaller `kAmbientSizeScale`. There is deliberately no case where some spawns
animate and others don't.

## Known limitation

Three analysis bands (four counting the split residual) is a coarse
approximation. Real drum/vocal/instrument separation would need spectral
analysis (FFT-based band energy, or an actual onset-detection model), not
three cascaded one-pole filters. This is accurate enough to be a meaningfully
better proxy than the 3-band version, not a claim of precise instrument
classification.
