[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Inspect', 'Install', 'Verify', 'Restore')]
    [string]$Operation,

    [Parameter(Mandatory = $true)]
    [string]$Target,

    [Parameter(Mandatory = $true)]
    [string]$PayloadDirectory,

    [Parameter(Mandatory = $true)]
    [string]$InstallerVersion,

    [string]$InstallerPath = '',

    [ValidateSet('Json', 'Human')]
    [string]$OutputMode = 'Json'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)

$script:ProductStateDirectoryName = '.unofficial-medieval-total-war-patch'
$script:ReceiptName = 'install-manifest.json'
$script:PayloadNames = @(
    'dgVoodoo_D3D9.dll',
    'ddraw.dll',
    'D3DImm.dll',
    'dgVoodoo.conf',
    'D3D9.dll'
)
$script:SupportedExecutableHash = '23724B034F8C97094CECD5560F053864A475A88ADAD077C046B2BEB79331ACE5'
$script:KnownD3D9Modes = @{
    'E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6' = 'stock-dgvoodoo'
    '34F855A17C10B6BBCEFD844B99A5B5A7F442DECDDFFDB0D898645FA2FF0B0F0C' = 'dust-only'
    'E421B9A1FF5927A8B93FD7B2A83E0C965EB1EA596D924561713C16F323924892' = 'r6f160'
    'A3FFCC0BCDD74044448BF0418F0FA732594F23D025DE667172AFE118CD82F9FA' = 'r185'
    '3EE7EE33946F9F73A61559C23505AFCC27D45E61067644AF611B09F627297AD8' = 'r185'
    '060136A2BE50A0F209FF74F242FFC264C6709C48135C8EB362771DCBF25931F1' = 'r185'
    'C0D597364734EAEA26ABA83F1FA6B8875B830E4D626D5CF41F42DAEB92106CB4' = 'r185'
    '9D1C8B4E5C0CF0A224FFEB8C20F762C52B1D8C2B306162A639D81295B103D04E' = 'r185'
    'E3D5D6D5E214D79592B7D6F7F26D52CDFF5A59EB028CCD9DE499900BCFF18D74' = 'r185'
    'F5F7EEDB312D251ECE1CCC726A08E866020C89A3C00212216B6A70FC0F5D6BE0' = 'r185'
    'CC3537E286863FA75200DFB80839F07E07D8AD91F2ABBB2A4C5629677CE965FD' = 'r185'
    '8FD9B9CE5809C52E2C815754ECD391D6ACDA14D33518A70BA9B4BE1FFD5DC7DF' = 'r185'
    'D61A5DB23EE091CAE0D97AB5E385D42BD301E97D7F9A4FB6F3A3CA1484E7B932' = 'r185'
}
$script:KnownPreviousPayloadHashes = @{
    'dgVoodoo.conf' = @(
        '23A43425ADBA421BAF9531220E75964F59E829F67CE8577BDE1C45EFBCAD61DA',
        'BD21E07D4B9282A8CA0F53613CCB852D419E5D54967A96C8E34A60D2F96E476A'
    )
}

$compilerWorkingDirectory = [System.IO.Path]::GetFullPath($PayloadDirectory)
if (-not (Test-Path -LiteralPath $compilerWorkingDirectory -PathType Container)) {
    throw "The embedded compatibility payload directory is missing."
}
$originalPowerShellLocation = (Get-Location).Path
$originalProcessDirectory = [Environment]::CurrentDirectory
try {
    # NSIS extracts a native plugin named System.dll beside the installer engine.
    # Add-Type probes the process working directory for framework references, so
    # compile from the verified payload directory rather than the plugin host.
    Set-Location -LiteralPath $compilerWorkingDirectory
    [Environment]::CurrentDirectory = $compilerWorkingDirectory
    if (-not ('UnofficialMedievalPatch.NativeMethods' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace UnofficialMedievalPatch {
    public static class NativeMethods {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool MoveFileEx(
            string existingFileName,
            string newFileName,
            int flags
        );
    }
}
'@
    }
}
finally {
    [Environment]::CurrentDirectory = $originalProcessDirectory
    Set-Location -LiteralPath $originalPowerShellLocation
}

function Throw-EngineError {
    param(
        [Parameter(Mandatory = $true)][string]$Code,
        [Parameter(Mandatory = $true)][string]$Message
    )
    $exception = New-Object System.InvalidOperationException($Message)
    $exception.Data['EngineCode'] = $Code
    throw $exception
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $algorithm = [System.Security.Cryptography.SHA256]::Create()
        try {
            $bytes = $algorithm.ComputeHash($stream)
            return ([System.BitConverter]::ToString($bytes)).Replace('-', '')
        }
        finally {
            $algorithm.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
}

function Assert-PathInside {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Candidate
    )
    $rootFull = Get-FullPath $Root
    $candidateFull = [System.IO.Path]::GetFullPath($Candidate)
    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidateFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        Throw-EngineError 'unsafe_path' "Refusing path outside the selected game folder: $candidateFull"
    }
}

