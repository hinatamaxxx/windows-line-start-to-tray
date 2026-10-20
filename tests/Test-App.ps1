param([Parameter(Mandatory)][string]$App)
# Exercises autostart, migration from 0.2.0 and uninstall cleanup in an isolated test root.
# LINETRAY_TEST_ROOT redirects the Startup/Programs folders, the data folder and the
# registry keys to HKCU\Software\LineTrayTest\<guid>, which is removed afterwards.
$ErrorActionPreference = 'Stop'
$App = (Resolve-Path $App).Path
$id = [Guid]::NewGuid().ToString('N')
$root = Join-Path ([IO.Path]::GetTempPath()) "LineTrayTest\$id"
$reg = "HKCU:\Software\LineTrayTest\$id\Microsoft\Windows\CurrentVersion"
$old = $env:LINETRAY_TEST_ROOT
function Invoke-App([string]$command) {
    $out = Join-Path $root 'result.txt'
    if (Test-Path $out) { Remove-Item $out }
    $process = Start-Process -FilePath $App -ArgumentList @('--test', $command, "`"$out`"") -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "$command exited with $($process.ExitCode)" }
    (Get-Content -LiteralPath $out -Raw).Trim()
}
function Assert($condition, [string]$message) { if (!$condition) { throw "FAIL: $message" } }
function Approved([byte]$first) { [byte[]](@($first) + (,0 * 11)) }
try {
    New-Item -ItemType Directory -Path $root, "$root\Data", "$root\Startup", "$root\Programs" -Force | Out-Null
    $env:LINETRAY_TEST_ROOT = $root
    $link = Join-Path $root 'Startup\LINEを最小化して起動.lnk'

    # Enable / disable through the shortcut; the StartupApproved value is only read.
    Assert ((Invoke-App 'status') -eq 'Off line=Off') 'starts off'
    Assert ((Invoke-App 'enable') -eq 'On') 'enable creates the shortcut'
    Assert (Test-Path -LiteralPath $link) 'shortcut exists'
    $shell = New-Object -ComObject WScript.Shell
    $target = $shell.CreateShortcut($link)
    Assert ($target.TargetPath -eq $App -and $target.Arguments -eq '--startup') 'shortcut starts the app with --startup'
    New-Item -Path "$reg\Explorer\StartupApproved\StartupFolder" -Force | Out-Null
    New-ItemProperty -Path "$reg\Explorer\StartupApproved\StartupFolder" -Name 'LINEを最小化して起動.lnk' -Value (Approved 3) -PropertyType Binary -Force | Out-Null
    Assert ((Invoke-App 'status') -eq 'DisabledByWindows line=Off') 'an odd StartupApproved byte means turned off in Windows'
    Set-ItemProperty -Path "$reg\Explorer\StartupApproved\StartupFolder" -Name 'LINEを最小化して起動.lnk' -Value (Approved 2)
    Assert ((Invoke-App 'status') -eq 'On line=Off') 'an even byte means on'
    Assert ((Get-ItemProperty "$reg\Explorer\StartupApproved\StartupFolder").'LINEを最小化して起動.lnk'[0] -eq 2) 'the app did not rewrite StartupApproved'
    Assert ((Invoke-App 'disable') -eq 'Off') 'disable removes the shortcut'
    Assert (!(Test-Path -LiteralPath $link)) 'shortcut removed'

    # LINE's own autostart state is read from Run + StartupApproved\Run.
    New-Item -Path "$reg\Run", "$reg\Explorer\StartupApproved\Run" -Force | Out-Null
    New-ItemProperty -Path "$reg\Run" -Name LINE -Value '"C:\LINE\bin\LineLauncher.exe" --booting' -Force | Out-Null
    Assert ((Invoke-App 'status') -eq 'Off line=On') 'LINE autostart on'
    New-ItemProperty -Path "$reg\Explorer\StartupApproved\Run" -Name LINE -Value (Approved 3) -PropertyType Binary -Force | Out-Null
    Assert ((Invoke-App 'status') -eq 'Off line=Off') 'LINE autostart off in Windows'

    # Migration from 0.2.0: Run entry enabled -> shortcut, old values and files removed.
    $oldHome = Join-Path $root 'LocalAppData\LineTrayStartup'
    New-ItemProperty -Path "$reg\Run" -Name WindowsLineStartToTray -Value "`"$oldHome\LineTrayStart.exe`"" -Force | Out-Null
    New-ItemProperty -Path "$reg\Explorer\StartupApproved\Run" -Name WindowsLineStartToTray -Value (Approved 2) -PropertyType Binary -Force | Out-Null
    foreach ($file in 'LineTrayStart.exe','Setup.exe','startup-backup.json','diagnostic.log') { Set-Content -LiteralPath (Join-Path "$root\Data" $file) -Value 'x' }
    $menu = Join-Path $root 'Programs\Windows版LINEを通知領域で起動.lnk'
    $oldLink = $shell.CreateShortcut($menu); $oldLink.TargetPath = "$oldHome\Setup.exe"; $oldLink.Save()
    Assert ((Invoke-App 'migrate') -eq 'On') 'enabled old entry becomes the shortcut'
    $run = Get-ItemProperty "$reg\Run"
    Assert ($null -eq $run.WindowsLineStartToTray) 'old Run value removed'
    Assert ($run.LINE -like '*LineLauncher.exe*') "LINE's own Run value untouched"
    $approved = Get-ItemProperty "$reg\Explorer\StartupApproved\Run"
    Assert ($null -eq $approved.WindowsLineStartToTray) 'old StartupApproved value removed'
    Assert ($approved.LINE[0] -eq 3) "LINE's StartupApproved value untouched"
    Assert (!(Test-Path "$root\Data\LineTrayStart.exe") -and !(Test-Path "$root\Data\Setup.exe") -and !(Test-Path "$root\Data\startup-backup.json")) 'old program files removed'
    Assert (Test-Path "$root\Data\diagnostic.log") 'diagnostic log kept'
    Assert (!(Test-Path -LiteralPath $menu)) 'old Start menu shortcut to Setup.exe removed'

    # A disabled old entry migrates without creating the shortcut.
    Invoke-App 'disable' | Out-Null
    New-ItemProperty -Path "$reg\Run" -Name WindowsLineStartToTray -Value "`"$oldHome\LineTrayStart.exe`"" -Force | Out-Null
    New-ItemProperty -Path "$reg\Explorer\StartupApproved\Run" -Name WindowsLineStartToTray -Value (Approved 3) -PropertyType Binary -Force | Out-Null
    Assert ((Invoke-App 'migrate') -eq 'Off') 'disabled old entry stays off'
    Assert ($null -eq (Get-ItemProperty "$reg\Run").WindowsLineStartToTray) 'disabled old Run value removed'

    # Rename from "Windows版LINEを通知領域で起動": the old Startup shortcut keeps its on/off state.
    Invoke-App 'disable' | Out-Null
    $oldStartup = Join-Path $root 'Startup\Windows版LINEを通知領域で起動.lnk'
    $made = $shell.CreateShortcut($oldStartup); $made.TargetPath = "$root\LocalAppData\WindowsLineStartToTray\current\WindowsLineStartToTray.exe"; $made.Save()
    Assert ((Invoke-App 'migrate') -eq 'On') 'old-named Startup shortcut becomes the new one'
    Assert (!(Test-Path -LiteralPath $oldStartup)) 'old-named Startup shortcut removed'
    Invoke-App 'disable' | Out-Null
    $made = $shell.CreateShortcut($oldStartup); $made.TargetPath = "$root\LocalAppData\WindowsLineStartToTray\current\WindowsLineStartToTray.exe"; $made.Save()
    New-ItemProperty -Path "$reg\Explorer\StartupApproved\StartupFolder" -Name 'Windows版LINEを通知領域で起動.lnk' -Value (Approved 3) -PropertyType Binary -Force | Out-Null
    Assert ((Invoke-App 'migrate') -eq 'Off') 'old shortcut turned off in Windows stays off'
    Assert (!(Test-Path -LiteralPath $oldStartup)) 'old-named Startup shortcut removed when off'

    # Uninstall cleanup: keep or remove the data folder.
    Invoke-App 'enable' | Out-Null
    Assert ((Invoke-App 'cleanup-keep') -eq 'left=0') 'cleanup keeping data'
    Assert (!(Test-Path -LiteralPath $link) -and (Test-Path "$root\Data\diagnostic.log")) 'shortcut removed, data kept'
    Assert ((Invoke-App 'cleanup-all') -eq 'left=0') 'cleanup removing data'
    Assert (!(Test-Path "$root\Data")) 'data folder removed'
    Assert ((Invoke-App 'decision') -eq 'taken=True') 'uninstall decision handed to the hook'
    Write-Output 'PASS: shortcut autostart, StartupApproved read-only, LINE state, 0.2.0 migration (on/off), rename migration (on/off), uninstall cleanup (keep/remove), decision handoff.'
} finally {
    $env:LINETRAY_TEST_ROOT = $old
    Remove-Item "HKCU:\Software\LineTrayTest\$id" -Recurse -Force -ErrorAction SilentlyContinue
    if (!(Get-ChildItem 'HKCU:\Software\LineTrayTest' -ErrorAction SilentlyContinue)) { Remove-Item 'HKCU:\Software\LineTrayTest' -Force -ErrorAction SilentlyContinue }
    Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path "HKCU:\Software\LineTrayTest\$id") { throw 'Test registry key was not removed.' }
}
