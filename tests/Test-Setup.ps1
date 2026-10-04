param([string]$Setup = (Join-Path $PSScriptRoot '..\release\WindowsLineStartToTray-Setup-0.2.0-preview.7.exe'))
$ErrorActionPreference = 'Stop'
$Setup = (Resolve-Path $Setup).Path
$id = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("LineTrayStartup-Test-$id")
$registryPath = "Software\LineTrayStartup.Tests\$id"
$testSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$physicalRegistryPath = $testSid + '\' + $registryPath
New-Item -ItemType Directory -Path $testRoot | Out-Null

function Remove-IsolatedRegistryTree([string]$Key) {
    if ($physicalRegistryPath -ne ($testSid + '\Software\LineTrayStartup.Tests\' + $id) -or
        $id -notmatch '^[a-f0-9]{32}$' -or
        !($Key -eq $physicalRegistryPath -or $Key.StartsWith($physicalRegistryPath + '\', [StringComparison]::OrdinalIgnoreCase))) {
        throw 'Refused to remove a registry key outside this test GUID.'
    }
    $arguments = @{ hDefKey = [uint32]2147483651; sSubKeyName = $Key }
    $children = Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName EnumKey -Arguments $arguments -ErrorAction Stop
    if ($children.ReturnValue -eq 2) { return }
    if ($children.ReturnValue -ne 0) { throw "Test key enumeration failed (Windows error $($children.ReturnValue))." }
    foreach ($child in $children.sNames) { Remove-IsolatedRegistryTree ($Key + '\' + $child) }
    $deleted = Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName DeleteKey -Arguments $arguments -ErrorAction Stop
    if ($deleted.ReturnValue -ne 0 -and $deleted.ReturnValue -ne 2) { throw "Test key deletion failed (Windows error $($deleted.ReturnValue))." }
}
try {
    $payload = Join-Path $testRoot 'payload'
    $process = Start-Process -FilePath $Setup -ArgumentList @('--extract', ('"' + $payload + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'Payload extraction failed.' }
    foreach ($name in 'LineTrayStart.exe','LineTrayHook32.dll','LineTrayHook64.dll') {
        if ((Get-FileHash "$payload\dist\$name").Hash -ne (Get-FileHash "$PSScriptRoot\..\dist\$name").Hash) { throw "Payload mismatch: $name" }
    }
    if (!(Test-Path -LiteralPath "$payload\StartupRegistry.ps1" -PathType Leaf)) { throw 'The physical registry helper was not embedded.' }
    if ((Get-FileHash "$payload\StartupRegistry.ps1").Hash -ne (Get-FileHash "$PSScriptRoot\..\StartupRegistry.ps1").Hash) { throw 'Registry helper payload mismatch.' }
    $powershell = "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe"
    & $powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\Exercise-Setup.ps1" -Payload $payload -DataRoot "$testRoot\data" -RegistryPrefix ($registryPath + '\')
    if ($LASTEXITCODE -ne 0) { throw 'Isolated install/restore test failed.' }
    $assembly = [Reflection.Assembly]::LoadFrom($Setup)
    $method = $assembly.GetType('Program').GetMethod('SaveSupportFiles', [Reflection.BindingFlags]'NonPublic,Static')
    $homePath = "$testRoot\data\LineTrayStartup"
    $shortcutPath = "$testRoot\setup.lnk"
    $shell = New-Object -ComObject WScript.Shell
    $legacyPath = "$testRoot\LINE Tray Startup.lnk"
    $legacy = $shell.CreateShortcut($legacyPath)
    $legacy.TargetPath = "$homePath\Setup.exe"
    $legacy.Save()
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($legacy)
    $method.Invoke($null, [object[]]@([string]$payload, [string]$homePath, [string]$Setup, [string]$shortcutPath)) | Out-Null
    if (Test-Path -LiteralPath $legacyPath) { throw 'Legacy shortcut was not migrated.' }
    $legacy = $shell.CreateShortcut($legacyPath)
    $legacy.TargetPath = "$testRoot\unrelated.exe"
    $legacy.Save()
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($legacy)
    $method.Invoke($null, [object[]]@([string]$payload, [string]$homePath, [string]$Setup, [string]$shortcutPath)) | Out-Null
    if (!(Test-Path -LiteralPath $legacyPath)) { throw 'Unrelated shortcut was removed.' }
    $shortcut = $shell.CreateShortcut($shortcutPath)
    if ($shortcut.TargetPath -ne "$homePath\Setup.exe") { throw 'Start menu shortcut target mismatch.' }
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut)
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
    Write-Output 'PASS: embedded payload hashes, setup shortcut, legacy migration, unrelated shortcut preserved.'
} finally {
    Remove-IsolatedRegistryTree $physicalRegistryPath
    $remaining = Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName EnumKey -Arguments @{ hDefKey = [uint32]2147483651; sSubKeyName = $physicalRegistryPath } -ErrorAction Stop
    if ($remaining.ReturnValue -ne 2) { throw 'The physical GUID test key remains after cleanup.' }
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $expected = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ("LineTrayStartup-Test-$id")))
    if ($resolved -eq $expected -and (Test-Path -LiteralPath $resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