function Assert-OrdinaryFileOrMissing {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )
    Assert-PathInside $Root $Path
    if (Test-Path -LiteralPath $Path) {
        $item = Get-Item -LiteralPath $Path -Force
        if ($item.PSIsContainer) {
            Throw-EngineError 'wrapper_conflict' "Expected a file but found a directory: $($item.Name)"
        }
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            Throw-EngineError 'unsafe_path' "Refusing reparse-point file: $($item.FullName)"
        }
    }
}

function Remove-SafeTree {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $rootFull = Get-FullPath $Root
    $pathFull = Get-FullPath $Path
    if ($pathFull.Equals($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        Throw-EngineError 'unsafe_path' 'Refusing to recursively remove the selected game folder.'
    }
    Assert-PathInside $rootFull $pathFull
    if (Test-Path -LiteralPath $pathFull) {
        Remove-Item -LiteralPath $pathFull -Recurse -Force
    }
}

function Set-FileAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [string]$ExpectedHash = ''
    )
    $destinationDirectory = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    }
    $temporary = Join-Path $destinationDirectory ('.umtwp-new-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Item -LiteralPath $Source -Destination $temporary -Force
        if ($ExpectedHash) {
            $actual = Get-Sha256 $temporary
            if ($actual -ne $ExpectedHash) {
                Throw-EngineError 'verification_failed' "Staged file hash mismatch for $(Split-Path -Leaf $Destination)."
            }
        }
        $flags = 8
        if (Test-Path -LiteralPath $Destination -PathType Leaf) {
            $flags = 9
        }
        $moved = [UnofficialMedievalPatch.NativeMethods]::MoveFileEx($temporary, $Destination, $flags)
        if (-not $moved) {
            $errorNumber = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            $detail = (New-Object ComponentModel.Win32Exception($errorNumber)).Message
            throw "Atomic replacement failed for $(Split-Path -Leaf $Destination) (Win32 ${errorNumber}: $detail)."
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        }
    }
}

function Write-JsonAtomic {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $temporary = Join-Path $directory ('.umtwp-json-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        $json = $Value | ConvertTo-Json -Depth 20
        $encoding = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($temporary, $json, $encoding)
        $flags = 8
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            $flags = 9
        }
        $moved = [UnofficialMedievalPatch.NativeMethods]::MoveFileEx($temporary, $Path, $flags)
        if (-not $moved) {
            $errorNumber = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            $detail = (New-Object ComponentModel.Win32Exception($errorNumber)).Message
            throw "Could not publish transaction receipt (Win32 ${errorNumber}: $detail)."
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        }
    }
}

function Read-JsonFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    try {
        return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
    }
    catch {
        Throw-EngineError 'receipt_invalid' "Could not parse transaction metadata: $Path"
    }
}

function Assert-ConfigInvariants {
    param([Parameter(Mandatory = $true)][string]$Path)
    $content = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    $required = [ordered]@{
        'Resampling' = 'lanczos-3'
        'ScalingMode' = 'stretched_ar'
        'FullscreenAttributes' = 'fake'
        'FastVideoMemoryAccess' = 'true'
        'FPSLimit' = '0'
    }
    foreach ($key in $required.Keys) {
        $escapedKey = [Regex]::Escape($key)
        $escapedValue = [Regex]::Escape([string]$required[$key])
        if ($content -notmatch "(?mi)^\s*$escapedKey\s*=\s*$escapedValue\s*(?:;.*)?$") {
            Throw-EngineError 'verification_failed' "Required dgVoodoo setting is missing or changed: $key = $($required[$key])"
        }
    }
}

function Get-PayloadManifest {
    param([Parameter(Mandatory = $true)][string]$Directory)
    $directoryFull = Get-FullPath $Directory
    $manifestPath = Join-Path $directoryFull 'payload-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Throw-EngineError 'invalid_payload' 'The embedded payload manifest is missing.'
    }
    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        Throw-EngineError 'invalid_payload' 'The embedded payload manifest is not valid JSON.'
    }
    if ([string]$manifest.target_executable.sha256 -ne $script:SupportedExecutableHash) {
        Throw-EngineError 'invalid_payload' 'The payload targets an unexpected game executable.'
    }
    foreach ($name in $script:PayloadNames) {
        $property = $manifest.files.PSObject.Properties[$name]
        if ($null -eq $property) {
            Throw-EngineError 'invalid_payload' "The payload manifest omits $name."
        }
        $source = Join-Path $directoryFull $name
        Assert-PathInside $directoryFull $source
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            Throw-EngineError 'invalid_payload' "The embedded payload omits $name."
        }
        $record = $property.Value
        $actualLength = (Get-Item -LiteralPath $source).Length
        $actualHash = Get-Sha256 $source
        if ($actualLength -ne [Int64]$record.length -or $actualHash -ne [string]$record.sha256) {
            Throw-EngineError 'invalid_payload' "Embedded payload verification failed for $name."
        }
    }
    Assert-ConfigInvariants (Join-Path $directoryFull 'dgVoodoo.conf')
    return [PSCustomObject]@{
        Directory = $directoryFull
        Manifest = $manifest
    }
}

