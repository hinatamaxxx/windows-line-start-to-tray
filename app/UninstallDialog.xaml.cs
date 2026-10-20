using System;
using System.Windows;
using System.Windows.Threading;

namespace LineTray
{
    // Confirmation before uninstalling. From the app or the Start menu the user can cancel.
    // When Windows' app list starts the uninstall, Velopack gives the hook 30 seconds and it
    // cannot be cancelled, so the dialog continues with the chosen option after a countdown.
    public partial class UninstallDialog : Window
    {
        private readonly DispatcherTimer timer;
        private int remaining = 25;
        internal bool KeepData => KeepBox.IsChecked == true;

        internal UninstallDialog(bool fromWindows)
        {
            InitializeComponent();
            bool dark = Theme.SystemPrefersDark;
            Theme.Apply(Resources, dark);
            SourceInitialized += delegate { Theme.ApplyFrame(this, dark); };
            RunningNote.Visibility = Startup.LineRunning() ? Visibility.Visible : Visibility.Collapsed;
            ProceedButton.Content = fromWindows ? "続ける" : "アンインストール";
            ProceedButton.Click += delegate { DialogResult = true; };
            // The safe choice gets the initial focus.
            Loaded += delegate { if (fromWindows) KeepBox.Focus(); else CancelButton.Focus(); };
            if (!fromWindows) return;

            Heading.Text = "設定とデータの扱いを選んでください";
            CancelButton.Visibility = Visibility.Collapsed;
            CountdownText.Visibility = Visibility.Visible;
            CountdownText.Text = remaining + " 秒後に、選んだ内容で続けます。";
            timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            timer.Tick += delegate {
                remaining--;
                CountdownText.Text = remaining + " 秒後に、選んだ内容で続けます。";
                if (remaining <= 0) { timer.Stop(); DialogResult = true; }
            };
            timer.Start();
            // Closing the window still continues; the caller ignores the result here.
            Closed += delegate { timer.Stop(); };
        }
    }
}
