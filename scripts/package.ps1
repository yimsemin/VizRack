param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'out/build/vs2026-x64',
    [string]$PackageDirectory = 'out/package'
)

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repository $BuildDirectory))
$packageRoot = [IO.Path]::GetFullPath((Join-Path $repository $PackageDirectory))
$allowedPackageRoot = [IO.Path]::GetFullPath((Join-Path $repository 'out/package'))
$allowedPackagePrefix = $allowedPackageRoot.TrimEnd([IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar

if (-not $packageRoot.Equals($allowedPackageRoot, [StringComparison]::OrdinalIgnoreCase) -and
    -not $packageRoot.StartsWith($allowedPackagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "PackageDirectory must remain under $allowedPackageRoot"
}

$sourceExecutable = Join-Path $buildRoot "$Configuration/VizRack.exe"
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    $sourceExecutable = Join-Path $buildRoot 'VizRack.exe'
}
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Release executable not found under: $buildRoot"
}

$destination = Join-Path $packageRoot 'VizRack'
$zipPath = Join-Path $packageRoot 'VizRack-win-x64.zip'

if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
New-Item -ItemType Directory -Path $destination | Out-Null
New-Item -ItemType Directory -Path (Join-Path $destination 'licenses') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $destination 'docs') | Out-Null

Copy-Item -LiteralPath $sourceExecutable -Destination $destination
Copy-Item -LiteralPath (Join-Path $repository 'README.md') -Destination $destination
Copy-Item -LiteralPath (Join-Path $repository 'LICENSE') -Destination $destination
Copy-Item -LiteralPath (Join-Path $repository 'THIRD_PARTY_NOTICES.md') -Destination $destination
Copy-Item -LiteralPath (Join-Path $repository 'licenses/Steinberg-VST3-SDK-MIT.txt') `
    -Destination (Join-Path $destination 'licenses')
Copy-Item -LiteralPath (Join-Path $repository 'docs/ARCHITECTURE.md') `
    -Destination (Join-Path $destination 'docs')
Copy-Item -LiteralPath (Join-Path $repository 'docs/BUILTIN_VISUALIZER_CORE.md') `
    -Destination (Join-Path $destination 'docs')

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zipPath -CompressionLevel Optimal

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLower()
$shaPath = "$zipPath.sha256"
[IO.File]::WriteAllText($shaPath, "$hash  VizRack-win-x64.zip`n", [Text.UTF8Encoding]::new($false))

Write-Host "Portable folder: $destination"
Write-Host "Portable ZIP:    $zipPath"
Write-Host "SHA-256:         $hash"
Write-Host "SHA-256 file:    $shaPath"
Write-Host 'No third-party plug-in binary or runtime data was included.'
