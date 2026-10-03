function Resolve-SevenZip {
    if ($SevenZipPath) {
        if (Test-Path -LiteralPath $SevenZipPath -PathType Leaf) { return [IO.Path]::GetFullPath($SevenZipPath) }
        throw 'The specified 7-Zip executable does not exist.'
    }
    if ($env:SEVENZIP_EXE -and (Test-Path -LiteralPath $env:SEVENZIP_EXE -PathType Leaf)) { return $env:SEVENZIP_EXE }
    $command = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw '7-Zip was not found. Supply -SevenZipPath or set SEVENZIP_EXE.'
}

function Resolve-NativeCompiler {
    if ($GccPath) {
        if (-not (Test-Path -LiteralPath $GccPath -PathType Leaf)) { throw 'The specified C compiler does not exist.' }
        return [IO.Path]::GetFullPath($GccPath)
    }
    if ($env:MTW_CC -and (Test-Path -LiteralPath $env:MTW_CC -PathType Leaf)) { return [IO.Path]::GetFullPath($env:MTW_CC) }
    foreach ($name in @('i686-w64-mingw32-gcc.exe','gcc.exe')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }
    throw 'Supply -GccPath or MTW_CC with an i686 MinGW-w64 GCC C99 compiler.'
}

function Invoke-LoggedCommand {
    param([string]$Executable,[string[]]$Arguments,[string]$LogName)
    $commandEnvironment = [ordered]@{}
    foreach ($name in @('MTW_CC','MTW_TEST_NATIVE_HELPER','MTW_TEST_NATIVE_UNSTRIPPED','MTW_TEST_UNINSTALLER',
                       'MTW_TEST_INSTALLER','MTW_TEST_C_BACKEND','MTW_RUN_LIFECYCLE_FAULTS',
                       'MTW_RUN_LEGACY_MIGRATION_TESTS','MTW_RUN_COMPILED_INSTALLER_TESTS',
                       'MTW_C_HISTORICAL_BULK','PSModulePath','TEMP','TMP','PYTHONDONTWRITEBYTECODE')) {
        $commandEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
    }
    [ordered]@{command=(@($Executable) + $Arguments);cwd=(Get-Location).Path;environment=$commandEnvironment} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ArtifactDirectory ($LogName + '.command.json')) -Encoding UTF8
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $ArtifactDirectory $LogName) | Out-Host
        $code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    if ($code -ne 0) { throw "$LogName failed with exit code $code." }
}

function Invoke-PytestStage {
    param([string]$Name,[string[]]$Modules)
    # Keep disposable paths short enough for Windows PowerShell 5.1 APIs.
    $stagePrefixes = @{project_contracts='p';native_guard='n';lifecycle='l';compiled_installer='c';legacy_migration='v';
        legacy_cpp='x';legacy_v2='w';historical_state='h';release_hygiene='r';final_manifest='f'}
    if (-not $stagePrefixes.ContainsKey($Name)) { throw "Unknown pytest stage: $Name" }
    $baseTemp = Join-Path $TestDirectory ($stagePrefixes[$Name] + '-' + $runId.Substring($runId.Length - 8))
    if (Test-Path -LiteralPath $baseTemp) { throw "Refusing to reuse pytest temporary directory: $baseTemp" }
    $report = Join-Path $ArtifactDirectory ($Name + '.junit.xml')
    $arguments = @('-m','pytest') + $Modules + @('-q','-p','no:cacheprovider','--basetemp',$baseTemp,'--junitxml',$report)
    # Python does not apply PowerShell 7's native-launch module-path filtering.
    # Scope Windows PowerShell modules to this test child so legacy PS5.1 fixture
    # processes can autoload their own modules. Restore the caller even on failure.
    $previousModulePath = $env:PSModulePath
    try {
        $env:PSModulePath = (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\Modules') + ';' +
            (Join-Path $env:ProgramFiles 'WindowsPowerShell\Modules')
        Invoke-LoggedCommand $python $arguments ($Name + '.log')
    }
    finally { $env:PSModulePath = $previousModulePath }
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) { throw "Missing pytest report: $Name" }
    return $report
}

function Get-ToolIdentity {
    param([string]$Role,[string]$Path,[string]$Version = '')
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Tool is missing: $Role" }
    if (-not $Version) { $Version = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path).FileVersion }
    return [ordered]@{
        role=$Role; filename=[IO.Path]::GetFileName($Path); version=$Version
        sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        length=(Get-Item -LiteralPath $Path).Length
    }
}

function Write-Checksums {
    $lines = foreach ($name in @([string]$product.output_filename,'RELEASE_MANIFEST.json','INSTALLER_AUDIT.json','LICENSE.txt')) {
        $path = Join-Path $OutputDirectory $name
        "$((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)  $name"
    }
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'),$lines,(New-Object Text.UTF8Encoding($false)))
}

function Get-BuildFileRecord {
    param([string]$Path)
    return [ordered]@{path=[IO.Path]::GetFullPath($Path);length=(Get-Item -LiteralPath $Path -ErrorAction Stop).Length;
        sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash}
}

