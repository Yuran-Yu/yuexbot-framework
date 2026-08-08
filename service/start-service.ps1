$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$node = (Get-Command node -ErrorAction Stop).Source
$pidFile = Join-Path $root "service.pid"

if (Test-Path $pidFile) {
  $oldPid = Get-Content $pidFile -ErrorAction SilentlyContinue
  if ($oldPid -and (Get-Process -Id $oldPid -ErrorAction SilentlyContinue)) {
    Write-Host "YuexBot service is already running: http://127.0.0.1:8787"
    exit 0
  }
}

$out = Join-Path $root "service.out.log"
$err = Join-Path $root "service.err.log"
$process = Start-Process -FilePath $node -ArgumentList "server.js" -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
Set-Content -Path $pidFile -Value $process.Id -Encoding ASCII
Write-Host "YuexBot service started: http://127.0.0.1:8787"
