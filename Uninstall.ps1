param([string]$RegistryPrefix = '')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\StartupRegistry.ps1"
if (!$RegistryPrefix -and $env:CODEX_WINDOWS_SANDBOX_PACKAGE_FAMILY) { throw 'セットアップEXEから設定を元に戻してください。Windowsの実際の保存先へ適用する必要があります。' }
$target = Join-Path $env:LOCALAPPDATA 'LineTrayStartup'
$OutputEncoding = [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$approved = $RegistryPrefix + 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
$runKey = $RegistryPrefix + 'Software\Microsoft\Windows\CurrentVersion\Run'
$backupPath = Join-Path $target 'startup-backup.json'
if (!(Test-Path $backupPath)) { throw '自動起動設定のバックアップが見つかりません。' }
$backup = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
if ($backup.PSObject.Properties['Run']) {
    if ($null -ne $backup.Run) {
        Set-StartupRegistryValue -Key $runKey -Name LINE -Kind String -Value $backup.Run
    } else {
        Remove-StartupRegistryValue -Key $runKey -Name LINE
    }
}
if ($null -ne $backup.HelperRun) {
    Set-StartupRegistryValue -Key $runKey -Name LineTrayStartup -Kind String -Value $backup.HelperRun
} else {
    Remove-StartupRegistryValue -Key $runKey -Name LineTrayStartup
}
if ($null -ne $backup.HelperApproved) {
    Set-StartupRegistryValue -Key $approved -Name LineTrayStartup -Kind Binary -Value ([Convert]::FromBase64String($backup.HelperApproved))
} else {
    Remove-StartupRegistryValue -Key $approved -Name LineTrayStartup
}
if ($null -ne $backup.ToolRun) {
    Set-StartupRegistryValue -Key $runKey -Name WindowsLineStartToTray -Kind String -Value $backup.ToolRun
} else {
    Remove-StartupRegistryValue -Key $runKey -Name WindowsLineStartToTray
}
if ($null -ne $backup.ToolApproved) {
    Set-StartupRegistryValue -Key $approved -Name WindowsLineStartToTray -Kind Binary -Value ([Convert]::FromBase64String($backup.ToolApproved))
} else {
    Remove-StartupRegistryValue -Key $approved -Name WindowsLineStartToTray
}
if ($null -ne $backup.Approved) {
    Set-StartupRegistryValue -Key $approved -Name LINE -Kind Binary -Value ([Convert]::FromBase64String($backup.Approved))
} else {
    Remove-StartupRegistryValue -Key $approved -Name LINE
}
# A later installation must capture the user's then-current settings again.
Move-Item -LiteralPath $backupPath -Destination (Join-Path $target 'startup-backup.restored.json') -Force
Write-Output 'LINE本来の自動起動設定に戻しました。LINEを通知領域から終了して起動し直すと、補助処理も外れます。'
