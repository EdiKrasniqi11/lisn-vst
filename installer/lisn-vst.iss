; LISN StemSplitter installer. From the repo root, after a Release build:
;   ISCC /DAppVersion=0.1.0 installer\lisn-vst.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define Engine "{commonappdata}\LISN\engine"

[Setup]
; Never change the AppId: it's how an upgrade finds the old install.
AppId={{E866911A-0569-4928-8FF5-A1B24278423C}
AppName=LISN StemSplitter
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoProductName=LISN StemSplitter
AppPublisher=LISN
AppPublisherURL=https://lisn.digital/
AppSupportURL=https://github.com/EdiKrasniqi11/lisn-vst/issues
DefaultDirName={autopf}\LISN StemSplitter
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
; The in-app update runs this silently while a DAW may have the plugin loaded: never offer to close apps, move the DLL aside instead.
CloseApplications=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=Output
OutputBaseFilename=LISN-StemSplitter-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\docs\design\logo\kit\app\lisn.ico
WizardSmallImageFile=..\docs\design\logo\kit\app\lisn-app-icon-256.png
UninstallDisplayName=LISN StemSplitter
; No exe to point at: the uninstaller carries SetupIconFile's icon, so Add/Remove Programs shows the logo.
UninstallDisplayIcon={uninstallexe}

[Messages]
FinishedLabelNoIcons=LISN StemSplitter is installed. Open FL Studio or Ableton Live and rescan your plugins to find it.

[Files]
Source: "..\build\StemSplitter_artefacts\Release\VST3\LISN StemSplitter.vst3\*"; DestDir: "{commoncf64}\VST3\LISN StemSplitter.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "uv.exe"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "setup-engine.ps1"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "requirements.txt"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "sitecustomize.py"; DestDir: "{app}\engine-setup"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "vc_redist.x64.exe"; DestDir: "{tmp}"

[InstallDelete]
Type: files; Name: "{commoncf64}\VST3\LISN StemSplitter.vst3\Contents\x86_64-win\*.old"

[UninstallDelete]
; The bundle folder too: Inno only removes folders it created, and a hand-copied beta may have created this one.
Type: filesandordirs; Name: "{commoncf64}\VST3\LISN StemSplitter.vst3"
Type: filesandordirs; Name: "{#Engine}"
Type: dirifempty; Name: "{commonappdata}\LISN"

[Code]
var
  EngineFailed: Boolean;

// A DAW that has the plugin loaded locks the DLL against overwrite, but not against rename: move it aside.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Dll, OldDll: String;
begin
  Result := '';
  Dll := ExpandConstant('{commoncf64}\VST3\LISN StemSplitter.vst3\Contents\x86_64-win\LISN StemSplitter.vst3');
  OldDll := Dll + '.old';
  if not FileExists(Dll) then
    Exit;
  DeleteFile(OldDll);
  if RenameFile(Dll, OldDll) then
    RestartReplace(OldDll, '')
  else if FileExists(Dll) then
    Result := 'LISN is in use. Close your DAW, then run this installer again.';
end;

// Exit code 101: installed, but the AI engine setup failed (the in-app updater reads it).
function GetCustomSetupExitCode(): Integer;
begin
  if EngineFailed then Result := 101 else Result := 0;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;

  WizardForm.StatusLabel.Caption := 'Installing the Microsoft Visual C++ runtime...';
  // 0 installed, 1638 newer one present, 3010 reboot pending: all fine. A real failure shows up in the engine check.
  Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  WizardForm.StatusLabel.Caption := 'Setting up the AI engine (about 390 MB download, a few minutes)...';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
              ExpandConstant('-NoProfile -ExecutionPolicy Bypass -File "{app}\engine-setup\setup-engine.ps1" -EngineDir "{#Engine}"'),
              '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
  begin
    EngineFailed := True;
    SuppressibleMsgBox('LISN StemSplitter is installed, but its AI engine could not be set up.' + #13#10#13#10 +
                       'Check your internet connection and run this installer again.' + #13#10 +
                       'Details: ' + ExpandConstant('{#Engine}\setup.log'), mbError, MB_OK, IDOK);
  end;
  WizardForm.ProgressGauge.Style := npbstNormal;
end;
