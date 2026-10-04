using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Management;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

[assembly: AssemblyTitle("Windows版LINEを通知領域で起動 Setup")]
[assembly: AssemblyVersion("0.2.0.7")]
[assembly: AssemblyFileVersion("0.2.0.7")]

internal static class Program
{
    internal const string Version = "0.2.0-preview.7";
    internal const string StartupEntry = "WindowsLineStartToTray";
    internal static readonly string Home = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LineTrayStartup");
    internal static readonly string Shortcut = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Windows版LINEを通知領域で起動.lnk");
    internal static readonly string[] Payloads = { "Install.ps1", "Uninstall.ps1", "StartupRegistry.ps1", "LineTrayStart.exe", "LineTrayHook32.dll", "LineTrayHook64.dll", "LICENSE.txt", "Detours-LICENSE.txt", "GUIDE.txt" };
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern uint GetFinalPathNameByHandle(SafeFileHandle file, StringBuilder path, uint size, uint flags);

    [STAThread]
    private static int Main(string[] args)
    {
        // Used by the release smoke test; does not install or change startup settings.
        if (args.Length == 2 && args[0] == "--extract") { Extract(args[1]); return 0; }
        bool relayed = args.Length > 0 && args[0] == "--outside-package";
        if (relayed) { string[] remaining = new string[args.Length - 1]; Array.Copy(args, 1, remaining, 0, remaining.Length); args = remaining; }
        // Package virtualization can be inherited without a package identity.
        // Always enter through the system process provider once, then run normally.
        if (!relayed)
        {
            try { RelaunchOutsidePackage(args); }
            catch (Exception error)
            {
                if (args.Length == 2 && args[0] == "--install") File.WriteAllText(args[1], "ERROR: " + error.Message, Encoding.UTF8);
                else MessageBox.Show(error.Message, "セットアップを起動できませんでした", MessageBoxButton.OK, MessageBoxImage.Error);
                return 1;
            }
            return 0;
        }
        if (args.Length == 2 && args[0] == "--check-context") { File.WriteAllText(args[1], "SystemProcessProvider=relayed\nSandboxPackage=" + Environment.GetEnvironmentVariable("CODEX_WINDOWS_SANDBOX_PACKAGE_FAMILY") + "\nHome=" + Home, Encoding.UTF8); return 0; }
        if (args.Length == 2 && args[0] == "--install") return InstallWithoutWindow(args[1]);
        new Application().Run(new SetupWindow());
        return 0;
    }

    private static string Quote(string argument)
    {
        string escaped = Regex.Replace(argument, @"(\\*)""", "$1$1\\\"");
        return "\"" + Regex.Replace(escaped, @"(\\+)$", "$1$1") + "\"";
    }

