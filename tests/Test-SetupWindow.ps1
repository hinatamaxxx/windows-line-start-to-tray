param([string]$Setup = (Join-Path $PSScriptRoot '..\release\WindowsLineStartToTray-Setup-0.2.0-preview.7.exe'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
$assembly = [Reflection.Assembly]::LoadFrom((Resolve-Path $Setup).Path)
$window = [Activator]::CreateInstance($assembly.GetType('SetupWindow'),$true)
try {
    $view = $window.Content
    $before = $view.Resources['WindowBrush'].Color.ToString()
    $view.FindName('ThemeButton').RaiseEvent((New-Object Windows.RoutedEventArgs([Windows.Controls.Button]::ClickEvent)))
    if ($before -eq $view.Resources['WindowBrush'].Color.ToString()) { throw 'Theme button did not switch the theme.' }
    $type = $window.GetType()
    $type.GetField('busy',[Reflection.BindingFlags]'Instance,NonPublic').SetValue($window,$true)
    $type.GetMethod('RefreshButtons',[Reflection.BindingFlags]'Instance,NonPublic').Invoke($window,$null)
    foreach ($name in 'InstallButton','LaunchButton','RestoreButton','CloseButton') {
        if ($view.FindName($name).IsEnabled) { throw "Action remains enabled while busy: $name" }
    }
    if ($view.FindName('Progress').Visibility -ne 'Visible') { throw 'Progress indicator is missing.' }
    $type.GetField('busy',[Reflection.BindingFlags]'Instance,NonPublic').SetValue($window,$false)
    $type.GetMethod('RefreshButtons',[Reflection.BindingFlags]'Instance,NonPublic').Invoke($window,$null)
    if (!$view.FindName('InstallButton').IsEnabled) { throw 'Primary action was not restored.' }
    if ($view.FindName('Progress').Visibility -ne 'Collapsed') { throw 'Progress indicator remains active.' }
    $window.ShowInTaskbar = $false
    $window.WindowStartupLocation = 'Manual'; $window.Left = -32000; $window.Top = -32000
    $window.Width = 680; $window.Height = 630
    $window.Show(); $window.UpdateLayout()
    if ($view.FindName('RestoreButton').ActualWidth -lt 80) { throw 'Restore action is clipped at minimum width.' }
    Write-Output 'PASS: theme button, busy-state controls, completion-state controls, minimum-size footer.'
} finally { $window.Close() }
