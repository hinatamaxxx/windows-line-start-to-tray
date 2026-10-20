param([Parameter(Mandatory)][string]$Output, [string]$Preview)
# Draws the app icon: a green chat bubble whose tail points at a tray icon on a taskbar.
# Every size is stored as PNG. The artwork fills 10..246 of 256 (about 4% margin on each
# side), matching other Windows apps in Start and the taskbar.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore, WindowsBase

function Brush([string]$top, [string]$bottom) {
    $brush = New-Object Windows.Media.LinearGradientBrush
    $brush.StartPoint = '0,0'; $brush.EndPoint = '0,1'
    $brush.GradientStops.Add((New-Object Windows.Media.GradientStop([Windows.Media.ColorConverter]::ConvertFromString($top), 0)))
    $brush.GradientStops.Add((New-Object Windows.Media.GradientStop([Windows.Media.ColorConverter]::ConvertFromString($bottom), 1)))
    $brush
}

function Render([int]$size) {
    $visual = New-Object Windows.Media.DrawingVisual
    $draw = $visual.RenderOpen()
    # Map the artwork (10..246) into a margin of 4%, but at least 1 px at small sizes,
    # where antialiased edges would otherwise touch the border.
    $margin = [math]::Max(1, [math]::Round($size * 10 / 256.0))
    $scale = ($size - 2 * $margin) / 236.0
    $offset = $margin - 10 * $scale
    $draw.PushTransform((New-Object Windows.Media.MatrixTransform($scale, 0, 0, $scale, $offset, $offset)))
    $small = $size -le 24
    # Taskbar
    $draw.DrawRoundedRectangle((Brush '#4A4F55' '#2C3035'), $null, (New-Object Windows.Rect(10, 182, 236, 64)), 22, 22)
    if (!$small) {
        $chevron = New-Object Windows.Media.Pen((New-Object Windows.Media.SolidColorBrush([Windows.Media.ColorConverter]::ConvertFromString('#C9CED4'))), 9)
        $chevron.StartLineCap = 'Round'; $chevron.EndLineCap = 'Round'; $chevron.LineJoin = 'Round'
        $draw.DrawGeometry($null, $chevron, [Windows.Media.Geometry]::Parse('M40,222 L54,208 L68,222'))
    }
    # Tray icon the bubble points at
    $draw.DrawRoundedRectangle((Brush '#5BE59A' '#21B865'), $null, (New-Object Windows.Rect(168, 196, 40, 36)), 10, 10)
    # Chat bubble with its tail towards the tray icon
    $bubble = [Windows.Media.Geometry]::Parse('M58,10 L198,10 Q242,10 242,54 L242,112 Q242,156 198,156 L204,156 L190,188 L166,156 L58,156 Q14,156 14,112 L14,54 Q14,10 58,10 Z')
    $draw.DrawGeometry((Brush '#4BE08F' '#14A85A'), $null, $bubble)
    $white = [Windows.Media.Brushes]::White
    $dot = if ($small) { 20 } else { 15 }
    foreach ($x in 80, 128, 176) { $draw.DrawEllipse($white, $null, (New-Object Windows.Point($x, 83)), $dot, $dot) }
    $draw.Pop()
    $draw.Close()
    $bitmap = New-Object Windows.Media.Imaging.RenderTargetBitmap($size, $size, 96, 96, [Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $bitmap
}

function Png($bitmap) {
    $encoder = New-Object Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = New-Object IO.MemoryStream; $encoder.Save($stream); $bytes = $stream.ToArray(); $stream.Dispose(); $bytes
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$images = foreach ($size in $sizes) {
    $bitmap = Render $size
    if ($Preview -and $size -eq 256) { [IO.File]::WriteAllBytes($Preview, (Png $bitmap)) }
    , @($size, (Png $bitmap))
}
$stream = [IO.File]::Create($Output)
$writer = New-Object IO.BinaryWriter($stream)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $size = $image[0]; $data = $image[1]
    $writer.Write([byte]($size % 256)); $writer.Write([byte]($size % 256)); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$data.Length); $writer.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($image in $images) { $writer.Write([byte[]]$image[1]) }
$writer.Dispose()
