using System;
using System.Threading.Tasks;
using Velopack;
using Velopack.Sources;

namespace LineTray
{
    // Updates come from this repository's GitHub releases over HTTPS. Velopack checks the
    // package hash from the release feed before applying it.
    internal static class Updater
    {
        // LINETRAY_UPDATE_FEED points at a local release folder to test updates before publishing.
        private static UpdateManager Manager()
        {
            string feed = Environment.GetEnvironmentVariable("LINETRAY_UPDATE_FEED");
            return string.IsNullOrEmpty(feed) ? new UpdateManager(new GithubSource(Paths.RepositoryUrl, null, true)) : new UpdateManager(feed);
        }

        internal static string CurrentVersion
        {
            get
            {
                try { return Manager().CurrentVersion?.ToString(); }
                catch (Exception) { return null; }
            }
        }

        internal static bool Installed
        {
            get
            {
                try { return Manager().IsInstalled; }
                catch (Exception) { return false; }
            }
        }

        internal static async Task<UpdateInfo> CheckAsync()
        {
            UpdateManager manager = Manager();
            if (!manager.IsInstalled) return null;
            UpdateInfo info = await manager.CheckForUpdatesAsync().ConfigureAwait(false);
            Log.Write(info == null ? "update-none" : "update-found " + info.TargetFullRelease.Version);
            return info;
        }

        internal static async Task DownloadAsync(UpdateInfo info, Action<int> progress)
        {
            await Manager().DownloadUpdatesAsync(info, progress).ConfigureAwait(false);
            Log.Write("update-downloaded " + info.TargetFullRelease.Version);
        }

        internal static void ApplyAndRestart(UpdateInfo info)
        {
            Log.Write("update-apply-restart " + info.TargetFullRelease.Version);
            Manager().ApplyUpdatesAndRestart(info.TargetFullRelease);
        }

        // Used at sign-in: apply silently after this process exits, without relaunching.
        internal static void ApplyAfterExit(UpdateInfo info)
        {
            Log.Write("update-apply-on-exit " + info.TargetFullRelease.Version);
            Manager().WaitExitThenApplyUpdates(info.TargetFullRelease, true, false);
        }
    }
}
