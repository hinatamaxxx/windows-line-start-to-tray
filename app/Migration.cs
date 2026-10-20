using System;
using System.IO;
using Microsoft.Win32;

namespace LineTray
{
    // Moves an installation made by 0.2.0-preview.9 or earlier to the shortcut-based setup.
    // The only registry writes are deletions of values the old version created itself.
    internal static class Migration
    {
        private static readonly string[] OldFiles = {
            "LineTrayStart.exe", "LineTrayHook32.dll", "LineTrayHook64.dll", "Setup.exe",
            "Install.ps1", "Uninstall.ps1", "StartupRegistry.ps1", "使い方.txt", "LICENSE.txt",
            "Detours-LICENSE.txt", "startup-backup.json", "startup-backup.restored.json" };

        internal static void Run()
        {
            try { MoveAutoStart(); } catch (Exception error) { Log.Write("migrate-autostart-failed " + error.Message); }
            try { RemoveOldShortcut(); } catch (Exception error) { Log.Write("migrate-shortcut-failed " + error.Message); }
            RemoveOldFiles();
            RemoveOldHookCopies();
        }

        private static void MoveAutoStart()
        {
            bool enabled = false, found = false;
            using (RegistryKey run = Registry.CurrentUser.OpenSubKey(Paths.RunKey, true))
            {
                if (run == null) return;
                foreach (string name in new[] { Paths.LegacyRunName, Paths.OlderRunName })
                {
                    string command = run.GetValue(name) as string;
                    if (command == null || command.IndexOf(@"\LineTrayStartup\", StringComparison.OrdinalIgnoreCase) < 0) continue;
                    found = true;
                    enabled |= !Startup.IsDisabled(Paths.ApprovedRunKey, name);
                    run.DeleteValue(name, false);
                    Log.Write("migrate-removed-run " + name);
                }
            }
            if (!found) return;
            using (RegistryKey approved = Registry.CurrentUser.OpenSubKey(Paths.ApprovedRunKey, true))
            {
                approved?.DeleteValue(Paths.LegacyRunName, false);
                approved?.DeleteValue(Paths.OlderRunName, false);
            }
            if (enabled && !File.Exists(Paths.StartupShortcut)) Startup.Enable();
        }

        // Shortcuts under the old name: the 0.2.0 setup's Start menu entry (pointing at Setup.exe),
        // and the Start menu and Startup entries of 0.3.0-preview.1. Only ours are touched.
        private static void RemoveOldShortcut()
        {
            string target = Shortcut.Target(Paths.OldMenuShortcut);
            if (target != null && (target.IndexOf(@"\LineTrayStartup\", StringComparison.OrdinalIgnoreCase) >= 0 ||
                target.IndexOf(@"\WindowsLineStartToTray\", StringComparison.OrdinalIgnoreCase) >= 0))
            {
                Shortcut.Delete(Paths.OldMenuShortcut);
                Log.Write("migrate-removed-menu-shortcut");
            }
            target = Shortcut.Target(Paths.OldStartupShortcut);
            if (target != null && target.IndexOf(@"\WindowsLineStartToTray\", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                // Keep the user's choice: Windows recorded it under the old file name.
                bool enabled = !Startup.IsDisabled(Paths.ApprovedFolderKey, Path.GetFileName(Paths.OldStartupShortcut));
                Shortcut.Delete(Paths.OldStartupShortcut);
                if (enabled && !File.Exists(Paths.StartupShortcut)) Startup.Enable();
                Log.Write("migrate-renamed-startup-shortcut enabled=" + enabled);
            }
        }

        // Files still loaded by a running LINE are retried on the next start.
        private static void RemoveOldFiles()
        {
            foreach (string name in OldFiles)
            {
                string path = Path.Combine(Paths.Data, name);
                if (!File.Exists(path)) continue;
                try { File.Delete(path); Log.Write("migrate-removed-file " + name); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }

        private static void RemoveOldHookCopies()
        {
            if (!Directory.Exists(Paths.HookCache)) return;
            string current = CurrentHookFolder();
            foreach (string folder in Directory.GetDirectories(Paths.HookCache))
            {
                if (string.Equals(Path.GetFileName(folder), current, StringComparison.OrdinalIgnoreCase)) continue;
                try { Directory.Delete(folder, true); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }

        // Same naming as the launcher: the last write time of the installed 32-bit hook.
        internal static string CurrentHookFolder()
        {
            string hook = Path.Combine(Paths.AppDirectory, "LineTrayHook32.dll");
            if (!File.Exists(hook)) return null;
            long time = File.GetLastWriteTimeUtc(hook).ToFileTimeUtc();
            return ((uint)(time >> 32)).ToString("x8") + ((uint)time).ToString("x8");
        }
    }
}
