# How VizRack release notes are written

The goal: every release reads the same, is written while the work is fresh, and
takes minutes — not an archaeology session — to cut. One source of truth
(`CHANGELOG.md`), one script (`scripts/generate-release-notes.ps1`), one shape.

## The one source of truth

`CHANGELOG.md` at the repo root, [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
format. Everything user-facing is recorded there under `## [Unreleased]` **in the
same commit that makes the change** — not at release time. If a commit changes
what a user sees, it also edits `CHANGELOG.md`; if it doesn't, it doesn't.

At release time the script promotes `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD`,
opens a fresh empty `[Unreleased]`, and writes `docs/release-notes/X_Y_Z.md`
(that version's section plus a fixed three-line footer: download line, one line
with the OS floor / signing note / SHA-256, and the full-changelog link). That
file is the exact body of the GitHub Release. It has **no top-level heading** —
GitHub renders "VizRack vX.Y.Z" as the release title already, so the body starts
with the italic lead line.

There is no "Requires" section: system requirements do not change release to
release and belong in the README. The footer keeps only what a release page
genuinely needs on its own — the download, the checksum (new every release), the
one-line signing caveat, and the diff link.

## Language

**English only.** The app UI and README are being localized separately; the
changelog is not. If a Korean edition of the notes is ever wanted, mirror a
released section into `docs/release-notes/X_Y_Z.ko.md` by hand — do not try to
keep `CHANGELOG.md` itself bilingual.

## What counts as a change worth recording

Record it if a user would notice: a new or removed visualizer, a new scene or
palette, a menu or setting that appears/moves/changes meaning, a visible bug
fixed, a behaviour change, a compatibility or packaging change, anything
security-relevant.

Do **not** record: refactors, test changes, doc edits, build-script churn, CI,
dependency bumps with no visible effect, internal renames. These stay out of the
notes entirely — the commit history already has them. (No "plus N internal
changes" line; it's noise.)

## Section order and meaning

Within a version, only include the headings that have entries, in this order:

| Heading | Use for |
| --- | --- |
| `### Added` | New visualizers, scenes, palettes, menus, settings, capabilities |
| `### Changed` | Behaviour or appearance of something that already existed |
| `### Deprecated` | Still works, will be removed later |
| `### Removed` | Gone this version |
| `### Fixed` | Bugs a user could hit |
| `### Security` | Anything with a security dimension |

## Which section? (this trips people up)

A patch is almost never `Added`. `Added` is only for a capability that did not
exist before. If you fixed or tweaked something that already shipped, it goes in
`Fixed` or `Changed`.

| What happened | Section | Example entry |
| --- | --- | --- |
| A brand-new visualizer / scene / palette / menu / setting | `Added` | **Campfire visualizer.** A calm fire that leans with the stereo image. |
| A bug in existing behaviour, now corrected | `Fixed` | Campfire flames sit in the fire pit instead of hovering above it. |
| Existing behaviour or look deliberately altered (not a bug) | `Changed` | The classic cascade draws as a shaded surface rather than stacked lines. |
| A feature added last version was broken, now works | `Fixed` (**not** `Added` again) | The 3D spectrum no longer stalls when the FFT size changes. |
| Perf win a user can feel (smoother frame pacing, faster load) | `Changed` | The art scenes hold 60 FPS on integrated GPUs. |
| A visualizer or option removed | `Removed` | The tube-stage visualizer is gone; it never read right. |
| Something still works but is on the way out | `Deprecated` | The legacy `data/` layout is deprecated; move settings with … |

Never re-announce a feature. Once `Foo visualizer` has appeared under `Added` in
a released version, later work on it is `Changed` or `Fixed` — never `Added`
a second time.

## Voice

Warm, plain, a little proud of the visuals — this is a design-forward product.
Not marketing.

- **Lead with an italic one-liner** directly under the version heading: the one
  thing this release is about, in a sentence. It becomes the first line a reader
  sees on GitHub. `_A campfire that breathes with the music, and a proper Help menu._`
- **Each entry describes what the user gets**, not what the developer did. Start
  with the noun (the feature), bold it, then a sentence on what it feels like or
  why it's there.
  - Good: **Campfire visualizer.** A calm fire whose embers lift on transients…
  - Not: `add a gentle audio-reactive campfire` (that's the commit subject)
- **Describe the end state, not the path.** If a feature landed over eight
  commits with three course-corrections, the entry is the final result in one
  bullet. The `[Unreleased]` section gets edited and squashed as work continues.
- Present tense, active voice. No version numbers inside entries, no "we", no
  exclamation marks, no emoji.
- **Text only.** No screenshots, GIFs or embedded images — not in `CHANGELOG.md`
  and not in the GitHub Release body. Describe the visual in words.

## SemVer while pre-1.0

- `0.MINOR.0` — new visualizers or features (may include fixes too).
- `0.0.PATCH` — fixes only, no new surface area. If a patch's notes end up with
  an `### Added` entry, that's the signal it should have been a minor bump —
  change the version, not the section.
- A genuinely breaking change to settings/data layout still only bumps the minor
  while we're on `0.x`; call it out in `### Changed` in plain words.
- `setOptions` normalizes any now-invalid persisted value, so a settings change
  never needs a migration note — just describe the new behaviour.

## Cutting a release (summary)

Full runbook: `docs/RELEASE_PROCESS.md`. In short, after the user approves a
version:

1. Make sure `[Unreleased]` in `CHANGELOG.md` reads the way the release should.
   Add the italic lead line if it's missing.
2. Bump `project(VizRack VERSION …)` in `CMakeLists.txt`.
3. `scripts\package.ps1` — builds the ZIP and writes `VizRack-win-x64.zip.sha256`.
4. `scripts\generate-release-notes.ps1` — promotes the changelog section, writes
   `docs/release-notes/X_Y_Z.md` with the SHA-256 and compare link filled in.
   Read it once; fix any wording the script couldn't.
5. Commit (`docs: release vX.Y.Z`), merge to `main`, tag, `gh release create`.
