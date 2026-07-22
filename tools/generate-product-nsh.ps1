[CmdletBinding()]
param(
    [string]$OutputPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $root 'include\product.nsh'
}
$productPath = Join-Path $root 'config\product.json'
$product = Get-Content -LiteralPath $productPath -Raw -Encoding UTF8 | ConvertFrom-Json

function ConvertTo-NsisLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return $Value.Replace('$', '$$').Replace('"', '$\"')
}

$defines = [ordered]@{
    PRODUCT_NAME = [string]$product.product_name
    PRODUCT_SETUP_CAPTION = [string]$product.setup_caption
    PRODUCT_VERSION = [string]$product.version
    PRODUCT_VERSION_QUAD = [string]$product.version_quad
    PRODUCT_OUTPUT_FILENAME = [string]$product.output_filename
    PRODUCT_COMPANY = [string]$product.company_name
    PRODUCT_COPYRIGHT = [string]$product.copyright
    PRODUCT_COMPONENT_NAME = [string]$product.component_name
    PRODUCT_DISCORD_URL = [string]$product.discord_url
    PRODUCT_KOFI_URL = [string]$product.kofi_url
}

$lines = @('; Generated from config/product.json. Do not edit by hand.')
foreach ($entry in $defines.GetEnumerator()) {
    $lines += ('!define {0} "{1}"' -f $entry.Key, (ConvertTo-NsisLiteral $entry.Value))
}
$lines += ''

$directory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $directory -Force | Out-Null
[System.IO.File]::WriteAllLines(
    [System.IO.Path]::GetFullPath($OutputPath),
    $lines,
    (New-Object System.Text.UTF8Encoding($false))
)
Write-Output "Generated $OutputPath"