function Assert-TargetFolder {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        Throw-EngineError 'target_missing' 'Select the folder containing Medieval_TW.exe.'
    }
    $full = Get-FullPath $Path
    $root = [System.IO.Path]::GetPathRoot($full).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    if ($full.Equals($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        Throw-EngineError 'unsafe_path' 'A drive root cannot be used as the game folder.'
    }
    $targetItem = Get-Item -LiteralPath $full -Force
    if (($targetItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        Throw-EngineError 'unsafe_path' 'The selected game folder is a reparse point; choose the real installation folder.'
    }
    $executable = Join-Path $full 'Medieval_TW.exe'
    Assert-OrdinaryFileOrMissing $full $executable
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        Throw-EngineError 'target_missing' 'Medieval_TW.exe was not found in the selected folder.'
    }
    $hash = Get-Sha256 $executable
    if ($hash -ne $script:SupportedExecutableHash) {
        Throw-EngineError 'unsupported_executable' "This Medieval_TW.exe build is unsupported (SHA-256 $hash). No files were changed."
    }
    return [PSCustomObject]@{
        Directory = $full
        Executable = $executable
        ExecutableHash = $hash
    }
}

function Assert-GameClosed {
    $running = @(Get-Process -Name 'Medieval_TW' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        $ids = ($running | ForEach-Object { $_.Id }) -join ', '
        Throw-EngineError 'game_running' "Close Medieval: Total War before continuing (PID $ids)."
    }
}

function Assert-TargetWritableAndSpacious {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][Int64]$PayloadBytes
    )
    $probe = Join-Path $Directory ('.umtwp-write-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [System.IO.File]::WriteAllText($probe, 'write-test', (New-Object System.Text.UTF8Encoding($false)))
    }
    catch {
        Throw-EngineError 'target_not_writable' 'The selected game folder is not writable. Grant your account write access or choose a writable game installation.'
    }
    finally {
        if (Test-Path -LiteralPath $probe) {
            Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
        }
    }
    $driveRoot = [System.IO.Path]::GetPathRoot($Directory)
    $drive = New-Object System.IO.DriveInfo($driveRoot)
    $required = ($PayloadBytes * 3) + 5MB
    if ($drive.AvailableFreeSpace -lt $required) {
        Throw-EngineError 'insufficient_space' "At least $required bytes of free space are required for a transactional installation."
    }
}

function Get-PreinstallMode {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)]$PayloadManifest
    )
    $d3d8 = Join-Path $Directory 'D3D8.dll'
    Assert-OrdinaryFileOrMissing $Directory $d3d8
    if (Test-Path -LiteralPath $d3d8 -PathType Leaf) {
        Throw-EngineError 'd3d8_conflict' 'D3D8.dll is present. Remove or isolate that wrapper before installing this patch.'
    }

    $d3d9 = Join-Path $Directory 'D3D9.dll'
    Assert-OrdinaryFileOrMissing $Directory $d3d9
    $mode = 'clean'
    if (Test-Path -LiteralPath $d3d9 -PathType Leaf) {
        $d3d9Hash = Get-Sha256 $d3d9
        if (-not $script:KnownD3D9Modes.ContainsKey($d3d9Hash)) {
            Throw-EngineError 'wrapper_conflict' "D3D9.dll is not a supported stock, dust-only, R6F160, or R185 build (SHA-256 $d3d9Hash). No files were changed."
        }
        $mode = $script:KnownD3D9Modes[$d3d9Hash]
    }

    foreach ($name in $script:PayloadNames) {
        if ($name -eq 'D3D9.dll') {
            continue
        }
        $path = Join-Path $Directory $name
        Assert-OrdinaryFileOrMissing $Directory $path
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $expected = [string]$PayloadManifest.files.PSObject.Properties[$name].Value.sha256
            $actual = Get-Sha256 $path
            $knownPrevious = $script:KnownPreviousPayloadHashes.ContainsKey($name) -and
                $script:KnownPreviousPayloadHashes[$name] -contains $actual
            if ($actual -ne $expected -and -not $knownPrevious) {
                Throw-EngineError 'wrapper_conflict' "$name differs from the accepted R185 runtime (SHA-256 $actual). No files were changed."
            }
        }
    }
    return $mode
}

