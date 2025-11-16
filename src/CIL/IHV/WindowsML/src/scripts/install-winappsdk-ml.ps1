param(
  [string]$Version = $env:WINAPPSDK_ML_VERSION,
  [string]$OutputDir = "$PSScriptRoot\..\.nuget\packages"
)

$ErrorActionPreference = 'Stop'

$nugetCliRoot = (Split-Path -Parent $OutputDir)
if (-not $nugetCliRoot) { $nugetCliRoot = "$PSScriptRoot\..\.nuget" }
$nugetCliDir = Join-Path $nugetCliRoot 'cli'

$nuget = & "$PSScriptRoot\ensure-nuget.ps1" -Destination $nugetCliDir -Quiet
Write-Host "Using nuget: $nuget"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$argsList = @('install','Microsoft.WindowsAppSDK.ML',
  '-OutputDirectory', $OutputDir,
  '-ExcludeVersion',
  '-NonInteractive',
  '-Source', 'https://api.nuget.org/v3/index.json',
  '-DirectDownload')

if ($Version -and ($Version -ne 'latest')) {
  $argsList = @('install','Microsoft.WindowsAppSDK.ML','-Version', $Version) + $argsList[2..($argsList.Length-1)]
}

& $nuget @argsList

if ($LASTEXITCODE -ne 0) {
  throw "NuGet install failed with exit code $LASTEXITCODE"
}

$installedVersion = if ($Version -and ($Version -ne 'latest')) { $Version } else { 'latest' }
Write-Host "Installed Microsoft.WindowsAppSDK.ML $installedVersion under $OutputDir\Microsoft.WindowsAppSDK.ML"


