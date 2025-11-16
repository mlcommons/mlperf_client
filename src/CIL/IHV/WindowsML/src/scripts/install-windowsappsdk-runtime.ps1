param(
  [string]$Version = '1.8',
  [string[]]$Architectures = @('x64','arm64')
)

$ErrorActionPreference = 'Stop'

function Ensure-Winget {
  $winget = (Get-Command winget -ErrorAction SilentlyContinue)
  if (-not $winget) {
    throw "winget is not available on this machine. Install App Installer from Microsoft Store to enable winget."
  }
  return $winget.Source
}

function Is-RuntimeInstalled([string]$ver) {
  try {
    $out = winget list --id Microsoft.WindowsAppRuntime.$ver -e --accept-source-agreements 2>$null
    return ($LASTEXITCODE -eq 0 -and $out)
  } catch { return $false }
}

$wingetPath = Ensure-Winget
Write-Host "Using winget: $wingetPath"

foreach ($arch in $Architectures) {
  Write-Host "Installing Windows App SDK Runtime $Version ($arch) if missing..."
  $already = Is-RuntimeInstalled $Version
  if ($already) {
    Write-Host "Windows App Runtime $Version appears installed. Skipping $arch."
    continue
  }
  winget install --id Microsoft.WindowsAppRuntime.$Version -e --architecture $arch --silent --accept-package-agreements --accept-source-agreements
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to install Windows App Runtime $Version ($arch) via winget. Exit code $LASTEXITCODE"
  }
}

Write-Host "Windows App SDK Runtime installation complete."


