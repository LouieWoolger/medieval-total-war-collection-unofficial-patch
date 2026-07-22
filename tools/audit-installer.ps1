[CmdletBinding()]
param(
    [string]$InstallerPath = '',
    [string]$SevenZipPath = '',
    [string]$OutputPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $InstallerPath) {
    $InstallerPath = Join-Path $root 'dist\Unofficial Medieval Total War Collection Patch.exe'
}
if (-not $OutputPath) {
    $OutputPath = Join-Path $root 'dist\INSTALLER_AUDIT.json'
}
if (-not $SevenZipPath -and $env:SEVENZIP_EXE) {
    $SevenZipPath = $env:SEVENZIP_EXE
}
if (-not $SevenZipPath) {
    $sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($sevenZipCommand) { $SevenZipPath = $sevenZipCommand.Source }
}
foreach ($required in @($InstallerPath, $SevenZipPath)) {
    if (-not $required -or -not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required release-audit input is missing: $required"
    }
}

function Test-ByteSequence {
    param([byte[]]$Haystack, [byte[]]$Needle)
    if ($Needle.Length -eq 0 -or $Haystack.Length -lt $Needle.Length) { return $false }
    for ($i = 0; $i -le $Haystack.Length - $Needle.Length; $i++) {
        $matched = $true
        for ($j = 0; $j -lt $Needle.Length; $j++) {
            if ($Haystack[$i + $j] -ne $Needle[$j]) { $matched = $false; break }
        }
        if ($matched) { return $true }
    }
    return $false
}

$listing = & $SevenZipPath l -slt $InstallerPath 2>&1
if ($LASTEXITCODE -ne 0) { throw "7-Zip archive inspection failed: $listing" }
$paths = @($listing | Where-Object { $_ -like 'Path = $PLUGINSDIR*' } | ForEach-Object {
    ($_ -split ' = ', 2)[1]
})
$joined = ($paths -join "`n").ToLowerInvariant()
$requiredEntries = @(
    'install-engine.ps1',
    'payload\payload-manifest.json',
    'payload\d3d9.dll',
    'payload\dgvoodoo_d3d9.dll',
    'payload\ddraw.dll',
    'payload\d3dimm.dll',
    'payload\dgvoodoo.conf',
    'compatibility.bmp',
    'uninstall.exe'
)
$forbiddenEntries = @('medieval_tw.exe', 'medieval.cfg', '~tmp.vrp', '.vrp', '.pdb', 'worklog', 'capture')
foreach ($entry in $requiredEntries) {
    if (-not $joined.Contains($entry)) { throw "Required embedded entry is missing: $entry" }
}
foreach ($entry in $forbiddenEntries) {
    if ($joined.Contains($entry)) { throw "Forbidden embedded material found: $entry" }
}

$bytes = [System.IO.File]::ReadAllBytes([System.IO.Path]::GetFullPath($InstallerPath))
$privateFragments = @(
    [System.IO.Path]::GetFullPath($root),
    [Environment]::GetFolderPath('UserProfile')
)
foreach ($fragment in $privateFragments) {
    foreach ($encoding in @([Text.Encoding]::ASCII, [Text.Encoding]::Unicode)) {
        if (Test-ByteSequence -Haystack $bytes -Needle $encoding.GetBytes($fragment)) {
            throw 'A private development path was embedded in the installer.'
        }
    }
}

$version = [Diagnostics.FileVersionInfo]::GetVersionInfo([System.IO.Path]::GetFullPath($InstallerPath))
$signature = Get-AuthenticodeSignature -LiteralPath $InstallerPath
$record = [ordered]@{
    schema = 'unofficial-medieval-total-war-patch-installer-audit-v1'
    installer = [System.IO.Path]::GetFileName($InstallerPath)
    length = (Get-Item -LiteralPath $InstallerPath).Length
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $InstallerPath).Hash
    metadata = [ordered]@{
        product_name = $version.ProductName
        file_description = $version.FileDescription
        company_name = $version.CompanyName
        file_version = $version.FileVersion
        product_version = $version.ProductVersion
        copyright = $version.LegalCopyright
    }
    authenticode = [string]$signature.Status
    embedded_entry_count = $paths.Count
    required_entries = $requiredEntries
    forbidden_entries_absent = $forbiddenEntries
    private_paths_absent = $true
    result = 'pass'
}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "Installer audit passed: $OutputPath"
