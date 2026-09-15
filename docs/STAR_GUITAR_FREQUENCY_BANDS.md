# Star Guitar — frequency bands

Reference for `src/builtin/star_guitar_engine.cpp`'s band split and what
drives each visual layer. The cutoffs and mapping below were chosen by hand
and can still be tuned interactively via the right-click sensitivity
settings (see "Customizing peak sensitivity" below) — revisit this doc
whenever that tuning settles or the layer/band mapping changes.

## Eight sub-bands, seven filters

Seven cascaded one-pole low-pass filters at increasing cutoffs split the
signal into eight sub-bands; each sub-band is the residual between
consecutive filters (the last sub-band is everything above the highest
cutoff):

| # | Range | Typical source |
| --- | --- | --- |
| 0 | <80Hz | Deep sub-bass |
| 1 | 80-300Hz | Kick/bass drum fundamental |
| 2 | 300Hz-1kHz | Bass guitar harmonics, low vocals/guitars |
| 3 | 1-4kHz | Vocals, guitars, keys (most harmonic content) |
| 4 | 4-6kHz | Snare/clap snap (lower part), pick attack |
| 5 | 6-8kHz | Snare/clap snap (upper part), vocal sibilance |
| 6 | 8-12kHz | Hi-hats, cymbals, shimmer |
| 7 | >12kHz | Air, cymbal shimmer tail, very high transients |

These are deliberately coarse, hand-picked splits, not a claim of precise
instrument separation — see "Known limitation" below.

## Peak detection: relative, not absolute

A sub-band "peaks" when its current level rises sharply **relative to its
own recent baseline** — not when it crosses some fixed number. `detectPeak()`
implements this once, shared by all eight sub-bands:

```
relativeRise = (level - baseline) / (baseline + floor)
peaks when relativeRise > threshold AND level > floorLevel AND cooldown expired
```

`baseline` is a slow (multi-second) follow of the sub-band's own level,
captured *before* the current frame updates it. A confirmed peak carries its
`magnitude` (the sub-band's own level at that instant) into
`sizeFromMagnitude()`, which every peak-driven spawn uses to size the
resulting object — a harder hit spawns a visibly bigger object, uniformly
across every sub-band.

## Customizing peak sensitivity

The eight sub-bands group into four coarse controls exposed in the
visualizer's right-click menu ("Low/Mid/Treble/Air sensitivity", 0-100):
each pair of adjacent sub-bands shares one setting.
`thresholdFromSensitivity()` maps 0-100 to the `relativeThreshold` above —
higher sensitivity means a smaller relative rise is enough to count as a
peak. These are genuinely meant to be tuned by ear per source material
rather than nailed down once in code; there is no single globally-correct
value.

Each group's curve is individually recentred so its own slider's midpoint
(50) reproduces a value found good by ear during tuning
(`kBandReferenceSensitivity` in the .cpp), while the endpoints (0 and 100)
stay at the same raw insensitive/sensitive extremes for every group:

| Group | Slider 50 reproduces raw value |
| --- | --- |
| Low | 70 |
| Mid | 80 |
| Treble | 90 |
| Air | 80 |

This is a two-segment piecewise-linear remap per group, not a shift of the
whole range — `sensitivity <= 50` interpolates between the raw-0 and
raw-reference thresholds, `sensitivity > 50` interpolates between
raw-reference and raw-100.

## Sub-band → object type → depth layer

Each sub-band drives exactly one object type in exactly one depth layer —
no sub-band drives more than one destination, and no layer receives spawns
from more than the two sub-bands in its group:

| Sub-band | Object | Layer | Sensitivity control |
| --- | --- | --- | --- |
| 0 (<80Hz) | Signal marker (streetlight) | Near (fastest/largest) | Low |
| 1 (80-300Hz) | Pine tree | Near | Low |
| 2 (300Hz-1kHz) | Tree | Mid | Mid |
| 3 (1-4kHz) | Telephone pole | Mid | Mid |
| 4 (4-6kHz) | Stepped building silhouette | Far (slowest/smallest) | Treble |
| 5 (6-8kHz) | Plain building | Far | Treble |
| 6 (8-12kHz) | Star | Sky | Air |
| 7 (>12kHz) | UFO | Sky | Air |

`pine` and `tree` are deliberately different silhouettes (pine: five
sharply-tapering tiers reading as a conifer; tree: three broader tiers) so
the two "tree" spawns stay visually distinguishable even though they share a
family resemblance. `waterTower`, `bird` and `plane` are currently unmapped
by any sub-band (kept in the type enum and their draw functions in case a
future remap reintroduces them).

This intentionally **inverts** the original version's depth assignment: low
frequencies now drive the *nearest* layer (big, fast, close) and the treble
group drives the *farthest* (small, slow, distant), with mid in between and
air reserved for the sky. Near/mid/far still keep their original speed and
scale characteristics (far is always slowest/smallest, near always
fastest/largest) — only which frequency group feeds which layer changed.

There is no tempo estimation or beat prediction anywhere in the engine: every
layer reacts only to confirmed peaks, with a plain jittered ambient timer
filling the gaps between them (a filler pine tree, in the near layer's
case). A tempo-tracking "predictive" mode was tried and removed — it added a
second, harder-to-reason-about code path for a benefit that didn't hold up
once the grow-in animation (see below) made peak-triggered spawns read as
clear beat cues on their own.

Sky has no ambient/filler fallback — it only ever reflects an actual air-band
peak, per the "하늘은 반드시 고역대" requirement from earlier design
discussion, now generalized: a sub-band with no peaks simply produces
nothing, rather than falling back to a timer.

Every spawn — peak-triggered or an ambient filler on a quiet-passage timer
(used by near/mid/far, not sky) — animates in with the same grow-from-ground
motion (`growEase()` in `buildFrame()`). The only thing distinguishing a real
hit from filler is size: peak spawns use `sizeFromMagnitude()`, filler
spawns use a fixed, smaller `kAmbientSizeScale`. There is deliberately no
case where some spawns animate and others don't.

## Known limitation

Eight one-pole-filter sub-bands is a coarse approximation, not real
spectral/instrument separation — a proper implementation would need an
FFT-based band analysis or an actual onset-detection model. This is accurate
enough to give each sub-band a distinct, describable character, not a claim
of precise instrument classification.
