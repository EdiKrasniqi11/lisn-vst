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
; SignPath checks the signed installers' product name and version.
VersionInfoVersion={#AppVersion}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoProductName=LISN StemSplitter
AppPublisher=LISN
AppPublisherURL=https://edikrasniqi11.github.io/lisn-site/
AppSupportURL=https://github.com/EdiKrasniqi11/lisn-vst/issues
DefaultDirName={autopf}\LISN
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
; Setup's RedirectionGuard reaches the engine step's uv, which then can't follow the junction it makes (error 448). Inno's help says child processes don't inherit it, but an A/B test showed they do: a cmd started by Setup couldn't traverse its own junction by default and could with /NOREDIRECTIONGUARD. This installer runs unelevated unless started with Run as administrator, so there is nothing for the guard to protect.
RedirectionGuard=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename=LISN-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\docs\design\logo\kit\app\lisn.ico
WizardSmallImageFile=..\docs\design\logo\kit\app\lisn-app-icon-256.png
UninstallDisplayName=LISN
UninstallDisplayIcon={app}\LISN.exe
; The Open with entries carry LISN.exe's icon: have Explorer refresh its icons after install and uninstall.
ChangesAssociations=yes

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
Filename: "{app}\LISN.exe"; Description: "Launch LISN"; Flags: postinstall nowait skipifsilent; Check: VCRuntimeIsCurrent

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

// Pascal Script has no BoolToStr.
function YesNo(Value: Boolean): String;
begin
  if Value then Result := 'yes' else Result := 'no';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Started: Boolean;
begin
  if CurStep <> ssPostInstall then
    Exit;

  if VCRuntimeIsCurrent() then
    Log('VC++ runtime is current, no admin prompt')
  else
  begin
    WizardForm.StatusLabel.Caption := 'Installing the Microsoft Visual C++ runtime...';
    // The one admin prompt.
    Started := ShellExec('runas', ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    // Started = False: the prompt was declined or the redist could not start. Exit 0 or 3010 (restart pending) is success.
    Log('VC++ runtime installer: started=' + YesNo(Started) + ', exit code ' + IntToStr(ResultCode));
    // LISN.exe and torch can't load without the runtime: stop here, before the engine download blames the network.
    if not VCRuntimeIsCurrent() then
    begin
      Log('VC++ runtime is still missing or old, skipping the AI engine step');
      SuppressibleMsgBox('LISN needs the Microsoft Visual C++ runtime. Run this installer again and allow the Windows prompt.', mbError, MB_OK, IDOK);
      Exit;
    end;
  end;

  WizardForm.StatusLabel.Caption := 'Setting up the AI engine (about 320 MB download, a few minutes)...';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  Started := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
                  ExpandConstant('-NoProfile -ExecutionPolicy Bypass -File "{app}\engine-setup\setup-engine.ps1" -EngineDir "{#Engine}"'),
                  '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if not Started or (ResultCode <> 0) then
  begin
    Log('AI engine setup failed: started=' + YesNo(Started) + ', exit code ' + IntToStr(ResultCode));
    // Suppressible: a silent install (/SUPPRESSMSGBOXES) must not hang on this box; the log line above is the record.
    SuppressibleMsgBox('LISN is installed, but its AI engine could not be set up.' + #13#10#13#10 +
                       'Check your internet connection and run this installer again.' + #13#10 +
                       'Details: ' + ExpandConstant('{#Engine}\setup.log'), mbError, MB_OK, IDOK);
  end
  else
    Log('AI engine is ready');
  WizardForm.ProgressGauge.Style := npbstNormal;
end;

// Inno's CloseApplications covers Setup only. Without this, an uninstall with LISN open leaves the in-use LISN.exe behind.
// JUCE's single-instance lock is the mutex "juceAppLock_" + the app name: Global\ if it can, else Local\.
function InitializeUninstall(): Boolean;
begin
  Result := True;
  while Result and CheckForMutexes('Global\juceAppLock_LISN,Local\juceAppLock_LISN') do
    Result := SuppressibleMsgBox('LISN is running. Close it, then click OK.', mbError, MB_OKCANCEL, IDCANCEL) = IDOK;
end;