function Write-BuildJson {
    param([string]$Path,$Value)
    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $temporary = Join-Path $directory ([Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::WriteAllText($temporary,($Value | ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))
        # PowerShell 5.1 converts $null to an empty path for this .NET overload.
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary,$Path,[NullString]::Value) }
        else { [IO.File]::Move($temporary,$Path) }
    }
    finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
}

function Assert-BuildFileRecord {
    param($Record)
    if (-not $Record.path -or -not [IO.Path]::IsPathRooted($Record.path) -or
        -not (Test-Path -LiteralPath $Record.path -PathType Leaf) -or
        (Get-Item -LiteralPath $Record.path).Length -ne $Record.length -or
        (Get-FileHash -LiteralPath $Record.path -Algorithm SHA256).Hash -ne $Record.sha256) {
        throw "Build input changed or is missing: $($Record.path). Build again before testing."
    }
}

function Read-BuildRecord {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'Build record is missing. Run build.ps1 first or supply -BuildReport.' }
    $record = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($record.schema -ne 'unofficial-medieval-total-war-patch-build-v2' -or $record.result -ne 'pass') {
        throw 'A successful current build record is required. Run build.ps1 first.'
    }
    foreach ($name in @('installer','nativeHelper','nativeUnstripped','uninstaller','sourceSnapshot',
                        'toolReport','nativeBuildReport','audit','releaseManifest')) {
        if (-not $record.inputs.PSObject.Properties[$name]) { throw "Build record input is missing: $name" }
    }
    foreach ($name in @('project_contracts','native_guard','release_hygiene')) {
        if (-not $record.reports.PSObject.Properties[$name]) { throw "Build test evidence is missing: $name" }
    }
    foreach ($group in @($record.inputs,$record.reports)) {
        foreach ($entry in $group.PSObject.Properties) { Assert-BuildFileRecord $entry.Value }
    }
    return $record
}

function Get-GameTestSelection {
    $cpp = @('tests/test_c_backend_lifecycle.py::test_c_and_frozen_cpp_independent_install_repair_and_removal_match',
        'tests/test_c_backend_lifecycle.py::test_c_recovers_real_frozen_cpp_interruption')
    $v2 = @('tests/test_native_compatibility.py::test_actual_powershell_v2_receipt_upgrades_and_restores_earliest_original',
        'tests/test_native_compatibility.py::test_native_recovers_actual_powershell_v2_journal')
    $state = 'tests/test_c_backend_state.py::test_c_genuine_saved_cpp_journals_and_receipts'
    $lifecycle = @('tests/test_install_engine.py','tests/test_lifecycle.py','tests/test_lifecycle_adversarial.py',
        'tests/test_lifecycle_permissions.py','tests/test_registry_isolation.py',
        'tests/test_native_compatibility.py','tests/test_c_backend_lifecycle.py')
    foreach ($node in @($cpp + $v2)) { $lifecycle += '--deselect=' + $node }
    return @{lifecycle=$lifecycle;legacy_cpp=$cpp;legacy_v2=$v2;historical_state=@($state);
        native_guard=@('tests/test_lifecycle_native.py','tests/test_c_backend_platform.py',
                       'tests/test_c_backend_state.py',('--deselect=' + $state))}
}

function Publish-ValidationFiles {
    param([string]$CandidateDirectory,[string]$DistributionDirectory)
    $entries = @()
    $completed = @()
    $keepBackups = $false
    try {
        foreach ($name in @('RELEASE_MANIFEST.json','SHA256SUMS.txt')) {
            $destination = Join-Path $DistributionDirectory $name
            if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw "Missing distribution metadata: $name" }
            $temporary = Join-Path $DistributionDirectory ('.validation-' + [Guid]::NewGuid().ToString('N'))
            $entry = @{destination=$destination;temporary=$temporary;backup=($temporary + '.bak')}
            $entries += $entry
            [IO.File]::Copy((Join-Path $CandidateDirectory $name),$temporary,$false)
        }
        foreach ($entry in $entries) {
            [IO.File]::Replace($entry.temporary,$entry.destination,$entry.backup)
            $completed += $entry
        }
    }
    catch {
        $failure = $_.Exception.Message
        for ($index = $completed.Count - 1; $index -ge 0; $index--) {
            $entry = $completed[$index]
            try { [IO.File]::Replace($entry.backup,$entry.destination,[NullString]::Value) }
            catch { $keepBackups = $true; $failure += " Recovery copy retained at $($entry.backup): $($_.Exception.Message)" }
        }
        throw "Validation metadata publication failed after $($completed.Count) replacement(s): $failure"
    }
    finally {
        foreach ($entry in $entries) {
            if (Test-Path -LiteralPath $entry.temporary) { Remove-Item -LiteralPath $entry.temporary -ErrorAction SilentlyContinue }
            if (-not $keepBackups -and (Test-Path -LiteralPath $entry.backup)) {
                Remove-Item -LiteralPath $entry.backup -ErrorAction SilentlyContinue
            }
        }
    }
}