function Get-Receipt {
    param([Parameter(Mandatory = $true)][string]$StateDirectory)
    $receiptPath = Join-Path $StateDirectory $script:ReceiptName
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) {
        Throw-EngineError 'receipt_invalid' 'Installation metadata is missing; automatic repair or restoration is unavailable.'
    }
    $receipt = Read-JsonFile $receiptPath
    if ([string]$receipt.schema -ne 'unofficial-medieval-total-war-patch-install-v1') {
        Throw-EngineError 'receipt_invalid' 'Installation metadata uses an unsupported schema.'
    }
    foreach ($name in $script:PayloadNames) {
        if ($null -eq $receipt.files.PSObject.Properties[$name]) {
            Throw-EngineError 'receipt_invalid' "Installation metadata omits $name."
        }
    }
    return $receipt
}

function New-StagedPayload {
    param(
        [Parameter(Mandatory = $true)][string]$StateDirectory,
        [Parameter(Mandatory = $true)]$Payload
    )
    $stage = Join-Path $StateDirectory ('stage-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    foreach ($name in $script:PayloadNames) {
        $source = Join-Path $Payload.Directory $name
        $destination = Join-Path $stage $name
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $expected = [string]$Payload.Manifest.files.PSObject.Properties[$name].Value.sha256
        if ((Get-Sha256 $destination) -ne $expected) {
            Throw-EngineError 'verification_failed' "Staging verification failed for $name."
        }
    }
    return $stage
}

function Test-FinalRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)]$PayloadManifest
    )
    foreach ($name in $script:PayloadNames) {
        $path = Join-Path $Directory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Throw-EngineError 'verification_failed' "Installed runtime file is missing: $name"
        }
        $expected = [string]$PayloadManifest.files.PSObject.Properties[$name].Value.sha256
        $actual = Get-Sha256 $path
        if ($actual -ne $expected) {
            Throw-EngineError 'verification_failed' "Installed runtime verification failed for $name."
        }
    }
    Assert-ConfigInvariants (Join-Path $Directory 'dgVoodoo.conf')
}

