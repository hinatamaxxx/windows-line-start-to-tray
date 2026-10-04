param([string]$Root = (Join-Path $PSScriptRoot '..\build\fixture'))
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path
$rootPrefix = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\build\fixture'))
if ($Root -ne $rootPrefix) { throw 'The fixture must use its dedicated build directory.' }
$oldData = $env:LOCALAPPDATA
$oldUpdate = $env:LINE_TRAY_FIXTURE_UPDATE
$oldDone = $env:LINE_TRAY_FIXTURE_DONE
try {
    foreach ($file in 'LineTrayStart.exe','LineTrayHook32.dll','LineTrayHook64.dll') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\dist\$file") -Destination (Join-Path $Root "LineTrayStartup\$file") -Force
    }
    $env:LOCALAPPDATA = $Root
    foreach ($updateChain in @($false,$true)) {
        $env:LINE_TRAY_FIXTURE_UPDATE = if ($updateChain) { '1' } else { $null }
        $eventName = 'Local\LineTrayFixture-' + [Guid]::NewGuid().ToString('N')
        $env:LINE_TRAY_FIXTURE_DONE = $eventName
        $done = New-Object Threading.EventWaitHandle($false, [Threading.EventResetMode]::ManualReset, $eventName)
        try {
            $log = Join-Path $Root 'LineTrayStartup\diagnostic.log'
            if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
            $process = Start-Process -FilePath (Join-Path $Root 'LineTrayStartup\LineTrayStart.exe') -WindowStyle Hidden -Wait -PassThru
            if ($process.ExitCode -ne 0 -or !$done.WaitOne(20000)) { throw 'The fixture startup chain failed or timed out.' }
            if ([IO.File]::ReadAllText((Join-Path $Root 'fixture-chain-result.txt')).Trim() -ne 'exit=0') { throw 'Fixture chain reported failure.' }
            if ([IO.File]::ReadAllText((Join-Path $Root 'fixture-result.txt')).Trim() -ne 'splashHidden=1 initiallyHidden=1 closeHidden=1 reopened=1') { throw 'Display suppression or reopening failed.' }
            $events = [IO.File]::ReadAllText($log)
            if ($events -notmatch 'launcher-splash-suppressed') { throw 'Launcher connection splash was not covered.' }
            if ($events -match 'injection-failed') { throw 'A child injection failed.' }
            if ($updateChain -and ($events -notmatch 'updater-hook-attached' -or $events -notmatch 'ansi-child-injected' -or ([regex]::Matches($events,'line-hook-attached')).Count -ne 2)) {
                throw 'The updated LINE did not inherit the hook through both architectures and ANSI process creation.'
            }
            Write-Output "PASS: updateChain=$updateChain; splash and main hidden; close handled; user reopening works."
        } finally { $done.Dispose() }
    }
} finally {
    $env:LOCALAPPDATA = $oldData
    $env:LINE_TRAY_FIXTURE_UPDATE = $oldUpdate
    $env:LINE_TRAY_FIXTURE_DONE = $oldDone
}
