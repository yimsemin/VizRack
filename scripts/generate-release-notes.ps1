param(
    [ValidatePattern('^v?\d+\.\d+\.\d+$')]
    [string]$Version
)

# Promotes the "## [Unreleased]" section of CHANGELOG.md to a dated release
# section, opens a fresh empty Unreleased, refreshes the compare links, and
# writes docs/release-notes/<X_Y_Z>.md - the verbatim body of the GitHub Release,
# with the portable-ZIP SHA-256 and the full-changelog link filled in.
#
# The changelog is hand-curated as work lands (docs/RELEASE_NOTES_STYLE.md); this
# script only reshapes it for release. It never invents or rewrites entries.

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$changelogPath = Join-Path $repo 'CHANGELOG.md'
$slug = 'yimsemin/VizRack'
$nl = "`n"
$utf8 = [Text.UTF8Encoding]::new($false)

if (-not $Version) {
    $cmake = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
    if ($cmake -notmatch 'project\(VizRack\s+VERSION\s+(?<v>\d+\.\d+\.\d+)') {
        throw 'Could not read project(VizRack VERSION ...) from CMakeLists.txt; pass -Version.'
    }
    $Version = $Matches.v
}
$version = $Version.TrimStart('v')
$tag = "v$version"
$date = (Get-Date).ToString('yyyy-MM-dd')
$notePath = Join-Path $repo ("docs/release-notes/" + ($version -replace '\.', '_') + '.md')

$baseTag = (& git -C $repo describe --tags --abbrev=0 --match 'v[0-9]*' HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $baseTag -notmatch '^v\d+\.\d+\.\d+$') {
    throw 'No reachable vMAJOR.MINOR.PATCH tag was found for the current branch.'
}
if ($baseTag -eq $tag) {
    throw "HEAD is already at $tag; bump project(VizRack VERSION ...) before generating notes."
}

# --- read and split the changelog --------------------------------------------
$lines = ([IO.File]::ReadAllText($changelogPath) -replace "`r`n", "`n").Split("`n")

$unreleasedAt = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^##\s+\[Unreleased\]\s*$') { $unreleasedAt = $i; break }
}
if ($unreleasedAt -lt 0) { throw 'CHANGELOG.md has no "## [Unreleased]" heading.' }

$bodyStart = $unreleasedAt + 1
$bodyEnd = $lines.Count
for ($i = $bodyStart; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^##\s+' -or $lines[$i] -match '^\[[^\]]+\]:\s+\S') { $bodyEnd = $i; break }
}
if ($bodyEnd -le $bodyStart) { throw '"## [Unreleased]" is empty. Nothing to release.' }
$body = ($lines[$bodyStart..($bodyEnd - 1)] -join $nl).Trim()
if (@([regex]::Matches($body, "(?m)^\s*-\s+\S")).Count -eq 0) {
    throw '"## [Unreleased]" has no entries. Curate CHANGELOG.md before releasing.'
}

$head = if ($unreleasedAt -gt 0) { ($lines[0..($unreleasedAt - 1)] -join $nl).TrimEnd() } else { '' }
$tailLines = if ($bodyEnd -lt $lines.Count) { $lines[$bodyEnd..($lines.Count - 1)] } else { @() }

# split any remaining release sections from the trailing link-reference block
$refs = @(); $tail = @()
foreach ($line in $tailLines) {
    if ($line -match '^\[[^\]]+\]:\s+\S') { $refs += $line } else { $tail += $line }
}
$tailText = ($tail -join $nl).Trim()

$compare = "https://github.com/$slug/compare"
$newRefs = @("[Unreleased]: $compare/$tag...HEAD", "[$version]: $compare/$baseTag...$tag")
foreach ($line in $refs) {
    if ($line -notmatch '^\[Unreleased\]:' -and $line -notmatch "^\[$([regex]::Escape($version))\]:") {
        $newRefs += $line
    }
}

$rebuilt = @(
    $head, '', '## [Unreleased]', '', "## [$version] - $date", '', $body, ''
    $tailText, '', ($newRefs -join $nl)
) -join $nl
$rebuilt = ($rebuilt -replace "$nl{3,}", "$nl$nl").TrimEnd() + $nl
[IO.File]::WriteAllText($changelogPath, $rebuilt, $utf8)

# --- docs/release-notes/<X_Y_Z>.md -----------------------------------------
$shaPath = Join-Path $repo 'out/package/VizRack-win-x64.zip.sha256'
$sha = if (Test-Path -LiteralPath $shaPath) {
    (((Get-Content -LiteralPath $shaPath -Raw).Trim() -split '\s+')[0]).ToLower()
} else {
    'MISSING - run scripts\package.ps1, then re-run this script'
}

$note = @(
    $body, '', '---', ''
    '**Download: VizRack-win-x64.zip** (attached below). Portable, no installer -'
    'unzip into a folder you can write to and run `VizRack.exe`.', ''
    "Windows 10 1703+ or Windows 11, x64. Not code-signed, so SmartScreen may warn. ``SHA-256: $sha``", ''
    "**Full changelog:** https://github.com/$slug/compare/$baseTag...$tag"
) -join $nl
[IO.File]::WriteAllText($notePath, ($note.TrimEnd() + $nl), $utf8)

# --- sanity check against commit subjects ----------------------------------
$subjects = @(& git -C $repo log "refs/tags/$baseTag..HEAD" --no-merges --format='%s')
$userFacing = @($subjects | Where-Object { $_ -match '^(feat|fix|perf)(\([^)]*\))?!?:' }).Count
$entryCount = @([regex]::Matches($body, "(?m)^\s*-\s+\S")).Count

Write-Host "Release note: $notePath  (body only - GitHub shows 'VizRack $tag' as the title)"
Write-Host "Changelog:    promoted [Unreleased] -> [$version] - $date"
Write-Host "Range:        $baseTag..HEAD  ($($subjects.Count) commits, $userFacing feat/fix/perf)"
Write-Host "Entries:      $entryCount bullet(s) in the released section"
if ($sha -notmatch '^[0-9a-f]{64}$') {
    Write-Warning 'SHA-256 is a placeholder - run scripts\package.ps1 and re-run this script.'
}
if ($userFacing -gt 0 -and $entryCount -gt ($userFacing * 2 + 3)) {
    Write-Warning "Entry count ($entryCount) is high vs $userFacing user-facing commits - eyeball the note."
}
