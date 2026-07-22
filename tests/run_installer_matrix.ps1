[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SupportedGameExecutable
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$python = (Get-Command python.exe -ErrorAction Stop).Source
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$artifactDirectory = Join-Path $root "artifacts\tests\installer-matrix-$stamp"
New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null

$env:MTW_TEST_GAME_EXE = [System.IO.Path]::GetFullPath($SupportedGameExecutable)
$env:MTW_RUN_COMPILED_INSTALLER_TESTS = '1'

& $python -m pytest (Join-Path $PSScriptRoot 'test_compiled_installer.py') -q --junitxml (Join-Path $artifactDirectory 'junit.xml')
$exitCode = $LASTEXITCODE

$record = [ordered]@{
    schema = 'unofficial-medieval-total-war-patch-installer-matrix-v1'
    completed_utc = [DateTime]::UtcNow.ToString('o')
    installer = (Join-Path $root 'dist\Unofficial Medieval Total War Collection Patch.exe')
    installer_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root 'dist\Unofficial Medieval Total War Collection Patch.exe')).Hash
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $SupportedGameExecutable).Hash
    pytest_exit_code = $exitCode
    result = $(if ($exitCode -eq 0) { 'pass' } else { 'fail' })
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $artifactDirectory 'result.json') -Encoding UTF8
Write-Output "Installer matrix artifacts: $artifactDirectory"
exit $exitCode
