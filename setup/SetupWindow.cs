using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shell;
using Microsoft.Win32;
using System.Management;
using System.Security.Principal;

internal sealed class SetupWindow : Window
{
    private readonly FrameworkElement view;
    private bool busy;
    private bool dark;
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

    internal SetupWindow()
    {
        Title = "Windows版LINEを通知領域で起動";
        Width = Math.Min(744, SystemParameters.WorkArea.Width - 24);
        Height = Math.Min(748, SystemParameters.WorkArea.Height - 24);
        MinWidth = Math.Min(680, Width); MinHeight = Math.Min(630, Height);
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        WindowStyle = WindowStyle.None;
        ResizeMode = ResizeMode.CanResize;
        UseLayoutRounding = true;
        WindowChrome.SetWindowChrome(this, new WindowChrome { CaptionHeight = 44, ResizeBorderThickness = new Thickness(5), GlassFrameThickness = new Thickness(0), CornerRadius = new CornerRadius(8), UseAeroCaptionButtons = false });
        using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("SetupWindow.xaml"))
            view = (FrameworkElement)XamlReader.Load(stream);
        Content = view;
        using (Stream icon = Assembly.GetExecutingAssembly().GetManifestResourceStream("AppIcon.ico"))
            Icon = BitmapFrame.Create(icon, BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
        Part<TextBlock>("VersionText").Text = "v" + Program.Version;
        Part<Button>("InstallButton").Click += async delegate { await Apply(false); };
        Part<Button>("RestoreButton").Click += async delegate { await Apply(true); };
        Part<Button>("LaunchButton").Click += delegate { StartLine(); };
        Part<Button>("CloseButton").Click += delegate { Close(); };
        Part<Button>("MinimizeButton").Click += delegate { WindowState = WindowState.Minimized; };
        Part<Button>("ThemeButton").Click += delegate { SetTheme(!dark); };
        Part<Button>("FolderButton").Click += delegate { Open(Program.Home); };
        Part<Button>("GithubButton").Click += delegate { Open("https://github.com/hinatamaxxx/windows-line-start-to-tray"); };
        Part<Button>("HelpButton").Click += delegate {
            string guide = Path.Combine(Program.Home, "使い方.txt");
            Open(File.Exists(guide) ? guide : "https://github.com/hinatamaxxx/windows-line-start-to-tray#readme");
        };
        Closing += delegate(object sender, System.ComponentModel.CancelEventArgs e) { if (busy) e.Cancel = true; };
        SourceInitialized += delegate {
            int round = 2;
            DwmSetWindowAttribute(new WindowInteropHelper(this).Handle, 33, ref round, sizeof(int));
        };
        bool preferDark = false;
        using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
            preferDark = key != null && Convert.ToInt32(key.GetValue("AppsUseLightTheme", 1)) == 0;
        SetTheme(preferDark);
        RefreshButtons();
        if (IsConfigured()) SetState("セットアップ済みです", "「LINEを起動」で動作を確認できます。再セットアップで更新することもできます。");
    }

    private T Part<T>(string name) where T : FrameworkElement { return (T)view.FindName(name); }
    internal void SetTheme(bool useDark)
    {
        dark = useDark;
        string[] names = { "WindowBrush", "SurfaceBrush", "TextBrush", "MutedBrush", "BorderBrush", "AccentBrush", "AccentHoverBrush", "AccentTextBrush", "SoftBrush", "SoftTextBrush" };
        string[] colors = useDark
            ? new[] { "#101714", "#19221D", "#EDF5EF", "#A8BAAF", "#2F4035", "#75D4A2", "#8DE0B4", "#092719", "#233D30", "#A5E4BE" }
            : new[] { "#F6F8FA", "#FFFFFF", "#18211F", "#60706A", "#DFE7E2", "#11734E", "#0C5F40", "#FFFFFF", "#E8F4EE", "#276247" };
        for (int i = 0; i < names.Length; i++) view.Resources[names[i]] = new SolidColorBrush((Color)ColorConverter.ConvertFromString(colors[i]));
        Background = (Brush)view.Resources["WindowBrush"];
    }

    private bool IsConfigured()
    {
        string exe = Path.Combine(Program.Home, "LineTrayStart.exe");
        if (!File.Exists(exe)) return false;
        string root = WindowsIdentity.GetCurrent().User.Value + @"\Software\Microsoft\Windows\CurrentVersion\";
        using (var registry = new ManagementClass(@"root\default", "StdRegProv", null))
        using (var input = registry.GetMethodParameters("GetStringValue"))
        {
            input["hDefKey"] = (uint)0x80000003;
            input["sSubKeyName"] = root + "Run";
            input["sValueName"] = Program.StartupEntry;
            using (var output = registry.InvokeMethod("GetStringValue", input, null))
                if (Convert.ToUInt32(output["ReturnValue"]) != 0 || !String.Equals(output["sValue"] as string, "\"" + exe + "\"", StringComparison.OrdinalIgnoreCase)) return false;
            using (var approvalInput = registry.GetMethodParameters("GetBinaryValue"))
            {
                approvalInput["hDefKey"] = (uint)0x80000003;
                approvalInput["sSubKeyName"] = root + @"Explorer\StartupApproved\Run";
                approvalInput["sValueName"] = Program.StartupEntry;
                using (var output = registry.InvokeMethod("GetBinaryValue", approvalInput, null))
                {
                    uint error = Convert.ToUInt32(output["ReturnValue"]);
                    byte[] flag = output["uValue"] as byte[];
                    if (!(error == 0 && flag != null && flag.Length > 0 && flag[0] == 2)) return false;
                }
                approvalInput["sValueName"] = "LINE";
                using (var output = registry.InvokeMethod("GetBinaryValue", approvalInput, null))
                {
                    byte[] flag = output["uValue"] as byte[];
                    return Convert.ToUInt32(output["ReturnValue"]) == 0 && flag != null && flag.Length > 0 && flag[0] == 3;
                }
            }
        }
    }

