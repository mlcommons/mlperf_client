<#
.SYNOPSIS
    [setup time sync with NTP server - run as Administrator]
.EXAMPLE
    PS> .\setup_timeSync.ps1
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

$ntpAddress = "time.google.com"
# $ntpAddress = "0.it.pool.ntp.org 1.it.pool.ntp.org 2.it.pool.ntp.org 3.it.pool.ntp.org"

# Requires elevated privileges
Write-Host "======================================================="
Write-Host "  Turn off the time service..."
Stop-Service -Name w32time

#Write-Host "======================================================================"
Write-Host "  Set the SNTP (Simple Network Time Protocol) source for the time server..."
w32tm /config /syncfromflags:manual /manualpeerlist:$ntpAddress
# If using WiFi:
#w32tm /config /syncfromflags:manual /manualpeerlist:"time.google.com"
# example of a list of ntp servers
# w32tm /config /syncfromflags:manual /manualpeerlist:"0.it.pool.ntp.org 1.it.pool.ntp.org 2.it.pool.ntp.org 3.it.pool.ntp.org"

Write-Host "  and then turn on the time service back on..."
Start-Service -Name w32time

Write-Host "  Tell the time sync service to use the changes..."
w32tm /config /update

Write-Host "  Reset the local computer's time against the time server..."
# Uncomment if you want to force rediscovery
# w32tm /resync /force /rediscover
w32tm /resync /rediscover

Write-Host "======================================================="