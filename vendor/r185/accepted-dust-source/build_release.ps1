[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$BackendSource = (Join-Path (Split-Path -Parent $PSScriptRoot) 'bin\common\dgVoodoo_D3D9.dll')
)

$ErrorActionPreference = 'Stop'
$vsDevShell = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1'
$clangCl = 'C:\Program Files\LLVM\bin\clang-cl.exe'
$expectedProxy = '34F855A17C10B6BBCEFD844B99A5B5A7F442DECDDFFDB0D898645FA2FF0B0F0C'
$expectedBackend = 'E36F5C8140EB6D1DC8F35E60AB231C07DFA2EB667F9CC0A909AC2D419DE078C6'

foreach ($required in @($vsDevShell, $clangCl, $BackendSource)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required build input/tool is missing: $required"
    }
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $BackendSource).Hash -ne $expectedBackend) {
    throw 'The preserved dgVoodoo backend does not match the pinned release input.'
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$proxy = Join-Path $OutputDirectory 'D3D9.dll'
$backend = Join-Path $OutputDirectory 'dgVoodoo_D3D9.dll'
$object = Join-Path $OutputDirectory 'd3d9_proxy.obj'
$testObject = Join-Path $OutputDirectory 'dust_cadence_tests.obj'
$testExe = Join-Path $OutputDirectory 'dust_cadence_tests.exe'
Remove-Item -LiteralPath $proxy, $backend, $object, $testObject, $testExe -Force -ErrorAction SilentlyContinue

& $vsDevShell -Arch x86 -HostArch amd64 -SkipAutomaticLocation
& $clangCl @(
    '/nologo', '/TC', '--target=i686-pc-windows-msvc', '/O2', '/W4', '/WX',
    ("/Fo" + $testObject), ("/Fe" + $testExe),
    (Join-Path $PSScriptRoot 'dust_cadence_tests.c'),
    '/link', '/MACHINE:X86', '/BREPRO', '/INCREMENTAL:NO'
)
if ($LASTEXITCODE -ne 0) { throw "Cadence model build failed: $LASTEXITCODE" }
& $testExe
if ($LASTEXITCODE -ne 0) { throw "Cadence model tests failed: $LASTEXITCODE" }

& $clangCl @(
    '/nologo', '/TC', '--target=i686-pc-windows-msvc', '/O2', '/W4', '/WX',
    '/DMTW_RELEASE_BUILD=1', '/DMTW_RELEASE_DENSE_HZ=15', '/DMTW_RELEASE_VIRTUAL_MANAGER_FPS=19',
    '/LD', ("/Fo" + $object), ("/Fe" + $proxy),
    (Join-Path $PSScriptRoot 'd3d9_proxy.c'),
    '/link', ("/DEF:" + (Join-Path $PSScriptRoot 'd3d9_proxy.def')),
    '/MACHINE:X86', '/BREPRO', '/INCREMENTAL:NO', 'bcrypt.lib', 'user32.lib'
)
if ($LASTEXITCODE -ne 0) { throw "Deterministic PE32 proxy build failed: $LASTEXITCODE" }

Remove-Item -LiteralPath $object, $testObject -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $BackendSource -Destination $backend -Force
$proxyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $proxy).Hash
$backendHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $backend).Hash
if ($proxyHash -ne $expectedProxy) { throw "Release proxy hash mismatch: $proxyHash" }
if ($backendHash -ne $expectedBackend) { throw "Release backend hash mismatch: $backendHash" }
Get-Item -LiteralPath $proxy, $backend
Get-FileHash -Algorithm SHA256 -LiteralPath $proxy, $backend

