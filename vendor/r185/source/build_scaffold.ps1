[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build'),
    [switch]$AllowUnpinnedOutput,
    [switch]$DiagnosticDepthState,
    [ValidateSet(0,1)][int]$CreditsFlashFix = 0
)

$ErrorActionPreference = 'Stop'
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$r185Root = Split-Path -Parent $PSScriptRoot
$repositoryTests = Join-Path (Split-Path -Parent (Split-Path -Parent $r185Root)) 'tests'
$acceptedSource = Join-Path $r185Root 'accepted-dust-source'
$dustFidelitySource = Join-Path (Split-Path -Parent $r185Root) 'dust-fidelity'
$backend = Join-Path (Split-Path -Parent $r185Root) 'runtime\dgVoodoo_D3D9.dll'
$controlBackend = Join-Path $acceptedSource 'dgVoodoo_D3D9-2.87.2-control.dll'
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
$expectedBackend = '6A0CA214784BE04B7C8B547105AA9D79ACF4DC26C0B6F8702B437DDCA54058B2'
$expectedControlBackend = 'E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6'
$expectedR185Proxy = '5CD672ACDE32034E0530147195FCFBE84751730A65F309C26ADF1CA8BF11D1AF'
$expectedScrollOffProxy = '7EBBB717AA71AA7207C8B435687DFE6CDC49AB3C220030FB84B16A73C4B15300'
$expectedSpriteOffProxy = 'C05CF7E7E8E2F6BC8ECCEFB7F957086ADB2BF0B6F6FFA9407C5DAF486F858AE4'
$expectedScrollSpriteOffProxy = 'DFA4C8776F9328CD99EDF5661785F882ECA9550620486534DA540C65B05A8CC4'
$expectedFastOnlyProxy = '197F95C52FFA2EA2682941F41A481826835AB3582CBB924E6C4049D22323787E'
$expectedScrollSpriteOffFastProxy = '5E182D985B1835B3D4D9FF9C2581918AF635B84054B8453BAF17A25185779B6B'
$expectedScrollSpriteOffIconOffProxy = 'C04AFDA290A467F237D69B923D00381209D497ECE78D7316DF76367408C783A1'
$expectedScrollSpriteOffFastIconOffProxy = 'CA3289FA7D29C437A0A545EB4FB5A90EED224460AE1F34D193587A59BD49A67D'
$expectedIconOnlyProxy = 'A95298ABBC213359432DB55B81EF2708616A998049575E1EF78AAC0FF74AC455'
$expectedFastIconProxy = '99ACF3CDCC95225ECB7B50224969EABD838BEF86FCCD0999F86686D8C29A55AC'

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
if (-not (Test-Path -LiteralPath $controlBackend -PathType Leaf) -or
    (Get-FileHash -Algorithm SHA256 -LiteralPath $controlBackend).Hash -ne
    $expectedControlBackend) {
    throw 'Pinned historical accepted-control backend drift.'
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

& (Join-Path $acceptedSource 'build_release.ps1') -OutputDirectory $control -BackendSource $controlBackend
if ($LASTEXITCODE -ne 0) { throw "Accepted control build failed: $LASTEXITCODE" }
$controlHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $control 'D3D9.dll')).Hash
if ($controlHash -ne $expectedAcceptedProxy) {
    throw "Accepted control reproduction failed: $controlHash"
}

