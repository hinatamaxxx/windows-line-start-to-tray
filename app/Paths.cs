using System;
using System.IO;

namespace LineTray
{
    // Every location the app touches. LINETRAY_TEST_ROOT redirects all of them, including
    // the registry keys, so the tests never change the real user profile.
    internal static class Paths
    {
        internal const string DisplayName = "LINEを最小化して起動";
        // Name used up to 0.3.0-preview.1 for the Start menu and Startup shortcuts.
        internal const string OldDisplayName = "Windows版LINEを通知領域で起動";
        internal const string RepositoryUrl = "https://github.com/hinatamaxxx/windows-line-start-to-tray";
        internal const string LegacyRunName = "WindowsLineStartToTray";
        internal const string OlderRunName = "LineTrayStartup";

        internal static string TestRoot => Environment.GetEnvironmentVariable("LINETRAY_TEST_ROOT");
        private static string Under(string name, Environment.SpecialFolder folder) =>
            TestRoot != null ? Path.Combine(TestRoot, name) : Environment.GetFolderPath(folder);

        internal static string AppDirectory => AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        internal static string AppExe => Path.Combine(AppDirectory, "LineMinimizedStart.exe");
        internal static string Helper => Path.Combine(AppDirectory, "LineTrayStart.exe");
        internal static string Guide => Path.Combine(AppDirectory, "使い方.txt");

        // Data that the hook and launcher also use: diagnostic.log and the hook copies.
        internal static string Data => TestRoot != null ? Path.Combine(TestRoot, "Data") :
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LineTrayStartup");
        internal static string HookCache => Path.Combine(Data, "hook");
        internal static string AppLog => Path.Combine(Data, "app.log");

        internal static string StartupShortcut => Path.Combine(Under("Startup", Environment.SpecialFolder.Startup), DisplayName + ".lnk");
        internal static string Programs => Under("Programs", Environment.SpecialFolder.Programs);
        internal static string MenuShortcut => Path.Combine(Programs, DisplayName + ".lnk");
        internal static string OldMenuShortcut => Path.Combine(Programs, OldDisplayName + ".lnk");
        internal static string OldStartupShortcut => Path.Combine(Under("Startup", Environment.SpecialFolder.Startup), OldDisplayName + ".lnk");
        internal static string UninstallShortcut => Path.Combine(Programs, OldDisplayName + " をアンインストール.lnk");
        internal static string UninstallDecision => Path.Combine(TestRoot != null ? Path.Combine(TestRoot, "Temp") : Path.GetTempPath(), "WindowsLineStartToTray-uninstall.txt");

        // Registry paths below HKCU. Only read, except for deleting values the old version wrote.
        internal static string RegistryRoot => TestRoot != null ? @"Software\LineTrayTest\" + Path.GetFileName(TestRoot) + @"\" : @"Software\";
        internal static string RunKey => RegistryRoot + @"Microsoft\Windows\CurrentVersion\Run";
        internal static string ApprovedRunKey => RegistryRoot + @"Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run";
        internal static string ApprovedFolderKey => RegistryRoot + @"Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\StartupFolder";
        internal static string UninstallKey => RegistryRoot + @"Microsoft\Windows\CurrentVersion\Uninstall\WindowsLineStartToTray";

        internal static string LineLauncher => Path.Combine(TestRoot != null ? Path.Combine(TestRoot, "LocalAppData") :
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"LINE\bin\LineLauncher.exe");
    }
}
