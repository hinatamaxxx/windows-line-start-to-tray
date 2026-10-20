param([switch]$SkipNativeBuild, [string]$Version = '0.3.0-preview.1', [string]$OutputDir = 'release')
$ErrorActionPreference = 'Stop'
$version = $Version
Push-Location $PSScriptRoot
# Native tools use the process working directory, which Push-Location does not change.
$previousDirectory = [Environment]::CurrentDirectory
[Environment]::CurrentDirectory = $PSScriptRoot
try {
    if (!$SkipNativeBuild) {
        & cmd.exe /c (Join-Path $PSScriptRoot 'build.cmd')
        if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
    }
    $app = Join-Path $PSScriptRoot 'build\app'
    if (Test-Path -LiteralPath $app) { Remove-Item -LiteralPath $app -Recurse -Force }
    & dotnet build app\WindowsLineStartToTray.csproj -c Release -nologo -v q "-p:Version=$version" -o $app
    if ($LASTEXITCODE -ne 0) { throw 'App build failed.' }
    Copy-Item dist\LineTrayStart.exe, dist\LineTrayHook32.dll, dist\LineTrayHook64.dll -Destination $app
    Copy-Item setup\GUIDE.txt -Destination (Join-Path $app '使い方.txt')
    Copy-Item LICENSE -Destination (Join-Path $app 'LICENSE.txt')
    Copy-Item third_party\Detours\LICENSE.md -Destination (Join-Path $app 'Detours-LICENSE.txt')

    $release = Join-Path $PSScriptRoot $OutputDir
    if (Test-Path -LiteralPath $release) { Remove-Item -LiteralPath $release -Recurse -Force }
    & dotnet tool restore | Out-Null
    & dotnet vpk pack --packId WindowsLineStartToTray --packVersion $version --packDir $app `
        --mainExe LineMinimizedStart.exe --packTitle 'LINEを最小化して起動' --packAuthors hinatamaxxx `
        --runtime win-x64 --icon setup\app.ico --shortcuts StartMenuRoot --noPortable --releaseNotes docs\release-notes.md --outputDir $release
    if ($LASTEXITCODE -ne 0) { throw 'Velopack packaging failed.' }
    $setup = Join-Path $release "WindowsLineStartToTray-Setup-$version.exe"
    Move-Item (Join-Path $release 'WindowsLineStartToTray-win-Setup.exe') $setup
    $hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($setup))" | Set-Content (Join-Path $release 'SHA256SUMS.txt') -Encoding ASCII
    Get-ChildItem $release | ForEach-Object { $_.FullName }
} finally { [Environment]::CurrentDirectory = $previousDirectory; Pop-Location }
