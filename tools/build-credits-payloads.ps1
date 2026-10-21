[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'vendor\r185\source'
$output = [IO.Path]::GetFullPath($OutputDirectory)
$scaffold = Join-Path $output 'scaffold'
$zoom = Join-Path $output 'zoom'
$products = Join-Path $output 'payloads'
New-Item -ItemType Directory -Force -Path $output,$products | Out-Null

& (Join-Path $source 'build_scaffold.ps1') -OutputDirectory $scaffold -AllowUnpinnedOutput -CreditsFlashFix 1 |
    Out-File -LiteralPath (Join-Path $output 'scaffold.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "Credits source build failed: $LASTEXITCODE" }
& (Join-Path $PSScriptRoot 'build-zoom-payloads.ps1') -OutputDirectory $zoom -CreditsFlashFix 1 |
    Out-File -LiteralPath (Join-Path $output 'zoom.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "Credits Zoom build failed: $LASTEXITCODE" }

$dev = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1'
$clang = Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'
if (-not (Test-Path -LiteralPath $dev) -or -not (Test-Path -LiteralPath $clang)) {
    throw 'The x86 Visual Studio and clang-cl toolchain is required.'
}
& $dev -Arch x86 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$bridge = Join-Path $output 'credits-only-bridge.obj'
& $clang /nologo /TC --target=i686-pc-windows-msvc /O2 /W4 /WX /c `
    /DMTW_ENABLE_FAST_MOVES=0 /DMTW_ENABLE_ZOOM=0 /DMTW_ENABLE_BATTLE_ICON_FIX=0 `
    /DMTW_ENABLE_CREDITS_FLASH_FIX=1 ('/Fo' + $bridge) `
    (Join-Path $source 'campaign_fast_moves_proxy.c')
if ($LASTEXITCODE -ne 0) { throw 'Credits-only bridge compilation failed.' }
$creditsOnly = Join-Path $products 'D3D9-credits-only.dll'
& $clang /nologo --target=i686-pc-windows-msvc /LD ('/Fe' + $creditsOnly) `
    $bridge (Join-Path $scaffold 'combined-r154\credits_flash_fix.obj') `
    /link ('/DEF:' + (Join-Path $source 'combined_proxy.def')) `
    /MACHINE:X86 /BREPRO /INCREMENTAL:NO advapi32.lib user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Credits-only proxy link failed.' }

$nonZoom = @(
    @{name='scroll-sprite-off'; terrain=$true; fast=$false; icon=$true},
    @{name='scroll-sprite-off-fast'; terrain=$true; fast=$true; icon=$true},
    @{name='fast-only'; terrain=$false; fast=$true; icon=$false},
    @{name='scroll-sprite-off-icon-off'; terrain=$true; fast=$false; icon=$false},
    @{name='scroll-sprite-off-fast-icon-off'; terrain=$true; fast=$true; icon=$false},
    @{name='icon-only'; terrain=$false; fast=$false; icon=$true},
    @{name='fast-icon'; terrain=$false; fast=$true; icon=$true}
)
$sourceInputs = [ordered]@{}
$inputFiles = @(Get-ChildItem -LiteralPath $source,(Join-Path $root 'vendor\dust-fidelity') -File -Recurse)
$inputFiles += Get-Item -LiteralPath (Join-Path $root 'include\mtw_exe_variants.h'), `
    (Join-Path $root 'tools\build-zoom-payloads.ps1'),$PSCommandPath
foreach ($inputFile in @($inputFiles | Sort-Object FullName -Unique)) {
    $relative = [IO.Path]::GetRelativePath($root, $inputFile.FullName).Replace('\','/')
    $sourceInputs[$relative] = [ordered]@{
        sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $inputFile.FullName).Hash
        length=$inputFile.Length
    }
}
$manifest = [ordered]@{
    schema='mtw-credits-payload-build-v1'
    status='development_candidate'
    executable_variant_table_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root 'include\mtw_exe_variants.h')).Hash
    credits_source_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $source 'credits_flash_fix.c')).Hash
    combined_bridge_source_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $source 'combined_proxy.c')).Hash
    feature_bridge_source_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $source 'campaign_fast_moves_proxy.c')).Hash
    scaffold_manifest_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $scaffold 'BUILD_MANIFEST.json')).Hash
    zoom_manifest_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $zoom 'zoom-build-manifest.json')).Hash
    source_inputs=$sourceInputs
    variants=@()
}
function Add-Variant([string]$name,[string]$sourceFile,[bool]$terrain,[bool]$fast,[bool]$icon,[bool]$zoomEnabled) {
    $target = Join-Path $products ('D3D9-' + $name + '-credits.dll')
    Copy-Item -LiteralPath $sourceFile -Destination $target -Force
    $file = Get-Item -LiteralPath $target
    $script:manifest.variants += [ordered]@{
        name=$name; terrain=$terrain; fast=$fast; icon=$icon; zoom=$zoomEnabled; credits=$true
        filename=$file.Name; length=$file.Length
        sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
    }
}
foreach ($v in $nonZoom) {
    Add-Variant $v.name (Join-Path $scaffold ('combined-r154\D3D9-' + $v.name + '.dll')) `
        $v.terrain $v.fast $v.icon $false
}
$zoomManifest = Get-Content -LiteralPath (Join-Path $zoom 'zoom-build-manifest.json') -Raw | ConvertFrom-Json
foreach ($v in $zoomManifest.variants) {
    Add-Variant $v.name (Join-Path $zoom ('payloads\D3D9-' + $v.name + '.dll')) `
        $v.terrain $v.fast $v.icon $true
}
$creditsFile = Get-Item -LiteralPath $creditsOnly
$manifest.variants += [ordered]@{
    name='credits-only'; terrain=$false; fast=$false; icon=$false; zoom=$false; credits=$true
    filename=$creditsFile.Name; length=$creditsFile.Length
    sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $creditsFile.FullName).Hash
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'credits-build-manifest.json') -Encoding utf8
$manifest | ConvertTo-Json -Depth 6
