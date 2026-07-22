[CmdletBinding()]
param(
    [string]$SupportedGameExecutable = '',
    [string]$NsisDirectory = '',
    [string]$SevenZipPath = '',
    [switch]$SkipR185Build,
    [switch]$SkipCompiledInstallerTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$python = (Get-Command python.exe -ErrorAction Stop).Source

function Resolve-MakeNsis {
    if ($NsisDirectory) {
        $candidate = Join-Path $NsisDirectory 'makensis.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    if ($env:NSIS_HOME) {
        $candidate = Join-Path $env:NSIS_HOME 'makensis.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'NSIS 3.x was not found. Supply -NsisDirectory or set NSIS_HOME.'
}

function Resolve-SevenZip {
    if ($SevenZipPath -and (Test-Path -LiteralPath $SevenZipPath -PathType Leaf)) {
        return [System.IO.Path]::GetFullPath($SevenZipPath)
    }
    if ($env:SEVENZIP_EXE -and (Test-Path -LiteralPath $env:SEVENZIP_EXE -PathType Leaf)) {
        return [System.IO.Path]::GetFullPath($env:SEVENZIP_EXE)
    }
    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw '7-Zip was not found. Supply -SevenZipPath or set SEVENZIP_EXE.'
}

$makeNsis = Resolve-MakeNsis
$sevenZip = Resolve-SevenZip
$dist = Join-Path $root 'dist'
$installer = Join-Path $dist 'Unofficial Medieval Total War Collection Patch.exe'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
if ($SupportedGameExecutable) {
    if (-not (Test-Path -LiteralPath $SupportedGameExecutable -PathType Leaf)) {
        throw "Supported game executable was not found: $SupportedGameExecutable"
    }
    $env:MTW_TEST_GAME_EXE = [System.IO.Path]::GetFullPath($SupportedGameExecutable)
}

Push-Location $root
try {
    & (Join-Path $root 'tools\generate-product-nsh.ps1')

    if (-not $SkipR185Build) {
        $r185Build = Join-Path $root ('artifacts\build\r185-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        & (Join-Path $root 'vendor\r185\source\build_scaffold.ps1') -OutputDirectory $r185Build
        if ($LASTEXITCODE -ne 0) { throw "R185 build_scaffold.ps1 failed: $LASTEXITCODE" }
        $rebuilt = Join-Path $r185Build 'combined-r154\D3D9.dll'
        $expected = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root 'vendor\runtime\D3D9.dll')).Hash
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $rebuilt).Hash
        if ($actual -ne $expected) { throw "Rebuilt R185 identity mismatch: $actual" }
    }

    & $python -m pytest `
        tests/test_payload_provenance.py `
        tests/test_install_engine.py `
        tests/test_assets.py `
        tests/test_installer_script.py -q
    if ($LASTEXITCODE -ne 0) { throw "Project pytest contracts failed: $LASTEXITCODE" }

    foreach ($name in @('Unofficial Medieval Total War Collection Patch.exe', 'Unofficial Medieval Total War Patch.exe', 'RELEASE_MANIFEST.json', 'SHA256SUMS.txt', 'INSTALLER_AUDIT.json')) {
        $path = Join-Path $dist $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
    }
    & $makeNsis /V2 (Join-Path $root 'installer.nsi')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $installer -PathType Leaf)) {
        throw "makensis failed: $LASTEXITCODE"
    }

    $matrixStatus = 'not-run'
    if (-not $SkipCompiledInstallerTests) {
        if (-not $SupportedGameExecutable) {
            throw 'The release gate requires -SupportedGameExecutable, or explicitly use -SkipCompiledInstallerTests.'
        }
        & (Join-Path $root 'tests\run_installer_matrix.ps1') -SupportedGameExecutable $SupportedGameExecutable
        if ($LASTEXITCODE -ne 0) { throw "Compiled installer matrix failed: $LASTEXITCODE" }
        $matrixStatus = 'pass'
    }

    & $python (Join-Path $root 'tools\build-release-manifest.py') `
        --installer $installer `
        --output (Join-Path $dist 'RELEASE_MANIFEST.json') `
        --compiled-matrix $matrixStatus
    if ($LASTEXITCODE -ne 0) { throw "Release manifest generation failed: $LASTEXITCODE" }

    $checksumLines = foreach ($name in @('Unofficial Medieval Total War Collection Patch.exe', 'RELEASE_MANIFEST.json')) {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $dist $name)).Hash
        "$hash  $name"
    }
    [System.IO.File]::WriteAllLines(
        (Join-Path $dist 'SHA256SUMS.txt'),
        $checksumLines,
        [Text.UTF8Encoding]::new($false))

    & (Join-Path $root 'tools\audit-installer.ps1') -InstallerPath $installer -SevenZipPath $sevenZip
    $env:SEVENZIP_EXE = $sevenZip
    & $python -m pytest tests/test_release_hygiene.py -q
    if ($LASTEXITCODE -ne 0) { throw "Release-hygiene pytest failed: $LASTEXITCODE" }

    Write-Output "Release build complete: $installer"
    Write-Output "SHA-256: $((Get-FileHash -Algorithm SHA256 -LiteralPath $installer).Hash)"
}
finally {
    Pop-Location
}
