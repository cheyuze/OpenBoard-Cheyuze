#define ProductRoot "..\..\build\win32\release\product"
#define ApplicationVersion GetVersionNumbersString(ProductRoot + "\OpenBoard.exe")

[Setup]
AppId={{D8C4AB4A-75D6-4EF4-B9AC-99A7D46316D4}
AppName=OpenBoard Team Edition
AppVersion={#ApplicationVersion}
AppVerName=OpenBoard Team Edition {#ApplicationVersion}
AppPublisher=Open Education Foundation
AppPublisherURL=https://openboard.ch/
AppSupportURL=https://openboard.ch/
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DefaultDirName={autopf}\OpenBoard Team Edition
DefaultGroupName=OpenBoard Team Edition
OutputDir=..\..\install\win32
OutputBaseFilename=OpenBoard_Team_Edition_1.7.7_x64
SetupIconFile=..\..\resources\win\OpenBoard.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
UninstallDisplayIcon={app}\OpenBoard.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#ProductRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\OpenBoard Team Edition"; Filename: "{app}\OpenBoard.exe"
Name: "{autodesktop}\OpenBoard Team Edition"; Filename: "{app}\OpenBoard.exe"; Tasks: desktopicon

[Registry]
Root: HKCR; Subkey: ".ubz"; ValueType: string; ValueName: ""; ValueData: "OpenBoardFile"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "OpenBoardFile"; ValueType: string; ValueName: ""; ValueData: "OpenBoard document"; Flags: uninsdeletekey
Root: HKCR; Subkey: "OpenBoardFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\OpenBoard.exe,1"
Root: HKCR; Subkey: "OpenBoardFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OpenBoard.exe"" ""%1"""

[Run]
Filename: "{app}\OpenBoard.exe"; Description: "{cm:LaunchProgram,OpenBoard Team Edition}"; Flags: nowait postinstall skipifsilent
