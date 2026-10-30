[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet(0,1)][int]$CreditsFlashFix = 0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'vendor\r185\source'
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$scaffold = Join-Path $output 'scaffold'
& (Join-Path $source 'build_scaffold.ps1') -OutputDirectory $scaffold -AllowUnpinnedOutput -CreditsFlashFix $CreditsFlashFix |
    Out-File -LiteralPath (Join-Path $output 'scaffold.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "R185 source build failed: $LASTEXITCODE" }

$dev = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1'
$clang = Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'
if (-not (Test-Path -LiteralPath $dev) -or -not (Test-Path -LiteralPath $clang)) {
    throw 'The x86 Visual Studio and clang-cl toolchain is required.'
}
if (-not ($env:VSCMD_VER -and $env:VSCMD_ARG_TGT_ARCH -eq 'x86' -and
        $env:VSCMD_ARG_HOST_ARCH -eq 'x64')) {
    & $dev -Arch x86 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$objects = Join-Path $scaffold 'combined-r154'
$products = Join-Path $output 'payloads'
New-Item -ItemType Directory -Force -Path $products | Out-Null
$def = Join-Path $source 'combined_proxy.def'
$minhook = @('minhook_buffer.obj','minhook_hook.obj','minhook_trampoline.obj','minhook_hde32.obj')
$shared = @('campaign_pan_core.obj','frontend_epoch_core.obj','presentation_input_core.obj',
    'primary_origin_guard_core.obj','resolution_filter_core.obj','window_transition_guard_core.obj',
    'loading_shadow_core.obj','focus_span_core.obj','focus_plane_shadow_core.obj',
    'guarded_memory_copy.obj','reentrant_lock_patch_core.obj','mapper_shader_clone_core.obj',
    'mapper_activation_core.obj')

function Compile-Bridge([string]$name,[bool]$terrain,[int]$fast,[int]$icon) {
    $inputFile = Join-Path $source $(if ($terrain) { 'combined_proxy.c' } else { 'campaign_fast_moves_proxy.c' })
    $object = Join-Path $products ($name + '.obj')
    $compileArgs = @('/nologo','/TC','--target=i686-pc-windows-msvc','/O2','/W4','/WX','/c',
        '/DMTW_ENABLE_ZOOM=1',('/DMTW_ENABLE_FAST_MOVES=' + $fast),
        ('/DMTW_ENABLE_CREDITS_FLASH_FIX=' + $CreditsFlashFix))
    if (-not $terrain) { $compileArgs += ('/DMTW_ENABLE_BATTLE_ICON_FIX=' + $icon) }
    $compileArgs += '/Fo' + $object
    $compileArgs += $inputFile
    & $clang @compileArgs
    if ($LASTEXITCODE -ne 0) { throw "Zoom bridge compilation failed: $name" }
    return $object
}

$variants = @(
    @{name='scroll-sprite-off-zoom'; terrain=$true; fast=0; icon=1; frontend='frontend_fix_scroll_sprite_off.obj'},
    @{name='scroll-sprite-off-fast-zoom'; terrain=$true; fast=1; icon=1; frontend='frontend_fix_scroll_sprite_off.obj'},
    @{name='scroll-sprite-off-icon-off-zoom'; terrain=$true; fast=0; icon=0; frontend='frontend_fix_scroll_sprite_off_icon_off.obj'},
    @{name='scroll-sprite-off-fast-icon-off-zoom'; terrain=$true; fast=1; icon=0; frontend='frontend_fix_scroll_sprite_off_icon_off.obj'},
    @{name='zoom-only'; terrain=$false; fast=0; icon=0},
    @{name='fast-only-zoom'; terrain=$false; fast=1; icon=0},
    @{name='icon-only-zoom'; terrain=$false; fast=0; icon=1},
    @{name='fast-icon-zoom'; terrain=$false; fast=1; icon=1}
)
$sourceInputs = [ordered]@{}
$sourceFiles = @(Get-ChildItem -LiteralPath $source,(Join-Path $root 'vendor\dust-fidelity') -File -Recurse)
$sourceFiles += Get-Item -LiteralPath (Join-Path $root 'include\mtw_exe_variants.h'),$PSCommandPath
$sourceFiles += Get-Item -LiteralPath (Join-Path $root 'tools\build-feature-payloads.py'), `
    (Join-Path $root 'tools\runtime_platform.py'),(Join-Path $root 'tests\native_feature_proxy_smoke.c')
$rootPrefix = [IO.Path]::GetFullPath($root) + [IO.Path]::DirectorySeparatorChar
foreach ($item in @($sourceFiles | Sort-Object FullName -Unique)) {
    if (-not $item.FullName.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'A Zoom source input is outside the project root.'
    }
    $relative = $item.FullName.Substring($rootPrefix.Length).Replace('\','/')
    $sourceInputs[$relative] = [ordered]@{sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash; length=$item.Length}
}
$manifest = [ordered]@{schema='mtw-zoom-payload-build-v1'; source=(Resolve-Path $source).Path;
    source_inputs=$sourceInputs; variants=@()}
foreach ($variant in $variants) {
    $name = $variant.name
    if (-not $variant.terrain) {
        $suffix = if ($CreditsFlashFix) { '-credits' } else { '' }
        $candidate = Join-Path $objects ('feature-runtime\payloads\D3D9-' + $name + $suffix + '.dll')
        $dll = Join-Path $products ('D3D9-' + $name + '.dll')
        Copy-Item -LiteralPath $candidate -Destination $dll -Force
        $item = Get-Item -LiteralPath $dll
        $manifest.variants += [ordered]@{
            name=$name; fast=[bool]$variant.fast; icon=[bool]$variant.icon;
            terrain=$false; length=$item.Length;
            sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash
        }
        continue
    }
    $bridge = Compile-Bridge $name $variant.terrain $variant.fast $variant.icon
    $dll = Join-Path $products ('D3D9-' + $name + '.dll')
    $inputs = @($minhook | ForEach-Object { Join-Path $objects $_ }) + @(
        $bridge,
        (Join-Path $objects 'campaign_fast_moves.obj'),
        (Join-Path $objects 'credits_flash_fix.obj'))
    if ($variant.terrain) {
        $inputs += @('accepted_dust.obj','dust_565_bridge.obj',$variant.frontend) |
            ForEach-Object { Join-Path $objects $_ }
        if ($variant.icon) { $inputs += Join-Path $objects 'battle_icon_atlas_fix.obj' }
        $inputs += $shared | ForEach-Object { Join-Path $objects $_ }
        $libraries = @('bcrypt.lib','advapi32.lib','user32.lib','d3d11.lib','d3dcompiler.lib')
    } else {
        if ($variant.icon) { $inputs += Join-Path $objects 'battle_icon_atlas_fix.obj' }
        $libraries = @('advapi32.lib','user32.lib')
    }
    & $clang @(@('/nologo','--target=i686-pc-windows-msvc','/LD',('/Fe' + $dll)) +
        $inputs + @('/link',('/DEF:' + $def),'/MACHINE:X86','/BREPRO','/INCREMENTAL:NO') + $libraries)
    if ($LASTEXITCODE -ne 0) { throw "Zoom payload link failed: $name" }
    $item = Get-Item -LiteralPath $dll
    $manifest.variants += [ordered]@{
        name=$name; fast=[bool]$variant.fast; icon=[bool]$variant.icon;
        terrain=[bool]$variant.terrain; length=$item.Length;
        sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash
    }
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'zoom-build-manifest.json') -Encoding utf8
$manifest | ConvertTo-Json -Depth 5
