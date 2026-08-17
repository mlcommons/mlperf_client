param(
  [string]$Version = $env:WINML_VERSION,
  [string]$OutputDir = "$PSScriptRoot\..\.nuget\packages",
  [string]$Source = $env:WINML_NUGET_SOURCE
)

$ErrorActionPreference = 'Stop'

if (-not $Source) {
  $Source = 'https://api.nuget.org/v3/index.json'
}

$nugetCliRoot = (Split-Path -Parent $OutputDir)
if (-not $nugetCliRoot) { $nugetCliRoot = "$PSScriptRoot\..\.nuget" }
$nugetCliDir = Join-Path $nugetCliRoot 'cli'

$nuget = & "$PSScriptRoot\ensure-nuget.ps1" -Destination $nugetCliDir -Quiet
Write-Host "Using nuget: $nuget"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Self-contained WinML runtime (winmd, import libs, headers, runtime DLLs).
$argsList = @('install', 'Microsoft.Windows.AI.MachineLearning',
  '-OutputDirectory', $OutputDir,
  '-ExcludeVersion',
  '-NonInteractive',
  '-Source', $Source,
  '-DirectDownload')
if ($Version -and ($Version -ne 'latest')) {
  $argsList = @('install', 'Microsoft.Windows.AI.MachineLearning', '-Version', $Version) + $argsList[2..($argsList.Length - 1)]
}

& $nuget @argsList

if ($LASTEXITCODE -ne 0) {
  throw "NuGet install failed with exit code $LASTEXITCODE"
}

Write-Host "Installed Microsoft.Windows.AI.MachineLearning under $OutputDir\Microsoft.Windows.AI.MachineLearning"
