using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using Microsoft.Win32;

namespace LineTray
{
    // Colours follow Windows 11 Settings: light/dark from the system theme and the user's
    // accent colour from the accent palette Windows keeps for the current user.
    internal static class Theme
    {
        private static readonly string[] Names = { "WindowBrush", "CardBrush", "CardBorderBrush", "TextBrush", "SecondaryBrush",
            "ControlBrush", "ControlBorderBrush", "ControlHoverBrush", "BandBrush", "WarnBackBrush", "WarnIconBrush",
            "InfoBackBrush", "ToggleOffBrush", "DividerBrush" };
        private static readonly string[] Light = { "#F3F3F3", "#FFFFFF", "#E5E5E5", "#1A1A1A", "#5F5F5F",
            "#FFFFFF", "#D1D1D1", "#F6F6F6", "#F3F3F3", "#FFF4CE", "#9D5D00",
            "#F6F6F6", "#8A8A8A", "#EAEAEA" };
        private static readonly string[] Dark = { "#202020", "#2B2B2B", "#1D1D1D", "#FFFFFF", "#C8C8C8",
            "#373737", "#454545", "#3D3D3D", "#202020", "#433519", "#FCE100",
            "#323232", "#A0A0A0", "#383838" };

        [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

        internal static bool SystemPrefersDark
        {
            get
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
                    return key != null && Convert.ToInt32(key.GetValue("AppsUseLightTheme", 1)) == 0;
            }
        }

        // AccentPalette holds eight RGBA colours from lightest to darkest. Settings uses the
        // second-darkest shade on light backgrounds and the second-lightest on dark ones.
        private static Color Accent(bool dark, int shift)
        {
            int index = dark ? 1 + shift : 4 + shift;
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Explorer\Accent"))
                    if (key?.GetValue("AccentPalette") is byte[] palette && palette.Length >= 32)
                        return Color.FromRgb(palette[index * 4], palette[index * 4 + 1], palette[index * 4 + 2]);
            }
            catch (Exception) { }
            return (Color)ColorConverter.ConvertFromString(dark ? (shift == 0 ? "#60CDFF" : "#99EBFF") : (shift == 0 ? "#005FB8" : "#004C94"));
        }

        internal static void Apply(ResourceDictionary resources, bool dark)
        {
            string[] colors = dark ? Dark : Light;
            for (int i = 0; i < Names.Length; i++)
                resources[Names[i]] = new SolidColorBrush((Color)ColorConverter.ConvertFromString(colors[i]));
            resources["AccentBrush"] = new SolidColorBrush(Accent(dark, 0));
            resources["AccentHoverBrush"] = new SolidColorBrush(Accent(dark, dark ? -1 : 1));
            resources["AccentTextBrush"] = new SolidColorBrush(dark ? Colors.Black : Colors.White);
        }

        // Standard title bar, coloured like the window, rounded on Windows 11.
        internal static void ApplyFrame(Window window, bool dark)
        {
            IntPtr handle = new WindowInteropHelper(window).Handle;
            if (handle == IntPtr.Zero) return;
            int round = 2, immersive = dark ? 1 : 0;
            int caption = dark ? 0x202020 : 0xF3F3F3;
            DwmSetWindowAttribute(handle, 33, ref round, sizeof(int));
            DwmSetWindowAttribute(handle, 20, ref immersive, sizeof(int));
            DwmSetWindowAttribute(handle, 35, ref caption, sizeof(int));
        }
    }
}
