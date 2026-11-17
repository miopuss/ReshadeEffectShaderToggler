param (
    [Parameter(Mandatory = $true)]
    [string]$Version
)

$normalizedVersion = $Version.Trim()

if ($normalizedVersion.StartsWith("v", [System.StringComparison]::OrdinalIgnoreCase)) {
    $normalizedVersion = $normalizedVersion.Substring(1)
}

if ($normalizedVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must use MAJOR.MINOR.PATCH format, for example 1.0.0."
}

$parts = $normalizedVersion.Split(".")
$numbers = @()

foreach ($part in $parts) {
    $value = [int]$part
    if ($value -lt 0 -or $value -gt 65535) {
        throw "Each version component must be between 0 and 65535."
    }
    $numbers += $value
}

$major, $minor, $patch = $numbers

$versionFile = @"
#pragma once
#define REST_VERSION $major,$minor,$patch,0
#define REST_VERSION_STRING "$major.$minor.$patch"
"@

Set-Content -Path (Join-Path $PSScriptRoot "version.h") -Value $versionFile -Encoding UTF8
