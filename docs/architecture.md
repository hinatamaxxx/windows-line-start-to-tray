# 実装の説明

## 全体像

LINEを最小化して起動は、LINE本体をディスク上で書き換えず、起動したプロセスのメモリに表示制御を追加する補助ツールです。

| ファイル | 役割 |
| --- | --- |
| `LineMinimizedStart.exe` | 設定画面。サインイン時の起動、自動起動の切り替え、更新、アンインストールも受け持つ |
| `LineTrayStart.exe` | LINE本来のランチャーを、補助DLLを読み込む状態で起動する |
| `LineTrayHook32.dll` | 32bitのランチャーの接続画面を抑え、LINE本体へ補助DLLの読み込みを引き継ぐ |
| `LineTrayHook64.dll` | 64bitのLINE本体で、起動時の表示要求と通知領域アイコンの登録を処理する |

ソースは、設定アプリが [app/](../app)、起動用EXEが [Launcher.cpp](../native/Launcher.cpp)、補助DLLが [Hook.cpp](../native/Hook.cpp) です。32bitと64bitのDLLは同じソースを異なる対象アーキテクチャでビルドします。設定アプリは .NET Framework 4.8 のWPFで作り、インストール・更新・アンインストールには [Velopack](https://github.com/velopack/velopack) を使います。

## 設定アプリ

| ソース | 内容 |
| --- | --- |
| [Program.cs](../app/Program.cs) | 起動の振り分け（設定画面、`--startup`、`--uninstall`）と、Velopackの導入・更新・削除時の処理 |
| [Startup.cs](../app/Startup.cs) | 自動起動のショートカットの作成・削除、Windowsでのオン/オフとLINE本体の自動起動の読み取り |
| [Migration.cs](../app/Migration.cs) | v0.2.0と旧名のショートカットからの移行 |
| [Cleanup.cs](../app/Cleanup.cs) | アンインストール時の削除と、確認画面での選択の受け渡し |
| [Updater.cs](../app/Updater.cs) | GitHubのReleasesからの更新 |
| [MainWindow.xaml](../app/MainWindow.xaml)、[UninstallDialog.xaml](../app/UninstallDialog.xaml)、[Styles.xaml](../app/Styles.xaml)、[Theme.cs](../app/Theme.cs) | 設定画面と確認画面。Windows 11の設定アプリに近い見た目で、ライト/ダークとアクセントカラーはWindowsの設定から読みます |

### 自動起動

スタートアップフォルダーに `LINEを最小化して起動.lnk` を置き、`LineMinimizedStart.exe --startup` を起動します。レジストリの `Run` は使いません。利用者がWindowsの「設定 → アプリ → スタートアップ」でオフにすると、Windowsは `Explorer\StartupApproved\StartupFolder` に記録します。設定アプリはこの値を読むだけで、書き換えません。先頭のバイトが奇数ならWindowsでオフ、値がないか偶数ならオンと判断します。

LINE本体の自動起動は `HKCU\...\Run` の `LINE` と `StartupApproved\Run` の `LINE` から判断します。オンなら設定画面に案内を出し、Windowsのスタートアップ設定を開くだけにします。

### サインイン時の処理

1. `--startup` で起動した設定アプリは、LINEが起動していなければ `LineTrayStart.exe` を起動します。
2. 旧版のファイルなど、前回消せなかったものの削除を試します。
3. GitHubのReleasesで新しいバージョンを確認し、あればダウンロードして、自分が終了したあとに入れ替えるようVelopackに頼みます（再起動はしません）。確認は最長3分で打ち切ります。

### インストール・更新・アンインストール

Velopackは `%LOCALAPPDATA%\WindowsLineStartToTray\` に導入し、「インストールされているアプリ」にアンインストール情報（`HKCU\...\Uninstall\WindowsLineStartToTray`）を登録します。本ツールが書くレジストリはこの1か所だけです。

- 導入・更新の直後に、Velopackが設定アプリを呼び出します。このとき旧版からの移行を行い、自動起動のショートカットがあれば行き先を作り直します。
- アンインストールでは、Velopackが本体を消す前に設定アプリを呼び出します。設定アプリは自動起動のショートカットを消し、選択に応じて記録フォルダーを消します。設定画面やスタートメニューから始めた場合は先に確認画面で選んでもらい、選択を一時ファイルで受け渡します。Windowsの設定から始めた場合は、Velopackの制限時間（30秒）があるため、25秒の残り時間を表示した確認画面を出し、時間内に答えがなければ既定どおりすべて削除します。

### 旧版からの移行

- v0.2.0の `Run\WindowsLineStartToTray`（と、さらに古い `Run\LineTrayStartup`）が `LineTrayStartup` フォルダーを指していれば、その値と `StartupApproved\Run` の値を削除します。オンだった場合はスタートアップフォルダーのショートカットを作ります。これは旧版が自分で書いた値の後始末です。
- `LineTrayStartup` フォルダーの旧版のプログラムとバックアップを削除します。LINEが読み込み中のDLLは、次の起動時に削除を試します。
- 旧名「Windows版LINEを通知領域で起動」のスタートメニューとスタートアップのショートカットは、本ツールのものだけを削除し、自動起動の状態は新しい名前のショートカットに引き継ぎます。
- LINE本体の `Run` と `StartupApproved\Run` の値は変えません。

## 起動から通知領域まで

1. `LineTrayStart.exe` が、補助DLLを記録フォルダーの `hook\<ビルドごとのフォルダー>` に写します。LINEは起動中ずっと補助DLLを読み込んだままにするため、インストール先のDLLを読み込ませると、更新で入れ替えられなくなるからです。
2. `DetourCreateProcessWithDllExW` で `LineLauncher.exe --booting` を起動します。補助DLLは、アプリの通常の処理が始まる前に読み込まれます。
3. ランチャー・LINE本体・更新関連プロセスの `ShellExecuteExW` / `ShellExecuteW` / `CreateProcessW` / `CreateProcessA` をフックし、`LINE.exe`、`LineLauncher.exe`、`LineUpdater.exe`、`LineAppMgr.exe` の起動にDLLを引き継ぎます。他の名前の実行ファイルは元のAPIへ渡します。32bitと64bitをまたぐ起動にはDetoursの対応機能を利用します。実際の32bit更新プログラムで使われているANSI版の `CreateProcessA` にも対応します。
4. ランチャーが作る `SPLASH` クラスの接続画面（「Connecting to LINE」）について、`CreateWindowExW` の `WS_VISIBLE` を外し、`ShowWindow` / `ShowWindowAsync` / `SetWindowPos` の表示要求も抑えます。
5. LINE本体の `CreateWindowExW`、`ShowWindow`、`ShowWindowAsync`、`SetWindowPos` をフックします。起動中のQtのトップレベルウィンドウについて、作成時の `WS_VISIBLE`、表示呼び出し、`SWP_SHOWWINDOW` を抑制します。
6. 起動画面は非表示のまま初期化を続けます。タイトルが `LINE` のメインウィンドウは、`DwmSetWindowAttribute` の `DWMWA_CLOAK` でクロークしてから表示させます。クロークしたウィンドウは画面にもタスクバーにもAlt+Tabにも出ません。表示はフォーカスを奪わない `SW_SHOWNA` と `SWP_NOACTIVATE` に置き換えます。LINEが通知領域のアイコンに未読の「N」を付けたら、または5秒たったら `WM_CLOSE` を送ります。LINE自身が通常の「閉じる」処理を行ってウィンドウを隠したあと、クロークを解除します。
   - 2026年10月20日の実機確認で、メイン画面を一度も表示せずに閉じると、LINEは既存の未読を数えず、新着のメッセージでも右下の通知と通知領域の「N」を出しませんでした。通常の起動で画面を開いて閉じた場合は「N」が出ました。クロークして表示させる方式では、起動から約1.5秒後に既存の未読の「N」が付きました。
   - 閉じる前にユーザーが通知領域のアイコンを操作した場合は、閉じる処理を取りやめ、クロークを解除します。
   - ログアウト状態で起動した場合、LINEはメイン画面と同じクラス・同じタイトル `LINE` のウィンドウにログイン画面を出します。表示要求の時点で、ログイン済みのメイン画面には `WS_MAXIMIZEBOX` があり（実機で `0x86CF0000`）、ログイン画面にはありません（`0x86CE0000`）。最大化ボタンのない場合はログイン画面とみなし、`startup-login-shown` を記録して表示を許可し、起動時の抑制を終了します。
7. 閉じる処理の完了後、起動時の表示抑制を解除します。通知領域アイコンをユーザーがクリックした場合も解除するため、以後のユーザー操作を受け付けます。

Qtのクラス名やメインウィンドウの識別はLINEの内部実装に依存します。LINEの更新後も必ず同じ動作になるとは限りません。

## 通知領域アイコン

`Shell_NotifyIconW` のアイコン登録・更新のたびに、LINEが渡したアイコンを32bitの画素として読み、LINE本体のロゴに状態を重ねたアイコンに差し替えます。クリックの通知先など、ほかの登録情報は引き継ぎます。LINEの画像や実行ファイルは配布物に含めません。

- 不透明な画素のうち、色のある画素（RGBの最大と最小の差が40を超える）が5%未満なら、接続中と判断します。2026年10月20日の実機観測で、LINE 26.5.0.3975は起動直後に灰色、接続後に緑のアイコンを登録していました。接続中はロゴを明るい灰色にします。
- オレンジ〜赤の画素（R>190、R−G>60、R−B>90、G>30）が4つ以上あれば、未読ありと判断します。同じ観測で、未読があるときは緑のアイコンの右側にオレンジの「N」の印が付いていました。印の範囲のオレンジの画素と、その内側の白い文字を、同じ大きさで取り出したロゴの同じ位置に写します。
- 合成できなかった場合はロゴをそのまま使い、ロゴも取り出せなかった場合はLINEが渡したアイコンを使います。

記録フォルダーに `tray-dump.on` を置くと、LINEが渡したアイコンと合成したアイコンをBMPで保存します。LINEがデザインを変えたときの調査に使います。

## 監視ループが不要な理由

表示APIや通知領域アイコンのAPIが呼ばれたタイミングだけ処理します。一定間隔でプロセスやウィンドウを調べる常駐処理はありません。時間を決めた処理は、未読がなく「N」が付かないときに、クロークしたメイン画面を5秒後に閉じるタイマー1つだけです。起動用EXEはランチャー起動後に終了し、補助DLLはLINEのプロセス内で動作します。サインイン時に起動する設定アプリも、更新を確認したら終了します。

## AIエージェントの環境での隔離

Codexの実行環境から起動した子プロセスでは、レジストリとAppDataへの書き込みがCodex専用の領域へ転送されていました。Claude Codeのデスクトップアプリから起動した場合も、`%LOCALAPPDATA%` への書き込みが同様に転送されることを確認しています（2026年10月20日）。どちらもそのまま読み返すと変更済みに見えるため、Windowsが実際に使う設定とファイルを確認できません。

v0.2では、セットアップEXEがWMIの `Win32_Process.Create` で自分自身を通常のWindowsプロセスとして起動し直していました。v0.3のセットアップはVelopackが作るため、この処理は持っていません。AIエージェントから実機に導入するときは、WMIでセットアップEXEを起動し、`CIM_DataFile` と `StdRegProv`（`HKEY_USERS\<SID>`）で結果を確かめます。

## 実際に確認したこと

- Windows 11 Home 23H2 x64、Microsoft StoreからインストールしたLINE 26.4.2.3957で確認。
- LINEを通常終了後、補助EXEで起動。最初の20秒間のWindows表示イベント監視でLINEのウィンドウ表示を検出しないことを確認。
- 起動画面とメインウィンドウの表示抑制、アイコン登録を診断ログで確認。
- 利用者がアイコンのダブルクリックによる再表示とログイン維持を確認。

2026年9月30日、再起動後のLINEに補助DLLが読み込まれない問題を確認しました。v0.2.0-preview.4で起動項目を変更しても、設定とファイルがCodex専用の隔離領域へ保存され、実際のWindowsには適用されていませんでした。v0.2.0-preview.5で通常プロセスへの起動し直しと実レジストリの操作を追加し、実ファイルと実際の起動項目を確認しています。

**同日、利用者が実機で修正後のPC再起動を行い、LINEのウィンドウが表示されず、通知領域にLINEのアイコンが表示されることを確認しました。** この再起動後の確認では、アイコンのダブルクリックによる再表示とログイン維持は再確認していません。

2026年10月1日の再起動では、補助EXEと旧LINEにDLLが読み込まれた後、自動更新で起動した26.5.0.3975のLINEにはDLLがありませんでした。旧版はLINE本体の子プロセス起動をフックせず、更新プログラムも対象外でした。さらに実際の `LINE` のRun値は通常のランチャーへ戻っていました。preview.6では子プロセス起動のフックをLINE・更新プロセスにも適用し、独立した起動項目へ移行しています。

更新経路のテスト用アプリで、32bitランチャー → 64bit LINE → 64bit管理プロセス → 32bit更新プログラム → 64bit更新後LINEを起動し、ANSI版・Unicode版のプロセス作成を経由しても初回表示の抑制と後からの再表示ができることを確認しました。これは実際のLINE更新を再実行したテストとは区別しています。

2026年10月19日、LINE 26.5.0.3975で、ログイン済みの起動は通知領域へ移り、ログアウト状態ではログイン画面が表示されることを確認しました（preview.8）。同日、毎回ログアウトされる問題は本ツールではなく、QRコードでのログインが原因だと分かりました。

2026年10月20日、v0.3.0の設定アプリをVelopackで導入し、v0.2.0-preview.9からの移行（自動起動のショートカットへの置き換え）と、スタートメニューのアイコン表示を実機で確認しました。通知領域アイコンの接続中・未読の判定は、実機で保存したLINEのアイコンを入力に確認しました。

## 参考

- [Microsoft Detours](https://github.com/microsoft/Detours)
- [DetourCreateProcessWithDllEx](https://github.com/microsoft/Detours/wiki/DetourCreateProcessWithDllEx)
- [Velopack](https://docs.velopack.io/)
- [Microsoft: Flexible virtualization](https://learn.microsoft.com/en-us/windows/msix/desktop/flexible-virtualization)
- [Microsoft: StdRegProv](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/regprov/stdregprov)
