param([string]$Setup = (Join-Path $PSScriptRoot '..\release\WindowsLineStartToTray-Setup-0.2.0-preview.7.exe'))
$ErrorActionPreference = 'Stop'
$setupPath = (Resolve-Path $Setup).Path
$resultPath = Join-Path (Split-Path $setupPath) ('context-' + [Guid]::NewGuid().ToString('N') + '.txt')
try {
    Start-Process -FilePath $setupPath -ArgumentList @('--check-context', ('"' + $resultPath + '"')) -WindowStyle Hidden -Wait | Out-Null
    $deadline = (Get-Date).AddSeconds(15)
    # Bounded test-only wait for the relayed process; the application has no polling.
    while (!(Test-Path -LiteralPath $resultPath)) {
        if ((Get-Date) -ge $deadline) { throw 'The relayed setup did not create its result.' }
        Start-Sleep -Milliseconds 100
    }
    $result = Get-Content -LiteralPath $resultPath -Raw
    if ($result -notmatch '(?m)^SystemProcessProvider=relayed\r?$' -or $result -notmatch '(?m)^SandboxPackage=\r?$') { throw 'Setup retained the sandbox package context.' }
    Write-Output 'PASS: setup launched through the system provider without sandbox package context.'
} finally { if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath } }
