# VizRack release process

VizRack uses one branch and one release-note file per upcoming version:

- Branch: `v0.2.0`
- Release note: `docs/release-notes/0_2_0.md`
- Final tag: `v0.2.0`

The repository-level `AGENTS.md` makes this the default Codex workflow, so it does not
need to be repeated in normal task prompts.

## During development

Each logical change is committed with a concise, public-facing Conventional Commit
subject. Release notes are not edited after each task.

```text
feat: add spectrum smoothing controls
fix: continue startup when a VST3 plug-in scan fails
perf: reduce allocations while rendering
```

Internal commit subjects are also included in the release note, so every subject should
be suitable for public display.

## After final approval

Generate the release note from commit subjects:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\generate-release-notes.ps1
```

The script finds the latest `vMAJOR.MINOR.PATCH` tag reachable from the current version
branch and lists every subsequent non-merge commit subject in chronological order. It
does not inspect diffs, categorize entries, or rewrite titles.

Then update the CMake version, run the required Release build, tests, smoke test, and
packaging, and create the release commit. After integrating it into `main`, create and
push the annotated tag and publish with the generated document:

```powershell
gh release create v0.2.0 `
    --verify-tag `
    --title "VizRack v0.2.0" `
    --notes-file docs/release-notes/0_2_0.md `
    out/package/VizRack-win-x64.zip
```

The version branch is deleted after successful publication. Until then, fully qualified
refs such as `refs/heads/v0.2.0` and `refs/tags/v0.2.0` avoid ambiguity between the
same-named branch and tag.