function Install-Fresh {
    param(
        [Parameter(Mandatory = $true)]$TargetInfo,
        [Parameter(Mandatory = $true)]$Payload,
        [Parameter(Mandatory = $true)][string]$Mode
    )
    $stateDirectory = Join-Path $TargetInfo.Directory $script:ProductStateDirectoryName
    $receiptPath = Join-Path $stateDirectory $script:ReceiptName
    $originalsDirectory = Join-Path $stateDirectory 'originals'
    $fileRecords = [ordered]@{}
    $stage = $null
    $transactionStarted = $false
    $receipt = $null
    try {
        if (Test-Path -LiteralPath $stateDirectory) {
            Throw-EngineError 'receipt_invalid' "The patch state directory already exists without usable installation metadata: $stateDirectory"
        }
        New-Item -ItemType Directory -Path $originalsDirectory -Force | Out-Null
        $stage = New-StagedPayload $stateDirectory $Payload

        foreach ($name in $script:PayloadNames) {
            $destination = Join-Path $TargetInfo.Directory $name
            $existed = Test-Path -LiteralPath $destination -PathType Leaf
            $originalHash = $null
            $originalLength = $null
            $snapshotRelative = $null
            if ($existed) {
                $originalHash = Get-Sha256 $destination
                $originalLength = (Get-Item -LiteralPath $destination).Length
                $snapshotRelative = 'originals/' + $name
                Copy-Item -LiteralPath $destination -Destination (Join-Path $originalsDirectory $name) -Force
                if ((Get-Sha256 (Join-Path $originalsDirectory $name)) -ne $originalHash) {
                    Throw-EngineError 'verification_failed' "Could not verify the private rollback snapshot for $name."
                }
            }
            $installedHash = [string]$Payload.Manifest.files.PSObject.Properties[$name].Value.sha256
            $sidecar = $destination + '.unofficial-patch.bak'
            $sidecarCreated = $false
            if ($existed -and $originalHash -ne $installedHash -and -not (Test-Path -LiteralPath $sidecar)) {
                Set-FileAtomically $destination $sidecar $originalHash
                $sidecarCreated = $true
            }
            $fileRecords[$name] = [ordered]@{
                existed = [bool]$existed
                original_sha256 = $originalHash
                original_length = $originalLength
                snapshot_relative = $snapshotRelative
                installed_sha256 = $installedHash
                installed_length = [Int64]$Payload.Manifest.files.PSObject.Properties[$name].Value.length
                sidecar_relative = $(if ($existed) { $name + '.unofficial-patch.bak' } else { $null })
                sidecar_created = $sidecarCreated
            }
        }

        $installerHash = $null
        if ($InstallerPath -and (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
            $installerHash = Get-Sha256 $InstallerPath
        }
        $receipt = [ordered]@{
            schema = 'unofficial-medieval-total-war-patch-install-v1'
            status = 'installing'
            installation_id = [Guid]::NewGuid().ToString('D')
            installer_version = $InstallerVersion
            installer_sha256 = $installerHash
            installed_utc = [DateTime]::UtcNow.ToString('o')
            target_directory = $TargetInfo.Directory
            target_executable_sha256 = $TargetInfo.ExecutableHash
            preinstall_mode = $Mode
            repair_count = 0
            files = $fileRecords
            locked_settings = [ordered]@{
                Resampling = 'lanczos-3'
                ScalingMode = 'stretched_ar'
                FullscreenAttributes = 'fake'
                FastVideoMemoryAccess = 'true'
                FPSLimit = '0'
            }
        }
        Write-JsonAtomic $receipt $receiptPath
        $transactionStarted = $true

        foreach ($name in $script:PayloadNames) {
            $source = Join-Path $stage $name
            $destination = Join-Path $TargetInfo.Directory $name
            $expected = [string]$Payload.Manifest.files.PSObject.Properties[$name].Value.sha256
            if ((Test-Path -LiteralPath $destination -PathType Leaf) -and (Get-Sha256 $destination) -eq $expected) {
                continue
            }
            Set-FileAtomically $source $destination $expected
        }
        Test-FinalRuntime $TargetInfo.Directory $Payload.Manifest
        $receipt.status = 'installed'
        $receipt.completed_utc = [DateTime]::UtcNow.ToString('o')
        Write-JsonAtomic $receipt $receiptPath
        if ($stage -and (Test-Path -LiteralPath $stage)) {
            Remove-SafeTree $stateDirectory $stage
        }
        return [ordered]@{
            status = 'ok'
            action = 'install'
            preinstall_mode = $Mode
            installation_id = $receipt.installation_id
            target = $TargetInfo.Directory
        }
    }
    catch {
        $originalFailure = $_.Exception.Message
        $rollbackFailure = $null
        try {
            if ($transactionStarted -or $fileRecords.Count -gt 0) {
                $reverseNames = @($script:PayloadNames)
                [Array]::Reverse($reverseNames)
                foreach ($name in $reverseNames) {
                    if (-not $fileRecords.Contains($name)) {
                        continue
                    }
                    $record = $fileRecords[$name]
                    $destination = Join-Path $TargetInfo.Directory $name
                    if ([bool]$record.existed) {
                        $snapshot = Join-Path $stateDirectory ([string]$record.snapshot_relative)
                        if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or (Get-Sha256 $destination) -ne [string]$record.original_sha256) {
                            Set-FileAtomically $snapshot $destination ([string]$record.original_sha256)
                        }
                    }
                    elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
                        Remove-Item -LiteralPath $destination -Force
                    }
                    if ([bool]$record.sidecar_created) {
                        $sidecar = $destination + '.unofficial-patch.bak'
                        if ((Test-Path -LiteralPath $sidecar -PathType Leaf) -and (Get-Sha256 $sidecar) -eq [string]$record.original_sha256) {
                            Remove-Item -LiteralPath $sidecar -Force
                        }
                    }
                }
            }
            if (Test-Path -LiteralPath $stateDirectory) {
                Remove-SafeTree $TargetInfo.Directory $stateDirectory
            }
        }
        catch {
            $rollbackFailure = $_.Exception.Message
        }
        if ($rollbackFailure) {
            Throw-EngineError 'transaction_failed' "Installation failed: $originalFailure Rollback also failed: $rollbackFailure"
        }
        Throw-EngineError 'transaction_failed' "Installation failed and was rolled back: $originalFailure"
    }
}

