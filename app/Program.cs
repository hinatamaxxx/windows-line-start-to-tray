using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using Microsoft.Win32;
using Velopack;

namespace LineTray
{
    internal static class Program
    {
        internal static string BuildVersion =>
            Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion?.Split('+')[0] ?? "dev";

        [STAThread]
        private static int Main(string[] args)
        {
            VelopackApp.Build()
                .OnAfterInstallFastCallback(v => AfterInstall())
                .OnAfterUpdateFastCallback(v => AfterInstall())
                .OnBeforeUninstallFastCallback(v => BeforeUninstall())
                .Run();

            if (args.Contains("--startup")) return SignIn();
            if (args.Contains("--uninstall"))
                return Uninstall(args.Any(a => a.Equals("-Quiet", StringComparison.OrdinalIgnoreCase)),
                    args.Any(a => a.Equals("-KeepSettings", StringComparison.OrdinalIgnoreCase)));
            if (args.Length >= 3 && args[0] == "--test" && Paths.TestRoot != null) return TestCommand(args[1], args[2]);

            using (var single = new Mutex(true, @"Local\WindowsLineStartToTray_SingleInstance", out bool first))
            {
                if (!first) return 0;
                Migration.Run();
                var app = new Application { ShutdownMode = ShutdownMode.OnMainWindowClose };
                return app.Run(new MainWindow());
            }
        }

        // Velopack hooks. They must finish within the time limit Velopack gives them.
        private static void AfterInstall()
        {
            Log.Write("velopack-after-install " + BuildVersion);
            // The Start menu keeps a single entry; uninstall is in Windows Settings and in the app.
            // 0.3.0-preview.1 test builds created a separate uninstall shortcut.
            try { Shortcut.Delete(Paths.UninstallShortcut); }
            catch (Exception error) { Log.Write("uninstall-shortcut-delete-failed " + error.Message); }
            Migration.Run();
            if (File.Exists(Paths.StartupShortcut)) Startup.Enable();
        }

        private static void BeforeUninstall()
        {
            bool? decided = Cleanup.TakeDecision();
            bool keep;
            if (decided.HasValue) keep = decided.Value;
            else
            {
                new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
                var dialog = new UninstallDialog(true);
                dialog.ShowDialog();
                keep = dialog.KeepData;
            }
            // Log first: writing afterwards would recreate the data folder that was just removed.
            Log.Write("velopack-before-uninstall keep=" + keep);
            List<string> left = Cleanup.Run(keep);
            if (keep) Log.Write("uninstall-left " + left.Count);
        }

        // Started by the Startup folder shortcut at sign-in.
        private static int SignIn()
        {
            Log.Write("signin-start " + BuildVersion);
            if (!Startup.LineRunning())
            {
                try { Startup.StartLine(); }
                catch (Exception error) { Log.Write("helper-launch-failed " + error.Message); }
            }
            Migration.Run();
            try
            {
                Task<int> work = Task.Run(async () => {
                    UpdateInfo info = await Updater.CheckAsync().ConfigureAwait(false);
                    if (info == null) return 0;
                    await Updater.DownloadAsync(info, null).ConfigureAwait(false);
                    Updater.ApplyAfterExit(info);
                    return 1;
                });
                work.Wait(TimeSpan.FromMinutes(3));
            }
            catch (Exception error) { Log.Write("signin-update-failed " + error.GetBaseException().Message); }
            return 0;
        }

        private static int Uninstall(bool quiet, bool keep)
        {
            if (!quiet)
            {
                var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
                var dialog = new UninstallDialog(false);
                if (dialog.ShowDialog() != true) return 1;
                keep = dialog.KeepData;
            }
            StartUninstall(keep);
            return 0;
        }

        // Records the choice for the uninstall hook and starts Velopack's uninstaller, which
        // stops this app, runs the hook, removes the program folder and its registration.
        internal static void StartUninstall(bool keep)
        {
            Cleanup.SaveDecision(keep);
            string command;
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(Paths.UninstallKey))
                command = key?.GetValue("UninstallString") as string;
            if (string.IsNullOrEmpty(command))
            {
                // Not installed through Velopack (development build): clean up our own parts only.
                Cleanup.TakeDecision();
                Cleanup.Run(keep);
                return;
            }
            string exe = command.StartsWith("\"") ? command.Substring(1, command.IndexOf('"', 1) - 1) : command.Split(' ')[0];
            string arguments = command.Substring(command.IndexOf(exe, StringComparison.Ordinal) + exe.Length).TrimStart('"', ' ');
            Log.Write("uninstall-start keep=" + keep);
            Process.Start(new ProcessStartInfo(exe, (arguments + " --silent").Trim()) { UseShellExecute = false });
        }

        // Automation for tests/Test-App.ps1. Only available with LINETRAY_TEST_ROOT set.
        private static int TestCommand(string command, string output)
        {
            string result;
            switch (command)
            {
                case "enable": Startup.Enable(); result = Startup.State.ToString(); break;
                case "disable": Startup.Disable(); result = Startup.State.ToString(); break;
                case "status": result = Startup.State + " line=" + Startup.LineState; break;
                case "migrate": Migration.Run(); result = Startup.State.ToString(); break;
                case "cleanup-keep": result = "left=" + Cleanup.Run(true).Count; break;
                case "cleanup-all": result = "left=" + Cleanup.Run(false).Count; break;
                case "decision": Cleanup.SaveDecision(true); result = "taken=" + Cleanup.TakeDecision(); break;
                default: result = "unknown"; break;
            }
            File.WriteAllText(output, result);
            return 0;
        }
    }
}
