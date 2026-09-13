#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef PackageDir
  #error PackageDir must point to the verified portable application directory.
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{76DDEDB6-771D-49A2-88CF-C6A418BE2270}
AppName=iFCN
AppVersion={#AppVersion}
AppPublisher=iFCN contributors
AppPublisherURL=https://github.com/li-yangshuai/iFCN
DefaultDirName={localappdata}\Programs\iFCN
DefaultGroupName=iFCN
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=iFCN-{#AppVersion}-windows-x86_64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\iFCN.exe
LicenseFile={#PackageDir}\LICENSE
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "fileassociation"; Description: "Associate .ifcn circuit files with iFCN"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\iFCN"; Filename: "{app}\iFCN.exe"
Name: "{autodesktop}\iFCN"; Filename: "{app}\iFCN.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.ifcn\OpenWithProgids"; ValueType: string; ValueName: "iFCN.Circuit"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKCU; Subkey: "Software\Classes\iFCN.Circuit"; ValueType: string; ValueName: ""; ValueData: "iFCN circuit"; Flags: uninsdeletekey; Tasks: fileassociation
Root: HKCU; Subkey: "Software\Classes\iFCN.Circuit\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\iFCN.exe"" ""%1"""; Tasks: fileassociation

[Run]
Filename: "{app}\iFCN.exe"; Description: "Launch iFCN"; Flags: nowait postinstall skipifsilent
