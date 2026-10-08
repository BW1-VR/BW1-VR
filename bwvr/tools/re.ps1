<#
.SYNOPSIS
    The tools to read the code of the game: the function names, and a Ghidra
    project of runblack.exe that has those names.

.DESCRIPTION
    One time:
      .\bwvr\tools\re.ps1 setup       get the names, then make the Ghidra project

    Then:
      .\bwvr\tools\re.ps1 decompile 0x005B3C70 0x005B5E70
      python bwvr\tools\disasm.py 0x005B3C70

    Each step alone:
      .\bwvr\tools\re.ps1 names       get the names (disasm.py needs only these)
      .\bwvr\tools\re.ps1 project     make the Ghidra project (some minutes)
      .\bwvr\tools\re.ps1 status      say what is there

    The names come from the public project openblack/bw1-decomp. This script
    puts one commit of it in third_party\bw1-decomp. The Ghidra project and
    the decompiled files are in local\ghidra. They hold code of the game, so
    they stay on your machine: git tracks none of these folders.

    "project" needs Ghidra 12 and a JDK 21 (JAVA_HOME, or java on the PATH).
    The script reads runblack.exe of the game folder of local\game-path.txt.
    It does not change a file of the game. The repo must be in a folder with
    none of these characters in its path: ( ) & % ! ^

.PARAMETER Address
    For "decompile": the addresses of the functions, in hex. An address inside
    a function gives that function.

.PARAMETER List
    For "decompile": a file with one address in each line. "#" starts a
    comment.

.PARAMETER Name
    For "decompile": the name of the output file, local\ghidra\out\<Name>.c.
    The default is the first address.

.PARAMETER Ghidra
    The folder of Ghidra (it holds "support\analyzeHeadless.bat"). The script
    keeps it in local\ghidra-path.txt. Without it, the script uses that file,
    then GHIDRA_INSTALL_DIR, then a folder "ghidra_*" in "tools" at the root
    of the repo.

.PARAMETER Force
    For "project": makes the project again.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('status', 'setup', 'names', 'project', 'decompile')]
    [string]$Command = 'status',
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Address,
    [string]$List,
    [string]$Name,
    [string]$Ghidra,
    [switch]$Force
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Repo        = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Bwvr        = Join-Path $Repo 'bwvr'
$LocalDir    = Join-Path $Repo 'local'
$GameFile    = Join-Path $LocalDir 'game-path.txt'
$GhidraFile  = Join-Path $LocalDir 'ghidra-path.txt'
$WorkDir     = Join-Path $LocalDir 'ghidra'
$ProjectDir  = Join-Path $WorkDir 'project'
$ProjectName = 'runblack'
$ProjectDone = Join-Path $WorkDir 'project-made.txt'
$OutDir      = Join-Path $WorkDir 'out'
$Scripts     = Join-Path $Bwvr 'ghidra'
$NamesDir    = Join-Path $Repo 'third_party\bw1-decomp'
$Symbols     = Join-Path $NamesDir 'config\BW1W120\symbols.txt'
$NamesUrl    = 'https://github.com/openblack/bw1-decomp'
# The commit of bw1-decomp that the docs of the mod were written with. To move
# to a newer one, change this, remove third_party\bw1-decomp, and run "names".
# Then run "python bwvr\tools\check_names.py", read each line of
# bwvr\ghidra\name-corrections.txt against the new file, and run
# "project -Force".
$NamesCommit = 'eebbaa16bc418e637353a407982b5dc6ce1f118d'

function Say([string]$Text) { Write-Host ('== ' + $Text) -ForegroundColor Cyan }
function Note([string]$Text) { Write-Host ('   ' + $Text) }
function Stop-Re([string]$Text) {
    Write-Host ''
    foreach ($line in ($Text -split "`n")) { Write-Host ('   ' + $line) -ForegroundColor Red }
    Write-Host ''
    exit 1
}

# The SHA-1 of the one runblack.exe that the mod and the names fit.
function Get-ExpectedSha1 {
    $header = Join-Path $Bwvr 'src\common\bw_addresses.h'
    $m = Select-String -LiteralPath $header -Pattern 'kExpectedSha1\[\]\s*=\s*"([0-9a-f]{40})"'
    if (-not $m) { Stop-Re "Cannot read kExpectedSha1 from:`n$header" }
    return $m.Matches[0].Groups[1].Value
}