    private void RefreshButtons()
    {
        bool backup = File.Exists(Path.Combine(Program.Home, "startup-backup.json"));
        Part<Button>("InstallButton").IsEnabled = !busy;
        Part<Button>("RestoreButton").IsEnabled = !busy && backup;
        Part<Button>("LaunchButton").IsEnabled = !busy && backup && File.Exists(Path.Combine(Program.Home, "LineTrayStart.exe"));
        Part<Button>("FolderButton").IsEnabled = !busy && Directory.Exists(Program.Home);
        Part<Button>("CloseButton").IsEnabled = !busy;
        Part<TextBlock>("StatusBadge").Text = busy ? "処理中" : IsConfigured() ? "セットアップ済み" : "未セットアップ";
        Part<ProgressBar>("Progress").Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
        Part<ProgressBar>("Progress").IsIndeterminate = busy;
    }

    private void SetState(string title, string message)
    {
        Part<TextBlock>("StateTitle").Text = title;
        Part<TextBlock>("StateText").Text = message;
    }

    private bool LineRunning()
    {
        Process[] processes = Process.GetProcessesByName("LINE");
        Process[] launchers = Process.GetProcessesByName("LineLauncher");
        bool found = processes.Length > 0 || launchers.Length > 0;
        foreach (Process process in processes) process.Dispose();
        foreach (Process process in launchers) process.Dispose();
        return found;
    }

    private async Task Apply(bool undo)
    {
        if (!undo && LineRunning())
        {
            SetState("先にLINEを終了してください", "通知領域のLINEアイコンを右クリックして「終了」を選んでから、もう一度「セットアップ」を押してください。×ボタンだけでは終了しません。");
            return;
        }
        if (undo && MessageBox.Show(this, "自動起動設定を導入前の状態に戻します。\nLINEのログイン情報は変更しません。続けますか？", "設定を元に戻す", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        busy = true;
        RefreshButtons();
        SetState(undo ? "設定を戻しています…" : "セットアップしています…", "この画面を開いたままお待ちください。");
        string temp = Path.Combine(Path.GetTempPath(), "LineTrayStartup-Setup", Guid.NewGuid().ToString("N"));
        string executable = Assembly.GetExecutingAssembly().Location;
        try
        {
            await Task.Run(delegate {
                Program.Extract(temp);
                Program.RunScript(temp, undo ? "Uninstall.ps1" : "Install.ps1");
                if (!undo) Program.SaveSupportFiles(temp, Program.Home, executable, Program.Shortcut);
                else
                {
                    if (File.Exists(Program.Shortcut)) File.Delete(Program.Shortcut);
                    Program.RemoveLegacyShortcut(Program.Home, Program.Shortcut);
                }
            });
            if (undo) SetState("設定を元に戻しました", "LINEを通知領域から終了して起動し直すと、通常の動作に戻ります。ファイルの削除方法は「使い方」をご覧ください。");
            else SetState("セットアップが完了しました", "次回サインインから自動起動します。「LINEを起動」で、今すぐ動作を確認できます。");
        }
        catch (Exception error)
        {
            SetState("セットアップを完了できませんでした", "表示されたエラー内容を確認して、もう一度お試しください。");
            MessageBox.Show(this, error.Message, "セットアップのエラー", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            string expectedRoot = Path.GetFullPath(Path.Combine(Path.GetTempPath(), "LineTrayStartup-Setup")) + Path.DirectorySeparatorChar;
            if (Path.GetFullPath(temp).StartsWith(expectedRoot, StringComparison.OrdinalIgnoreCase))
                try { Directory.Delete(temp, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            busy = false;
            RefreshButtons();
        }
    }

    private void StartLine()
    {
        if (LineRunning()) { SetState("LINEは起動しています", "通知領域のLINEアイコンをダブルクリックすると開けます。"); return; }
        try
        {
            Process.Start(new ProcessStartInfo(Path.Combine(Program.Home, "LineTrayStart.exe")) { UseShellExecute = false, CreateNoWindow = true });
            SetState("LINEの起動を開始しました", "時計の近くにアイコンが現れたら、ダブルクリックしてLINEを開けます。ログアウトしている場合は、ログイン画面が表示されます。");
        }
        catch (Exception error) { SetState("LINEを起動できませんでした", error.Message); }
    }

    private void Open(string target)
    {
        try { Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); }
        catch (Exception error) { SetState("開けませんでした", error.Message); }
    }
}
