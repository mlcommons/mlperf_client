param(
  [string]$Destination = "$PSScriptRoot\..\.nuget\cli",
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Write-Info($msg) { if (-not $Quiet) { Write-Host "[nuget] $msg" } }

# 1) Use explicit env var if provided
if ($env:NUGET_EXE -and (Test-Path $env:NUGET_EXE)) {
  Write-Info "Using NUGET_EXE: $env:NUGET_EXE"
  return (Resolve-Path $env:NUGET_EXE).Path
}

# 2) Try to find on PATH via Get-Command
$cmd = Get-Command nuget.exe -ErrorAction SilentlyContinue
if ($cmd -and (Test-Path $cmd.Source)) {
  Write-Info "Found on PATH: $($cmd.Source)"
  return (Resolve-Path $cmd.Source).Path
}

# 3) Download a local copy
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$nugetExe = Join-Path $Destination 'nuget.exe'
if (-not (Test-Path $nugetExe)) {
  $url = 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe'
  Write-Info "Downloading: $url"
  Invoke-WebRequest -Uri $url -OutFile $nugetExe -UseBasicParsing
}

Write-Info "Ready: $nugetExe"
(Resolve-Path $nugetExe).Path


