using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace LineTray
{
    internal enum AutoStart { Off, On, DisabledByWindows }
    internal enum LineAutoStart { Off, On, Unknown }

    // Autostart is a shortcut in the Startup folder. Windows records the user's on/off choice
    // in StartupApproved; the app only reads those values and never writes them.
    internal static class Startup
    {
        internal static AutoStart State
        {
            get
            {
                if (!File.Exists(Paths.StartupShortcut)) return AutoStart.Off;
                return IsDisabled(Paths.ApprovedFolderKey, Path.GetFileName(Paths.StartupShortcut)) ? AutoStart.DisabledByWindows : AutoStart.On;
            }
        }

        internal static void Enable()
        {
            Shortcut.Create(Paths.StartupShortcut, Paths.AppExe, "--startup", Paths.DisplayName + " (サインイン時の起動)");
            Log.Write("autostart-enabled");
        }

        internal static void Disable()
        {
            Shortcut.Delete(Paths.StartupShortcut);
            Log.Write("autostart-disabled");
        }

        // LINE registers itself in HKCU Run. Running both would open LINE twice, so the
        // setup screen asks the user to turn LINE off in Windows' startup settings.
        internal static LineAutoStart LineState
        {
            get
            {
                using (RegistryKey run = Registry.CurrentUser.OpenSubKey(Paths.RunKey))
                {
                    if (run == null || run.GetValue("LINE") == null) return LineAutoStart.Off;
                }
                return IsDisabled(Paths.ApprovedRunKey, "LINE") ? LineAutoStart.Off : LineAutoStart.On;
            }
        }

        // An odd first byte means the user turned the entry off. No value or an even byte is on.
        internal static bool IsDisabled(string key, string name)
        {
            using (RegistryKey approved = Registry.CurrentUser.OpenSubKey(key))
            {
                byte[] value = approved?.GetValue(name) as byte[];
                return value != null && value.Length > 0 && (value[0] & 1) == 1;
            }
        }

        internal static void OpenSettings()
        {
            Process.Start(new ProcessStartInfo("ms-settings:startupapps") { UseShellExecute = true });
        }

        internal static bool LineRunning()
        {
            foreach (string name in new[] { "LINE", "LineLauncher" })
            {
                Process[] processes = Process.GetProcessesByName(name);
                bool found = processes.Length > 0;
                foreach (Process process in processes) process.Dispose();
                if (found) return true;
            }
            return false;
        }

        internal static void StartLine()
        {
            Process.Start(new ProcessStartInfo(Paths.Helper) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = Paths.AppDirectory });
            Log.Write("helper-launched");
        }
    }
}
