# The state of the desktop that a run must not change: the place of the real
# cursor and the window in front. Dot-source this file.
#
#   $s = Get-BwvrDeskState
#   $s.Cursor      "1147 1197", in screen pixels
#   $s.Front       the class of the window in front, or "none"
#   $s.FrontPid    the process of that window
#   $s.Idle        the seconds since the last input of a person
#   $s.At          the time of the sample
#
# Only reads. SetCursorPos of a program is not an input, so the idle time
# tells a move by a program from a move by a person.
if (-not ('BwvrDesk' -as [type])) {
    Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class BwvrDesk {
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
  [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll", CharSet = CharSet.Ansi)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO i);
  public static string Cursor() {
    POINT p; GetCursorPos(out p);
    return p.X + " " + p.Y;
  }
  public static string Front() {
    IntPtr h = GetForegroundWindow();
    if (h == IntPtr.Zero) return "none";
    StringBuilder s = new StringBuilder(256);
    GetClassName(h, s, 256);
    return s.ToString();
  }
  public static int FrontPid() {
    IntPtr h = GetForegroundWindow();
    if (h == IntPtr.Zero) return 0;
    uint pid; GetWindowThreadProcessId(h, out pid);
    return (int)pid;
  }
  public static double IdleSeconds() {
    LASTINPUTINFO i = new LASTINPUTINFO(); i.cbSize = 8;
    GetLastInputInfo(ref i);
    return ((uint)Environment.TickCount - i.dwTime) / 1000.0;
  }
}
'@
}

function Get-BwvrDeskState {
    return [pscustomobject]@{
        Cursor   = [BwvrDesk]::Cursor()
        Front    = [BwvrDesk]::Front()
        FrontPid = [BwvrDesk]::FrontPid()
        Idle     = [BwvrDesk]::IdleSeconds()
        At       = Get-Date
    }
}
