using System;
using System.Collections.Generic;
using System.IO;

namespace LineTray
{
    // Uninstall work the app owns. Velopack removes the program folder and its own
    // uninstall registration afterwards.
    internal static class Cleanup
    {
        // Returns the paths that could not be removed (files loaded by a running LINE).
        internal static List<string> Run(bool keepData)
        {
            var left = new List<string>();
            foreach (string shortcut in new[] { Paths.StartupShortcut, Paths.OldStartupShortcut, Paths.UninstallShortcut })
            {
                try { Shortcut.Delete(shortcut); }
                catch (Exception) { left.Add(shortcut); }
            }
            if (!keepData && Directory.Exists(Paths.Data))
            {
                foreach (string file in Directory.GetFiles(Paths.Data, "*", SearchOption.AllDirectories))
                {
                    try { File.Delete(file); }
                    catch (IOException) { left.Add(file); }
                    catch (UnauthorizedAccessException) { left.Add(file); }
                }
                var folders = new List<string>(Directory.GetDirectories(Paths.Data, "*", SearchOption.AllDirectories));
                folders.Sort((a, b) => b.Length.CompareTo(a.Length));
                folders.Add(Paths.Data);
                foreach (string folder in folders)
                {
                    try { if (Directory.GetFileSystemEntries(folder).Length == 0) Directory.Delete(folder); }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                }
            }
            return left;
        }

        internal static void SaveDecision(bool keepData)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Paths.UninstallDecision));
            File.WriteAllText(Paths.UninstallDecision, keepData ? "keep" : "remove");
        }

        // A decision made in the app's own dialog within the last ten minutes.
        internal static bool? TakeDecision()
        {
            try
            {
                if (!File.Exists(Paths.UninstallDecision)) return null;
                bool fresh = DateTime.UtcNow - File.GetLastWriteTimeUtc(Paths.UninstallDecision) < TimeSpan.FromMinutes(10);
                string value = File.ReadAllText(Paths.UninstallDecision).Trim();
                File.Delete(Paths.UninstallDecision);
                return fresh ? value == "keep" : (bool?)null;
            }
            catch (IOException) { return null; }
        }
    }
}
