[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'x86')]
    [string[]]$Platform = @('x64', 'x86'),

    [ValidateSet('en', 'zh')]
    [string[]]$Language = @('en', 'zh'),

    [string]$Toolset
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio 2022 or newer with the Desktop development with C++ workload.'
}

$installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json |
    ConvertFrom-Json | Select-Object -First 1
if (-not $installation) {
    throw 'Visual Studio with MSBuild and the C++ x86/x64 tools was not found.'
}

$msbuild = Join-Path $installation.installationPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not $Toolset) {
    $Toolset = if (([version]$installation.installationVersion).Major -ge 18) { 'v145' } else { 'v143' }
}

$solution = Join-Path $PSScriptRoot 'src\ReshadeEffectShaderToggler.sln'
foreach ($architecture in $Platform) {
    $projectPlatform = if ($architecture -eq 'x86') { 'Win32' } else { 'x64' }
    $extension = if ($architecture -eq 'x86') { 'addon32' } else { 'addon64' }
    $artifact = "ReshadeEffectShaderToggler.$extension"

    foreach ($locale in $Language) {
        Write-Host "Building $Configuration $architecture ($locale)"
        & $msbuild $solution /m:1 /nr:false /nologo /v:m /t:Build "/p:Configuration=$Configuration" "/p:Platform=$architecture" "/p:PlatformToolset=$Toolset" "/p:RestLanguage=$locale"
        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild failed for $Configuration $architecture ($locale) with exit code $LASTEXITCODE."
        }

        $buildConfiguration = if ($locale -eq 'zh') { "$Configuration-zh" } else { $Configuration }
        $binary = Join-Path $PSScriptRoot "src\$projectPlatform\$buildConfiguration\$artifact"
        $destination = Join-Path $PSScriptRoot "dist\$Configuration\$locale"
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -LiteralPath $binary -Destination $destination -Force
    }
}