& $vsDevShell -Arch x86 -HostArch amd64 -SkipAutomaticLocation
$acceptedObj = Join-Path $combined 'accepted_dust.obj'
$dustBridgeObj = Join-Path $combined 'dust_565_bridge.obj'
$minhookBufferObj = Join-Path $combined 'minhook_buffer.obj'
$minhookHookObj = Join-Path $combined 'minhook_hook.obj'
$minhookTrampolineObj = Join-Path $combined 'minhook_trampoline.obj'
$minhookHdeObj = Join-Path $combined 'minhook_hde32.obj'
$publicObj = Join-Path $combined 'combined_proxy.obj'
$fastPublicObj = Join-Path $combined 'combined_proxy_fast.obj'
$fastOnlyPublicObj = Join-Path $combined 'campaign_fast_moves_proxy.obj'
$iconOnlyPublicObj = Join-Path $combined 'battle_icon_only_proxy.obj'
$fastIconPublicObj = Join-Path $combined 'campaign_fast_battle_icon_proxy.obj'
$fastMovesObj = Join-Path $combined 'campaign_fast_moves.obj'
$battleIconObj = Join-Path $combined 'battle_icon_atlas_fix.obj'
$creditsObj = Join-Path $combined 'credits_flash_fix.obj'
$frontendObj = Join-Path $combined 'frontend_fix.obj'
$frontendScrollOffObj = Join-Path $combined 'frontend_fix_scroll_off.obj'
$frontendSpriteOffObj = Join-Path $combined 'frontend_fix_sprite_off.obj'
$frontendScrollSpriteOffObj = Join-Path $combined 'frontend_fix_scroll_sprite_off.obj'
$frontendScrollSpriteOffIconOffObj = Join-Path $combined 'frontend_fix_scroll_sprite_off_icon_off.obj'
$campaignPanObj = Join-Path $combined 'campaign_pan_core.obj'
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
$scrollOffDll = Join-Path $combined 'D3D9-scroll-off.dll'
$spriteOffDll = Join-Path $combined 'D3D9-sprite-off.dll'
$scrollSpriteOffDll = Join-Path $combined 'D3D9-scroll-sprite-off.dll'
$fastOnlyDll = Join-Path $combined 'D3D9-fast-only.dll'
$iconOnlyDll = Join-Path $combined 'D3D9-icon-only.dll'
$fastIconDll = Join-Path $combined 'D3D9-fast-icon.dll'
$scrollSpriteOffFastDll = Join-Path $combined 'D3D9-scroll-sprite-off-fast.dll'
$scrollSpriteOffIconOffDll = Join-Path $combined 'D3D9-scroll-sprite-off-icon-off.dll'
$scrollSpriteOffFastIconOffDll = Join-Path $combined 'D3D9-scroll-sprite-off-fast-icon-off.dll'

$dustCompileDefinitions = @('/DMTW_DUST_CHILD_DELTA=1')
$dustArgs = @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DDirect3DCreate9=mtw_accepted_dust_direct3dcreate9'
) + $dustCompileDefinitions + @(
    ('/Fo' + $acceptedObj),(Join-Path $dustFidelitySource 'd3d9_proxy.c')
)
Write-Output ('dust compilation: ' + ($dustArgs -join ' '))
& $clangCl @dustArgs
if ($LASTEXITCODE -ne 0) { throw "Dust simulation object build failed: $LASTEXITCODE" }

$bridgeCompileDefinitions = @('/DMTW_DUST_DEFERRED_TAIL=1')
if ($DiagnosticDepthState) {
    $bridgeCompileDefinitions += '/DMTW_DIAGNOSTIC_DEPTH_STATE=1'
}
$bridgeArgs = @(
    '/nologo','/TP','--target=i686-pc-windows-msvc','/EHsc','/std:c++17',
    '/O2','/W4','/WX','/c'
) + $bridgeCompileDefinitions + @(
    ('/Fo' + $dustBridgeObj),(Join-Path $dustFidelitySource 'dust_565_bridge.cpp')
)
& $clangCl @bridgeArgs
if ($LASTEXITCODE -ne 0) { throw "Dust render bridge object build failed: $LASTEXITCODE" }

