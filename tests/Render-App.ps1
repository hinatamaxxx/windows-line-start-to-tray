param([Parameter(Mandatory)][string]$App, [Parameter(Mandatory)][string]$Output,
    [ValidateSet('Main','Uninstall','UninstallFromWindows')][string]$View = 'Main', [ValidateSet('Light','Dark')][string]$Theme = 'Light', [switch]$ToggleOn, [string]$Version, [double]$Scale = 1)
# Renders a window of the app to a PNG without showing it on screen, for visual checks.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
$assembly = [Reflection.Assembly]::LoadFrom((Resolve-Path $App).Path)
$flags = [Reflection.BindingFlags]'Instance,NonPublic,Public'
if (![Windows.Application]::Current) { $null = New-Object Windows.Application }
if ($View -eq 'Main') { $window = [Activator]::CreateInstance($assembly.GetType('LineTray.MainWindow')) }
else { $window = $assembly.GetType('LineTray.UninstallDialog').GetConstructors($flags)[0].Invoke(@($View -eq 'UninstallFromWindows')) }
$themeType = $assembly.GetType('LineTray.Theme')
$themeType.GetMethod('Apply', [Reflection.BindingFlags]'Static,NonPublic').Invoke($null, @($window.Resources, ($Theme -eq 'Dark'))) | Out-Null
$window.ShowInTaskbar = $false
$window.WindowStartupLocation = [Windows.WindowStartupLocation]::Manual
$window.Left = -32000; $window.Top = -32000
$window.Show()
# For README images: show the update card as an installed, up-to-date copy would.
if ($Version) {
    $window.FindName('UpdateTitle').Text = "バージョン $Version"
    $window.FindName('UpdateText').Text = '最新のバージョンです。サインイン時にも確認し、新しいバージョンがあれば自動で更新します。'
    $window.FindName('CheckButton').IsEnabled = $true
    $window.FindName('VersionText').Text = "バージョン $Version"
}
if ($ToggleOn) { $window.FindName('AutoStartToggle').IsChecked = $true; Start-Sleep -Milliseconds 400 }
$window.UpdateLayout()
$frame = New-Object Windows.Threading.DispatcherFrame
[Windows.Threading.Dispatcher]::CurrentDispatcher.BeginInvoke([Windows.Threading.DispatcherPriority]::ApplicationIdle, [Action]{ $frame.Continue = $false }) | Out-Null
[Windows.Threading.Dispatcher]::PushFrame($frame)
$content = $window.Content
$bitmap = New-Object Windows.Media.Imaging.RenderTargetBitmap([int]($content.ActualWidth * $Scale), [int]($content.ActualHeight * $Scale), (96 * $Scale), (96 * $Scale), [Windows.Media.PixelFormats]::Pbgra32)
$backdrop = New-Object Windows.Media.DrawingVisual
$context = $backdrop.RenderOpen()
$context.DrawRectangle($window.Background, $null, (New-Object Windows.Rect(0, 0, $content.ActualWidth, $content.ActualHeight)))
$context.Close()
$bitmap.Render($backdrop)
$bitmap.Render($content)
$encoder = New-Object Windows.Media.Imaging.PngBitmapEncoder
$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
$stream = [IO.File]::Create($Output); $encoder.Save($stream); $stream.Dispose()
$window.Close()
