; holyphp.iss -- Inno Setup script for the HolyPHP Windows installer.
;
; Produces a normal, standard Windows installer: welcome page, install-folder
; choice, PATH checkbox, progress, finish page -- plus an "Apps & features"
; entry with a proper uninstaller. No admin rights needed (per-user install).
;
; Build (make-exe.ps1 does this for you):
;   iscc /DAppVersion=1.0.0 packaging\windows\holyphp.iss
;
; Ships the compiler only: hphp.exe with runtime + stdlib embedded. No gcc, and
; no libraries -- install those with the package manager afterwards
; (`hphp pkg install ui`, `hphp pkg install websocket`).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{4F6E1B7C-9D2A-4E8B-A1C3-5D7F9B2E4A6C}
AppName=HolyPHP
AppVersion={#AppVersion}
AppPublisher=HolyPHP
DefaultDirName={autopf}\HolyPHP
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
ChangesEnvironment=yes
SetupLogging=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
OutputDir=out
OutputBaseFilename=HolyPHP-{#AppVersion}-x64-setup
Compression=lzma2/max
SolidCompression=yes
UninstallDisplayIcon={app}\hphp.exe

[Tasks]
Name: "modifypath"; Description: "Add HolyPHP to your PATH environment variable"; \
    Flags: checkedonce

[Files]
Source: "stage\hphp\hphp.exe"; DestDir: "{app}"; Flags: ignoreversion

[Code]
const
  EnvironmentKey = 'Environment';

var
  GccMissing: Boolean;

function GetUserPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Result) then
    Result := '';
end;

procedure SetUserPath(const Value: string);
begin
  { REG_EXPAND_SZ so %VAR% entries elsewhere on the PATH keep working }
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Value);
end;

procedure EnvAddPath(const AddPath: string);
var
  Paths: string;
begin
  Paths := GetUserPath;
  if Pos(';' + Uppercase(AddPath) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    Exit; { already there }
  if Paths <> '' then
    Paths := Paths + ';';
  SetUserPath(Paths + AddPath);
end;

procedure EnvRemovePath(const RemovePath: string);
var
  Paths, NewPath: string;
  Parts: TStringList;
  I: Integer;
begin
  Paths := GetUserPath;
  if Paths = '' then
    Exit;
  Parts := TStringList.Create;
  try
    Parts.Delimiter := ';';
    Parts.StrictDelimiter := True;
    Parts.DelimitedText := Paths;
    NewPath := '';
    for I := 0 to Parts.Count - 1 do
      if (Trim(Parts[I]) <> '') and (CompareText(Trim(Parts[I]), RemovePath) <> 0) then begin
        if NewPath <> '' then
          NewPath := NewPath + ';';
        NewPath := NewPath + Parts[I];
      end;
    if NewPath <> Paths then
      SetUserPath(NewPath);
  finally
    Parts.Free;
  end;
end;

{ The old script-based wizard installed to %LOCALAPPDATA%\HolyPHP; offer to
  clean it up so users do not end up with two copies on the PATH. }
procedure RemoveLegacyInstall;
var
  LegacyDir: string;
begin
  LegacyDir := ExpandConstant('{localappdata}\HolyPHP');
  if CompareText(LegacyDir, ExpandConstant('{app}')) = 0 then
    Exit;
  if not FileExists(AddBackslash(LegacyDir) + 'hphp.exe') then
    Exit;
  if MsgBox('An older HolyPHP installation was found in' + #13#10 + LegacyDir + #13#10#13#10 +
            'Remove it (including its old PATH entry)?',
            mbConfirmation, MB_YESNO) = IDYES then begin
    DelTree(LegacyDir, True, True, True);
    EnvRemovePath(LegacyDir);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;

  if WizardIsTaskSelected('modifypath') then
    EnvAddPath(ExpandConstant('{app}'));

  RemoveLegacyInstall;

  { verify the binary actually runs -- AV scanners occasionally swallow
    freshly written exes (same battle the other installers fight) }
  if (not Exec(ExpandConstant('{app}\hphp.exe'), 'version', ExpandConstant('{app}'),
       SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then begin
    Log(Format('hphp.exe did not run (exit code %d)', [ResultCode]));
    MsgBox('HolyPHP was installed, but hphp.exe did not run.' + #13#10 +
           'Your antivirus may have quarantined it -- whitelist the install' + #13#10 +
           'folder, then run "hphp version" to verify.', mbError, MB_OK);
  end;

  GccMissing := (not Exec(ExpandConstant('{cmd}'), '/C where gcc >nul 2>nul', '',
                  SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then begin
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'Libraries are not bundled -- install them with the package manager:' + #13#10 +
      '    hphp install ui' + #13#10 +
      '    hphp install websocket';
    if GccMissing then
      WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
        'Note: gcc was not found. hphp runs fine, but "hphp build/run" needs it' + #13#10 +
        'to compile programs:  winget install MSYS2.MSYS2   (or: choco install mingw)';
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    EnvRemovePath(ExpandConstant('{app}'));
end;
