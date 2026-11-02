[Setup]
AppId=MomentStandaloneUninstaller
AppName=Moment Uninstaller
AppVersion=1.0.0
AppPublisher=Moment
Uninstallable=no
CreateUninstallRegKey=no
CreateAppDir=no
PrivilegesRequired=lowest
DisableDirPage=yes
DisableProgramGroupPage=yes
WizardStyle=modern
OutputDir=..\..\dist
OutputBaseFilename=Moment-Uninstall-x64
Compression=lzma2/ultra64
SolidCompression=yes

[Code]
function HasNoPromptSwitch(): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/NO-PROMPT') = 0 then begin
      Result := True;
      Exit;
    end;
end;

function InitializeSetup(): Boolean;
var
  UninstallCommand: string;
  UninstallerPath: string;
  ExitCode: Integer;
begin
  Result := False;
  if not RegQueryStringValue(HKCU,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{F8E2DBA4-539E-4D6C-A2AC-3B86F17829E1}_is1',
    'UninstallString', UninstallCommand) then begin
    MsgBox('Moment was not found as an installed app for this Windows user.', mbInformation, MB_OK);
    Exit;
  end;

  UninstallerPath := RemoveQuotes(UninstallCommand);
  if not FileExists(UninstallerPath) then begin
    MsgBox('The Moment uninstaller is missing. Reinstall Moment, then try again.', mbError, MB_OK);
    Exit;
  end;

  if not HasNoPromptSwitch() then
    if MsgBox('Uninstall Moment now? Your clips and settings will be kept.',
      mbConfirmation, MB_YESNO) <> IDYES then
      Exit;

  if not Exec(UninstallerPath, '/SILENT /SUPPRESSMSGBOXES /NORESTART', '',
    SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode) then begin
    MsgBox('Windows could not start the Moment uninstaller.', mbError, MB_OK);
    Exit;
  end;

  if ExitCode <> 0 then
    MsgBox(Format('Moment uninstallation failed with exit code %d.', [ExitCode]), mbError, MB_OK)
  else if not HasNoPromptSwitch() then
    MsgBox('Moment was uninstalled. Your clips and settings were kept.', mbInformation, MB_OK);
end;
