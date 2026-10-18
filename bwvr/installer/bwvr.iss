; Black & White VR Mod: the Windows installer.
;
; Built with Inno Setup 6.7.3 or later. Compile it with tools\package.ps1,
; which builds the mod, stages every file, checks the third party binaries
; and then runs ISCC on this script.
;
; What this installer does NOT do:
;   - It never ships a game file, and it never writes to one.
;   - It never touches Profiles\ (the saved games) or Scripts\.
;   - It never writes the game's own HKCU values. An elevated Setup can hold
;     a different user's registry hive, so only the launcher, which runs as
;     the player, writes those.
;   - It never creates bwvr.ini. The mod's built-in defaults are the tested
;     setup, and the launcher writes the file when the player saves. A
;     bwvr.ini that is already there is left exactly as it is.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Payload
  #define Payload "payload"
#endif
#ifndef OutDir
  #define OutDir "..\out"
#endif
#ifndef TestBuild
  #define TestBuild 0
#endif

#define AppName "Black & White VR Mod"
#define AppPublisher "Buck"
#define AppCopyright "Copyright (C) 2026 Buck"
#define SupportUrl "https://ko-fi.com/buckzor"
#define LauncherExe "Black & White VR Mod.exe"

#if TestBuild
  #define OutBase AppName + " " + AppVersion + " Setup (test)"
#else
  #define OutBase AppName + " " + AppVersion + " Setup"
#endif

