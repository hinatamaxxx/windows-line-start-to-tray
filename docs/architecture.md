# 実装の説明

## 全体像

Windows版LINEを通知領域で起動は、LINE本体をディスク上で書き換えず、起動したプロセスのメモリに表示制御を追加する補助ツールです。

| ファイル | 役割 |
| --- | --- |
| `LineTrayStart.exe` | LINE本来のランチャーを、補助DLLを読み込む状態で起動する |
| `LineTrayHook32.dll` | 32bitのランチャーの接続画面を抑え、LINE本体へ補助DLLの読み込みを引き継ぐ |
| `LineTrayHook64.dll` | 64bitのLINE本体で、起動時の表示要求と通知領域アイコンの登録を処理する |
| `Setup.exe` | セットアップ・復元・手動起動の画面を提供する |
| `Install.ps1` / `Uninstall.ps1` | 導入時と復元時だけ、自動起動の登録を変更する |

ソースはそれぞれ [Launcher.cpp](../native/Launcher.cpp)、[Hook.cpp](../native/Hook.cpp)、[Setup.cs](../setup/Setup.cs)です。セットアップ画面はWPFで作り、[SetupWindow.xaml](../setup/SetupWindow.xaml)にレイアウトと配色、[SetupWindow.cs](../setup/SetupWindow.cs)に操作と状態表示を定義しています。32bitと64bitのDLLは同じソースを異なる対象アーキテクチャでビルドします。

## セットアップの隔離を避ける処理

Codexの実行環境から起動した子プロセスでは、レジストリとAppDataへの書き込みがCodex専用の領域へ転送されていました。そこで読み返しても変更済みに見えるため、Windowsが実際に使う設定とファイルを確認できていませんでした。

セットアップEXEは、最初にWMIの `Win32_Process.Create` で自分自身を通常のWindowsプロセスとして起動し直します。この処理はセットアップ画面の起動時だけ行い、再起動を繰り返さないための引数を付けています。実行ファイルの保存先には `GetFinalPathNameByHandle` で取得した実際のパスを使い、元のEXEが隔離領域にある場合にも対応します。PowerShell子プロセスにはWindows PowerShellの標準モジュールパスを明示します。

レジストリ操作は [StartupRegistry.ps1](../StartupRegistry.ps1) で、システムの `StdRegProv` を使って `HKEY_USERS\<現在のユーザーSID>` 内の実際の値を読み書きします。各戻り値と変更後の値を確認します。セットアップ済みかどうかの表示も、同じ実レジストリを参照します。

実機の確認では、`Win32_StartupCommand` と `StdRegProv` で起動先を読み取り、`CIM_DataFile` と通常プロセスからのファイル確認で、実際のLocalAppDataへ補助EXEとDLLが存在することを確かめています。`GetCurrentPackageFullName` はこの環境で隔離されたプロセスでも「パッケージなし」を返したため、隔離判定には使っていません。

## 起動から通知領域まで

preview.7では、ランチャーが作る `SPLASH` クラスの接続画面にも表示制御を適用します。2026年10月4日の実機観測で、旧版では `LineLauncher.exe` のこの画面が表示されることを確認しました。`CreateWindowExW` の `WS_VISIBLE` を外し、`ShowWindow` / `ShowWindowAsync` / `SetWindowPos` の表示要求も抑えます。対象はランチャー内の専用クラスに限定し、接続処理や他のウィンドウには変更を加えません。

1. Windowsのユーザー別スタートアップから `LineTrayStart.exe` を起動します。
2. `DetourCreateProcessWithDllExW` で `LineLauncher.exe --booting` を起動します。補助DLLは、アプリの通常の処理が始まる前に読み込まれます。
3. ランチャー・LINE本体・更新関連プロセスの `ShellExecuteExW` / `ShellExecuteW` / `CreateProcessW` / `CreateProcessA` をフックし、`LINE.exe`、`LineLauncher.exe`、`LineUpdater.exe`、`LineAppMgr.exe` の起動にDLLを引き継ぎます。他の名前の実行ファイルは元のAPIへ渡します。32bitと64bitをまたぐ起動にはDetoursの対応機能を利用します。実際の32bit更新プログラムで使われているANSI版の `CreateProcessA` にも対応します。
4. LINE本体の `CreateWindowExW`、`ShowWindow`、`ShowWindowAsync`、`SetWindowPos` をフックします。起動中のQtのトップレベルウィンドウについて、作成時の `WS_VISIBLE`、表示呼び出し、`SWP_SHOWWINDOW` を抑制します。
5. 起動画面は非表示のまま初期化を続けます。タイトルが `LINE` のメインウィンドウでは、最初の表示要求を抑えた後に `WM_CLOSE` を送ります。LINE自身が通常の「閉じる」処理を行い、通知領域へ移ります。
6. 閉じる処理の完了後、起動時の表示抑制を解除します。通知領域アイコンをユーザーがクリックした場合も解除するため、以後のユーザー操作を受け付けます。

Qtのクラス名やメインウィンドウの識別はLINEの内部実装に依存します。LINEの更新後も必ず同じ動作になるとは限りません。