# The runblack.exe of the game folder. Stops when it is not the v1.42 file.
function Get-GameExe {
    if (-not (Test-Path -LiteralPath $GameFile)) {
        Stop-Re ("No game folder is set. Run this first, at the root of the repo:`n" +
            '  .\dev.ps1 setup -Game "<the folder that holds runblack.exe>"')
    }
    $exe = Join-Path (Get-Content -LiteralPath $GameFile -TotalCount 1).Trim() 'runblack.exe'
    if (-not (Test-Path -LiteralPath $exe)) { Stop-Re "There is no runblack.exe at:`n$exe" }
    $have = (Get-FileHash -Algorithm SHA1 -LiteralPath $exe).Hash.ToLowerInvariant()
    if ($have -ne (Get-ExpectedSha1)) {
        Stop-Re "This runblack.exe is not the file of the unofficial patch v1.42, and the names do not fit it:`n$exe"
    }
    return $exe
}

function Find-Ghidra {
    $tries = @()
    if ($Ghidra) { $tries += $Ghidra }
    if (Test-Path -LiteralPath $GhidraFile) { $tries += (Get-Content -LiteralPath $GhidraFile -TotalCount 1).Trim() }
    if ($env:GHIDRA_INSTALL_DIR) { $tries += $env:GHIDRA_INSTALL_DIR }
    $tools = Join-Path $Repo 'tools'
    if (Test-Path -LiteralPath $tools) {
        $tries += @(Get-ChildItem -LiteralPath $tools -Directory -Filter 'ghidra_*' |
            Sort-Object Name -Descending | ForEach-Object { $_.FullName })
    }
    foreach ($dir in $tries) {
        if ($dir -and (Test-Path -LiteralPath (Join-Path $dir 'support\analyzeHeadless.bat'))) { return $dir }
    }
    return $null
}

function Get-Ghidra {
    $dir = Find-Ghidra
    if (-not $dir) {
        Stop-Re ("Ghidra was not found. Get Ghidra 12 (github.com/NationalSecurityAgency/ghidra, `"Releases`"),`n" +
            "unpack it, and give its folder one time:`n" +
            '  .\bwvr\tools\re.ps1 project -Ghidra "<the folder of Ghidra>"')
    }
    if ($Ghidra) {
        [void](New-Item -ItemType Directory -Force -Path $LocalDir)
        Set-Content -LiteralPath $GhidraFile -Value $dir -Encoding ascii
    }
    return $dir
}

function Find-Java {
    if ($env:JAVA_HOME -and (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\java.exe'))) { return $env:JAVA_HOME }
    $java = Get-Command java -ErrorAction SilentlyContinue
    if ($java) { return $java.Source }
    return $null
}

# Ghidra starts with batch files (analyzeHeadless.bat, launch.bat), and they
# put each argument in a line with no quotes. So an argument with one of
# these characters stops them, also inside quotes. Each argument of this
# script is a path in the repo, so the repo must be in a folder without them.
function Assert-PlainPath([string]$Path) {
    if ($Path -match '[()&%!^"]') {
        Stop-Re ("Ghidra starts with a batch file that cannot take these characters in a path: ( ) & % ! ^`n" +
            "Put the repo in a folder without them. The path is:`n$Path")
    }
}

# Runs Ghidra with no window. The command line is made here, with each
# argument in quotes, so that a space in a path is safe. The output goes to
# the log through pipes, not through ">" in the command line: launch.bat of
# Ghidra reads the command line of cmd.exe, and a ">" in it stops the batch
# file. The input is closed, so Ghidra cannot wait for an answer. Gives the
# exit code.
function Invoke-Headless([string]$GhidraDir, [string[]]$Arguments, [string]$Log) {
    $bat = Join-Path $GhidraDir 'support\analyzeHeadless.bat'
    $line = '"' + $bat + '"'
    foreach ($a in $Arguments) {
        Assert-PlainPath $a
        $line += ' "' + $a + '"'
    }
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $env:ComSpec
    $info.Arguments = '/d /s /c "' + $line + '"'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $process.StandardInput.Close()
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    [IO.File]::WriteAllText($Log, $out.Result + $err.Result)
    return $process.ExitCode
}

# One run of Ghidra at a time: a project takes one user. A second run waits.
function Enter-ProjectLock {
    $mutex = New-Object System.Threading.Mutex($false, 'bwvr-ghidra-project')
    $got = $false
    try { $got = $mutex.WaitOne([TimeSpan]::FromMinutes(30)) }
    catch [System.Threading.AbandonedMutexException] { $got = $true }
    if (-not $got) { Stop-Re 'A different run of Ghidra on this project did not end in 30 minutes.' }
    return $mutex
}

function Invoke-Names {
    if (Test-Path -LiteralPath $Symbols) { Note "names: there ($Symbols)"; return }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Stop-Re 'git is not on the PATH. Install Git for Windows.' }
    if (Test-Path -LiteralPath $NamesDir) {
        Stop-Re "This folder is there, but it has no config\BW1W120\symbols.txt. Move it away, then run this again:`n$NamesDir"
    }
    Say "Get the names: $NamesUrl, commit $($NamesCommit.Substring(0, 7))"
    # No login goes out: the project is public.
    & git init -q $NamesDir
    if ($LASTEXITCODE -eq 0) { & git -C $NamesDir remote add origin $NamesUrl }
    if ($LASTEXITCODE -eq 0) { & git -C $NamesDir -c credential.helper= fetch -q --depth 1 origin $NamesCommit }
    if ($LASTEXITCODE -eq 0) { & git -C $NamesDir -c advice.detachedHead=false checkout -q --detach FETCH_HEAD }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Symbols)) {
        Stop-Re "git did not get the names. Look at the lines above. The folder is:`n$NamesDir"
    }
    Note "names: $((Get-Content -LiteralPath $Symbols).Count) lines in $Symbols"
}

