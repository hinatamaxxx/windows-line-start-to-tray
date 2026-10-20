param([string]$Root = (Split-Path -Parent $MyInvocation.MyCommand.Path))
# Runs the tray icon composition test with the tool's own icon as the logo.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $Root
$out = Join-Path $repo 'build\tray-test'
New-Item -ItemType Directory -Path $out -Force | Out-Null
& (Join-Path $repo 'build\TrayIconTest.exe') (Join-Path $repo 'setup\app.ico') $out
if ($LASTEXITCODE -ne 0) { throw 'Tray icon composition test failed.' }
