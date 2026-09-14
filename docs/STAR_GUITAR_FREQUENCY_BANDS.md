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

## Visual mapping

| Band | Onset envelope | Drives |
| --- | --- | --- |
| low | `lowBeatLevel_` | Far layer (building/water tower, "쿵") + mid layer's primary pulse (pole/tree, in both algorithm modes) + the tempo tracker |
| mid | none (sustained `midLevel_` only) | `songEnergy_` contribution + reactive mode's ambient mid-layer cadence |
| presence | `presenceBeatLevel_` | Mid layer's secondary marker ("짝") |
| air | `airBeatLevel_` | Near layer's cymbal/hi-hat flash **and** the sky layer (bird/plane/star) |

The sky layer is intentionally restricted to the air band only — never mid —
per the "하늘은 반드시 고역대" requirement: a song with no hi-hat/cymbal/shimmer
content simply never spawns anything in the sky, rather than falling back to
a generic timer or a denser band.

## Known limitation

Three analysis bands (four counting the split residual) is a coarse
approximation. Real drum/vocal/instrument separation would need spectral
analysis (FFT-based band energy, or an actual onset-detection model), not
three cascaded one-pole filters. This is accurate enough to be a meaningfully
better proxy than the 3-band version, not a claim of precise instrument
classification.