function Invoke-Project {
    $exe = Get-GameExe
    if (-not (Test-Path -LiteralPath $Symbols)) { Invoke-Names }
    $ghidraDir = Get-Ghidra
    if (-not (Find-Java)) { Stop-Re 'No JDK was found. Install a JDK 21 (adoptium.net), and set JAVA_HOME or put java on the PATH.' }
    if ((Test-Path -LiteralPath $ProjectDone) -and -not $Force) {
        Note "project: there ($ProjectDir). Use -Force to make it again."
        return
    }
    $lock = Enter-ProjectLock
    try {
        if (Test-Path -LiteralPath $ProjectDir) {
            # A folder that this script made. Never follow a link.
            $item = Get-Item -LiteralPath $ProjectDir -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Stop-Re "This is a link, not a folder. Remove it by hand:`n$ProjectDir" }
            Remove-Item -LiteralPath $ProjectDir -Recurse -Force
        }
        if (Test-Path -LiteralPath $ProjectDone) { Remove-Item -LiteralPath $ProjectDone -Force }
        [void](New-Item -ItemType Directory -Force -Path $ProjectDir)
        $log = Join-Path $WorkDir 'project.log'
        Say 'Make the Ghidra project: import, names, analysis. This takes some minutes.'
        $started = Get-Date
        # The import reads a copy of the exe in the work folder, because the
        # batch files of Ghidra stop on "(" in a path, and a game folder is
        # often in "Program Files (x86)". The copy goes away after the import.
        $copy = Join-Path $WorkDir 'runblack.exe'
        try {
            Copy-Item -LiteralPath $exe -Destination $copy -Force
            $code = Invoke-Headless $ghidraDir @(
                $ProjectDir, $ProjectName, '-import', $copy,
                '-processor', 'x86:LE:32:default', '-cspec', 'windows',
                '-scriptPath', $Scripts, '-preScript', 'ApplySymbols.java', $Symbols,
                '-analysisTimeoutPerFile', '5400') $log
        }
        finally {
            if (Test-Path -LiteralPath $copy) { Remove-Item -LiteralPath $copy -Force }
        }
        $text = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { '' }
        $names = [regex]::Match($text, 'ApplySymbols: functions (\d+), labels (\d+), fails (\d+)')
        if ($code -ne 0 -or -not $names.Success -or $text -notmatch 'REPORT: Import succeeded') {
            $why = [regex]::Match($text, 'ApplySymbols: STOPPED[^\r\n]*').Value
            Stop-Re "Ghidra did not make the project (exit code $code). $why`nThe log is:`n$log"
        }
        $minutes = [Math]::Round(((Get-Date) - $started).TotalMinutes, 1)
        Set-Content -LiteralPath $ProjectDone -Encoding ascii -Value @(
            "made $(Get-Date -Format s)",
            "ghidra $ghidraDir",
            "names $NamesCommit",
            "functions $($names.Groups[1].Value) labels $($names.Groups[2].Value) fails $($names.Groups[3].Value)")
        Note "project: made in $minutes minutes. $($names.Groups[1].Value) functions and $($names.Groups[2].Value) labels have a name."
    }
    finally { $lock.ReleaseMutex(); $lock.Dispose() }
}

