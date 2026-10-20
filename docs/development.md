# ビルドとテスト

## 必要なもの

- Windows x64、Windows PowerShell 5.1、Git
- Visual Studio 2022（Build Toolsでも可）の「C++によるデスクトップ開発」
- .NET SDK 8以降（設定アプリのビルドと、インストーラーを作る `vpk` の実行に使います。設定アプリ自体は .NET Framework 4.8 向けです）

`build.cmd` と `tests/BuildFixture.cmd` はVisual Studio 2022 Communityの標準インストール先を使用します。別の場所やBuild Toolsへ導入している場合は `VSROOT` を設定してください。

## 構成

| 場所 | 内容 |
| --- | --- |
| `native/` | 起動用EXE（`LineTrayStart.exe`）とフックDLL（`LineTrayHook32.dll` / `LineTrayHook64.dll`）。Detoursを使うC++ |
| `app/` | 設定アプリ（`LineMinimizedStart.exe`）。.NET Framework 4.8のWPFで、Velopackの導入・更新・削除の処理も持ちます |
| `setup/` | アイコン（`app.ico`）とその作成台本、同梱する使い方（`GUIDE.txt`） |
| `tests/` | 模擬テストと画面の描画台本 |
| `Build-Release.ps1` | すべてをビルドし、Velopackでインストーラーと更新用のファイルを作ります |

## 配布ファイルをビルドする

```powershell
git clone --recurse-submodules https://github.com/hinatamaxxx/windows-line-start-to-tray.git
cd windows-line-start-to-tray
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build-Release.ps1
```

`release` に次のファイルを出力します。リリースにはすべてをAssetsとして添付します。

| ファイル | 用途 |
| --- | --- |
| `WindowsLineStartToTray-Setup-<version>.exe` | 利用者がダウンロードするセットアップ |
| `WindowsLineStartToTray-<version>-full.nupkg` | 更新用のパッケージ |
| `releases.win.json`、`RELEASES`、`assets.win.json` | Velopackが更新の確認に使う一覧 |
| `SHA256SUMS.txt` | セットアップEXEの照合用 |

`-Version` で版を、`-OutputDir` で出力先を変えられます。すでにビルド済みのネイティブファイルを使う場合は `-SkipNativeBuild` を指定します。`vpk`（Velopackのコマンド）は `dotnet-tools.json` で1.2.161に固定しています。

アイコンを描き直すときは、`setup/Create-Icon.ps1 -Output setup\app.ico` を実行します。16〜256pxの各サイズをPNG形式で格納し、256pxのとき上下左右に10px（約4%）の余白を取ります。

Microsoft Detoursはv4.0.1のコミット `e4bfd6b03e50de46b47abfbd1e46b384f0c5f833` に固定しています。

## 設定アプリのテスト

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\Test-App.ps1 -App .\build\app\LineMinimizedStart.exe
```

自動起動のオン/オフ、Windowsでオフにしたときの判定、LINE本体の自動起動の判定、旧版（Runへの登録）と旧名のショートカットからの移行、アンインストール時の削除（記録を残す/すべて削除）を確認します。環境変数 `LINETRAY_TEST_ROOT` で、スタートアップとスタートメニューのフォルダー、記録フォルダー、レジストリ（`HKCU\Software\LineTrayTest\<GUID>`）をテスト用の場所に切り替えます。テスト後にはテスト用のレジストリを削除し、削除できたことを確認します。

画面は次の台本でPNGに描画して確認します。`-View` に `Main`、`Uninstall`、`UninstallFromWindows`、`-Theme` に `Light` か `Dark` を指定します。README用の画像は `-ToggleOn -Version <版> -Scale 2` で作ります。

```powershell
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File .\tests\Render-App.ps1 -App .\build\app\LineMinimizedStart.exe -Output main.png
```

インストール前に更新の流れを試すときは、環境変数 `LINETRAY_UPDATE_FEED` に、`Build-Release.ps1 -OutputDir` で作った別の版のフォルダーを指定します。

## 表示制御のテスト

`tests/Fixture.cpp` と `tests/BuildFixture.cmd` に、32bitランチャーから64bitアプリを起動するテスト用アプリがあります。起動画面、初回メイン画面、閉じる処理、その後の再表示を確認するために使用します。

```powershell
cmd.exe /c tests\BuildFixture.cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\Test-Fixture.ps1
```

通常の起動（メイン画面がクロークされた状態で表示され、閉じたあとにクロークが解除されること）とログアウト状態の起動（最大化ボタンのないログイン画面）に加え、LINE → 管理プロセス → 32bit更新プログラム → 更新後の64bit LINEという経路を模擬します。`ShellExecuteExW`、`CreateProcessW`、`CreateProcessA`を経由したDLLの引き継ぎと、補助DLLが記録フォルダーへ写されてから読み込まれることを確認します。実際のLINE、ユーザーの起動設定、認証データは変更しません。

通知領域アイコンの合成は次のテストで確認します。LINEの画像は使わず、色のある正方形・灰色の正方形・オレンジの印付きの正方形をテスト用に作り、接続済み・接続中・未読ありと判定されることと、未読のときだけ印が重なることを確かめます。ロゴには本ツールのアイコンを使います。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\Test-TrayIcon.ps1
```

LINEがアイコンのデザインを変えたときは、記録フォルダー（`%LOCALAPPDATA%\LineTrayStartup`）に `tray-dump.on` という空のファイルを置いてからLINEを起動します。LINEが渡したアイコンと本ツールが登録したアイコンが、`tray-*-line.bmp` と `tray-*-tool.bmp` として保存されます。`build\TrayIconTest.exe <LINE.exeのパス> <出力先> <保存したBMP>...` で、保存した画像を入力に合成を試せます。調べ終わったら `tray-dump.on` を削除してください。LINEの画像はリポジトリに入れないでください。

実際のLINEの表示を観測する場合は、LINEを通知領域から通常終了した後に、次を実行します。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\Observe-LineStartup.ps1
```

このテストはインストール済みの起動用EXEからLINEを起動し、20秒間のWindows表示イベントと、終了時点のウィンドウ状態を出力します。LINEのログイン状態や会話内容は検査しません。PC再起動テストの代わりにはなりません。

## AIエージェントから実機に導入するとき

Codexなど、AIエージェントのアプリから起動したプロセスでは、`%LOCALAPPDATA%` への書き込みやレジストリがそのアプリ専用の領域へ転送されることがあります。そのままセットアップEXEを実行すると、本体がWindowsから見えない場所に入ります。実機に導入するときは、WMIの `Win32_Process.Create` でセットアップEXEを通常のプロセスとして起動し、結果は `CIM_DataFile` と `StdRegProv` で確かめてください。

## 新しい版を出すとき

`Build-Release.ps1` と `app/WindowsLineStartToTray.csproj` のバージョン、READMEのダウンロードリンク、`docs/release-notes.md` を同じ版へ更新します。ビルド出力、診断ログ、LINE本体はGitへ追加しません。
