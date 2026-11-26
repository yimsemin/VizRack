# VizRack branching, commits and releases

## Branches

| Branch | Purpose |
| --- | --- |
| `main` | The only long-lived branch. Protected: changes land only through a squash-merged PR. Every release is tagged `vX.Y.Z` (annotated) on a `main` commit. |
| `<type>/<slug>` | One branch per unit of work, cut from an up-to-date `origin/main`. `<type>` is one of `feat`, `fix`, `docs`, `chore` (also for refactoring, performance, tests and build work), `ci`; `<slug>` is 2–4 kebab-case words. Short-lived; deleted (local + `origin`) once merged. |

## Parallel work

Several branches can be in flight at once. Keep them independent:

- **One worktree per branch.** `git worktree add .claude/worktrees/<slug> -b <type>/<slug> origin/main`
  gives each branch its own directory (inside the project folder, git-ignored) and its
  own `out/` build tree, so parallel builds never collide. A new worktree has no
  `external/vst3sdk`; pass `-DVIZRACK_VST3_SDK_PATH=<main checkout>/external/vst3sdk`
  to reuse it instead of fetching again. `git worktree remove .claude/worktrees/<slug>` when the branch
  merges.
- **Never merge one branch into another.** Each branch stays a clean delta against
  `main`. When `main` moves, `git fetch origin && git rebase origin/main` on the
  branch and resolve conflicts there.
- **Sequence shared-contract changes.** If two in-flight branches both need to
  touch `src/builtin/` public headers, `draw_list.*`, the catalog registration or
  `src/core/i18n_strings.inc`, land one first and rebase the other onto the new
  `main` — do not develop the conflicting halves in parallel.
- **Gate every branch on its own.** `cmake --build`, `ctest` and
  `VizRack.exe --smoke-test` must pass on the branch before it is handed off.

## Merging a finished branch

1. `git fetch origin`; on the branch, `git rebase origin/main`.
2. Resolve conflicts. A `CHANGELOG.md` `## [Unreleased]` conflict is expected when
   several branches added notes — keep every side's bullets.
3. Re-run `cmake --build`, `ctest` and `--smoke-test` on the rebased tip.
4. Push the branch and open a PR to `main`; squash-merge it (the PR title becomes
   the commit subject). Merge commits and rebase merges are disabled.
5. Delete the branch (local + `origin`) and `git worktree remove` its worktree.

## Commits

- One logical, self-contained change per commit. Commit after each meaningful step
  once `cmake --build`, `ctest` and `VizRack.exe --smoke-test` all pass. Do not
  batch unrelated work into one commit, and never commit `out/` or other build
  artifacts.
- Subject: imperative mood. Format, types, length and language follow the Git
  rules in [`AGENTS.md`](../AGENTS.md) and are checked by `.githooks/commit-msg`.
- The changelog is **not** derived from commit subjects. Any commit that changes
  what a user sees also edits `## [Unreleased]` in `CHANGELOG.md` in the same
  commit; internal commits (`refactor` / `docs` / `test` / `build` / `chore`)
  touch nothing there. See `docs/RELEASE_NOTES_STYLE.md` for how entries are
  worded.
- Optional body explains the *why*, wrapped near 72 columns.

## Cutting a release

Only after the user gives explicit approval for a version. Do not push tags or
create GitHub releases outside this flow.

1. On a `chore/release-vX-Y-Z` branch cut from `origin/main`, make sure
   `## [Unreleased]` in `CHANGELOG.md` reads the way the release should,
   including the italic one-line lead under the heading.
2. Bump `project(VizRack VERSION X.Y.Z ...)` in `CMakeLists.txt`.
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
5. Commit the version bump, changelog and generated note
   (`chore(release): vX.Y.Z`), push, and squash-merge the PR into `main`.
6. Tag the merged `main` commit and push the tag:

   ```powershell
   git checkout main
   git pull --ff-only origin main
   git tag -a vX.Y.Z -m "VizRack vX.Y.Z"
   git push origin refs/tags/vX.Y.Z
   ```

7. Publish:

   ```powershell
   gh release create vX.Y.Z `
       --verify-tag `
       --title "VizRack vX.Y.Z" `
       --notes-file docs/release-notes/X_Y_Z.md `
       out/package/VizRack-win-x64.zip
   ```