function Invoke-Decompile {
    if (-not (Test-Path -LiteralPath $ProjectDone)) { Stop-Re "There is no Ghidra project. Make it first:`n  .\bwvr\tools\re.ps1 setup" }
    $ghidraDir = Get-Ghidra
    $asked = @()
    if ($Address) { $asked += @($Address | ForEach-Object { $_ -split '[,\s]+' } | Where-Object { $_ }) }
    if ($List) {
        if (-not (Test-Path -LiteralPath $List)) { Stop-Re "There is no list file at:`n$List" }
        $asked += @(Get-Content -LiteralPath $List | ForEach-Object { ($_ -replace '#.*$', '').Trim() } | Where-Object { $_ })
    }
    if ($asked.Count -eq 0) { Stop-Re "Give an address or a list:`n  .\bwvr\tools\re.ps1 decompile 0x005B3C70`n  .\bwvr\tools\re.ps1 decompile -List my-list.txt -Name hand" }
    $lines = @()
    foreach ($a in $asked) {
        if ($a -notmatch '^(0[xX])?[0-9A-Fa-f]{1,8}$') { Stop-Re "This is not an address in hex: $a" }
        $lines += ('0x{0:X8}' -f [Convert]::ToUInt32(($a -replace '^0[xX]', ''), 16))
    }
    $file = if ($Name) { $Name } else { $lines[0].Substring(2) }
    if ($file -notmatch '^[A-Za-z0-9_.-]+$') { Stop-Re "Use only letters, digits, '_', '.' and '-' in -Name: $file" }
    [void](New-Item -ItemType Directory -Force -Path $OutDir)
    $listFile = Join-Path $OutDir "$file.txt"
    $out = Join-Path $OutDir "$file.c"
    $log = Join-Path $OutDir "$file.log"
    [IO.File]::WriteAllLines($listFile, [string[]]$lines)
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Force }
    $lock = Enter-ProjectLock
    try {
        $code = Invoke-Headless $ghidraDir @(
            $ProjectDir, $ProjectName, '-process', 'runblack.exe', '-noanalysis', '-readOnly',
            '-scriptPath', $Scripts, '-postScript', 'Decompile.java', $listFile, $out) $log
    }
    finally { $lock.ReleaseMutex(); $lock.Dispose() }
    $text = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { '' }
    $result = [regex]::Match($text, 'Decompile: functions (\d+), fails (\d+)')
    if ($code -ne 0 -or -not $result.Success -or -not (Test-Path -LiteralPath $out)) {
        Stop-Re "Ghidra did not decompile (exit code $code). The log is:`n$log"
    }
    Note "$($result.Groups[1].Value) functions, $($result.Groups[2].Value) fails: $out"
}

function Show-Status {
    Say 'The tools to read the code of the game'
    if (Test-Path -LiteralPath $GameFile) {
        Note "game:    $((Get-Content -LiteralPath $GameFile -TotalCount 1).Trim())"
    } else {
        Note 'game:    not set (run .\dev.ps1 setup -Game "<folder>")'
    }
    if (Test-Path -LiteralPath $Symbols) { Note "names:   there ($Symbols)" }
    else { Note 'names:   not there (run .\bwvr\tools\re.ps1 names)' }
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        Note 'Python:  not found (disasm.py needs Python 3 and "pip install capstone")'
    } else {
        & python -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('capstone') else 1)"
        if ($LASTEXITCODE -eq 0) { Note 'Python:  there, with capstone (disasm.py can run)' }
        else { Note 'Python:  there, but disasm.py needs capstone (run: python -m pip install capstone)' }
    }
    $ghidraDir = Find-Ghidra
    if ($ghidraDir) { Note "Ghidra:  $ghidraDir" }
    else { Note 'Ghidra:  not found (give it with -Ghidra "<folder>")' }
    $java = Find-Java
    if ($java) { Note "Java:    $java" }
    else { Note 'Java:    not found (set JAVA_HOME, or put java on the PATH)' }
    if (Test-Path -LiteralPath $ProjectDone) {
        Note "project: $ProjectDir"
        Get-Content -LiteralPath $ProjectDone | ForEach-Object { Note "         $_" }
    } else {
        Note 'project: not made (run .\bwvr\tools\re.ps1 project)'
    }
}

switch ($Command) {
    'status'    { Show-Status }
    'names'     { Invoke-Names }
    'project'   { Invoke-Project }
    'setup'     { Invoke-Names; Invoke-Project }
    'decompile' { Invoke-Decompile }
}
