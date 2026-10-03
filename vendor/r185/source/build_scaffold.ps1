[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build')
)

$ErrorActionPreference = 'Stop'
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$r185Root = Split-Path -Parent $PSScriptRoot
$acceptedSource = Join-Path $r185Root 'accepted-dust-source'
$backend = Join-Path (Split-Path -Parent $r185Root) 'runtime\dgVoodoo_D3D9.dll'
$vsDevShell = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1'
$clangCommand = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
$clangCl = if ($clangCommand) {
    $clangCommand.Source
} else {
    Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'
}
$problemShader = Join-Path (Split-Path -Parent $PSScriptRoot) `
    'tests\assets\mapper-lanczos3-ps-36116AC1.bin'
$expectedProblemShader = '36116AC1645DDFF0128FAC5370FAA4B332CBEEB72C0840C23CEED929ADA697B9'
$singleSampleHlsl = Join-Path $PSScriptRoot 'mapper_single_sample_linear_ps.hlsl'
$singleSampleCso = Join-Path $PSScriptRoot 'mapper_single_sample_linear_ps.cso'
$expectedSingleSampleHlsl = '758DF9E5D66FE4434D1458A1BC1B5D4A6A134851628977153F22DD1F6EB5ABC3'
$expectedSingleSampleCso = '72F8484170140CA1E899DFA290A20152A31129CC8A6957EB83A78AE548ADB2B3'

$expected = [ordered]@{
    'd3d9_proxy.c' = 'EC191555CD70FC481D7B0335D1D00C5B62335C1ACB020BDA3AE63EED6F92D384'
    'd3d9_proxy.def' = '54048027188ADD4DB797EB0C9A40900ED1124F6D0265751FE53FDC50C672AF0E'
    'dust_cadence.h' = '2BE1C02CB48F1E4534072C718AE24604E5492F48F132F0FA0D50D4AB28A7613C'
    'particle_normalization.h' = '9852910C2F4B85BE605D2D275E31F3A13E1A74A92CB0687A2DB9926EC24526D4'
    'dust_cadence_tests.c' = 'DD8DDAAFA85452E62FC5871DB3E492A9FAA75D80BC9EEACD739C92056FD4E753'
    'build_release.ps1' = '3844FAF9D400B8CCE765FCEC618EB7A01EEE7248E6AB197465922A3C79CA8257'
}
$expectedAcceptedProxy = '34F855A17C10B6BBCEFD844B99A5B5A7F442DECDDFFDB0D898645FA2FF0B0F0C'
$expectedBackend = 'E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6'
$expectedR185Proxy = 'CBB6A16CE535640B4FDB6526F42E575EF882E4CFE232BA8CF8BAAF8735E8596A'

foreach ($entry in $expected.GetEnumerator()) {
    $path = Join-Path $acceptedSource $entry.Key
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing pinned accepted input: $path"
    }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    if ($actual -ne $entry.Value) {
        throw "Pinned accepted input drift: $($entry.Key): $actual"
    }
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $backend).Hash -ne $expectedBackend) {
    throw 'Pinned backend drift.'
}
foreach ($tool in @($vsDevShell, $clangCl)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Missing build tool: $tool"
    }
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $problemShader).Hash -ne
    $expectedProblemShader) {
    throw 'Pinned mapper lanczos-3 shader drift.'
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $singleSampleHlsl).Hash -ne
    $expectedSingleSampleHlsl) {
    throw 'Pinned single-sample linear HLSL drift.'
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $singleSampleCso).Hash -ne
    $expectedSingleSampleCso) {
    throw 'Pinned single-sample linear bytecode drift.'
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$control = Join-Path $OutputDirectory 'accepted-control'
$combined = Join-Path $OutputDirectory 'combined-r154'
New-Item -ItemType Directory -Force -Path $control,$combined | Out-Null
$generatedShader = Join-Path $combined 'mapper_single_sample_linear_bytecode.inc'
$shaderBytes = [System.IO.File]::ReadAllBytes($singleSampleCso)
$shaderLines = for ($offset = 0; $offset -lt $shaderBytes.Length; $offset += 16) {
    $last = [Math]::Min($offset + 15, $shaderBytes.Length - 1)
    '    ' + (($shaderBytes[$offset..$last] | ForEach-Object {
        '0x{0:X2}' -f $_
    }) -join ',') + $(if ($last -lt $shaderBytes.Length - 1) { ',' } else { '' })
}
$generatedText = "static const unsigned char mapper_single_sample_linear_bytecode[] = {`r`n" +
    ($shaderLines -join "`r`n") + "`r`n};`r`n"
[System.IO.File]::WriteAllText(
    $generatedShader, $generatedText,
    [System.Text.UTF8Encoding]::new($false))

& (Join-Path $acceptedSource 'build_release.ps1') -OutputDirectory $control -BackendSource $backend
if ($LASTEXITCODE -ne 0) { throw "Accepted control build failed: $LASTEXITCODE" }
$controlHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $control 'D3D9.dll')).Hash
if ($controlHash -ne $expectedAcceptedProxy) {
    throw "Accepted control reproduction failed: $controlHash"
}

& $vsDevShell -Arch x86 -HostArch amd64 -SkipAutomaticLocation
$acceptedObj = Join-Path $combined 'accepted_dust.obj'
$publicObj = Join-Path $combined 'combined_proxy.obj'
$frontendObj = Join-Path $combined 'frontend_fix.obj'
$frontendEpochObj = Join-Path $combined 'frontend_epoch_core.obj'
$presentationInputObj = Join-Path $combined 'presentation_input_core.obj'
$primaryOriginGuardObj = Join-Path $combined 'primary_origin_guard_core.obj'
$resolutionFilterObj = Join-Path $combined 'resolution_filter_core.obj'
$windowTransitionGuardObj = Join-Path $combined 'window_transition_guard_core.obj'
$shadowObj = Join-Path $combined 'loading_shadow_core.obj'
$focusObj = Join-Path $combined 'focus_span_core.obj'
$focusPlaneObj = Join-Path $combined 'focus_plane_shadow_core.obj'
$guardedCopyObj = Join-Path $combined 'guarded_memory_copy.obj'
$lockPatchObj = Join-Path $combined 'reentrant_lock_patch_core.obj'
$mapperCloneObj = Join-Path $combined 'mapper_shader_clone_core.obj'
$mapperActivationObj = Join-Path $combined 'mapper_activation_core.obj'
$combinedDll = Join-Path $combined 'D3D9.dll'

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_RELEASE_BUILD=1','/DMTW_RELEASE_DENSE_HZ=15','/DMTW_RELEASE_VIRTUAL_MANAGER_FPS=19',
    '/DDirect3DCreate9=mtw_accepted_dust_direct3dcreate9',
    ('/Fo' + $acceptedObj),(Join-Path $acceptedSource 'd3d9_proxy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Accepted object build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $publicObj),(Join-Path $PSScriptRoot 'combined_proxy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Public bridge build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $frontendObj),(Join-Path $PSScriptRoot 'frontend_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Inert frontend build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $frontendEpochObj),(Join-Path $PSScriptRoot 'frontend_epoch_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Frontend epoch core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $presentationInputObj),(Join-Path $PSScriptRoot 'presentation_input_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Presentation input core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/EHa','/c',
    ('/Fo' + $primaryOriginGuardObj),(Join-Path $PSScriptRoot 'primary_origin_guard_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Primary origin guard core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $resolutionFilterObj),(Join-Path $PSScriptRoot 'resolution_filter_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Resolution filter core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $windowTransitionGuardObj),(Join-Path $PSScriptRoot 'window_transition_guard_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Window transition guard core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $shadowObj),(Join-Path $PSScriptRoot 'loading_shadow_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Loading shadow core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $focusObj),(Join-Path $PSScriptRoot 'focus_span_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Focus span core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $focusPlaneObj),(Join-Path $PSScriptRoot 'focus_plane_shadow_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Focus plane shadow core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $guardedCopyObj),(Join-Path $PSScriptRoot 'guarded_memory_copy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Guarded memory copy build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $lockPatchObj),(Join-Path $PSScriptRoot 'reentrant_lock_patch_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Reentrant lock patch core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/I' + $combined),
    ('/Fo' + $mapperCloneObj),(Join-Path $PSScriptRoot 'mapper_shader_clone_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Mapper shader clone core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $mapperActivationObj),(Join-Path $PSScriptRoot 'mapper_activation_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Mapper activation core build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $combinedDll),$acceptedObj,$publicObj,$frontendObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','user32.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Combined inert proxy link failed: $LASTEXITCODE" }
$combinedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $combinedDll).Hash
if ($combinedHash -ne $expectedR185Proxy) {
    throw "R185 reproducible build drift: $combinedHash"
}
Copy-Item -LiteralPath $backend -Destination (Join-Path $combined 'dgVoodoo_D3D9.dll') -Force

$smokeExe = Join-Path $combined 'smoke_loader.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $smokeExe),(Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\smoke_loader.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Smoke loader build failed: $LASTEXITCODE" }
Push-Location -LiteralPath $combined
try {
    & $smokeExe
if ($LASTEXITCODE -ne 0) { throw "Smoke loader failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

$shadowTests = Join-Path $combined 'loading_shadow_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $shadowTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\loading_shadow_tests.c'),
    (Join-Path $PSScriptRoot 'loading_shadow_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Loading shadow tests build failed: $LASTEXITCODE" }
& $shadowTests
if ($LASTEXITCODE -ne 0) { throw "Loading shadow tests failed: $LASTEXITCODE" }

$focusTests = Join-Path $combined 'focus_span_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $focusTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\focus_span_tests.c'),
    (Join-Path $PSScriptRoot 'focus_span_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Focus span tests build failed: $LASTEXITCODE" }
& $focusTests
if ($LASTEXITCODE -ne 0) { throw "Focus span tests failed: $LASTEXITCODE" }

$focusPlaneTests = Join-Path $combined 'focus_plane_shadow_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $focusPlaneTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\focus_plane_shadow_tests.c'),
    (Join-Path $PSScriptRoot 'focus_plane_shadow_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Focus plane shadow tests build failed: $LASTEXITCODE" }
& $focusPlaneTests
if ($LASTEXITCODE -ne 0) { throw "Focus plane shadow tests failed: $LASTEXITCODE" }

$guardedCopyTests = Join-Path $combined 'guarded_memory_copy_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $guardedCopyTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\guarded_memory_copy_tests.c'),
    (Join-Path $PSScriptRoot 'guarded_memory_copy.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Guarded memory copy tests build failed: $LASTEXITCODE" }
& $guardedCopyTests
if ($LASTEXITCODE -ne 0) { throw "Guarded memory copy tests failed: $LASTEXITCODE" }

$frontendEpochTests = Join-Path $combined 'frontend_epoch_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $frontendEpochTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\frontend_epoch_tests.c'),
    (Join-Path $PSScriptRoot 'frontend_epoch_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Frontend epoch tests build failed: $LASTEXITCODE" }
& $frontendEpochTests
if ($LASTEXITCODE -ne 0) { throw "Frontend epoch tests failed: $LASTEXITCODE" }

$primaryOriginGuardTests = Join-Path $combined 'primary_origin_guard_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/EHa',
    ('/Fe' + $primaryOriginGuardTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\primary_origin_guard_tests.c'),
    (Join-Path $PSScriptRoot 'primary_origin_guard_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Primary origin guard tests build failed: $LASTEXITCODE" }
& $primaryOriginGuardTests
if ($LASTEXITCODE -ne 0) { throw "Primary origin guard tests failed: $LASTEXITCODE" }

$resolutionFilterTests = Join-Path $combined 'resolution_filter_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $resolutionFilterTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\resolution_filter_tests.c'),
    (Join-Path $PSScriptRoot 'resolution_filter_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Resolution filter tests build failed: $LASTEXITCODE" }
& $resolutionFilterTests
if ($LASTEXITCODE -ne 0) { throw "Resolution filter tests failed: $LASTEXITCODE" }

$windowTransitionGuardTests = Join-Path $combined 'window_transition_guard_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $windowTransitionGuardTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\window_transition_guard_tests.c'),
    (Join-Path $PSScriptRoot 'window_transition_guard_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Window transition guard tests build failed: $LASTEXITCODE" }
& $windowTransitionGuardTests
if ($LASTEXITCODE -ne 0) { throw "Window transition guard tests failed: $LASTEXITCODE" }

$presentationInputTests = Join-Path $combined 'presentation_input_core_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $presentationInputTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\presentation_input_core_tests.c'),
    (Join-Path $PSScriptRoot 'presentation_input_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Presentation input core tests build failed: $LASTEXITCODE" }
& $presentationInputTests
if ($LASTEXITCODE -ne 0) { throw "Presentation input core tests failed: $LASTEXITCODE" }

# Avoid the installer-detection filename heuristic for this console test.
$lockPatchTests = Join-Path $combined 'reentrant_lock_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $lockPatchTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\reentrant_lock_patch_tests.c'),
    (Join-Path $PSScriptRoot 'reentrant_lock_patch_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Reentrant lock patch tests build failed: $LASTEXITCODE" }
& $lockPatchTests
if ($LASTEXITCODE -ne 0) { throw "Reentrant lock patch tests failed: $LASTEXITCODE" }

$mapperCloneTests = Join-Path $combined 'mapper_shader_clone_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/I' + $PSScriptRoot),('/I' + $combined),
    ('/Fe' + $mapperCloneTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\mapper_shader_clone_tests.c'),
    (Join-Path $PSScriptRoot 'mapper_shader_clone_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Mapper shader clone tests build failed: $LASTEXITCODE" }
& $mapperCloneTests $problemShader
if ($LASTEXITCODE -ne 0) { throw "Mapper shader clone tests failed: $LASTEXITCODE" }

$mapperActivationTests = Join-Path $combined 'mapper_activation_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $mapperActivationTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\mapper_activation_tests.c'),
    (Join-Path $PSScriptRoot 'mapper_activation_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Mapper activation tests build failed: $LASTEXITCODE" }
& $mapperActivationTests
if ($LASTEXITCODE -ne 0) { throw "Mapper activation tests failed: $LASTEXITCODE" }

$manifest = [ordered]@{
    schema = 'mtw.combined-proxy-r185.v1'
    status = 'accepted_release'
    accepted_control_sha256 = $controlHash
    combined_inert_sha256 = $combinedHash
    backend_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $combined 'dgVoodoo_D3D9.dll')).Hash
    smoke_loader_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $smokeExe).Hash
    smoke_loader_exit = 0
    loading_shadow_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $shadowTests).Hash
    loading_shadow_tests_exit = 0
    focus_span_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $focusTests).Hash
    focus_span_tests_exit = 0
    focus_plane_shadow_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $focusPlaneTests).Hash
    focus_plane_shadow_tests_exit = 0
    guarded_memory_copy_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $guardedCopyTests).Hash
    guarded_memory_copy_tests_exit = 0
    frontend_epoch_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $frontendEpochTests).Hash
    frontend_epoch_tests_exit = 0
    primary_origin_guard_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $primaryOriginGuardTests).Hash
    primary_origin_guard_tests_exit = 0
    resolution_filter_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolutionFilterTests).Hash
    resolution_filter_tests_exit = 0
    window_transition_guard_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $windowTransitionGuardTests).Hash
    window_transition_guard_tests_exit = 0
    presentation_input_core_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $presentationInputTests).Hash
    presentation_input_core_tests_exit = 0
    reentrant_lock_patch_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $lockPatchTests).Hash
    reentrant_lock_patch_tests_exit = 0
    mapper_shader_clone_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $mapperCloneTests).Hash
    mapper_shader_clone_tests_exit = 0
    mapper_activation_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $mapperActivationTests).Hash
    mapper_activation_tests_exit = 0
    frontend_fix_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'frontend_fix.c')).Hash
    frontend_epoch_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'frontend_epoch_core.c')).Hash
    presentation_input_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'presentation_input_core.c')).Hash
    primary_origin_guard_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'primary_origin_guard_core.c')).Hash
    resolution_filter_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'resolution_filter_core.c')).Hash
    window_transition_guard_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'window_transition_guard_core.c')).Hash
    loading_shadow_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'loading_shadow_core.c')).Hash
    focus_span_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'focus_span_core.c')).Hash
    focus_plane_shadow_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'focus_plane_shadow_core.c')).Hash
    guarded_memory_copy_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'guarded_memory_copy.c')).Hash
    reentrant_lock_patch_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'reentrant_lock_patch_core.c')).Hash
    mapper_shader_clone_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'mapper_shader_clone_core.c')).Hash
    mapper_activation_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'mapper_activation_core.c')).Hash
    accepted_inputs = $expected
    accepted_source_root = 'vendor/r185/accepted-dust-source'
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'BUILD_MANIFEST.json') -Encoding utf8
$manifest | ConvertTo-Json -Depth 5
