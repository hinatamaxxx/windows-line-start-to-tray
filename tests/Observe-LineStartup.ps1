param([string]$Launcher = "$env:LOCALAPPDATA\WindowsLineStartToTray\current\LineTrayStart.exe", [string]$Arguments = '')
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public class LineObserver {
  delegate void WinEvent(IntPtr hook,uint ev,IntPtr h,int obj,int child,uint tid,uint time);
  delegate void Timer(IntPtr h,uint msg,UIntPtr id,uint tick);
  delegate bool EnumWindow(IntPtr h,IntPtr p);
  [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr h; public uint m; public UIntPtr w; public IntPtr l; public uint t; public int x,y; public uint priv; }
  [DllImport("user32.dll")] static extern IntPtr SetWinEventHook(uint min,uint max,IntPtr module,WinEvent fn,uint pid,uint tid,uint flags);
  [DllImport("user32.dll")] static extern bool UnhookWinEvent(IntPtr hook);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindow cb,IntPtr p);
  [DllImport("user32.dll")] static extern int GetMessage(out MSG msg,IntPtr h,uint a,uint b);
  [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG msg);
  [DllImport("user32.dll")] static extern UIntPtr SetTimer(IntPtr h,UIntPtr id,uint delay,Timer cb);
  [DllImport("user32.dll")] static extern bool KillTimer(IntPtr h,UIntPtr id);
  [DllImport("user32.dll")] static extern void PostQuitMessage(int code);
  static void Record(string kind,IntPtr h) {
    uint pid; GetWindowThreadProcessId(h,out pid);
    try {
      using(var p=Process.GetProcessById((int)pid)) {
        if(p.ProcessName!="LINE") return;
        var cls=new StringBuilder(256); GetClassName(h,cls,256);
        Console.WriteLine(kind+" pid="+pid+" hwnd="+h+" class="+cls+" visible="+IsWindowVisible(h));
      }
    } catch(ArgumentException) {}
  }
  public static void Run(string launcher,string arguments) {
    WinEvent callback=(hook,ev,h,obj,child,tid,time)=>{if(obj==0 && child==0)Record("SHOW",h);};
    var eventHook=SetWinEventHook(0x8002,0x8002,IntPtr.Zero,callback,0,0,0);
    if(eventHook==IntPtr.Zero) throw new Exception("Observer hook failed");
    Timer stop=(h,msg,id,tick)=>PostQuitMessage(0);
    var timer=SetTimer(IntPtr.Zero,UIntPtr.Zero,20000,stop);
    Process.Start(new ProcessStartInfo(launcher,arguments){UseShellExecute=false,CreateNoWindow=true}).Dispose();
    MSG message;
    while(GetMessage(out message,IntPtr.Zero,0,0)>0) DispatchMessage(ref message);
    KillTimer(IntPtr.Zero,timer); UnhookWinEvent(eventHook);
    EnumWindows((h,p)=>{Record("FINAL",h);return true;},IntPtr.Zero);
    GC.KeepAlive(callback); GC.KeepAlive(stop);
  }
}
'@
[LineObserver]::Run($Launcher,$Arguments)
