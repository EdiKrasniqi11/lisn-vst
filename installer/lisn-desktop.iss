; LISN, the desktop app. Per-user, no admin (one UAC prompt only if the VC++ runtime is missing or old).
; From the repo root, after a Release build, with uv.exe and vc_redist.x64.exe in installer\ (see build.yml):
;   ISCC /DAppVersion=0.1.0 installer\lisn-desktop.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define Engine "{localappdata}\LISN\engine"

[Setup]
; Never change the AppId: it's how an upgrade finds the old install.
AppId={{93E72666-5647-4F9A-8152-9DA75AC91D0F}
AppName=LISN
AppVersion={#AppVersion}
AppPublisher=LISN
AppPublisherURL=https://edikrasniqi11.github.io/lisn-site/
AppSupportURL=https://github.com/EdiKrasniqi11/lisn-vst/issues
DefaultDirName={autopf}\LISN
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=LISN-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=LISN
UninstallDisplayIcon={app}\LISN.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "openwith"; Description: "Add LISN to Explorer's Open with menu for audio files"

[Files]
Source: "..\build\StemSplitter_artefacts\Release\Standalone\LISN StemSplitter.exe"; DestDir: "{app}"; DestName: "LISN.exe"; Flags: ignoreversion
Source: "uv.exe"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "setup-engine.ps1"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "requirements.txt"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "sitecustomize.py"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "vc_redist.x64.exe"; DestDir: "{tmp}"

[Icons]
Name: "{autoprograms}\LISN"; Filename: "{app}\LISN.exe"
Name: "{autodesktop}\LISN"; Filename: "{app}\LISN.exe"; Tasks: desktopicon

[Registry]
; Open with for the audio types the app accepts, never the default app. FriendlyAppName makes the menu say "LISN"
; (the exe's version resource says "LISN StemSplitter", shared with the VST).
Root: HKCU; Subkey: "Software\Classes\LISN.Audio"; ValueType: string; ValueName: ""; ValueData: "Audio file"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\LISN.Audio\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\LISN.exe"",0"; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\LISN.Audio\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "LISN"; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\LISN.Audio\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\LISN.exe"" ""%1"""; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\Applications\LISN.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "LISN"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.wav\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.flac\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.aif\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.aiff\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.ogg\OpenWithProgids"; ValueType: string; ValueName: "LISN.Audio"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith

[Run]
Filename: "{app}\LISN.exe"; Description: "Launch LISN"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{#Engine}"
Type: dirifempty; Name: "{localappdata}\LISN"
; Only the app's own settings file: another LISN product may share the folder one day.
Type: files; Name: "{userappdata}\LISN\LISN.settings"
Type: dirifempty; Name: "{userappdata}\LISN"

[Code]
// True when the x64 VC++ runtime is at least as new as the bundled redist (64-bit registry view, as Microsoft documents).
function VCRuntimeIsCurrent(): Boolean;
var
  Key: String;
  Installed, Major, Minor, Bld, MS, LS: Cardinal;
begin
  Result := False;
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if not (RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) and (Installed = 1)
          and RegQueryDWordValue(HKLM64, Key, 'Major', Major)
          and RegQueryDWordValue(HKLM64, Key, 'Minor', Minor)
          and RegQueryDWordValue(HKLM64, Key, 'Bld', Bld)) then
    Exit;
  if not GetVersionNumbers(ExpandConstant('{tmp}\vc_redist.x64.exe'), MS, LS) then
    Exit;
  Result := (Major > (MS shr 16)) or ((Major = (MS shr 16)) and
            ((Minor > (MS and $FFFF)) or ((Minor = (MS and $FFFF)) and (Bld >= (LS shr 16)))));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;

  if VCRuntimeIsCurrent() then
    Log('VC++ runtime is current, no admin prompt')
  else
  begin
    WizardForm.StatusLabel.Caption := 'Installing the Microsoft Visual C++ runtime...';
    // The one admin prompt. Declined or failed shows up below: the engine check loads torch's DLLs.
    ShellExec('runas', ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;

  WizardForm.StatusLabel.Caption := 'Setting up the AI engine (about 320 MB download, a few minutes)...';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
              ExpandConstant('-NoProfile -ExecutionPolicy Bypass -File "{app}\engine-setup\setup-engine.ps1" -EngineDir "{#Engine}"'),
              '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    MsgBox('LISN is installed, but its AI engine could not be set up.' + #13#10#13#10 +
           'Check your internet connection and run this installer again.' + #13#10 +
           'Details: ' + ExpandConstant('{#Engine}\setup.log'), mbError, MB_OK);
  WizardForm.ProgressGauge.Style := npbstNormal;
end;