[Setup]
; Fixed forever. An upgrade must find the earlier install, so never change it.
AppId={{9B1A7467-358C-4346-94C3-64949C63BE08}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright={#AppCopyright}
AppSupportURL={#SupportUrl}
AppUpdatesURL={#SupportUrl}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoCopyright={#AppCopyright}
VersionInfoDescription={#AppName} Setup

; The mod installs into the game folder, beside runblack.exe. The registry
; gives the default, and the wizard refuses any folder that does not hold the
; v1.42 runblack.exe. AppendDefaultDirName keeps Setup from adding a product
; name to that path, and DirExistsWarning is off because the folder must
; already exist.
DefaultDirName={code:GetGameDir}
AppendDefaultDirName=no
DirExistsWarning=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no

; The game is usually under Program Files, so Setup has to elevate to write
; there and to set the folder rights the launcher needs. admin is the honest
; choice: it asks once, up front, instead of failing half way through, and the
; uninstaller then elevates the same way. A tester can still force the other
; mode from the command line with /CURRENTUSER.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline

; The game is a 32 bit program. Setup stays 32 bit with it, so {sys} and the
; registry views match what the game sees.
ArchitecturesAllowed=x86compatible

; Restart Manager finds the game holding ddraw.dll and offers to close it.
; Nothing is restarted afterwards: the player starts the launcher.
CloseApplications=yes
RestartApplications=no

UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#LauncherExe}
; The uninstaller must not look like the game's own uninstaller, so its files
; go outside the game folder.
UninstallFilesDir={autoappdata}\{#AppName}
#if TestBuild
; The test build leaves no trace in Apps & Features.
CreateUninstallRegKey=no
#endif

OutputDir={#OutDir}
OutputBaseFilename={#OutBase}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no

#if FileExists(AddBackslash(SourcePath) + "art\launcher.ico")
SetupIconFile=art\launcher.ico
#endif
#if FileExists(AddBackslash(SourcePath) + "art\wizard-side.png")
WizardImageFile=art\wizard-side.png
#endif
#if FileExists(AddBackslash(SourcePath) + "art\wizard-small.png")
WizardSmallImageFile=art\wizard-small.png
#endif

; The third party notices, shown as an information page. It is not a licence
; the player agrees to, so it gets a page with a Next button, not an
; accept-or-decline page.
InfoBeforeFile={#Payload}\Notices.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.OpenSupportPage=Open the support page at ko-fi.com/buckzor
english.DesktopIconComment=Play Black & White in VR

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Dirs]
; Our own folder inside the game folder. It gets no extra rights: the one
; file in it that the launcher writes is given them on its own, below.
Name: "{app}\dxwrapper"

[Files]
; The mod. BeforeInstall saves a ddraw.dll that belongs to the player, and
; ignoreversion is needed because the DLL carries a version resource that a
; rebuild can leave unchanged.
Source: "{#Payload}\ddraw.dll"; DestDir: "{app}"; Flags: ignoreversion; BeforeInstall: BackUpPlayerDdraw

; The OpenXR loader. The mod imports it by name, so without this file the
; game does not start at all.
Source: "{#Payload}\openxr_loader.dll"; DestDir: "{app}"; Flags: ignoreversion

; The launcher.
Source: "{#Payload}\{#LauncherExe}"; DestDir: "{app}"; Flags: ignoreversion

; dxwrapper. The stub must keep the name ddraw.dll inside this folder,
; because dxwrapper reads its own file name to pick its mode.
Source: "{#Payload}\dxwrapper\ddraw.dll"; DestDir: "{app}\dxwrapper"; Flags: ignoreversion
Source: "{#Payload}\dxwrapper\dxwrapper.dll"; DestDir: "{app}\dxwrapper"; Flags: ignoreversion
; The launcher writes dxwrapper.ini when the player saves, so this one file
; gets modify rights for users. The DLLs beside it keep their own rights.
Source: "{#Payload}\dxwrapper\dxwrapper.ini"; DestDir: "{app}\dxwrapper"; Flags: ignoreversion; Permissions: users-modify
Source: "{#Payload}\dxwrapper\ddraw.ini"; DestDir: "{app}\dxwrapper"; Flags: ignoreversion

; The game folder already holds a readme.txt, from 2001. Windows does not
; care about the case of a file name, so ours has to be called something
; else. The names match the launcher, so the mod's files sit together.
Source: "{#Payload}\README.txt"; DestDir: "{app}"; DestName: "{#AppName} readme.txt"; Flags: ignoreversion
Source: "{#Payload}\Notices.txt"; DestDir: "{app}"; DestName: "{#AppName} notices.txt"; Flags: ignoreversion

[INI]
; Windows is not always on drive C, and the value AUTO makes dxwrapper load
; the mod's own ddraw.dll again, so the true path goes in here at install
; time. {syswow64} is the 32 bit system folder, which is what a 32 bit game
; needs.
Filename: "{app}\dxwrapper\dxwrapper.ini"; Section: "General"; Key: "RealDllPath"; String: "{syswow64}\ddraw.dll"
Filename: "{app}\dxwrapper\ddraw.ini"; Section: "General"; Key: "RealDllPath"; String: "{syswow64}\ddraw.dll"

[Registry]
; Our own key, not the game's. It records where the mod went and which
; ddraw.dll we put there, so a later install can tell our file from one that
; belongs to the player.
; The publisher key comes first so that the uninstaller, which undoes these
; in reverse, looks at it only after the key inside it has gone.
#if TestBuild
; A test build writes nothing at all to the registry.
#else
Root: HKA; Subkey: "Software\{#AppPublisher}"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\{#AppPublisher}\{#AppName}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
#endif
; A test build writes nothing at all to the registry, so an install test on a
; developer's PC leaves it exactly as it was. The checks that tell the mod's
; own ddraw.dll from a player's then fall back to reading the file itself.

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; Comment: "{cm:DesktopIconComment}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; Comment: "{cm:DesktopIconComment}"; Tasks: desktopicon

[Run]
; StringChange doubles the ampersand, or Windows turns "White" into an access
; key and the text reads "Black  White VR Mod".
; runasoriginaluser matters: Setup is elevated, and the launcher must run as
; the player so that the files it writes belong to the player.
Filename: "{app}\{#LauncherExe}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent runasoriginaluser
; Never ticked by default.
Filename: "{#SupportUrl}"; Description: "{cm:OpenSupportPage}"; Flags: shellexec nowait postinstall skipifsilent unchecked runasoriginaluser

[Code]
const
  { The game's own registry key. The game writes GameDir under HKCU. }
  GameKey = 'SOFTWARE\Lionhead Studios Ltd\Black & White';
  { Keep in step with kGameExeSha1 in bwvr/src/common/exe_check.h. }
  GameExeSha1 = '9c9aea96f046911ab1b7e496fae511ed5ebbfefe';
  GameExeSize = 8663040;
  PatchUrl = 'https://www.bwfiles.com/';

  { Our own key. HKA is HKLM for an admin install, HKCU otherwise. }
  MarkerKey = 'Software\{#AppPublisher}\{#AppName}';
  MarkerValue = 'ModDdrawSha256';

  { The name a ddraw.dll that belongs to the player is kept under. }
  BackupName = 'ddraw.dll.before-bwvr';

  { The product name in the mod's version resource. }
  ModProductName = '{#AppName}';
  { An ASCII string only the mod holds. The mod opens bwvr.ini with the ANSI
    profile calls, so this text is in the file as plain bytes. }
  ModAsciiMark = 'bwvr.ini';

  { What CheckGameDir found. }
  GameOk = 0;
  GameMissing = 1;
  GameUnreadable = 2;
  GameOtherBuild = 3;

  UsersSid = '*S-1-5-32-545';

{ ---------------------------------------------------------------- the game }

function IsGameDir(const Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + 'runblack.exe');
end;

{ The game writes GameDir under HKCU. An old install can have it under HKLM.
  The last choice is the folder that the original installer uses. }
function FindGameDir(): String;
var
  Dir: String;
begin
  if RegQueryStringValue(HKCU, GameKey, 'GameDir', Dir) and IsGameDir(Dir) then
  begin
    Result := RemoveBackslashUnlessRoot(Dir);
    Exit;
  end;
  if RegQueryStringValue(HKLM32, GameKey, 'GameDir', Dir) and IsGameDir(Dir) then
  begin
    Result := RemoveBackslashUnlessRoot(Dir);
    Exit;
  end;
  Result := ExpandConstant('{commonpf32}\Lionhead Studios Ltd\Black & White');
end;

function GetGameDir(Param: String): String;
begin
  Result := FindGameDir();
end;

{ Builds that are known and not supported, so that the message can name them. }
function KnownBuildName(const Sha1: String): String;
begin
  Result := '';
  if Sha1 = '85dc3180803153162a46d43433f8dcaa883af02c' then Result := 'version 1.00'
  else if Sha1 = '800254f0c91684024cc51bc3f479192d7fec1068' then Result := 'a changed version 1.00'
  else if Sha1 = '43ad8e54f51e0de9f97fcb884b37bf7d315c19d6' then Result := 'a changed version 1.00'
  else if Sha1 = '3c7297dae357834860ff46a89ca5d1a552747100' then Result := 'version 1.10'
  else if Sha1 = 'c9bba68e17f5ec162ff235a496779f43090508db' then Result := 'version 1.20'
  else if Sha1 = 'a0fd87fdb1352fef65ba728ab732bcb1671f67e9' then Result := 'version 1.30';
end;

#if TestBuild
{ Test builds only: /SKIPGAMECHECK=1 installs into any folder. The release
  build has no such switch. }
function SkipGameCheck(): Boolean;
begin
  Result := ExpandConstant('{param:SKIPGAMECHECK|0}') = '1';
end;
#else
function SkipGameCheck(): Boolean;
begin
  Result := False;
end;
#endif

{ Returns GameOk when Dir holds runblack.exe from the v1.42 patch.
  Otherwise Message gets the text to show.

  Every hook address in the mod is fixed for that one file, so any other
  build has to be refused. Hashing 8.6 MB costs about 20 ms, which is why the
  check runs when the folder page moves on. }
function CheckGameDir(const Dir: String; var Message: String): Integer;
var
  Exe, Sha1, Build, Found: String;
  Size: Int64;
begin
  Message := '';
  Exe := AddBackslash(Dir) + 'runblack.exe';
  if not FileExists(Exe) then
  begin
    Message := 'Setup cannot find runblack.exe in this folder:' + #13#10 +
      Dir + #13#10#13#10 +
      'Select the Black & White game folder.';
    Result := GameMissing;
    Exit;
  end;

  if not FileSize64(Exe, Size) then
  begin
    Message := 'Setup cannot read runblack.exe in this folder:' + #13#10 +
      Dir + #13#10#13#10 +
      'If the game is open, close it. Then try again.';
    Result := GameUnreadable;
    Exit;
  end;

  Sha1 := '';
  { A file this far from the right size is not the game exe. Skip the hash so
    that a huge file cannot hold Setup up. }
  if Size < 67108864 then
  begin
    try
      Sha1 := Lowercase(GetSHA1OfFile(Exe));
    except
      Message := 'Setup cannot read runblack.exe in this folder:' + #13#10 +
        Dir + #13#10#13#10 +
        'If the game is open, close it. Then try again.';
      Result := GameUnreadable;
      Exit;
    end;
  end;

  Log(Format('Game check: %s is %d bytes, SHA-1 %s', [Exe, Size, Sha1]));
  if (Size = GameExeSize) and (Sha1 = GameExeSha1) then
  begin
    Result := GameOk;
    Exit;
  end;

  Build := KnownBuildName(Sha1);
  if Build <> '' then
    Found := 'This copy of Black & White is ' + Build + '.'
  else
    Found := 'This copy of Black & White is not version 1.42.';

  Message := Found + #13#10 +
    'Black & White VR Mod works only with version 1.42, the unofficial fan ' +
    'patch.' + #13#10#13#10 +
    'To fix this:' + #13#10 +
    '1. Close Setup.' + #13#10 +
    '2. Get the unofficial v1.42 patch from bwfiles.com.' + #13#10 +
    '3. Install the patch into this folder:' + #13#10 +
    '    ' + Dir + #13#10 +
    '4. Start Setup again.' + #13#10#13#10 +
    'What Setup found:' + #13#10 +
    '    runblack.exe, ' + IntToStr(Size) + ' bytes' + #13#10 +
    '    SHA-1 ' + Sha1 + #13#10 +
    'What version 1.42 is:' + #13#10 +
    '    runblack.exe, ' + IntToStr(GameExeSize) + ' bytes' + #13#10 +
    '    SHA-1 ' + GameExeSha1;
  Result := GameOtherBuild;
end;

{ ------------------------------------------------ whose ddraw.dll is that? }

{ True when Data holds Marker, one byte per character when Stride is 1 and as
  UTF-16 when Stride is 2. Marker must be ASCII.

  Written out by hand for two reasons. A Pascal string cannot carry the zero
  bytes that a UTF-16 search string would need, and searching the bytes
  directly keeps a binary file from ever being read as text. }
function HasMarker(const Data: AnsiString; const Marker: String;
  Stride: Integer): Boolean;
var
  I, J, N, M, Last: Integer;
  Ok: Boolean;
begin
  Result := False;
  N := Length(Data);
  M := Length(Marker);
  if (M = 0) or (N < M * Stride) then Exit;
  Last := N - M * Stride + 1;
  for I := 1 to Last do
  begin
    if Data[I] <> Marker[1] then Continue;
    Ok := True;
    for J := 1 to M do
    begin
      if Data[I + (J - 1) * Stride] <> Marker[J] then
      begin
        Ok := False;
        Break;
      end;
      if (Stride = 2) and (Ord(Data[I + (J - 1) * Stride + 1]) <> 0) then
      begin
        Ok := False;
        Break;
      end;
    end;
    if Ok then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

{ True when the file at Path is a ddraw.dll the mod put there.

  Three tests, cheapest first:
    1. the SHA-256 that the last install of the mod recorded,
    2. an ASCII string that only the mod holds,
    3. the product name in the version resource, stored as UTF-16.

  Getting this wrong in one direction only costs a spare copy of a file.
  Getting it wrong in the other direction would put the mod's own DLL back as
  if it were the player's, so the answer has to be certain before it is yes. }
function IsOurDdraw(const Path: String): Boolean;
var
  Recorded, Sha256: String;
  Data: AnsiString;
begin
  Result := False;
  if not FileExists(Path) then Exit;

  if RegQueryStringValue(HKA, MarkerKey, MarkerValue, Recorded) and
     (Recorded <> '') then
  begin
    try
      Sha256 := Lowercase(GetSHA256OfFile(Path));
    except
      Sha256 := '';
    end;
    if (Sha256 <> '') and (Sha256 = Lowercase(Recorded)) then
    begin
      Log('ddraw.dll: the recorded SHA-256 matches, so it is ours');
      Result := True;
      Exit;
    end;
  end;

  if not LoadStringFromLockedFile(Path, Data) then
  begin
    Log('ddraw.dll: cannot be read, treating it as the player''s');
    Exit;
  end;

  if HasMarker(Data, ModAsciiMark, 1) then
  begin
    Log('ddraw.dll: holds the mod''s own file name, so it is ours');
    Result := True;
    Exit;
  end;
  if HasMarker(Data, ModProductName, 1) or
     HasMarker(Data, ModProductName, 2) then
  begin
    Log('ddraw.dll: holds the product name, so it is ours');
    Result := True;
    Exit;
  end;
  Log('ddraw.dll: none of the mod''s marks are in it, so it is the player''s');
end;

{ Saves a ddraw.dll that belongs to the player before the mod replaces it.
  Runs from BeforeInstall on the mod's own file.

  A ddraw.dll can already be there from DDrawCompat, dgVoodoo or cnc-ddraw.
  That file is the player's, so it is kept and put back on uninstall. An
  earlier backup is never overwritten: the first one is the real original. }
procedure BackUpPlayerDdraw();
var
  Live, Backup: String;
begin
  Live := ExpandConstant('{app}\ddraw.dll');
  Backup := ExpandConstant('{app}\' + BackupName);

  if not FileExists(Live) then Exit;
  if FileExists(Backup) then
  begin
    Log('Backup: ' + Backup + ' is already there, leaving it alone');
    Exit;
  end;
  if IsOurDdraw(Live) then Exit;

  if CopyFile(Live, Backup, True) then
    Log('Backup: kept the player''s ddraw.dll as ' + Backup)
  else
    Log('Backup: could not copy ' + Live + ' to ' + Backup);
end;

{ ------------------------------------------------------------ VirtualStore }

{ Moves the mod's own files out of one VirtualStore folder.
  They are moved, not deleted, because a redirected bwvr.ini holds settings
  that somebody chose, and it can belong to another account on this PC. It
  only has to stop hiding the real file. }
procedure SweepOneStore(const Base: String);
var
  I: Integer;
  Names: TArrayOfString;
  Live, Aside: String;
begin
  SetArrayLength(Names, 3);
  Names[0] := 'bwvr.ini';
  Names[1] := 'bwvr.log';
  Names[2] := 'dxwrapper\dxwrapper.ini';
  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Live := Base + Names[I];
    if not FileExists(Live) then Continue;
    Aside := Live + '.before-bwvr-setup';
    DeleteFile(Aside);
    if RenameFile(Live, Aside) then
      Log('VirtualStore: moved ' + Live + ' aside')
    else if DeleteFile(Live) then
      Log('VirtualStore: removed ' + Live)
    else
      Log('VirtualStore: could not move ' + Live);
  end;
end;

{ Where the user profiles live. The registry value holds %SystemDrive%, which
  ExpandConstant does not touch, so it is put in by hand. }
function ProfilesDir(): String;
var
  S: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM,
      'SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList',
      'ProfilesDirectory', S) then
  begin
    StringChange(S, '%SystemDrive%', ExpandConstant('{sd}'));
    Result := S;
  end;
  if (Pos('%', Result) > 0) or not DirExists(Result) then
    Result := ExtractFileDir(ExpandConstant('{%USERPROFILE|}'));
  if not DirExists(Result) then Result := '';
end;

{ runblack.exe has no manifest, so Windows sends its writes under Program
  Files to VirtualStore. A copy of bwvr.ini left there hides the real file
  from the game. Setup can be elevated as another account, so the sweep
  covers every profile, and it only ever deletes the mod's own file names
  under a path that mirrors the game folder. }
procedure SweepVirtualStore();
var
  Profiles, Rel, Tail: String;
  Rec: TFindRec;
begin
  Rel := ExpandConstant('{app}');
  { VirtualStore drops the drive letter and the colon. }
  if (Length(Rel) > 2) and (Rel[2] = ':') then
    Rel := Copy(Rel, 4, Length(Rel) - 3)
  else
    Exit;
  Tail := '\AppData\Local\VirtualStore\' + AddBackslash(Rel);

  { The account Setup runs as, first and for certain. }
  SweepOneStore(AddBackslash(ExpandConstant('{localappdata}')) +
                'VirtualStore\' + AddBackslash(Rel));

  Profiles := ProfilesDir();
  if Profiles = '' then Exit;

  if FindFirst(AddBackslash(Profiles) + '*', Rec) then
  begin
    try
      repeat
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then Continue;
        if (Rec.Name = '.') or (Rec.Name = '..') then Continue;
        SweepOneStore(AddBackslash(Profiles) + Rec.Name + Tail);
      until not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

{ ------------------------------------------------------------------ rights }

{ Gives the players the rights the launcher and the mod need, and no more.

  The game folder is usually under Program Files, where a standard user
  cannot write. The launcher runs as the player and saves bwvr.ini, and the
  mod writes bwvr.log, so the folder needs the right to make new files. The
  grant carries no inheritance flags, so it lands on the folder alone: the
  game's own files keep the rights they have, and Modify on a folder does not
  include deleting what is in it. A new file belongs to the player who makes
  it, which is what lets the launcher write it again later.

  Anything that is already there gets the right by name, because a file made
  by another account would otherwise stay closed to this one. }
procedure GrantWriteRights();
var
  Icacls, Dir: String;
  Code, I: Integer;
  Files: TArrayOfString;
begin
  Icacls := ExpandConstant('{sys}\icacls.exe');
  if not FileExists(Icacls) then
  begin
    Log('Rights: icacls.exe not found, leaving the folder rights alone');
    Exit;
  end;
  Dir := ExpandConstant('{app}');

  if not Exec(Icacls, '"' + Dir + '" /grant "' + UsersSid + '":(M) /Q', '',
              SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    Log(Format('Rights: icacls on the folder returned %d', [Code]))
  else
    Log('Rights: users can make files in ' + Dir);

  SetArrayLength(Files, 3);
  Files[0] := Dir + '\bwvr.ini';
  Files[1] := Dir + '\bwvr.log';
  Files[2] := Dir + '\dxwrapper\dxwrapper.ini';
  for I := 0 to GetArrayLength(Files) - 1 do
  begin
    if not FileExists(Files[I]) then Continue;
    if not Exec(Icacls, '"' + Files[I] + '" /grant "' + UsersSid + '":(M) /Q',
                '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
      Log(Format('Rights: icacls on %s returned %d', [Files[I], Code]))
    else
      Log('Rights: users can write ' + Files[I]);
  end;
end;

{ ------------------------------------------------------------ wizard pages }

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Message: String;
  State, ErrorCode: Integer;
begin
  Result := True;
  if (CurPageID <> wpSelectDir) or SkipGameCheck() then Exit;
  State := CheckGameDir(WizardDirValue, Message);
  if State = GameOk then Exit;
  Result := False;
  if State = GameOtherBuild then
  begin
    if SuppressibleTaskDialogMsgBox('Black & White is not version 1.42',
         Message, mbError, MB_YESNO, ['Open bwfiles.com', 'OK'], 0,
         IDNO) = IDYES then
      ShellExecAsOriginalUser('open', PatchUrl, '', '', SW_SHOWNORMAL,
        ewNoWait, ErrorCode);
  end
  else
    SuppressibleMsgBox(Message, mbError, MB_OK, IDOK);
end;

{ The last guard. It also covers a silent install with /DIR=. }
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Message: String;
begin
  Result := '';
  if SkipGameCheck() then Exit;
  if CheckGameDir(ExpandConstant('{app}'), Message) <> GameOk then
    Result := Message;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Sha256: String;
begin
  if CurStep <> ssPostInstall then Exit;

  { Record what we installed, so the next install can tell our ddraw.dll from
    one the player put there. A test build writes no registry value at all. }
#if TestBuild
  Sha256 := '';
#else
  try
    Sha256 := Lowercase(GetSHA256OfFile(ExpandConstant('{app}\ddraw.dll')));
    RegWriteStringValue(HKA, MarkerKey, MarkerValue, Sha256);
  except
    Log('Could not record the SHA-256 of the mod''s ddraw.dll');
  end;
#endif

  GrantWriteRights();
  SweepVirtualStore();
end;

{ ---------------------------------------------------------------- uninstall }

{ Puts the player's ddraw.dll back, if there is one to put back. A backup
  that turns out to be the mod's own DLL is thrown away instead: restoring it
  would leave a game that loads the mod with nothing behind it. }
procedure RestorePlayerDdraw();
var
  Live, Backup: String;
begin
  Backup := ExpandConstant('{app}\' + BackupName);
  Live := ExpandConstant('{app}\ddraw.dll');
  if not FileExists(Backup) then Exit;

  if IsOurDdraw(Backup) then
  begin
    DeleteFile(Backup);
    Log('Uninstall: the backup was the mod''s own file, so it was removed');
    Exit;
  end;

  DeleteFile(Live);
  if RenameFile(Backup, Live) then
    Log('Uninstall: put the player''s ddraw.dll back')
  else
    Log('Uninstall: could not put ' + Backup + ' back as ddraw.dll');
end;

{ Deletes the files in Dir that match Pattern. Folders stay. }
procedure DeleteMatching(const Dir, Pattern: String);
var
  Rec: TFindRec;
begin
  if FindFirst(AddBackslash(Dir) + Pattern, Rec) then
  begin
    try
      repeat
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then Continue;
        DeleteFile(AddBackslash(Dir) + Rec.Name);
      until not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

{ The files the mod writes. Never a game file, never Profiles\ or Scripts\. }
procedure RemoveModFiles();
var
  Dir: String;
begin
  Dir := ExpandConstant('{app}');
  DeleteFile(Dir + '\bwvr.log');
  DeleteFile(Dir + '\bwvr-draw-probe.txt');
  DeleteFile(Dir + '\bwvr-eye-frames.txt');
  DeleteFile(Dir + '\bwvr-hint-counts.ini');
  DeleteFile(Dir + '\bwvr-hint-counts.ini.new');
  DeleteMatching(Dir, 'bwvr-old-*.log');
  DeleteMatching(Dir, 'bwvr-*.bmp');
  { The Steam Frame add-on and its log (bwvr/src/vr/bridge/README.md). }
  DelTree(Dir + '\bwvr-host', True, True, True);
end;

{ The game's own display settings, as they were before the mod.
  The launcher runs as the player, and copies ScreenW, ScreenH, FullScreen
  and ScreenD into a [game_before] section of bwvr.ini before it first
  changes them. The mod asks the game for a square window, which the plain
  game would open too, so the values go back here. -1 means the game had no
  value of its own, and then the value is removed.

  Only the hive of the account that runs the uninstall can be reached, and an
  elevated uninstall can hold another account's hive. So this only acts when
  the hive still holds the square size the launcher wrote. Any other hive was
  never changed by the mod. }
procedure RestoreGameDisplay();
var
  Ini, Key: String;
  I, Saved: Integer;
  NowW, NowH: Cardinal;
  Names: TArrayOfString;
begin
  Ini := ExpandConstant('{app}wvr.ini');
  if not FileExists(Ini) then Exit;
  if GetIniInt('game_before', 'saved', 0, 0, 0, Ini) = 0 then Exit;

  Key := GameKey + '\BWSetup';
  if not RegQueryDWordValue(HKCU, Key, 'ScreenW', NowW) then Exit;
  if not RegQueryDWordValue(HKCU, Key, 'ScreenH', NowH) then Exit;
  if NowW <> NowH then
  begin
    Log('Uninstall: the game''s size is not the one the mod set, so it stays');
    Exit;
  end;

  SetArrayLength(Names, 4);
  Names[0] := 'ScreenW';
  Names[1] := 'ScreenH';
  Names[2] := 'FullScreen';
  Names[3] := 'ScreenD';
  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    Saved := GetIniInt('game_before', Names[I], -1, -1, 100000, Ini);
    if Saved >= 0 then
    begin
      if RegWriteDWordValue(HKCU, Key, Names[I], Saved) then
        Log(Format('Uninstall: %s is %d again', [Names[I], Saved]));
    end
    else
      RegDeleteValue(HKCU, Key, Names[I]);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Ini: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;

  RestorePlayerDdraw();
  RemoveModFiles();
  SweepVirtualStore();
  RestoreGameDisplay();

  Ini := ExpandConstant('{app}\bwvr.ini');
  if FileExists(Ini) then
  begin
    if SuppressibleTaskDialogMsgBox('Keep your settings?',
         'Your Black & White VR Mod settings are in this file:' + #13#10 +
         Ini + #13#10#13#10 +
         'Keep it, and a later install of the mod starts where you left off.',
         mbConfirmation, MB_YESNO, ['Keep it', 'Delete it'], 0, IDYES) = IDNO then
      DeleteFile(Ini);
  end;

  { The folder is the game's, so it stays. Only our own subfolder goes. }
  RemoveDir(ExpandConstant('{app}\dxwrapper'));
end;
