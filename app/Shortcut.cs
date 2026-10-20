using System;
using System.IO;
using System.Reflection;

namespace LineTray
{
    internal static class Shortcut
    {
        internal static void Create(string path, string target, string arguments, string description)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(shellType);
            object link = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            Type linkType = link.GetType();
            linkType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, link, new object[] { target });
            linkType.InvokeMember("Arguments", BindingFlags.SetProperty, null, link, new object[] { arguments ?? "" });
            linkType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, link, new object[] { Path.GetDirectoryName(target) });
            linkType.InvokeMember("IconLocation", BindingFlags.SetProperty, null, link, new object[] { Paths.AppExe + ",0" });
            linkType.InvokeMember("Description", BindingFlags.SetProperty, null, link, new object[] { description });
            linkType.InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
        }

        internal static string Target(string path)
        {
            if (!File.Exists(path)) return null;
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(shellType);
            object link = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            return link.GetType().InvokeMember("TargetPath", BindingFlags.GetProperty, null, link, null) as string;
        }

        internal static void Delete(string path)
        {
            if (File.Exists(path)) File.Delete(path);
        }
    }
}
