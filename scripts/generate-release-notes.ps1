param(
    [ValidatePattern('^v?\d+\.\d+\.\d+$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$branch = (& git -C $repository symbolic-ref --quiet --short HEAD 2>$null) -join ''

if (-not $Version) {
    if ($branch -notmatch '^v(?<version>\d+\.\d+\.\d+)$') {
        throw "Current branch '$branch' is not a version branch such as v0.2.0."
    }
    $Version = $Matches.version
}

$version = $Version.TrimStart('v')
$releaseName = "v$version"
$branchRef = "refs/heads/$releaseName"
$noteName = $version.Replace('.', '_') + '.md'
$notePath = Join-Path $repository "docs/release-notes/$noteName"

& git -C $repository rev-parse --verify "$branchRef^{commit}" 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Release branch not found: $branchRef"
}
if ($branch -ne $releaseName) {
    throw "Current branch '$branch' does not match '$releaseName'."
}

$baseTag = ((& git -C $repository describe --tags --abbrev=0 `
    --match 'v[0-9]*' $branchRef 2>$null) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or $baseTag -notmatch '^v\d+\.\d+\.\d+$') {
    throw "No reachable version tag was found for $branchRef."
}

$baseRef = "refs/tags/$baseTag"
$range = "$baseRef..$branchRef"
$subjects = @(& git -C $repository log $range --reverse --no-merges --format='%s')
if ($LASTEXITCODE -ne 0) {
    throw "Could not collect commit subjects from $range."
}
if ($subjects.Count -eq 0) {
    throw "No commits found after $baseTag."
}

$lines = @("# VizRack $releaseName", '', '## Changes', '')
$lines += $subjects | ForEach-Object { "- $_" }

New-Item -ItemType Directory -Path (Split-Path $notePath) -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($notePath, (($lines -join "`n") + "`n"), $utf8)

Write-Host "Release note: $notePath"
Write-Host "Base tag:     $baseTag"
Write-Host "Titles:       $($subjects.Count)"
