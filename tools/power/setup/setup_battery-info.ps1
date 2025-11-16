<#
.SYNOPSIS
    [Provide current status of the batery]
.EXAMPLE
    PS> .\setup_battery-info.ps1
	  > Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
#>
# === Initialization & Safety Check ===
# Ensure script is run as Administrator
if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Warning "This script must be run as Administrator. Exiting..."
    Start-Sleep -Seconds 2
    exit
}
# Optional: Set strict mode for better error handling
Set-StrictMode -Version Latest
# Optional: Timestamp for logging
$scriptStartTime = Get-Date
Write-Host "Script started at $scriptStartTime"

# === START ===

# Get battery information using WMI
$battery = Get-WmiObject -Class Win32_Battery
Write-Host "======================================================="

# Check if battery info is available
if ($battery) {
    $status = switch ($battery.BatteryStatus) {
        1 { "Discharging" }
        2 { "AC Power (Not Charging)" }
        3 { "Fully Charged" }
        4 { "Low" }
        5 { "Critical" }
        6 { "Charging" }
        7 { "Charging and High" }
        8 { "Charging and Low" }
        9 { "Charging and Critical" }
        10 { "Undefined" }
        11 { "Partially Charged" }
        default { "Unknown" }
    }

    Write-Host "  Battery Status: $status"
    Write-Host "  Charge Percentage: $($battery.EstimatedChargeRemaining)%"
} else {
    Write-Host "  No battery information available. This device may not have a battery."
}
Write-Host "======================================================="