function Repair-Installed {
    param(
        [Parameter(Mandatory = $true)]$TargetInfo,
        [Parameter(Mandatory = $true)]$Payload,
        [Parameter(Mandatory = $true)][string]$StateDirectory,
        [Parameter(Mandatory = $true)]$Receipt
    )
    $repairDirectory = Join-Path $StateDirectory ('repair-' + [Guid]::NewGuid().ToString('N'))
    $rollbackDirectory = Join-Path $repairDirectory 'rollback'
    $stage = $null
    $currentRecords = [ordered]@{}
    try {
        foreach ($name in $script:PayloadNames) {
            $record = $Receipt.files.PSObject.Properties[$name].Value
            $destination = Join-Path $TargetInfo.Directory $name
            $exists = Test-Path -LiteralPath $destination -PathType Leaf
            $currentHash = $null
            if ($exists) {
                $currentHash = Get-Sha256 $destination
            }
            $installedHash = [string]$record.installed_sha256
            $originalHash = [string]$record.original_sha256
            $knownPrevious = $false
            if ($exists) {
                $knownPrevious = ($name -eq 'D3D9.dll' -and $script:KnownD3D9Modes.ContainsKey($currentHash)) -or
                    ($script:KnownPreviousPayloadHashes.ContainsKey($name) -and
                        $script:KnownPreviousPayloadHashes[$name] -contains $currentHash)
            }
            if ($exists -and $currentHash -ne $installedHash -and
                (-not [bool]$record.existed -or $currentHash -ne $originalHash) -and
                -not $knownPrevious) {
                Throw-EngineError 'postinstall_modified' "$name was changed after installation. Repair made no changes."
            }
            $currentRecords[$name] = [ordered]@{ existed = $exists; sha256 = $currentHash }
        }

        New-Item -ItemType Directory -Path $rollbackDirectory -Force | Out-Null
        $stage = New-StagedPayload $repairDirectory $Payload
        foreach ($name in $script:PayloadNames) {
            $destination = Join-Path $TargetInfo.Directory $name
            if ([bool]$currentRecords[$name].existed) {
                Copy-Item -LiteralPath $destination -Destination (Join-Path $rollbackDirectory $name) -Force
            }
        }
        foreach ($name in $script:PayloadNames) {
            $expected = [string]$Payload.Manifest.files.PSObject.Properties[$name].Value.sha256
            $destination = Join-Path $TargetInfo.Directory $name
            if ((Test-Path -LiteralPath $destination -PathType Leaf) -and (Get-Sha256 $destination) -eq $expected) {
                continue
            }
            Set-FileAtomically (Join-Path $stage $name) $destination $expected
        }
        Test-FinalRuntime $TargetInfo.Directory $Payload.Manifest
        foreach ($name in $script:PayloadNames) {
            $record = $Receipt.files.PSObject.Properties[$name].Value
            $payloadRecord = $Payload.Manifest.files.PSObject.Properties[$name].Value
            $record.installed_sha256 = [string]$payloadRecord.sha256
            $record.installed_length = [Int64]$payloadRecord.length
        }
        $Receipt.installer_version = $InstallerVersion
        if ($InstallerPath -and (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
            $Receipt.installer_sha256 = Get-Sha256 $InstallerPath
        }
        $Receipt.locked_settings = [ordered]@{
            Resampling = 'lanczos-3'
            ScalingMode = 'stretched_ar'
            FullscreenAttributes = 'fake'
            FastVideoMemoryAccess = 'true'
            FPSLimit = '0'
        }
        $Receipt.repair_count = [int]$Receipt.repair_count + 1
        $Receipt | Add-Member -NotePropertyName 'last_repaired_utc' -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
        $Receipt.status = 'installed'
        Write-JsonAtomic $Receipt (Join-Path $StateDirectory $script:ReceiptName)
        Remove-SafeTree $StateDirectory $repairDirectory
        return [ordered]@{
            status = 'ok'
            action = 'repair'
            preinstall_mode = [string]$Receipt.preinstall_mode
            installation_id = [string]$Receipt.installation_id
            repair_count = [int]$Receipt.repair_count
            target = $TargetInfo.Directory
        }
    }
    catch {
        $originalFailure = $_.Exception.Message
        $rollbackFailure = $null
        try {
            $reverseNames = @($script:PayloadNames)
            [Array]::Reverse($reverseNames)
            foreach ($name in $reverseNames) {
                if (-not $currentRecords.Contains($name)) {
                    continue
                }
                $destination = Join-Path $TargetInfo.Directory $name
                $record = $currentRecords[$name]
                if ([bool]$record.existed) {
                    $rollbackFile = Join-Path $rollbackDirectory $name
                    if (Test-Path -LiteralPath $rollbackFile -PathType Leaf) {
                        if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or (Get-Sha256 $destination) -ne [string]$record.sha256) {
                            Set-FileAtomically $rollbackFile $destination ([string]$record.sha256)
                        }
                    }
                }
                elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
                    Remove-Item -LiteralPath $destination -Force
                }
            }
            if (Test-Path -LiteralPath $repairDirectory) {
                Remove-SafeTree $StateDirectory $repairDirectory
            }
        }
        catch {
            $rollbackFailure = $_.Exception.Message
        }
        $existingCode = $null
        if ($_.Exception.Data.Contains('EngineCode')) {
            $existingCode = [string]$_.Exception.Data['EngineCode']
        }
        if ($existingCode -eq 'postinstall_modified') {
            throw $_.Exception
        }
        if ($rollbackFailure) {
            Throw-EngineError 'transaction_failed' "Repair failed: $originalFailure Rollback also failed: $rollbackFailure"
        }
        Throw-EngineError 'transaction_failed' "Repair failed and was rolled back: $originalFailure"
    }
}

