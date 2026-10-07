<#
.SYNOPSIS
Test the last built installer using disposable copies of a supported game executable.
.EXAMPLE
.\test.ps1 -SupportedGameExecutable 'C:\Games\Medieval - Total War\Medieval_TW.exe'
.NOTES
-BuildReport selects an explicit BUILD_RESULT.json instead of the latest successful build.
Lifecycle fault tests are always enabled. Historical package tests additionally use
MTW_RUN_LEGACY_MIGRATION_TESTS=1, MTW_LEGACY_PACKAGE_PROOF and MTW_PRIMARY_LEGACY_FIXTURE.
MTW_LEGACY_CPP_HELPER, MTW_LEGACY_V2_SOURCE and MTW_C_HISTORICAL_BULK enable preserved development fixtures.
These are test inputs only. No game installation is discovered or modified in place.
#>
[CmdletBinding()]
param(
    [string]$SupportedGameExecutable = '',
    [string]$BuildReport = '',
    [string]$PythonPath = '',
    [string]$GccPath = '',
    [string]$SevenZipPath = '',
    [string]$ArtifactDirectory = '',
    [string]$TestDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
. (Join-Path $root 'tools\build-support.ps1')
if (-not $SupportedGameExecutable) { throw 'Supply -SupportedGameExecutable with the game executable to copy for testing.' }
$SupportedGameExecutable = [IO.Path]::GetFullPath($SupportedGameExecutable)
$payload = Get-Content -LiteralPath (Join-Path $root 'vendor\runtime\payload-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not (Test-Path -LiteralPath $SupportedGameExecutable -PathType Leaf)) { throw 'Supported game executable was not found.' }
if ((Get-FileHash -LiteralPath $SupportedGameExecutable -Algorithm SHA256).Hash -ne $payload.target_executable.sha256) {
    throw 'The supplied executable does not match the supported game identity.'
}
if (-not $BuildReport) {
    $latest = Join-Path $root 'artifacts\latest-build.json'
    if (-not (Test-Path -LiteralPath $latest -PathType Leaf)) { throw 'Run build.ps1 first or supply -BuildReport.' }
    $pointer = Get-Content -LiteralPath $latest -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-BuildFileRecord $pointer
    $BuildReport = $pointer.path
}
$record = Read-BuildRecord $BuildReport
$product = Get-Content -LiteralPath (Join-Path $root 'config\product.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$installer = $record.inputs.installer.path
$distributionDirectory = Split-Path -Parent $installer
$sourceSnapshot = $record.inputs.sourceSnapshot.path
$nativeHelper = $record.inputs.nativeHelper.path
$manifestScript = Join-Path $root 'tools\build-release-manifest.py'
if (-not $PythonPath) { $PythonPath = (Get-Command python.exe -ErrorAction Stop).Source }
$python = [IO.Path]::GetFullPath($PythonPath)
$gcc = Resolve-NativeCompiler
$sevenZip = Resolve-SevenZip
$compilerBin = Split-Path -Parent $gcc
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $root ('artifacts\tests\' + $runId) }
if (-not $TestDirectory) { $TestDirectory = Join-Path $ArtifactDirectory 'fixtures' }
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$TestDirectory = [IO.Path]::GetFullPath($TestDirectory)
if (Test-Path -LiteralPath $ArtifactDirectory) { throw 'Choose a new -ArtifactDirectory for each test run.' }
New-Item -ItemType Directory -Path $ArtifactDirectory,$TestDirectory -Force | Out-Null
$tempDirectory = Join-Path $ArtifactDirectory 'temp'
New-Item -ItemType Directory -Path $tempDirectory -ErrorAction Stop | Out-Null
$OutputDirectory = Join-Path $ArtifactDirectory 'candidate'
New-Item -ItemType Directory -Path $OutputDirectory -ErrorAction Stop | Out-Null
$reports = [ordered]@{}
$result = [ordered]@{schema='unofficial-medieval-game-tests-v1';result='incomplete';
    started_utc=[DateTime]::UtcNow.ToString('o');installer_sha256=$record.inputs.installer.sha256}
$environmentNames = @('MTW_TEST_GAME_EXE','MTW_STOCK_EXE','MTW_TEST_INSTALLER','MTW_RUN_COMPILED_INSTALLER_TESTS',
    'MTW_RUN_LIFECYCLE_FAULTS','MTW_ENABLE_LIFECYCLE_FAULTS','MTW_RELEASE_DIRECTORY','MTW_TEST_REPORT_DIRECTORY',
    'MTW_TEST_NATIVE_HELPER','MTW_TEST_NATIVE_UNSTRIPPED','MTW_TEST_UNINSTALLER','MTW_TEST_C_BACKEND',
    'MTW_CC','SEVENZIP_EXE','PYTHONDONTWRITEBYTECODE','TEMP','TMP','PATH')
$priorEnvironment = @{}
foreach ($name in $environmentNames) { $priorEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
Push-Location $root
try {
    $env:MTW_TEST_GAME_EXE = $SupportedGameExecutable
    $env:MTW_STOCK_EXE = $SupportedGameExecutable
    $env:MTW_TEST_INSTALLER = $installer
    $env:MTW_RUN_COMPILED_INSTALLER_TESTS = '1'
    $env:MTW_RUN_LIFECYCLE_FAULTS = '1'
    $env:MTW_ENABLE_LIFECYCLE_FAULTS = '0'
    $env:MTW_RELEASE_DIRECTORY = $OutputDirectory
    $env:MTW_TEST_REPORT_DIRECTORY = $ArtifactDirectory
    $env:MTW_TEST_NATIVE_HELPER = $nativeHelper
    $env:MTW_TEST_NATIVE_UNSTRIPPED = $record.inputs.nativeUnstripped.path
    $env:MTW_TEST_UNINSTALLER = $record.inputs.uninstaller.path
    $env:MTW_TEST_C_BACKEND = '1'
    $env:MTW_CC = $gcc
    $env:SEVENZIP_EXE = $sevenZip
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $env:TEMP = $tempDirectory
    $env:TMP = $tempDirectory
    $env:PATH = $compilerBin + ';' + $env:PATH
    Invoke-LoggedCommand $python @($manifestScript,'--verify-source-snapshot',$sourceSnapshot) 'source-before-tests.log'
    foreach ($name in @([string]$product.output_filename,'LICENSE.txt','INSTALLER_AUDIT.json')) {
        Copy-Item -LiteralPath (Join-Path $distributionDirectory $name) -Destination (Join-Path $OutputDirectory $name) -ErrorAction Stop
    }
    Write-Output "Testing existing installer: $installer"
    Write-Output "SHA-256: $($record.inputs.installer.sha256)"
    foreach ($entry in $record.reports.PSObject.Properties) {
        $destination = Join-Path $ArtifactDirectory ([IO.Path]::GetFileName($entry.Value.path))
        Copy-Item -LiteralPath $entry.Value.path -Destination $destination -ErrorAction Stop
        $reports[$entry.Name] = $destination
    }
    $selection = Get-GameTestSelection
    $reports['sprite_exe'] = Invoke-PytestStage 'sprite_exe' @('tests/test_sprite_exe_patch.py')
    [xml]$spriteSuite = Get-Content -LiteralPath $reports['sprite_exe'] -Raw -Encoding UTF8
    $spriteCounts = $spriteSuite.testsuites.testsuite
    # pytest counts unittest subtests in the suite total, while JUnit emits one
    # testcase element per method. Require all four methods and no failed subtests.
    if (@($spriteCounts.testcase).Count -ne 4 -or [int]$spriteCounts.tests -lt 4 -or
        [int]$spriteCounts.skipped -ne 0 -or
        [int]$spriteCounts.failures -ne 0 -or [int]$spriteCounts.errors -ne 0) {
        throw 'The four exact-binary Sprite EXE tests must run without skips or failures.'
    }
    $reports['lifecycle'] = Invoke-PytestStage 'lifecycle' $selection.lifecycle
    foreach ($entry in @{legacy_cpp='MTW_LEGACY_CPP_HELPER';legacy_v2='MTW_LEGACY_V2_SOURCE';
                         historical_state='MTW_C_HISTORICAL_BULK'}.GetEnumerator()) {
        if ([Environment]::GetEnvironmentVariable($entry.Value,'Process')) {
            $reports[$entry.Key] = Invoke-PytestStage $entry.Key $selection[$entry.Key]
        }
    }
    $reports['compiled_installer'] = Invoke-PytestStage 'compiled_installer' @('tests/test_compiled_installer.py')
    if ($env:MTW_RUN_LEGACY_MIGRATION_TESTS -eq '1') {
        $reports['legacy_migration'] = Invoke-PytestStage 'legacy_migration' @('tests/test_legacy_migration.py')
    }
    # Recheck immutable build inputs before attaching fresh test evidence.
    $null = Read-BuildRecord $BuildReport
    $audit = Join-Path $OutputDirectory 'INSTALLER_AUDIT.json'
    if ((Get-FileHash -LiteralPath $audit -Algorithm SHA256).Hash -ne $record.inputs.audit.sha256) {
        throw 'The distributed installer audit changed since the build.'
    }
    $manifestArguments = @($manifestScript,'--installer',$installer,
        '--output',(Join-Path $OutputDirectory 'RELEASE_MANIFEST.json'),
        '--source-snapshot',$sourceSnapshot,'--tools',$record.inputs.toolReport.path,
        '--audit',$audit,'--native-build',$record.inputs.nativeBuildReport.path,'--lifecycle-fault-tests')
    if ($record.inputs.PSObject.Properties['r185Evidence']) {
        $manifestArguments += @('--r185-build-manifest',$record.inputs.r185Evidence.path)
    }
    foreach ($entry in $reports.GetEnumerator()) { $manifestArguments += @('--test-result',($entry.Key + '=' + $entry.Value)) }
    Invoke-LoggedCommand $python $manifestArguments 'game-manifest.log'
    Write-Checksums
    # This checks the final combined evidence without overwriting the copied build report.
    $null = Invoke-PytestStage 'final_manifest' @('tests/test_release_hygiene.py')
    Invoke-LoggedCommand $python @($manifestScript,'--verify-source-snapshot',$sourceSnapshot) 'source-after-tests.log'
    $manifest = Get-Content -LiteralPath (Join-Path $OutputDirectory 'RELEASE_MANIFEST.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $result['game_tests'] = $manifest.validation.game_tests
    if ($manifest.validation.game_tests.status -ne 'pass') { throw $manifest.validation.game_tests.reason }
    $null = Read-BuildRecord $BuildReport
    Publish-ValidationFiles $OutputDirectory $distributionDirectory
    $result['result'] = 'pass'
    $result['reports'] = $reports
    Write-Output 'Game tests passed. The installer was not rebuilt.'
    Write-Output "Test reports: $ArtifactDirectory"
}
catch {
    $result['result'] = 'fail'
    $result['error'] = $_.Exception.Message
    throw
}
finally {
    $result['completed_utc'] = [DateTime]::UtcNow.ToString('o')
    try { Write-BuildJson (Join-Path $ArtifactDirectory 'TEST_RESULT.json') $result }
    finally {
        foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$priorEnvironment[$name],'Process') }
        Pop-Location
    }
}