foreach ($entry in @(
    @('src\buffer.c', $minhookBufferObj),
    @('src\hook.c', $minhookHookObj),
    @('src\trampoline.c', $minhookTrampolineObj),
    @('src\hde\hde32.c', $minhookHdeObj)
)) {
    & $clangCl @(
        '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W3','/c',
        ('/Fo' + $entry[1]),(Join-Path $dustFidelitySource ('minhook\' + $entry[0]))
    )
    if ($LASTEXITCODE -ne 0) { throw "MinHook x86 build failed: $($entry[0])" }
}

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/DMTW_ENABLE_CREDITS_FLASH_FIX=' + $CreditsFlashFix),
    ('/Fo' + $publicObj),(Join-Path $PSScriptRoot 'combined_proxy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Public bridge build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_FAST_MOVES=1',
    ('/DMTW_ENABLE_CREDITS_FLASH_FIX=' + $CreditsFlashFix),
    ('/Fo' + $fastPublicObj),(Join-Path $PSScriptRoot 'combined_proxy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Fast combined bridge build failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/DMTW_ENABLE_CREDITS_FLASH_FIX=' + $CreditsFlashFix),
    ('/Fo' + $fastOnlyPublicObj),(Join-Path $PSScriptRoot 'campaign_fast_moves_proxy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Fast feature-only bridge build failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_FAST_MOVES=0','/DMTW_ENABLE_BATTLE_ICON_FIX=1',
    ('/DMTW_ENABLE_CREDITS_FLASH_FIX=' + $CreditsFlashFix),
    ('/Fo' + $iconOnlyPublicObj),(Join-Path $PSScriptRoot 'campaign_fast_moves_proxy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Icon feature-only bridge build failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_FAST_MOVES=1','/DMTW_ENABLE_BATTLE_ICON_FIX=1',
    ('/DMTW_ENABLE_CREDITS_FLASH_FIX=' + $CreditsFlashFix),
    ('/Fo' + $fastIconPublicObj),(Join-Path $PSScriptRoot 'campaign_fast_moves_proxy.c')
)
if ($LASTEXITCODE -ne 0) { throw "Fast-and-icon feature-only bridge build failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $fastMovesObj),(Join-Path $PSScriptRoot 'campaign_fast_moves.c')
)
if ($LASTEXITCODE -ne 0) { throw "Fast campaign runtime build failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $battleIconObj),(Join-Path $PSScriptRoot 'battle_icon_atlas_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Battle icon atlas runtime build failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $creditsObj),(Join-Path $PSScriptRoot 'credits_flash_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Credits runtime build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_CAMPAIGN_SCROLL_FIX=1',
    '/DMTW_ENABLE_SPRITE_CLIP_FIX=1',
    ('/Fo' + $frontendObj),(Join-Path $PSScriptRoot 'frontend_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Inert frontend build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_CAMPAIGN_SCROLL_FIX=0',
    '/DMTW_ENABLE_SPRITE_CLIP_FIX=1',
    ('/Fo' + $frontendScrollOffObj),(Join-Path $PSScriptRoot 'frontend_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Scroll-disabled frontend build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_CAMPAIGN_SCROLL_FIX=1',
    '/DMTW_ENABLE_SPRITE_CLIP_FIX=0',
    ('/Fo' + $frontendSpriteOffObj),(Join-Path $PSScriptRoot 'frontend_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Sprite-disabled frontend build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_CAMPAIGN_SCROLL_FIX=0',
    '/DMTW_ENABLE_SPRITE_CLIP_FIX=0',
    ('/Fo' + $frontendScrollSpriteOffObj),(Join-Path $PSScriptRoot 'frontend_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Scroll-and-sprite-disabled frontend build failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    '/DMTW_ENABLE_CAMPAIGN_SCROLL_FIX=0',
    '/DMTW_ENABLE_SPRITE_CLIP_FIX=0',
    '/DMTW_ENABLE_BATTLE_ICON_FIX=0',
    ('/Fo' + $frontendScrollSpriteOffIconOffObj),(Join-Path $PSScriptRoot 'frontend_fix.c')
)
if ($LASTEXITCODE -ne 0) { throw "Icon-disabled frontend build failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
    ('/Fo' + $campaignPanObj),(Join-Path $PSScriptRoot 'campaign_pan_core.c')
)
if ($LASTEXITCODE -ne 0) { throw "Campaign pan core build failed: $LASTEXITCODE" }

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
    ('/Fe' + $combinedDll),$creditsObj,$acceptedObj,$dustBridgeObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$publicObj,$frontendObj,$battleIconObj,$campaignPanObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Combined inert proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $scrollOffDll),$creditsObj,$acceptedObj,$dustBridgeObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$publicObj,$frontendScrollOffObj,$battleIconObj,$campaignPanObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Scroll-disabled proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $spriteOffDll),$creditsObj,$acceptedObj,$dustBridgeObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$publicObj,$frontendSpriteOffObj,$battleIconObj,$campaignPanObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Sprite-disabled proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $scrollSpriteOffDll),$creditsObj,$acceptedObj,$dustBridgeObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$publicObj,$frontendScrollSpriteOffObj,$battleIconObj,$campaignPanObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Scroll-and-sprite-disabled proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $fastOnlyDll),$creditsObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,
    $fastOnlyPublicObj,$fastMovesObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','advapi32.lib','user32.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Fast feature-only proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $scrollSpriteOffFastDll),$creditsObj,$acceptedObj,$dustBridgeObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$fastPublicObj,$fastMovesObj,$frontendScrollSpriteOffObj,$battleIconObj,$campaignPanObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Fast combined proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $scrollSpriteOffIconOffDll),$creditsObj,$acceptedObj,$dustBridgeObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$publicObj,$frontendScrollSpriteOffIconOffObj,$campaignPanObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Icon-disabled Terrain proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $scrollSpriteOffFastIconOffDll),$creditsObj,$acceptedObj,$dustBridgeObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$fastPublicObj,$fastMovesObj,$frontendScrollSpriteOffIconOffObj,$campaignPanObj,$frontendEpochObj,$presentationInputObj,$primaryOriginGuardObj,$resolutionFilterObj,$windowTransitionGuardObj,$shadowObj,$focusObj,$focusPlaneObj,$guardedCopyObj,$lockPatchObj,$mapperCloneObj,$mapperActivationObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Icon-disabled Fast Terrain proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $iconOnlyDll),$creditsObj,$iconOnlyPublicObj,$battleIconObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','advapi32.lib','user32.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Icon-only proxy link failed: $LASTEXITCODE" }
