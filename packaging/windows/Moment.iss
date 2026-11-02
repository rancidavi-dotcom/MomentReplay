#ifndef SourceDir
#define SourceDir "dist\Moment"
#endif

#ifndef OutputDir
#define OutputDir "dist"
#endif

#ifndef AppVersion
#define AppVersion "0.1.0"
#endif

[Setup]
AppId={{F8E2DBA4-539E-4D6C-A2AC-3B86F17829E1}
AppName=Moment
AppVersion={#AppVersion}
AppPublisher=Moment
Uninstallable=yes
UninstallDisplayName=Moment
DefaultDirName={localappdata}\Programs\Moment
DefaultGroupName=Moment
UninstallDisplayIcon={app}\Moment.exe
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=Moment-Setup-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Moment"; Filename: "{app}\Moment.exe"
Name: "{autodesktop}\Moment"; Filename: "{app}\Moment.exe"; Tasks: desktopicon
Name: "{autoprograms}\Uninstall Moment"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Moment.exe"; Description: "Launch Moment"; Flags: postinstall nowait skipifsilent
