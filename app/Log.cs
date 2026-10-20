using System;
using System.IO;

namespace LineTray
{
    internal static class Log
    {
        internal static void Write(string message)
        {
            try
            {
                Directory.CreateDirectory(Paths.Data);
                File.AppendAllText(Paths.AppLog, DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ") + " " + message + Environment.NewLine);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }
}