& $clangCl @(
    '/nologo','--target=i686-pc-windows-msvc','/LD',
    ('/Fe' + $fastIconDll),$creditsObj,$minhookBufferObj,$minhookHookObj,$minhookTrampolineObj,$minhookHdeObj,$fastIconPublicObj,$fastMovesObj,$battleIconObj,
    '/link',('/DEF:' + (Join-Path $PSScriptRoot 'combined_proxy.def')),
    '/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','advapi32.lib','user32.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Fast-and-icon-only proxy link failed: $LASTEXITCODE" }
$combinedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $combinedDll).Hash
$scrollOffHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $scrollOffDll).Hash
$spriteOffHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $spriteOffDll).Hash
$scrollSpriteOffHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $scrollSpriteOffDll).Hash
$fastOnlyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $fastOnlyDll).Hash
$scrollSpriteOffFastHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $scrollSpriteOffFastDll).Hash
$scrollSpriteOffIconOffHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $scrollSpriteOffIconOffDll).Hash
$scrollSpriteOffFastIconOffHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $scrollSpriteOffFastIconOffDll).Hash
$iconOnlyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $iconOnlyDll).Hash
$fastIconHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $fastIconDll).Hash
if (-not $AllowUnpinnedOutput -and $combinedHash -ne $expectedR185Proxy) {
    throw "R185 reproducible build drift: $combinedHash"
}
if (-not $AllowUnpinnedOutput -and $scrollOffHash -ne $expectedScrollOffProxy) {
    throw "Scroll-disabled reproducible build drift: $scrollOffHash"
}
if (-not $AllowUnpinnedOutput -and $spriteOffHash -ne $expectedSpriteOffProxy) {
    throw "Sprite-disabled reproducible build drift: $spriteOffHash"
}
if (-not $AllowUnpinnedOutput -and $scrollSpriteOffHash -ne $expectedScrollSpriteOffProxy) {
    throw "Scroll-and-sprite-disabled reproducible build drift: $scrollSpriteOffHash"
}
foreach ($entry in @(
    @('fast-only', $fastOnlyHash, $expectedFastOnlyProxy),
    @('fast-terrain', $scrollSpriteOffFastHash, $expectedScrollSpriteOffFastProxy),
    @('icon-off-terrain', $scrollSpriteOffIconOffHash, $expectedScrollSpriteOffIconOffProxy),
    @('icon-off-fast-terrain', $scrollSpriteOffFastIconOffHash, $expectedScrollSpriteOffFastIconOffProxy),
    @('icon-only', $iconOnlyHash, $expectedIconOnlyProxy),
    @('fast-icon', $fastIconHash, $expectedFastIconProxy)
)) {
    if (-not $AllowUnpinnedOutput -and $entry[1] -ne $entry[2]) {
        throw "$($entry[0]) reproducible build drift: $($entry[1])"
    }
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

$menuFocusConversionTests = Join-Path $combined 'menu_focus_conversion_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/I' + $PSScriptRoot),
    ('/Fe' + $menuFocusConversionTests),
    (Join-Path $repositoryTests 'native_menu_focus_conversion.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Menu focus conversion tests build failed: $LASTEXITCODE" }
& $menuFocusConversionTests
if ($LASTEXITCODE -ne 0) { throw "Menu focus conversion tests failed: $LASTEXITCODE" }

$menuShaderMarkerTests = Join-Path $combined 'menu_shader_marker_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/I' + $PSScriptRoot),
    ('/Fe' + $menuShaderMarkerTests),
    (Join-Path $repositoryTests 'native_menu_shader_marker.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO','d3d11.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Menu shader marker tests build failed: $LASTEXITCODE" }
& $menuShaderMarkerTests
if ($LASTEXITCODE -ne 0) { throw "Menu shader marker tests failed: $LASTEXITCODE" }

$introBackingTests = Join-Path $combined 'intro_backing_repair_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/I' + $PSScriptRoot),
    ('/Fe' + $introBackingTests),
    (Join-Path $repositoryTests 'native_intro_backing_repair.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Intro backing repair tests build failed: $LASTEXITCODE" }
& $introBackingTests
if ($LASTEXITCODE -ne 0) { throw "Intro backing repair tests failed: $LASTEXITCODE" }

$introMorphTests = Join-Path $combined 'intro_morph_guard_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/I' + $PSScriptRoot),
    ('/Fe' + $introMorphTests),
    (Join-Path $repositoryTests 'native_intro_morph_guard.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Intro morph guard tests build failed: $LASTEXITCODE" }
& $introMorphTests
if ($LASTEXITCODE -ne 0) { throw "Intro morph guard tests failed: $LASTEXITCODE" }

$dustIdentityTests = Join-Path $combined 'dust_draw_identity_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/I' + $dustFidelitySource),
    ('/Fe' + $dustIdentityTests),
    (Join-Path $repositoryTests 'native_dust_draw_identity.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Dust draw identity tests build failed: $LASTEXITCODE" }
& $dustIdentityTests
if ($LASTEXITCODE -ne 0) { throw "Dust draw identity tests failed: $LASTEXITCODE" }

$battleIconAtlasTests = Join-Path $combined 'battle_icon_atlas_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/I' + $PSScriptRoot),
    ('/Fe' + $battleIconAtlasTests),
    (Join-Path $repositoryTests 'native_battle_icon_atlas.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Battle icon atlas tests build failed: $LASTEXITCODE" }
& $battleIconAtlasTests
if ($LASTEXITCODE -ne 0) { throw "Battle icon atlas tests failed: $LASTEXITCODE" }

$campaignPanTests = Join-Path $combined 'campaign_pan_core_tests.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $campaignPanTests),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\campaign_pan_core_tests.c'),
    (Join-Path $PSScriptRoot 'campaign_pan_core.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Campaign pan tests build failed: $LASTEXITCODE" }
& $campaignPanTests
if ($LASTEXITCODE -ne 0) { throw "Campaign pan tests failed: $LASTEXITCODE" }

$fastMovesTests = Join-Path $combined 'native_fast_computer_moves.exe'
& $clangCl @(
    '/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX',
    ('/Fe' + $fastMovesTests),
    (Join-Path $repositoryTests 'native_fast_computer_moves.c'),
    '/link','/MACHINE:X86','/BREPRO','/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Fast computer moves tests build failed: $LASTEXITCODE" }
& $fastMovesTests
if ($LASTEXITCODE -ne 0) { throw "Fast computer moves tests failed: $LASTEXITCODE" }

$manifest = [ordered]@{
    schema = 'mtw.combined-proxy-r185.v1'
    status = if ($AllowUnpinnedOutput) { 'development_candidate' } else { 'accepted_release' }
    accepted_control_sha256 = $controlHash
    combined_inert_sha256 = $combinedHash
    scroll_off_sha256 = $scrollOffHash
    sprite_off_sha256 = $spriteOffHash
    scroll_sprite_off_sha256 = $scrollSpriteOffHash
    fast_only_sha256 = $fastOnlyHash
    scroll_sprite_off_fast_sha256 = $scrollSpriteOffFastHash
    scroll_sprite_off_icon_off_sha256 = $scrollSpriteOffIconOffHash
    scroll_sprite_off_fast_icon_off_sha256 = $scrollSpriteOffFastIconOffHash
    icon_only_sha256 = $iconOnlyHash
    fast_icon_sha256 = $fastIconHash
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
    menu_focus_conversion_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $menuFocusConversionTests).Hash
    menu_focus_conversion_tests_exit = 0
    menu_shader_marker_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $menuShaderMarkerTests).Hash
    menu_shader_marker_tests_exit = 0
    intro_backing_repair_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $introBackingTests).Hash
    intro_backing_repair_tests_exit = 0
    intro_morph_guard_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $introMorphTests).Hash
    intro_morph_guard_tests_exit = 0
    dust_draw_identity_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $dustIdentityTests).Hash
    dust_draw_identity_tests_exit = 0
    battle_icon_atlas_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $battleIconAtlasTests).Hash
    battle_icon_atlas_tests_exit = 0
    campaign_pan_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $campaignPanTests).Hash
    campaign_pan_tests_exit = 0
    fast_computer_moves_tests_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $fastMovesTests).Hash
    fast_computer_moves_tests_exit = 0
    fast_computer_moves_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'campaign_fast_moves.c')).Hash
    fast_computer_moves_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'campaign_fast_moves_core.h')).Hash
    campaign_pan_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'campaign_pan_core.c')).Hash
    frontend_fix_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'frontend_fix.c')).Hash
    credits_flash_fix_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'credits_flash_fix.c')).Hash
    credits_flash_fix_header_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'credits_flash_fix.h')).Hash
    executable_variants_header_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot '../../../include/mtw_exe_variants.h')).Hash
    combined_proxy_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'combined_proxy.c')).Hash
    feature_only_proxy_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'campaign_fast_moves_proxy.c')).Hash
    battle_icon_atlas_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'battle_icon_atlas_core.h')).Hash
    battle_icon_atlas_fix_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'battle_icon_atlas_fix.c')).Hash
    battle_icon_atlas_fix_header_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'battle_icon_atlas_fix.h')).Hash
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
    menu_focus_conversion_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'menu_focus_conversion_core.h')).Hash
    menu_focus_shader_bytecode_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'menu_focus_shader_bytecode.h')).Hash
    menu_focus_shader_marker_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'menu_focus_shader_marker.h')).Hash
    intro_backing_repair_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'intro_backing_repair_core.h')).Hash
    intro_morph_guard_core_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'intro_morph_guard_core.h')).Hash
    menu_focus_pixel_aligned_vs_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'menu_focus_pixel_aligned_vs.hlsl')).Hash
    dust_draw_identity_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $dustFidelitySource 'dust_draw_identity.h')).Hash
    accepted_inputs = $expected
    accepted_source_root = 'vendor/r185/accepted-dust-source'
    accepted_control_backend_sha256 = $expectedControlBackend
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'BUILD_MANIFEST.json') -Encoding utf8
$manifest | ConvertTo-Json -Depth 5
