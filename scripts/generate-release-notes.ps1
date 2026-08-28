param(
    [ValidatePattern('^v?\d+\.\d+\.\d+$')]
    [string]$Version
)

# Writes docs/release-notes/<version>.md from the Conventional Commit subjects on
# the current branch since the last vMAJOR.MINOR.PATCH tag. feat/fix/perf become
# the public changelog; every other type is summarised as a single count so day
# to day commits can stay small without cluttering the notes.

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if (-not $Version) {
    $cmake = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
    if ($cmake -notmatch 'project\(VizRack\s+VERSION\s+(?<v>\d+\.\d+\.\d+)') {
        throw 'Could not read project(VizRack VERSION ...) from CMakeLists.txt; pass -Version.'
    }
    $Version = $Matches.v
}
$version = $Version.TrimStart('v')
$tag = "v$version"
$notePath = Join-Path $repo ("docs/release-notes/" + ($version -replace '\.', '_') + '.md')

$baseTag = (& git -C $repo describe --tags --abbrev=0 --match 'v[0-9]*' HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $baseTag -notmatch '^v\d+\.\d+\.\d+$') {
    throw 'No reachable vMAJOR.MINOR.PATCH tag was found for the current branch.'
}

$subjects = @(& git -C $repo log "refs/tags/$baseTag..HEAD" --reverse --no-merges --format='%s')
if ($LASTEXITCODE -ne 0) { throw "Could not read commit subjects from $baseTag..HEAD." }
if ($subjects.Count -eq 0) { throw "No commits found after $baseTag." }

$feat = @(); $fix = @(); $perf = @(); $internal = 0
foreach ($subject in $subjects) {
    if ($subject -match '^feat(\([^)]*\))?!?:\s*(.+)$')      { $feat += "- $($Matches[2])" }
    elseif ($subject -match '^fix(\([^)]*\))?!?:\s*(.+)$')   { $fix  += "- $($Matches[2])" }
    elseif ($subject -match '^perf(\([^)]*\))?!?:\s*(.+)$')  { $perf += "- $($Matches[2])" }
    else { $internal++ }
}

$lines = [Collections.Generic.List[string]]::new()
$lines.Add("# VizRack $tag")
$lines.Add("")
function Add-Section([string]$heading, [string[]]$entries) {
    if ($entries.Count -eq 0) { return }
    $script:lines.Add($heading)
    $script:lines.Add("")
    foreach ($entry in $entries) { $script:lines.Add($entry) }
    $script:lines.Add("")
}
Add-Section "### Added" $feat
Add-Section "### Fixed" $fix
Add-Section "### Performance" $perf
if ($internal) {
    $plural = if ($internal -ne 1) { 's' } else { '' }
    $lines.Add("_Plus $internal internal change$plural (refactor, docs, tests, build)._")
    $lines.Add("")
}

$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($notePath, (($lines -join "`n").TrimEnd() + "`n"), $utf8)

Write-Host "Release note: $notePath"
Write-Host "Range:        $baseTag..HEAD"
Write-Host "Commits:      $($subjects.Count) ($($feat.Count) feat, $($fix.Count) fix, $($perf.Count) perf, $internal internal)"
