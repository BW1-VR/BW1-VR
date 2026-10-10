# The seconds since the last input of a person.
# The owner's "test now": while the time in test-now-until.txt is in the
# future (Test-BwvrTestNow of game.ps1 reads the file), the wait for an idle
# PC is off, and this gives a large number.
. (Join-Path $PSScriptRoot 'game.ps1')
if (Test-BwvrTestNow) { 99999; return }
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class IdleNow {
  [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO i);
  public static double Seconds() { LASTINPUTINFO i = new LASTINPUTINFO(); i.cbSize = 8; GetLastInputInfo(ref i); return ((uint)Environment.TickCount - i.dwTime) / 1000.0; }
}
'@
[int][IdleNow]::Seconds()
