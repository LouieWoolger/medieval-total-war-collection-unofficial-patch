[CmdletBinding()]
param(
    [string]$InstallerPath = '',
    [string]$NativeHelperPath = '',
    [string]$UninstallerOutputPath = '',
    [string]$PythonPath = '',
    [string]$SevenZipPath = '',
    [string]$OutputPath = '',
    [string]$ArtifactDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $NativeHelperPath) { $NativeHelperPath = $env:MTW_TEST_NATIVE_HELPER }
if (-not $NativeHelperPath) { $NativeHelperPath = Join-Path $root 'build\medieval_fix_patcher.exe' }
if (-not $PythonPath) { $PythonPath = (Get-Command python.exe -ErrorAction Stop).Source }
$product = Get-Content -LiteralPath (Join-Path $root 'config\product.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $InstallerPath) { $InstallerPath = Join-Path $root ('dist\' + $product.output_filename) }
if (-not $OutputPath) { $OutputPath = Join-Path (Split-Path -Parent $InstallerPath) 'INSTALLER_AUDIT.json' }
if (-not $ArtifactDirectory) {
    $ArtifactDirectory = Join-Path $root ('artifacts\audit-' + [Guid]::NewGuid().ToString('N'))
}
if (-not $SevenZipPath -and $env:SEVENZIP_EXE) { $SevenZipPath = $env:SEVENZIP_EXE }
if (-not $SevenZipPath) {
    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($command) { $SevenZipPath = $command.Source }
}
foreach ($required in @($InstallerPath, $SevenZipPath, $NativeHelperPath, $PythonPath)) {
    if (-not $required -or -not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required installer-audit input is missing: $required"
    }
}
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$extractRoot = Join-Path $ArtifactDirectory ('embedded-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $extractRoot -ErrorAction Stop | Out-Null

$engineNames = @('medieval_fix_patcher.exe')
$runtimeNames = @('payload-manifest.json','D3D9.dll','dgVoodoo_D3D9.dll','ddraw.dll','D3DImm.dll','dgVoodoo.conf')
$runtimeVariants = @('payload','payload-scroll-off')
$uiNames = @('compatibility.bmp','discord-badge.bmp','discord-badge-hover.bmp','kofi-badge.bmp','kofi-badge-hover.bmp')
$documentation = [ordered]@{'LICENSE.txt'='LICENSE';'MinGW-w64-runtime.txt'='licenses\MinGW-w64-runtime.txt'}
$common = @('modern-wizard.bmp','nsDialogs.dll','System.dll') + $engineNames + @($documentation.Keys)
$common += @(foreach ($variant in $runtimeVariants) {
    foreach ($name in $runtimeNames) { $variant + '\' + $name }
})
$requiredEntries = @($common + $uiNames + [string]$product.uninstaller_filename)
$forbiddenEntries = @('medieval_tw.exe','medieval.cfg','~tmp.vrp','.vrp','.pdb','worklog','capture','.ps1','.cs','powershell','mscoree','libstdc++','libgcc','libwinpthread')
$privateFragments = @([IO.Path]::GetFullPath($root),[Environment]::GetFolderPath('UserProfile'))
$byteEncoding = [Text.Encoding]::GetEncoding(28591)

function Assert-NoPrivatePaths {
    param([string]$Path)
    $binaryText = $byteEncoding.GetString([IO.File]::ReadAllBytes($Path))
    foreach ($fragment in $privateFragments) {
        foreach ($encoding in @([Text.Encoding]::UTF8,[Text.Encoding]::Unicode)) {
            $needle = $byteEncoding.GetString($encoding.GetBytes($fragment))
            if ($binaryText.Contains($needle)) { throw 'A private development path was embedded in the installer.' }
        }
    }
}

function Read-ArchiveEntries {
    param([string]$Archive,[string]$Label,[string[]]$Expected)
    $listing = @(& $SevenZipPath l -slt $Archive 2>&1)
    $exitCode = $LASTEXITCODE
    $listing | Set-Content -LiteralPath (Join-Path $ArtifactDirectory ($Label + '-archive.txt')) -Encoding UTF8
    if ($exitCode -ne 0) { throw "7-Zip $Label inspection failed: $exitCode" }
    $allPaths = @($listing | Where-Object { $_ -like 'Path = *' } | ForEach-Object { ($_ -split ' = ',2)[1] })
    if ($allPaths.Count -lt 2) { throw "$Label archive contained no entries." }
    $entries = @($allPaths | Select-Object -Skip 1)
    $relative = foreach ($entry in $entries) {
        if (-not $entry.StartsWith('$PLUGINSDIR\',[StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected $Label archive destination: $entry"
        }
        $name = $entry.Substring(12)
        if ($name -notin $Expected) { throw "Undeclared $Label archive entry: $name" }
        foreach ($forbidden in $forbiddenEntries) {
            if ($name.ToLowerInvariant().Contains($forbidden)) { throw "Forbidden $Label archive entry: $name" }
        }
        $name
    }
    if (@($relative | Select-Object -Unique).Count -ne $relative.Count) {
        throw "Duplicate $Label archive destinations are not allowed."
    }
    foreach ($name in $Expected) {
        if ($name -notin $relative) { throw "Missing $Label archive entry: $name" }
    }
    return $relative
}

function Expand-And-Verify {
    param([string]$Archive,[string]$Label)
    $destination = Join-Path $extractRoot $Label
    New-Item -ItemType Directory -Path $destination -ErrorAction Stop | Out-Null
    $output = @(& $SevenZipPath x $Archive ('-o' + $destination) -y 2>&1)
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $ArtifactDirectory ($Label + '-extraction.txt')) -Encoding UTF8
    if ($exitCode -ne 0) { throw "7-Zip $Label extraction failed: $exitCode" }
    $pluginDirectory = Join-Path $destination '$PLUGINSDIR'
    foreach ($name in $engineNames) {
        $embedded = Join-Path $pluginDirectory $name
        $source = $NativeHelperPath
        if ((Get-FileHash -LiteralPath $embedded -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
            throw "Embedded $Label native helper differs from the built input: $name"
        }
        Assert-NoPrivatePaths $embedded
    }
    foreach ($variant in $runtimeVariants) {
        foreach ($name in $runtimeNames) {
            $sourceName = $name
            if ($variant -eq 'payload-scroll-off') {
                if ($name -eq 'payload-manifest.json') { $sourceName = 'payload-manifest-scroll-off.json' }
                if ($name -eq 'D3D9.dll') { $sourceName = 'D3D9-scroll-off.dll' }
            }
            $embedded = Join-Path $pluginDirectory ($variant + '\' + $name)
            $source = Join-Path $root ('vendor\runtime\' + $sourceName)
            if ((Get-FileHash -LiteralPath $embedded -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
                throw "Embedded $Label runtime differs from pinned payload: $variant/$name"
            }
            Assert-NoPrivatePaths $embedded
        }
    }
    foreach ($name in $documentation.Keys) {
        $embedded = Join-Path $pluginDirectory $name
        $source = Join-Path $root $documentation[$name]
        if ((Get-FileHash -LiteralPath $embedded -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
            throw "Embedded $Label license or attribution differs from source: $name"
        }
        Assert-NoPrivatePaths $embedded
    }
    return $pluginDirectory
}

$installerEntries = @(Read-ArchiveEntries $InstallerPath 'installer' $requiredEntries)
Assert-NoPrivatePaths $InstallerPath
$installerExtract = Expand-And-Verify $InstallerPath 'installer'
$uninstaller = Join-Path $installerExtract ([string]$product.uninstaller_filename)
$uninstallerEntries = @(Read-ArchiveEntries $uninstaller 'uninstaller' $common)
Assert-NoPrivatePaths $uninstaller
$uninstallerExtract = Expand-And-Verify $uninstaller 'uninstaller'
if ($UninstallerOutputPath) {
    if (Test-Path -LiteralPath $UninstallerOutputPath) { throw 'Refusing to replace an existing generated uninstaller.' }
    Copy-Item -LiteralPath $uninstaller -Destination $UninstallerOutputPath -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $uninstaller -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $UninstallerOutputPath -Algorithm SHA256).Hash) {
        throw 'Generated uninstaller copy verification failed.'
    }
}

$sourceNames = @('installer.nsi','installer-support.nsh','installer-uninstall.nsh','include\product.nsh','include\ui.nsh','config\product.json')
$sourceNames += @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -File | Where-Object { $_.Extension -in @('.c','.h','.rc','.manifest') } | ForEach-Object { 'src\' + $_.Name })
$sourceNames += @($documentation.Values)
$sourceHashes = [ordered]@{}
foreach ($name in $sourceNames) {
    $path = Join-Path $root $name
    $sourceHashes[$name.Replace('\','/')] = [ordered]@{
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        length=(Get-Item -LiteralPath $path).Length
    }
}
$version = [Diagnostics.FileVersionInfo]::GetVersionInfo($InstallerPath)
$signature = Get-AuthenticodeSignature -LiteralPath $InstallerPath
$nativePeReport = Join-Path $ArtifactDirectory 'native-helper-pe.json'
& $PythonPath (Join-Path $root 'tools\build-release-manifest.py') --audit-native-helper (Join-Path $installerExtract 'medieval_fix_patcher.exe') --output $nativePeReport
if ($LASTEXITCODE -ne 0) { throw 'Native helper PE/import/CLR audit failed.' }
$nativePe = Get-Content -LiteralPath $nativePeReport -Raw -Encoding UTF8 | ConvertFrom-Json
$uninstallerPeReport = Join-Path $ArtifactDirectory 'uninstaller-helper-pe.json'
& $PythonPath (Join-Path $root 'tools\build-release-manifest.py') --audit-native-helper (Join-Path $uninstallerExtract 'medieval_fix_patcher.exe') --output $uninstallerPeReport
if ($LASTEXITCODE -ne 0) { throw 'Embedded uninstaller helper PE/import/CLR audit failed.' }
$uninstallerPe = Get-Content -LiteralPath $uninstallerPeReport -Raw -Encoding UTF8 | ConvertFrom-Json
if ($version.FileVersion -ne $product.version -or $version.ProductVersion -ne $product.version) {
    throw 'Installer version metadata does not match config/product.json.'
}
$record = [ordered]@{
    schema='unofficial-medieval-total-war-patch-installer-audit-v2'
    installer=[IO.Path]::GetFileName($InstallerPath)
    length=(Get-Item -LiteralPath $InstallerPath).Length
    sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $InstallerPath).Hash
    metadata=[ordered]@{
        product_name=$version.ProductName; file_description=$version.FileDescription
        company_name=$version.CompanyName; file_version=$version.FileVersion
        product_version=$version.ProductVersion; copyright=$version.LegalCopyright
    }
    component=[string]$product.component_name
    native_helper=$nativePe
    uninstaller=[ordered]@{
        filename=[string]$product.uninstaller_filename; location='game-root'
        embedded_sha256=(Get-FileHash -LiteralPath $uninstaller -Algorithm SHA256).Hash
        embedded_entry_count=$uninstallerEntries.Count; required_entries=$common
        native_helper=$uninstallerPe
        engine_and_payload_match_source=$true
    }
    authenticode=[string]$signature.Status
    embedded_entry_count=$installerEntries.Count
    required_entries=$requiredEntries
    forbidden_entries_absent=$forbiddenEntries
    private_paths_absent=$true
    engine_and_payload_match_source=$true
    license_and_attribution_match_source=$true
    source_inputs=$sourceHashes
    audit_tool=[ordered]@{
        filename=[IO.Path]::GetFileName($SevenZipPath)
        version=[Diagnostics.FileVersionInfo]::GetVersionInfo($SevenZipPath).FileVersion
        sha256=(Get-FileHash -LiteralPath $SevenZipPath -Algorithm SHA256).Hash
    }
    result='pass'
}
New-Item -ItemType Directory -Path (Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))) -Force | Out-Null
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "Installer audit passed: $OutputPath"