    private static void RelaunchOutsidePackage(string[] args)
    {
        string executable;
        using (FileStream file = File.OpenRead(Assembly.GetExecutingAssembly().Location))
        {
            var path = new StringBuilder(32768);
            uint size = GetFinalPathNameByHandle(file.SafeFileHandle, path, (uint)path.Capacity, 0);
            if (size == 0 || size >= path.Capacity) throw new IOException("セットアップの実際の保存先を確認できませんでした。");
            executable = path.ToString();
            if (executable.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) executable = @"\\" + executable.Substring(8);
            else if (executable.StartsWith(@"\\?\")) executable = executable.Substring(4);
        }
        var command = new StringBuilder(Quote(executable) + " --outside-package");
        foreach (string argument in args) command.Append(" " + Quote(argument));
        using (var process = new ManagementClass("Win32_Process"))
        using (var startupClass = new ManagementClass("Win32_ProcessStartup"))
        using (var startup = startupClass.CreateInstance())
        using (var input = process.GetMethodParameters("Create"))
        {
            bool headless = args.Length > 0 && (args[0] == "--install" || args[0] == "--check-context");
            startup["ShowWindow"] = (ushort)(headless ? 0 : 1);
            input["CommandLine"] = command.ToString();
            input["CurrentDirectory"] = Path.GetDirectoryName(executable);
            input["ProcessStartupInformation"] = startup;
            using (var output = process.InvokeMethod("Create", input, null))
                if (Convert.ToUInt32(output["ReturnValue"]) != 0) throw new IOException("セットアップを通常起動できませんでした。");
        }
    }

    private static int InstallWithoutWindow(string report)
    {
        string temp = Path.Combine(Path.GetTempPath(), "LineTrayStartup-Setup", Guid.NewGuid().ToString("N"));
        try
        {
            Extract(temp);
            RunScript(temp, "Install.ps1");
            SaveSupportFiles(temp, Home, Assembly.GetExecutingAssembly().Location, Shortcut);
            File.WriteAllText(report, "PASS", Encoding.UTF8);
            return 0;
        }
        catch (Exception error) { File.WriteAllText(report, "ERROR: " + error.Message, Encoding.UTF8); return 1; }
        finally
        {
            string expectedRoot = Path.GetFullPath(Path.Combine(Path.GetTempPath(), "LineTrayStartup-Setup")) + Path.DirectorySeparatorChar;
            if (Path.GetFullPath(temp).StartsWith(expectedRoot, StringComparison.OrdinalIgnoreCase))
                try { Directory.Delete(temp, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
    }

    internal static void Extract(string directory)
    {
        Directory.CreateDirectory(Path.Combine(directory, "dist"));
        foreach (string name in Payloads)
        {
            string path = Path.Combine(directory, name.EndsWith(".exe") || name.EndsWith(".dll") ? "dist\\" + name : name);
            using (Stream input = Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
            {
                if (input == null) throw new IOException("セットアップに必要なファイルがありません: " + name);
                using (FileStream output = File.Create(path)) input.CopyTo(output);
            }
        }
    }

    internal static string RunScript(string root, string name)
    {
        string ps = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell\\v1.0\\powershell.exe");
        var start = new ProcessStartInfo(ps, "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + Path.Combine(root, name) + "\"");
        start.UseShellExecute = false;
        start.CreateNoWindow = true;
        start.WindowStyle = ProcessWindowStyle.Hidden;
        start.RedirectStandardOutput = true;
        start.RedirectStandardError = true;
        start.StandardOutputEncoding = Encoding.UTF8;
        start.StandardErrorEncoding = Encoding.UTF8;
        start.EnvironmentVariables["PSModulePath"] = Path.Combine(Path.GetDirectoryName(ps), "Modules") + ";" + Environment.GetEnvironmentVariable("PSModulePath");
        using (Process process = Process.Start(start))
        {
            Task<string> output = process.StandardOutput.ReadToEndAsync();
            Task<string> error = process.StandardError.ReadToEndAsync();
            process.WaitForExit();
            Task.WaitAll(output, error);
            if (process.ExitCode != 0) throw new IOException(error.Result.Length > 0 ? error.Result : output.Result);
            return output.Result;
        }
    }

    internal static void SaveSupportFiles(string root, string home, string executablePath, string shortcutPath)
    {
        string target = Path.Combine(home, "Setup.exe");
        if (!String.Equals(Path.GetFullPath(executablePath), target, StringComparison.OrdinalIgnoreCase))
            File.Copy(executablePath, target, true);
        File.Copy(Path.Combine(root, "Uninstall.ps1"), Path.Combine(home, "Uninstall.ps1"), true);
        File.Copy(Path.Combine(root, "StartupRegistry.ps1"), Path.Combine(home, "StartupRegistry.ps1"), true);
        File.Copy(Path.Combine(root, "LICENSE.txt"), Path.Combine(home, "LICENSE.txt"), true);
        File.Copy(Path.Combine(root, "Detours-LICENSE.txt"), Path.Combine(home, "Detours-LICENSE.txt"), true);
        File.Copy(Path.Combine(root, "GUIDE.txt"), Path.Combine(home, "使い方.txt"), true);
        Type shellType = Type.GetTypeFromProgID("WScript.Shell");
        object shell = Activator.CreateInstance(shellType);
        object shortcut = null;
        try
        {
            shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { shortcutPath });
            Type type = shortcut.GetType();
            type.InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, new object[] { target });
            type.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut, new object[] { home });
            type.InvokeMember("Description", BindingFlags.SetProperty, null, shortcut, new object[] { "Windows版LINEを通知領域で起動 の設定と解除" });
            type.InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
            RemoveLegacyShortcut(home, shortcutPath);
        }
        finally
        {
            if (shortcut != null) Marshal.ReleaseComObject(shortcut);
            Marshal.ReleaseComObject(shell);
        }
    }

    internal static void RemoveLegacyShortcut(string home, string shortcutPath)
    {
        string legacy = Path.Combine(Path.GetDirectoryName(shortcutPath), "LINE Tray Startup.lnk");
        if (!File.Exists(legacy)) return;
        Type shellType = Type.GetTypeFromProgID("WScript.Shell");
        object shell = Activator.CreateInstance(shellType);
        object shortcut = null;
        try
        {
            shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { legacy });
            string target = (string)shortcut.GetType().InvokeMember("TargetPath", BindingFlags.GetProperty, null, shortcut, null);
            if (String.Equals(target, Path.Combine(home, "Setup.exe"), StringComparison.OrdinalIgnoreCase))
                File.Delete(legacy);
        }
        finally
        {
            if (shortcut != null) Marshal.ReleaseComObject(shortcut);
            Marshal.ReleaseComObject(shell);
        }
    }
}
