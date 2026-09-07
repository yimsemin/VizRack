# VizRack branching, commits and releases

## Branches

| Branch | Purpose |
| --- | --- |
| `main` | Released code only. Every release is a `--no-ff` merge from `develop` plus an annotated `vX.Y.Z` tag. Never commit directly. |
| `develop` | The integration branch. Branched once from `main`; long-lived, never deleted. Only `--no-ff` feature merges and release-prep commits land here — no feature work directly on `develop`. Pushed to `origin` after every merge. |
| `<type>/<slug>` | One branch per unit of work, cut from an up-to-date `develop`. `<type>` is the Conventional Commit type the branch's main commit carries (`feat`, `fix`, `perf`, `refactor`, `docs`, …); `<slug>` is 2–4 kebab-case words. Short-lived; deleted (local + `origin`) once merged. |

`main` and the tags use `vX.Y.Z`; the integration branch is `develop`, so a bare
ref is never ambiguous.

## Parallel work

Several branches can be in flight at once. Keep them independent:

- **One worktree per branch.** `git worktree add ../vizrack-wt/<slug> -b <type>/<slug> develop`
  gives each branch its own directory and its own `out/` build tree, so parallel
  builds never collide. `git worktree remove ../vizrack-wt/<slug>` when the branch
  merges.
- **Branch from a fresh `develop`.** `git fetch origin` first; base the branch on
  `origin/develop`.
- **Never merge one branch into another.** Each branch stays a clean delta against
  `develop`. When `develop` moves, `git rebase origin/develop` on the branch and
  resolve conflicts there, not at merge time.
- **Sequence shared-contract changes.** If two in-flight branches both need to
  touch `src/builtin/` public headers, `draw_list.*`, the catalog registration or
  `src/core/i18n_strings.inc`, land one first and rebase the other onto the new
  `develop` — do not develop the conflicting halves in parallel.
- **Gate every branch on its own.** `cmake --build`, `ctest` and
  `VizRack.exe --smoke-test` must pass on the branch before it is handed off.
- Push each active branch you want backed up; delete it from `origin` after it
  merges.

## Merging a finished branch

1. `git fetch origin`; on the branch, `git rebase origin/develop`.
2. Resolve conflicts. A `CHANGELOG.md` `## [Unreleased]` conflict is expected when
   several branches added notes — keep every side's bullets.
3. Re-run `cmake --build`, `ctest` and `--smoke-test` on the rebased tip.
4. `git checkout develop && git merge --ff-only origin/develop`, then
   `git merge --no-ff <type>/<slug>`. The merge commit is the integration point
   and keeps the branch revertable as a unit.
5. `git push origin develop` immediately.
6. `git branch -d <type>/<slug>`, `git push origin --delete <type>/<slug>` if it
   was pushed, and `git worktree remove` its worktree.

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

Push `develop` to `origin` after every merge, and push in-flight `<type>/<slug>`
branches you want backed up. Do **not** push tags or create GitHub releases
outside the release flow below.

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