## 通知領域アイコン

`Shell_NotifyIconW` のアイコン登録・更新時に、LINEの実行ファイルから取得したアイコンを設定します。クリックの通知先など、ほかの登録情報は引き継ぎます。LINEの画像や実行ファイルは配布物に含めません。

固定したアイコンに置き換えるため、LINEがアイコンに描画する未読バッジ等が反映されない場合があります。

## 監視ループが不要な理由

表示APIや通知領域アイコンのAPIが呼ばれたタイミングだけ処理します。一定間隔でプロセスやウィンドウを調べる常駐処理、秒数を決めた遅延処理はありません。起動用EXEはランチャー起動後に終了し、補助DLLはLINEのプロセス内で動作します。

セットアップ画面には通常のWindowsイベント処理があり、テスト用の表示観測にもイベント処理を使います。これらは、起動後のLINEを定期監視する処理ではありません。

## セットアップで変更する内容

- `%LOCALAPPDATA%\LineTrayStartup` に本ツールのファイルを配置します。
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` に `WindowsLineStartToTray` を登録し、本ツールの `LineTrayStart.exe` を指定します。
- `Explorer\StartupApproved\Run` で `WindowsLineStartToTray` を有効（先頭バイト2）、`LINE` を無効（先頭バイト3）にします。LINEが自分のRun値を書き換えても、専用の起動項目は維持されます。
- preview.4/.5で `LINE` の起動先を補助EXEに変更していた場合は、バックアップの起動先に戻します。旧項目 `LineTrayStartup` は削除します。登録項目は分けますが、有効な起動経路は本ツールの1つです。
- 変更前の関連する値を `startup-backup.json` に保存します。
- スタートメニューに設定画面へのショートカットを作ります。
- 旧版のタスク `LINE startup to tray` が存在する場合は削除します。

設定を元に戻すと、保存していたLINEの起動コマンドとスタートアップ状態を復元します。旧版のバックアップも引き継ぎます。復元済みバックアップは別名で保存し、後日再導入した際は、その時点の設定を新しく記録します。

起動用EXEは、LINEランチャーのフォルダーを作業ディレクトリに指定します。Windowsサインイン時の作業ディレクトリに依存させません。また、起動用EXE自身が `helper-start`、`launcher-created` または `launcher-create-failed` をUTC日時付きで記録するため、補助EXEの起動とDLLの読み込みを分けて診断できます。

LINEの認証ファイル、会話データ、認証関連のレジストリは変更しません。PowerShellは導入・復元時だけ非表示で実行し、PC起動時には使用しません。

## 実際に確認したこと

- Windows 11 Home 23H2 x64、Microsoft StoreからインストールしたLINE 26.4.2.3957で確認。
- LINEを通常終了後、補助EXEで起動。最初の20秒間のWindows表示イベント監視でLINEのウィンドウ表示を検出しないことを確認。
- 起動画面とメインウィンドウの表示抑制、アイコン登録を診断ログで確認。
- 利用者がアイコンのダブルクリックによる再表示とログイン維持を確認。
- セットアップの導入、繰り返し実行、設定の復元、再導入、ショートカットを独立したテスト領域で確認。

2026年9月30日、再起動後のLINEに補助DLLが読み込まれない問題を確認しました。v0.2.0-preview.4で起動項目を変更しても、設定とファイルがCodex専用の隔離領域へ保存され、実際のWindowsには適用されていませんでした。v0.2.0-preview.5で通常プロセスへの起動し直しと実レジストリの操作を追加し、実ファイルと実際の起動項目を確認しています。

**同日、利用者が実機で修正後のPC再起動を行い、LINEのウィンドウが表示されず、通知領域にLINEのアイコンが表示されることを確認しました。** 今回の再起動後の確認では、アイコンのダブルクリックによる再表示とログイン維持は再確認していません。

2026年10月1日の再起動では、補助EXEと旧LINEにDLLが読み込まれた後、自動更新で起動した26.5.0.3975のLINEにはDLLがありませんでした。旧版はLINE本体の子プロセス起動をフックせず、更新プログラムも対象外でした。さらに実際の `LINE` のRun値は通常のランチャーへ戻っていました。preview.6では子プロセス起動のフックをLINE・更新プロセスにも適用し、独立した起動項目へ移行しています。

更新経路のテスト用アプリで、32bitランチャー → 64bit LINE → 64bit管理プロセス → 32bit更新プログラム → 64bit更新後LINEを起動し、ANSI版・Unicode版のプロセス作成を経由しても初回表示の抑制と後からの再表示ができることを確認しました。これは実際のLINE更新を再実行したテストとは区別しています。

## 参考

- [Microsoft Detours](https://github.com/microsoft/Detours)
- [DetourCreateProcessWithDllEx](https://github.com/microsoft/Detours/wiki/DetourCreateProcessWithDllEx)
- [Using Detours](https://github.com/microsoft/Detours/wiki/Using-Detours)
- [Microsoft: Flexible virtualization](https://learn.microsoft.com/en-us/windows/msix/desktop/flexible-virtualization)
- [Microsoft: StdRegProv](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/regprov/stdregprov)
