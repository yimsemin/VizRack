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
- `feat`, `fix` and `perf` are user-facing and become the changelog. Everything
  else is internal and is summarised as a count in the release notes, so small
  `refactor` / `docs` / `test` commits are encouraged.
- Optional body explains the *why*, wrapped near 72 columns.
- AI-assisted commits keep the `Co-Authored-By: Claude …` trailer.

## Pushing

Push `develop` to `origin` continuously. Do **not** push tags or create GitHub
releases outside the release flow below.

## Cutting a release

Only after the user gives explicit approval for a version:

1. On `develop`, bump `project(VizRack VERSION X.Y.Z ...)` in `CMakeLists.txt`.
2. Generate the notes from commit subjects:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\generate-release-notes.ps1
   ```

   The script reads the version from `CMakeLists.txt`, takes the range from the
   last `vX.Y.Z` tag to `HEAD`, and writes `docs/release-notes/X_Y_Z.md`. It does
   not inspect diffs or rewrite subjects.
3. Run the required Release build, `ctest`, `--smoke-test` and `scripts\package.ps1`.
4. Commit the version bump and generated note on `develop` and push.
5. Merge into `main` and tag:

   ```powershell
   git checkout main
   git merge --no-ff refs/heads/develop
   git tag -a refs/tags/vX.Y.Z -m "VizRack vX.Y.Z"
   git push origin refs/heads/main refs/tags/vX.Y.Z
   ```

6. Publish:

   ```powershell
   gh release create vX.Y.Z `
       --verify-tag `
       --title "VizRack vX.Y.Z" `
       --notes-file docs/release-notes/X_Y_Z.md `
       out/package/VizRack-win-x64.zip
   ```

7. `git checkout develop` and continue. `develop` is not deleted.
