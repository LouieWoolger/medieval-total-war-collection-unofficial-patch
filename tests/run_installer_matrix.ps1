# Compatibility entry point; test.ps1 owns game validation and its evidence.
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

$ErrorActionPreference = 'Stop'
& (Join-Path (Split-Path -Parent $PSScriptRoot) 'test.ps1') @PSBoundParameters
