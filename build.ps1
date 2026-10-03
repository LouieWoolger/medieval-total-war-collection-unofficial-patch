<#
.SYNOPSIS
Build the native x86 installer and run checks that need no game files.
.EXAMPLE
.\build.ps1
.NOTES
The GCC C99 compiler must target i686-w64-mingw32. Use -GccPath and
-WindresPath for tools outside PATH. External output directories are supported.
Run test.ps1 afterwards for installation and removal tests on disposable game copies.
#>
[CmdletBinding()]
param(
    [string]$NsisDirectory = '',
    [string]$SevenZipPath = '',
    [string]$PythonPath = '',
    [string]$GccPath = '',
    [string]$WindresPath = '',
    [string]$OutputDirectory = '',
    [string]$BuildDirectory = '',
    [string]$ArtifactDirectory = '',
    [string]$TestDirectory = '',
    [switch]$SkipR185Build
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
. (Join-Path $root 'tools\build-support.ps1')
$product = Get-Content -LiteralPath (Join-Path $root 'config\product.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'dist' }
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $root ('artifacts\build\' + $runId) }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $ArtifactDirectory 'build' }
if (-not $TestDirectory) { $TestDirectory = Join-Path $ArtifactDirectory 'fixtures' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$TestDirectory = [IO.Path]::GetFullPath($TestDirectory)
if ([IO.Path]::GetFileName([string]$product.output_filename) -cne $product.output_filename) {
    throw 'The product output filename must be a filename, not a path.'
}
if (Test-Path -LiteralPath (Join-Path $ArtifactDirectory 'source-inputs.json')) {
    throw 'This artifact directory already contains a build. Choose a fresh -ArtifactDirectory.'
}
if (-not $PythonPath) { $PythonPath = (Get-Command python.exe -ErrorAction Stop).Source }
$python = [IO.Path]::GetFullPath($PythonPath)

function Resolve-MakeNsis {
    if ($NsisDirectory) {
        $candidate = Join-Path $NsisDirectory 'makensis.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
        throw 'The specified NSIS directory does not contain makensis.exe.'
    }
    if ($env:NSIS_HOME) {
        $candidate = Join-Path $env:NSIS_HOME 'makensis.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'NSIS 3.x was not found. Supply -NsisDirectory or set NSIS_HOME.'
}

function Build-NativeHelper {
    $triple = [string](& $gcc -dumpmachine)
    if ($LASTEXITCODE -ne 0 -or $triple.Trim() -ne 'i686-w64-mingw32') { throw "Expected i686-w64-mingw32, got '$triple'." }
    $compilerVersion = [string](& $gcc -dumpfullversion -dumpversion)
    if ($LASTEXITCODE -ne 0) { throw 'Native compiler version probe failed.' }
    $compilerFlags = @('-std=c99','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0501','-Os','-Wall','-Wextra','-Werror',
        '-municode','-static','-static-libgcc','-Wl,--subsystem,console:5.1')
    $prefix = @()
    $libexec = Join-Path (Split-Path -Parent $compilerBin) ('libexec\gcc\' + $triple.Trim() + '\' + $compilerVersion.Trim())
    if (Test-Path -LiteralPath (Join-Path $libexec 'cc1.exe') -PathType Leaf) { $prefix = @('-B',($libexec + '\')) }
    $resourceObject = Join-Path $BuildDirectory 'medieval_fix_patcher_res.o'
    $unstrippedHelper = Join-Path $BuildDirectory 'medieval_fix_patcher.unstripped.exe'
    Push-Location (Join-Path $root 'src')
    try {
        # Give windres the exact selected compiler as preprocessor, including its
        # relocatable cc1 prefix. Both resources and code come from this tree.
        $resourceArguments = @('--target=pe-i386',('--preprocessor=' + $gcc), '--preprocessor-arg=-E',
            '--preprocessor-arg=-xc','--preprocessor-arg=-DRC_INVOKED')
        foreach ($flag in $prefix) { $resourceArguments += ('--preprocessor-arg=' + $flag) }
        $resourceArguments += @('medieval_fix_patcher.rc','-O','coff','-o',$resourceObject)
        Invoke-LoggedCommand $windres $resourceArguments 'native-resources.log'
        $arguments = $prefix + $compilerFlags + @(('-ffile-prefix-map=' + $root + '=.'), ('-fmacro-prefix-map=' + $root + '=.'),
            'medieval_fix_patcher.c',$resourceObject,'-ladvapi32','-lrpcrt4','-lversion','-o',$unstrippedHelper)
        Invoke-LoggedCommand $gcc $arguments 'native-build.log'
    }
    finally { Pop-Location }
    $nm = Join-Path $compilerBin 'i686-w64-mingw32-nm.exe'
    $strip = Join-Path $compilerBin 'i686-w64-mingw32-strip.exe'
    if (-not (Test-Path -LiteralPath $nm -PathType Leaf)) { $nm = Join-Path $compilerBin 'nm.exe' }
    if (-not (Test-Path -LiteralPath $strip -PathType Leaf)) { $strip = Join-Path $compilerBin 'strip.exe' }
    $symbols = @(& $nm -C $unstrippedHelper)
    if ($LASTEXITCODE -ne 0) { throw 'Native symbol inspection failed.' }
    $symbols | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'native-symbols.txt') -Encoding UTF8
    if (($symbols -join "`n") -match '__cxa_|__gxx_|_Unwind_|std::|operator new|operator delete') {
        throw 'The installer helper unexpectedly links a C++ runtime.'
    }
    Invoke-LoggedCommand $strip @('-p','-s','-o',$nativeHelper,$unstrippedHelper) 'native-strip.log'
    Invoke-LoggedCommand $python @($manifestScript,'--audit-native-helper',$nativeHelper,'--output',$nativePeReport) 'native-pe-audit.log'
    $nativeSources = [ordered]@{}
    foreach ($path in @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -File | Where-Object { $_.Extension -in @('.c','.h','.rc','.manifest') } | Sort-Object Name)) {
        $nativeSources['src/' + $path.Name] = [ordered]@{sha256=(Get-FileHash -LiteralPath $path.FullName -Algorithm SHA256).Hash;length=$path.Length}
    }
    $nativeToolchain = @()
    foreach ($name in @('cc1.exe','as.exe','ld.exe')) {
        $tool = if ($name -like 'cc1*') { Join-Path $libexec $name } else { Join-Path $compilerBin $name }
        if (Test-Path -LiteralPath $tool -PathType Leaf) { $nativeToolchain += Get-ToolIdentity ('native-' + $name) $tool }
    }
    $nativeLibraries = @()
    foreach ($name in @('libgcc.a','libgcc_eh.a','libmingw32.a','libmingwex.a','libmsvcrt.a')) {
        $path = [string](& $gcc @prefix ('-print-file-name=' + $name))
        if ($LASTEXITCODE -ne 0) { throw "Static native library probe failed: $name" }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            if ($name -eq 'libgcc_eh.a') { continue }
            throw "Missing static native library: $name"
        }
        $nativeLibraries += Get-ToolIdentity 'static-library' $path
    }
    [ordered]@{
        schema='unofficial-medieval-native-build-v1';language='C99';target=$triple.Trim();compiler_version=$compilerVersion.Trim()
        compiler=(Get-ToolIdentity 'native-gcc' $gcc $compilerVersion.Trim())
        resource_compiler=(Get-ToolIdentity 'native-windres' $windres)
        toolchain=($nativeToolchain + @((Get-ToolIdentity 'native-nm' $nm),(Get-ToolIdentity 'native-strip' $strip)))
        static_libraries=$nativeLibraries;strip_flags=@('-p','-s');cplusplus_symbols_absent=$true
        flags=$compilerFlags;libraries=@('advapi32','rpcrt4','version');source_inputs=$nativeSources
        helper=(Get-Content -LiteralPath $nativePeReport -Raw -Encoding UTF8 | ConvertFrom-Json)
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $nativeBuildReport -Encoding UTF8
}

$makeNsis = Resolve-MakeNsis
$sevenZip = Resolve-SevenZip
$gcc = Resolve-NativeCompiler
$compilerBin = Split-Path -Parent $gcc
if ($WindresPath) { $windres = [IO.Path]::GetFullPath($WindresPath) }
else {
    $windres = Join-Path $compilerBin 'i686-w64-mingw32-windres.exe'
    if (-not (Test-Path -LiteralPath $windres -PathType Leaf)) { $windres = Join-Path $compilerBin 'windres.exe' }
}
if (-not (Test-Path -LiteralPath $windres -PathType Leaf)) { throw 'Supply -WindresPath with the MinGW resource compiler.' }
$payload = Get-Content -LiteralPath (Join-Path $root 'vendor\runtime\payload-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
New-Item -ItemType Directory -Path $OutputDirectory,$ArtifactDirectory,$BuildDirectory,$TestDirectory -Force | Out-Null
$tempDirectory = Join-Path $BuildDirectory ('temp-' + $runId)
New-Item -ItemType Directory -Path $tempDirectory -ErrorAction Stop | Out-Null
$installer = Join-Path $OutputDirectory ([string]$product.output_filename)
$manifestScript = Join-Path $root 'tools\build-release-manifest.py'
$sourceSnapshot = Join-Path $ArtifactDirectory 'source-inputs.json'
$toolReport = Join-Path $ArtifactDirectory 'tool-identities.json'
$nativeHelper = Join-Path $BuildDirectory 'medieval_fix_patcher.exe'
$nativePeReport = Join-Path $ArtifactDirectory 'native-pe.json'
$nativeBuildReport = Join-Path $ArtifactDirectory 'native-build.json'
$audit = Join-Path $OutputDirectory 'INSTALLER_AUDIT.json'
$reports = [ordered]@{}
$buildResult = [ordered]@{schema='unofficial-medieval-total-war-patch-build-v2';result='incomplete';started_utc=[DateTime]::UtcNow.ToString('o')}
$environmentNames = @('MTW_TEST_GAME_EXE','MTW_TEST_INSTALLER','MTW_RUN_COMPILED_INSTALLER_TESTS',
    'MTW_RUN_LIFECYCLE_FAULTS','MTW_ENABLE_LIFECYCLE_FAULTS','MTW_RELEASE_DIRECTORY',
    'MTW_TEST_REPORT_DIRECTORY','MTW_TEST_NATIVE_HELPER','MTW_TEST_NATIVE_UNSTRIPPED','MTW_TEST_UNINSTALLER','MTW_TEST_C_BACKEND',
    'MTW_CC','MTW_RUN_LEGACY_MIGRATION_TESTS','MTW_C_HISTORICAL_BULK','MTW_LEGACY_CPP_HELPER',
    'SEVENZIP_EXE','PYTHONDONTWRITEBYTECODE','TEMP','TMP','PATH')
$priorEnvironment = @{}
foreach ($name in $environmentNames) { $priorEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
Push-Location $root
try {
    $env:MTW_TEST_GAME_EXE = $null
    $env:MTW_RUN_LEGACY_MIGRATION_TESTS = '0'
    $env:MTW_C_HISTORICAL_BULK = $null
    $env:MTW_LEGACY_CPP_HELPER = $null
    $env:MTW_TEST_INSTALLER = $installer
    $env:MTW_RUN_COMPILED_INSTALLER_TESTS = '0'
    $env:MTW_RUN_LIFECYCLE_FAULTS = '0'
    $env:MTW_ENABLE_LIFECYCLE_FAULTS = '0'
    $env:MTW_RELEASE_DIRECTORY = $OutputDirectory
    $env:MTW_TEST_REPORT_DIRECTORY = $ArtifactDirectory
    $env:SEVENZIP_EXE = $sevenZip
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $env:TEMP = $tempDirectory
    $env:TMP = $tempDirectory
    $env:PATH = $compilerBin + ';' + $env:PATH
    $env:MTW_TEST_NATIVE_HELPER = $nativeHelper
    $env:MTW_TEST_NATIVE_UNSTRIPPED = Join-Path $BuildDirectory 'medieval_fix_patcher.unstripped.exe'
    $env:MTW_CC = $gcc
    $env:MTW_TEST_C_BACKEND = '1'
    & (Join-Path $root 'tools\generate-product-nsh.ps1')
    Invoke-LoggedCommand $python @($manifestScript,'--write-source-snapshot',$sourceSnapshot) 'source-snapshot.log'
    Build-NativeHelper

    $r185Evidence = ''
    $r185Clang = ''
    if (-not $SkipR185Build) {
        # Resolve the same tool as build_scaffold.ps1, including its fallback.
        $clangCommand = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
        $r185Clang = if ($clangCommand) { $clangCommand.Source } else { Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe' }
        if (-not (Test-Path -LiteralPath $r185Clang -PathType Leaf)) { throw 'The R185 clang-cl compiler is missing.' }
        $r185Build = Join-Path $BuildDirectory ('r185-' + $runId)
        New-Item -ItemType Directory -Path $r185Build -ErrorAction Stop | Out-Null
        Push-Location $r185Build
        try {
            & (Join-Path $root 'vendor\r185\source\build_scaffold.ps1') -OutputDirectory $r185Build 2>&1 |
                Tee-Object -FilePath (Join-Path $ArtifactDirectory 'r185-build.log') | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "R185 build failed: $LASTEXITCODE" }
        }
        finally { Pop-Location }
        $rebuilt = Join-Path $r185Build 'combined-r154\D3D9.dll'
        if ((Get-FileHash -LiteralPath $rebuilt -Algorithm SHA256).Hash -ne $payload.files.'D3D9.dll'.sha256) {
            throw 'Rebuilt R185 bytes do not match the packaged runtime.'
        }
        $r185Evidence = Join-Path $ArtifactDirectory 'r185-build-manifest.json'
        Copy-Item -LiteralPath (Join-Path $r185Build 'BUILD_MANIFEST.json') -Destination $r185Evidence -ErrorAction Stop
    }

    Invoke-LoggedCommand $python @($manifestScript,'--verify-source-snapshot',$sourceSnapshot) 'source-before-package.log'

    # Delete only known release products. Tests and build scratch use fresh directories.
    foreach ($name in @([string]$product.output_filename,'Unofficial Medieval Total War Patch.exe','RELEASE_MANIFEST.json','SHA256SUMS.txt','INSTALLER_AUDIT.json','SOURCE_ATTRIBUTION.md')) {
        $path = Join-Path $OutputDirectory $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
    }
    Invoke-LoggedCommand $makeNsis @('/V3',('/DOUTPUT_FILE=' + $installer),('/DNATIVE_HELPER=' + $nativeHelper),(Join-Path $root 'installer.nsi')) 'makensis.log'
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'makensis did not produce the requested installer.' }
    Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $OutputDirectory 'LICENSE.txt') -ErrorAction Stop
    $env:MTW_TEST_UNINSTALLER = Join-Path $BuildDirectory ([string]$product.uninstaller_filename)
    & (Join-Path $root 'tools\audit-installer.ps1') -InstallerPath $installer -NativeHelperPath $nativeHelper -UninstallerOutputPath $env:MTW_TEST_UNINSTALLER -PythonPath $python -SevenZipPath $sevenZip -OutputPath $audit -ArtifactDirectory (Join-Path $BuildDirectory ('audit-' + $runId))
    Copy-Item -LiteralPath $audit -Destination (Join-Path $ArtifactDirectory 'INSTALLER_AUDIT.json') -ErrorAction Stop

    $reports['project_contracts'] = Invoke-PytestStage 'project_contracts' @(
        'tests/test_payload_provenance.py','tests/test_assets.py','tests/test_installer_script.py','tests/test_package_audit.py',
        'tests/test_build_workflow.py')
    $selection = Get-GameTestSelection
    $reports['native_guard'] = Invoke-PytestStage 'native_guard' $selection.native_guard
    $toolIdentities = @(
        (Get-ToolIdentity 'python' $python ([string](& $python --version))),
        (Get-ToolIdentity 'nsis' $makeNsis ([string](& $makeNsis /VERSION))),
        (Get-ToolIdentity '7zip' $sevenZip),
        (Get-ToolIdentity 'native-gcc' $gcc ([string](& $gcc -dumpfullversion -dumpversion))),
        (Get-ToolIdentity 'native-windres' $windres),
        (Get-ToolIdentity 'powershell' ([Diagnostics.Process]::GetCurrentProcess().MainModule.FileName) $PSVersionTable.PSVersion.ToString())
    )
    if ($r185Clang) {
        $clangVersion = @(& $r185Clang --version)
        if ($LASTEXITCODE -ne 0) { throw 'R185 compiler version probe failed.' }
        # Later lines include InstalledDir; only the version line is public.
        $toolIdentities += Get-ToolIdentity 'r185-clang-cl' $r185Clang ([string]$clangVersion[0])
    }
    foreach ($name in @('cl.exe','link.exe')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) { $toolIdentities += Get-ToolIdentity $name $command.Source }
    }
    foreach ($name in @('System.dll','nsDialogs.dll')) {
        $path = Join-Path (Split-Path -Parent $makeNsis) ('Plugins\x86-unicode\' + $name)
        if (Test-Path -LiteralPath $path -PathType Leaf) { $toolIdentities += Get-ToolIdentity ('nsis-plugin-' + $name) $path }
    }
    $packages = & $python -c "import importlib.metadata,json; print(json.dumps({n:importlib.metadata.version(n) for n in ('pytest','pefile','Pillow')}))"
    if ($LASTEXITCODE -ne 0) { throw 'Python dependency identity collection failed.' }
    [ordered]@{schema='unofficial-medieval-build-tools-v1';executables=$toolIdentities;python_packages=($packages | ConvertFrom-Json)} |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $toolReport -Encoding UTF8
    $manifestArguments = @($manifestScript,'--installer',$installer,'--output',(Join-Path $OutputDirectory 'RELEASE_MANIFEST.json'),
        '--source-snapshot',$sourceSnapshot,'--tools',$toolReport,'--audit',$audit,'--native-build',$nativeBuildReport)
    if ($r185Evidence) { $manifestArguments += @('--r185-build-manifest',$r185Evidence) }
    foreach ($entry in $reports.GetEnumerator()) { $manifestArguments += @('--test-result',($entry.Key + '=' + $entry.Value)) }
    Invoke-LoggedCommand $python $manifestArguments 'release-manifest-draft.log'
    Write-Checksums
    $reports['release_hygiene'] = Invoke-PytestStage 'release_hygiene' @('tests/test_release_hygiene.py')
    $manifestArguments += @('--test-result',('release_hygiene=' + $reports['release_hygiene']))
    Invoke-LoggedCommand $python $manifestArguments 'release-manifest-final.log'
    Write-Checksums
    # The evidence-only manifest update changes its checksum; verify that final
    # artifact once without rewriting the evidence referenced by the manifest.
    $null = Invoke-PytestStage 'final_manifest' @('tests/test_release_hygiene.py::test_release_manifest_and_checksums_match_dist')
    Invoke-LoggedCommand $python @($manifestScript,'--verify-source-snapshot',$sourceSnapshot) 'source-final.log'
    $buildResult['result'] = 'pass'
    $buildResult['installer_sha256'] = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
    $buildResult['release_manifest_sha256'] = (Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'RELEASE_MANIFEST.json') -Algorithm SHA256).Hash
    $buildResult['game_tests'] = 'not-run'
    $frozenManifest = Join-Path $ArtifactDirectory 'BUILD_MANIFEST.json'
    Copy-Item -LiteralPath (Join-Path $OutputDirectory 'RELEASE_MANIFEST.json') -Destination $frozenManifest -ErrorAction Stop
    $buildResult['inputs'] = [ordered]@{}
    foreach ($entry in ([ordered]@{installer=$installer;nativeHelper=$nativeHelper;
        nativeUnstripped=$env:MTW_TEST_NATIVE_UNSTRIPPED;uninstaller=$env:MTW_TEST_UNINSTALLER;
        sourceSnapshot=$sourceSnapshot;toolReport=$toolReport;nativeBuildReport=$nativeBuildReport;
        audit=(Join-Path $ArtifactDirectory 'INSTALLER_AUDIT.json');releaseManifest=$frozenManifest}).GetEnumerator()) {
        $buildResult.inputs[$entry.Key] = Get-BuildFileRecord $entry.Value
    }
    if ($r185Evidence) { $buildResult.inputs['r185Evidence'] = Get-BuildFileRecord $r185Evidence }
    $buildResult['reports'] = [ordered]@{}
    foreach ($entry in $reports.GetEnumerator()) { $buildResult.reports[$entry.Key] = Get-BuildFileRecord $entry.Value }
    Write-Output "Build complete: $installer"
    Write-Output 'Game tests have not run. Run test.ps1 with -SupportedGameExecutable to test this installer.'
    Write-Output "SHA-256: $($buildResult.installer_sha256)"
    Write-Output "Build diagnostics and test evidence: $ArtifactDirectory"
}
catch {
    $buildResult['result'] = 'fail'
    $buildResult['error'] = $_.Exception.Message
    throw
}
finally {
    $buildResult['completed_utc'] = [DateTime]::UtcNow.ToString('o')
    try {
        $recordPath = Join-Path $ArtifactDirectory 'BUILD_RESULT.json'
        Write-BuildJson $recordPath $buildResult
        if ($buildResult.result -eq 'pass') {
            Write-BuildJson (Join-Path $root 'artifacts\latest-build.json') (Get-BuildFileRecord $recordPath)
        }
    }
    finally {
        foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$priorEnvironment[$name],'Process') }
        Pop-Location
    }
}