function Restore-Installed {
    param(
        [Parameter(Mandatory = $true)]$TargetInfo,
        [Parameter(Mandatory = $true)][string]$StateDirectory,
        [Parameter(Mandatory = $true)]$Receipt
    )
    $restoreRollback = Join-Path $StateDirectory ('restore-rollback-' + [Guid]::NewGuid().ToString('N'))
    $currentRecords = [ordered]@{}

    foreach ($name in $script:PayloadNames) {
        $record = $Receipt.files.PSObject.Properties[$name].Value
        $expectedSnapshotRelative = 'originals/' + $name
        if ([bool]$record.existed) {
            if ([string]$record.snapshot_relative -ne $expectedSnapshotRelative) {
                Throw-EngineError 'receipt_invalid' "Unsafe rollback path in metadata for $name."
            }
            $snapshot = Join-Path $StateDirectory $expectedSnapshotRelative
            Assert-PathInside $StateDirectory $snapshot
            if (-not (Test-Path -LiteralPath $snapshot -PathType Leaf) -or (Get-Sha256 $snapshot) -ne [string]$record.original_sha256) {
                Throw-EngineError 'receipt_invalid' "The private rollback snapshot for $name is missing or damaged."
            }
        }
        $destination = Join-Path $TargetInfo.Directory $name
        $exists = Test-Path -LiteralPath $destination -PathType Leaf
        $currentHash = $null
        if ($exists) {
            $currentHash = Get-Sha256 $destination
        }
        $safe = $false
        if ($exists -and $currentHash -eq [string]$record.installed_sha256) {
            $safe = $true
        }
        elseif ([bool]$record.existed -and $exists -and $currentHash -eq [string]$record.original_sha256) {
            $safe = $true
        }
        elseif (-not [bool]$record.existed -and -not $exists) {
            $safe = $true
        }
        if (-not $safe) {
            Throw-EngineError 'postinstall_modified' "$name was changed after installation. Automatic restore made no changes."
        }
        $currentRecords[$name] = [ordered]@{ existed = $exists; sha256 = $currentHash }
    }

    try {
        New-Item -ItemType Directory -Path $restoreRollback -Force | Out-Null
        foreach ($name in $script:PayloadNames) {
            $destination = Join-Path $TargetInfo.Directory $name
            if (Test-Path -LiteralPath $destination -PathType Leaf) {
                Copy-Item -LiteralPath $destination -Destination (Join-Path $restoreRollback $name) -Force
            }
        }

        $reverseNames = @($script:PayloadNames)
        [Array]::Reverse($reverseNames)
        foreach ($name in $reverseNames) {
            $record = $Receipt.files.PSObject.Properties[$name].Value
            $destination = Join-Path $TargetInfo.Directory $name
            if ([bool]$record.existed) {
                if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or (Get-Sha256 $destination) -ne [string]$record.original_sha256) {
                    Set-FileAtomically (Join-Path $StateDirectory ([string]$record.snapshot_relative)) $destination ([string]$record.original_sha256)
                }
            }
            elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
                Remove-Item -LiteralPath $destination -Force
            }
        }

        foreach ($name in $script:PayloadNames) {
            $record = $Receipt.files.PSObject.Properties[$name].Value
            $destination = Join-Path $TargetInfo.Directory $name
            if ([bool]$record.existed) {
                if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or (Get-Sha256 $destination) -ne [string]$record.original_sha256) {
                    throw "Restore verification failed for $name."
                }
            }
            elseif (Test-Path -LiteralPath $destination) {
                throw "Restore verification failed because installer-created $name still exists."
            }
        }

        foreach ($name in $script:PayloadNames) {
            $record = $Receipt.files.PSObject.Properties[$name].Value
            if ([bool]$record.sidecar_created) {
                $sidecar = Join-Path $TargetInfo.Directory ($name + '.unofficial-patch.bak')
                if ((Test-Path -LiteralPath $sidecar -PathType Leaf) -and (Get-Sha256 $sidecar) -eq [string]$record.original_sha256) {
                    Remove-Item -LiteralPath $sidecar -Force
                }
            }
        }
        Remove-SafeTree $TargetInfo.Directory $StateDirectory
        return [ordered]@{
            status = 'ok'
            action = 'restore'
            restored_preinstall_mode = [string]$Receipt.preinstall_mode
            target = $TargetInfo.Directory
        }
    }
    catch {
        $originalFailure = $_.Exception.Message
        $rollbackFailure = $null
        try {
            foreach ($name in $script:PayloadNames) {
                $destination = Join-Path $TargetInfo.Directory $name
                $current = $currentRecords[$name]
                if ([bool]$current.existed) {
                    $rollbackFile = Join-Path $restoreRollback $name
                    if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or (Get-Sha256 $destination) -ne [string]$current.sha256) {
                        Set-FileAtomically $rollbackFile $destination ([string]$current.sha256)
                    }
                }
                elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
                    Remove-Item -LiteralPath $destination -Force
                }
            }
            if (Test-Path -LiteralPath $restoreRollback) {
                Remove-SafeTree $StateDirectory $restoreRollback
            }
        }
        catch {
            $rollbackFailure = $_.Exception.Message
        }
        if ($rollbackFailure) {
            Throw-EngineError 'transaction_failed' "Restore failed: $originalFailure Rollback also failed: $rollbackFailure"
        }
        Throw-EngineError 'transaction_failed' "Restore failed and was rolled back: $originalFailure"
    }
}

