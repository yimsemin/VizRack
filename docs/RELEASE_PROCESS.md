# VizRack branching, commits and releases

## Branches

| Branch | Purpose |
| --- | --- |
| `main` | Released code only. Every release is a `--no-ff` merge from `develop` plus an annotated `vX.Y.Z` tag. Never commit directly. |
| `develop` | The default working branch. All normal work lands here and is pushed to `origin` after every step. Branched once from `main`; it is long-lived. |
| `topic/<slug>` | Optional. Short-lived branch off `develop` for risky or large work; merge back with `--no-ff` (or squash) and delete. |

`main` and the tags use `vX.Y.Z`; the working branch is `develop`, so a bare ref is
never ambiguous.

## Commits

- One logical, self-contained change per commit. Commit after each meaningful step
  once `cmake --build`, `ctest` and `VizRack.exe --smoke-test` all pass. Do not
  batch unrelated work into one commit, and never commit `out/` or other build
  artifacts.
- Conventional Commit subject: imperative mood, English, roughly ≤ 72 characters.
  Types: `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `build`, `chore`.
- The changelog is **not** derived from commit subjects. Any commit that changes
  what a user sees also edits `## [Unreleased]` in `CHANGELOG.md` in the same
  commit; internal commits (`refactor` / `docs` / `test` / `build` / `chore`)
  touch nothing there. See `docs/RELEASE_NOTES_STYLE.md` for how entries are
  worded.
- Optional body explains the *why*, wrapped near 72 columns.
- Do not add AI-attribution trailers (`Co-Authored-By: Claude …`, "Generated
  with Claude Code", or similar). The `.githooks/commit-msg` hook strips them.

## Pushing

Push `develop` to `origin` continuously. Do **not** push tags or create GitHub
releases outside the release flow below.

## Cutting a release

Only after the user gives explicit approval for a version:

1. Make sure `## [Unreleased]` in `CHANGELOG.md` reads the way the release
   should, including the italic one-line lead under the heading.
2. On `develop`, bump `project(VizRack VERSION X.Y.Z ...)` in `CMakeLists.txt`.
3. Build Release, run `ctest`, `--smoke-test`, then `scripts\package.ps1` (it
   writes `out/package/VizRack-win-x64.zip.sha256`).
4. Generate the notes:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\generate-release-notes.ps1
   ```

   The script reads the version from `CMakeLists.txt`, promotes `[Unreleased]` to
   `[X.Y.Z] - <date>` in `CHANGELOG.md`, refreshes the compare links, and writes
   `docs/release-notes/X_Y_Z.md` (that section plus a download/requirements
   footer with the SHA-256 filled in). Read the note once and fix any wording the
   script could not. It never invents entries.
5. Commit the version bump, changelog and generated note on `develop`
   (`docs: release vX.Y.Z`) and push.
6. Merge into `main` and tag:

   ```powershell
   git checkout main
   git merge --no-ff refs/heads/develop
   git tag -a refs/tags/vX.Y.Z -m "VizRack vX.Y.Z"
   git push origin refs/heads/main refs/tags/vX.Y.Z
   ```

7. Publish:

   ```powershell
   gh release create vX.Y.Z `
       --verify-tag `
       --title "VizRack vX.Y.Z" `
       --notes-file docs/release-notes/X_Y_Z.md `
       out/package/VizRack-win-x64.zip
   ```

8. `git checkout develop` and continue. `develop` is not deleted.
