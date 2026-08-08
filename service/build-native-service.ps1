$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$jadebot = Split-Path -Parent $root
$repo = Split-Path -Parent $jadebot
$compiler = Get-ChildItem -Path "D:\*\*\mingw64\bin\g++.exe" -ErrorAction SilentlyContinue |
  Select-Object -First 1 -ExpandProperty FullName
$outDir = Join-Path $root "release\YuexBotService-v0.1.0"

if (-not $compiler -or -not (Test-Path $compiler)) {
  throw "Compiler not found. Please install MinGW x64 or update build-native-service.ps1."
}

New-Item -ItemType Directory -Path $outDir -Force | Out-Null

& $compiler -O2 -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 `
  -I"$jadebot" -I"$repo\third_party" `
  "$root\native\YuexBotService.cpp" `
  -mwindows -static -lws2_32 -lole32 -loleaut32 -lcomctl32 -lgdi32 -luser32 -lkernel32 -lshell32 `
  -o "$outDir\YuexBotService.exe"

Copy-Item "$jadebot\JadeView_x64.dll" "$outDir\JadeView_x64.dll" -Force
Copy-Item "$root\README.md" "$outDir\README.md" -Force
Copy-Item "$root\docs" "$outDir\docs" -Recurse -Force

Write-Host "Built: $outDir\YuexBotService.exe"