try {
    $targetInfo = Assert-TargetFolder $Target
    $payload = Get-PayloadManifest $PayloadDirectory
    $stateDirectory = Join-Path $targetInfo.Directory $script:ProductStateDirectoryName
    $receiptPath = Join-Path $stateDirectory $script:ReceiptName
    $hasManagedReceipt = Test-Path -LiteralPath $receiptPath -PathType Leaf
    $managedReceipt = $null
    if (($Operation -eq 'Install' -or $Operation -eq 'Restore') -and $hasManagedReceipt) {
        $managedReceipt = Get-Receipt $stateDirectory
        $mode = [string]$managedReceipt.preinstall_mode
    }
    else {
        $mode = Get-PreinstallMode $targetInfo.Directory $payload.Manifest
    }

    if ($Operation -eq 'Inspect') {
        $managed = $hasManagedReceipt
        $result = [ordered]@{
            status = 'ok'
            action = 'inspect'
            mode = $mode
            managed_installation = [bool]$managed
            target = $targetInfo.Directory
            target_executable_sha256 = $targetInfo.ExecutableHash
        }
    }
    elseif ($Operation -eq 'Verify') {
        Test-FinalRuntime $targetInfo.Directory $payload.Manifest
        $result = [ordered]@{
            status = 'ok'
            action = 'verify'
            mode = 'r185'
            target = $targetInfo.Directory
            target_executable_sha256 = $targetInfo.ExecutableHash
        }
    }
    elseif ($Operation -eq 'Install') {
        Assert-GameClosed
        $payloadBytes = 0L
        foreach ($name in $script:PayloadNames) {
            $payloadBytes += (Get-Item -LiteralPath (Join-Path $payload.Directory $name)).Length
        }
        Assert-TargetWritableAndSpacious $targetInfo.Directory $payloadBytes
        if ($hasManagedReceipt) {
            $receipt = $managedReceipt
            $result = Repair-Installed $targetInfo $payload $stateDirectory $receipt
        }
        else {
            $result = Install-Fresh $targetInfo $payload $mode
        }
    }
    else {
        Assert-GameClosed
        if (-not (Test-Path -LiteralPath $stateDirectory -PathType Container)) {
            Throw-EngineError 'receipt_invalid' 'No managed installation was found to restore.'
        }
        $receipt = $managedReceipt
        $result = Restore-Installed $targetInfo $stateDirectory $receipt
    }

    if ($OutputMode -eq 'Human') {
        if ([string]$result.action -eq 'inspect') {
            [Console]::Out.WriteLine("Supported Medieval: Total War folder. Detected state: $($result.mode).")
        }
        elseif ([string]$result.action -eq 'install') {
            [Console]::Out.WriteLine("Installed and verified the Terrain Movement Fix. Previous state: $($result.preinstall_mode).")
        }
        elseif ([string]$result.action -eq 'repair') {
            [Console]::Out.WriteLine("Repaired and verified the existing installation. Repair count: $($result.repair_count).")
        }
        elseif ([string]$result.action -eq 'restore') {
            [Console]::Out.WriteLine("Restored and verified the complete pre-install state: $($result.restored_preinstall_mode).")
        }
        else {
            [Console]::Out.WriteLine('The R185 runtime and required settings are verified.')
        }
    }
    else {
        [Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 12 -Compress))
    }
    exit 0
}
catch {
    $code = 'unexpected_error'
    if ($_.Exception.Data.Contains('EngineCode')) {
        $code = [string]$_.Exception.Data['EngineCode']
    }
    $failure = [ordered]@{
        status = 'error'
        code = $code
        operation = $Operation.ToLowerInvariant()
        message = $_.Exception.Message
    }
    if ($OutputMode -eq 'Human') {
        if ($code -eq 'unexpected_error') {
            [Console]::Out.WriteLine('The compatibility check failed unexpectedly. No game files were changed. Error code: unexpected_error.')
        }
        else {
            [Console]::Out.WriteLine($_.Exception.Message)
        }
    }
    else {
        [Console]::Out.WriteLine(($failure | ConvertTo-Json -Depth 12 -Compress))
    }
    exit 1
}
