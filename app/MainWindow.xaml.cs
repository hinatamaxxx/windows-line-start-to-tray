using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using Microsoft.Win32;
using Velopack;

namespace LineTray
{
    public partial class MainWindow : Window
    {
        private bool dark;
        private bool busy;
        private bool refreshing;
        private UpdateInfo update;

        public MainWindow()
        {
            InitializeComponent();
            Width = Math.Min(Width, SystemParameters.WorkArea.Width - 24);
            Height = Math.Min(Height, SystemParameters.WorkArea.Height - 24);
            ApplyTheme();
            VersionText.Text = "バージョン " + (Updater.CurrentVersion ?? Program.BuildVersion);
            AutoStartToggle.Click += delegate { Toggle(); };
            AutoStartSettingsButton.Click += delegate { Open("ms-settings:startupapps"); };
            LineWarningButton.Click += delegate { Open("ms-settings:startupapps"); };
            LaunchButton.Click += delegate { Launch(); };
            CheckButton.Click += async delegate { await CheckUpdate(); };
            UpdateButton.Click += async delegate { await InstallUpdate(); };
            HelpButton.Click += delegate { Open(File.Exists(Paths.Guide) ? Paths.Guide : Paths.RepositoryUrl + "#readme"); };
            FolderButton.Click += delegate { Directory.CreateDirectory(Paths.Data); Open(Paths.Data); };
            GithubButton.Click += delegate { Open(Paths.RepositoryUrl); };
            UninstallButton.Click += delegate { Uninstall(); };
            Closing += delegate(object sender, System.ComponentModel.CancelEventArgs e) { if (busy) e.Cancel = true; };
            SourceInitialized += delegate { Theme.ApplyFrame(this, dark); };
            // The user may change startup settings or the Windows theme while this window is open.
            Activated += delegate { Refresh(); };
            SystemEvents.UserPreferenceChanged += OnPreferenceChanged;
            Closed += delegate { SystemEvents.UserPreferenceChanged -= OnPreferenceChanged; };
            Loaded += async delegate { await CheckUpdate(); };
            Refresh();
        }

        private void OnPreferenceChanged(object sender, UserPreferenceChangedEventArgs e)
        {
            if (e.Category == UserPreferenceCategory.General || e.Category == UserPreferenceCategory.Color)
                Dispatcher.BeginInvoke(new Action(ApplyTheme));
        }

        private void ApplyTheme()
        {
            dark = Theme.SystemPrefersDark;
            Theme.Apply(Resources, dark);
            Theme.ApplyFrame(this, dark);
        }

        private void Refresh()
        {
            refreshing = true;
            AutoStart state = Startup.State;
            AutoStartToggle.IsChecked = state == AutoStart.On;
            AutoStartSettingsButton.Visibility = state == AutoStart.DisabledByWindows ? Visibility.Visible : Visibility.Collapsed;
            AutoStartText.Text = state == AutoStart.On ? "次回のサインインから、LINEを画面に出さずに通知領域で起動します。"
                : state == AutoStart.DisabledByWindows ? "Windowsのスタートアップ設定でオフになっています。オンにするには、スタートアップ設定で「" + Paths.DisplayName + "」をオンにしてください。"
                : "オンにすると、Windowsのスタートアップに登録します。";
            bool lineOn = Startup.LineState == LineAutoStart.On;
            LineStateText.Text = lineOn ? "オン" : "オフ";
            LineWarning.Visibility = lineOn ? Visibility.Visible : Visibility.Collapsed;
            AutoStartToggle.IsEnabled = LaunchButton.IsEnabled = UninstallButton.IsEnabled = CheckButton.IsEnabled = !busy;
            refreshing = false;
        }

        private void Toggle()
        {
            if (refreshing) return;
            try
            {
                switch (Startup.State)
                {
                    case AutoStart.On: Startup.Disable(); break;
                    // Windows owns this switch; the app does not override the user's choice there.
                    case AutoStart.DisabledByWindows: Startup.OpenSettings(); break;
                    default: Startup.Enable(); break;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show(this, error.Message, "自動起動を変更できませんでした", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            Refresh();
        }

        private void Launch()
        {
            if (Startup.LineRunning())
            {
                LaunchText.Text = "LINEはすでに起動しています。通知領域のLINEアイコンをダブルクリックすると開けます。見当たらないときは、タスクバー右端の「^」を開いてください。試し直すときは、アイコンを右クリックして「終了」を選んでから押してください。";
                return;
            }
            try
            {
                Startup.StartLine();
                LaunchText.Text = "起動を始めました。LINEのアイコンは通知領域に入ります。タスクバーに見当たらないときは、右端の「^」を開いてください。アイコンをダブルクリックすると開けます。";
            }
            catch (Exception error)
            {
                LaunchText.Text = "起動できませんでした: " + error.Message;
            }
        }

        private async Task CheckUpdate()
        {
            update = null;
            UpdateButton.Visibility = Visibility.Collapsed;
            if (!Updater.Installed)
            {
                UpdateTitle.Text = "開発用のビルド";
                UpdateText.Text = "インストールしたものではないため、更新は確認しません。";
                CheckButton.IsEnabled = false;
                return;
            }
            UpdateTitle.Text = "バージョン " + Updater.CurrentVersion;
            UpdateText.Text = "更新を確認しています…";
            CheckButton.IsEnabled = false;
            try
            {
                update = await Updater.CheckAsync();
                if (update == null) UpdateText.Text = "最新のバージョンです。サインイン時にも確認し、新しいバージョンがあれば自動で更新します。";
                else
                {
                    UpdateText.Text = "新しいバージョン " + update.TargetFullRelease.Version + " があります。";
                    UpdateButton.Visibility = Visibility.Visible;
                }
            }
            catch (Exception error)
            {
                UpdateText.Text = "更新を確認できませんでした。インターネットへの接続を確かめてください。";
                Log.Write("update-check-failed " + error.Message);
            }
            CheckButton.IsEnabled = !busy;
        }

        private async Task InstallUpdate()
        {
            if (update == null) return;
            busy = true;
            UpdateButton.IsEnabled = false;
            Progress.Visibility = Visibility.Visible;
            Refresh();
            try
            {
                UpdateText.Text = "ダウンロードしています…";
                await Updater.DownloadAsync(update, value => Dispatcher.Invoke(() => Progress.Value = value));
                UpdateText.Text = "更新を適用して、再起動します…";
                Updater.ApplyAndRestart(update);
            }
            catch (Exception error)
            {
                UpdateText.Text = "更新できませんでした。今のバージョンのまま使えます。";
                Log.Write("update-failed " + error.Message);
                busy = false;
                UpdateButton.IsEnabled = true;
                Progress.Visibility = Visibility.Collapsed;
                Refresh();
            }
        }

        private void Uninstall()
        {
            var dialog = new UninstallDialog(false) { Owner = this };
            if (dialog.ShowDialog() != true) return;
            Program.StartUninstall(dialog.KeepData);
            Close();
        }

        private void Open(string target)
        {
            try { Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); }
            catch (Exception error) { MessageBox.Show(this, error.Message, "開けませんでした", MessageBoxButton.OK, MessageBoxImage.Error); }
        }
    }
}
