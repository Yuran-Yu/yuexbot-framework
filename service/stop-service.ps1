$ErrorActionPreference = "SilentlyContinue"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$pidFile = Join-Path $root "service.pid"

if (Test-Path $pidFile) {
  $pidValue = Get-Content $pidFile
  if ($pidValue) {
    Stop-Process -Id $pidValue -Force
  }
  Remove-Item $pidFile -Force
}

Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -like "*yuexbot-service*" -or $_.CommandLine -like "*service\\server.js*" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

Write-Host "YuexBot service stopped."
