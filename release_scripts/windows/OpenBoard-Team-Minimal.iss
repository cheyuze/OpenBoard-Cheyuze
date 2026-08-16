#define ProductRoot "..\..\build\win32\release\product"
#define ApplicationVersion GetVersionNumbersString(ProductRoot + "\OpenBoard.exe")

[Setup]
AppId={{D8C4AB4A-75D6-4EF4-B9AC-99A7D46316D4}
AppName=OpenBoard 车厘子定制版
AppVersion={#ApplicationVersion}
AppVerName=OpenBoard 车厘子定制版 {#ApplicationVersion}
AppPublisher=车禹泽 (cheyuze)
AppPublisherURL=https://github.com/cheyuze/OpenBoard-Cheyuze
AppSupportURL=https://github.com/cheyuze/OpenBoard-Cheyuze/issues
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DefaultDirName={autopf}\OpenBoard 车厘子定制版
DefaultGroupName=OpenBoard 车厘子定制版
OutputDir=..\..\install\win32
OutputBaseFilename=OpenBoard-cheyuze-1.7.14-x64
SetupIconFile=..\..\resources\win\OpenBoard.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
UninstallDisplayIcon={app}\OpenBoard.exe

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl"

[Messages]
SetupAppTitle=安装程序
SetupWindowTitle=安装 - %1
WizardInstalling=正在安装
InstallingLabel=请稍候，安装程序正在将 [name] 安装到您的电脑。
StatusExtractFiles=正在解压文件...
ButtonCancel=取消
FinishedHeadingLabel=[name] 安装完成
FinishedLabel=安装程序已将 [name] 安装到您的电脑。
ClickFinish=单击“完成”退出安装程序。
ButtonFinish=完成

[CustomMessages]
AdditionalIcons=附加快捷方式：
CreateDesktopIcon=创建桌面快捷方式
LaunchProgram=运行 %1

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#ProductRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\OpenBoard 车厘子定制版"; Filename: "{app}\OpenBoard.exe"
Name: "{autodesktop}\OpenBoard 车厘子定制版"; Filename: "{app}\OpenBoard.exe"; Tasks: desktopicon

[Registry]
Root: HKCR; Subkey: ".ubz"; ValueType: string; ValueName: ""; ValueData: "OpenBoardFile"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "OpenBoardFile"; ValueType: string; ValueName: ""; ValueData: "OpenBoard document"; Flags: uninsdeletekey
Root: HKCR; Subkey: "OpenBoardFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\OpenBoard.exe,1"
Root: HKCR; Subkey: "OpenBoardFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OpenBoard.exe"" ""%1"""

[Run]
Filename: "{app}\OpenBoard.exe"; Description: "{cm:LaunchProgram,OpenBoard 车厘子定制版}"; Flags: nowait postinstall skipifsilent

[Code]
procedure InitializeWizard;
var
  TrainingNotice: TNewStaticText;
begin
  TrainingNotice := TNewStaticText.Create(WizardForm);
  TrainingNotice.Parent := WizardForm.InstallingPage;
  TrainingNotice.Left := 0;
  TrainingNotice.Top := WizardForm.ProgressGauge.Top + WizardForm.ProgressGauge.Height + ScaleY(42);
  TrainingNotice.Width := WizardForm.InstallingPage.ClientWidth;
  TrainingNotice.Height := ScaleY(90);
  TrainingNotice.AutoSize := False;
  TrainingNotice.Alignment := taCenter;
  TrainingNotice.Font.Size := 14;
  TrainingNotice.Font.Color := clGreen;
  TrainingNotice.Caption :=
    '初中学部—中台教学培训，祝您月月 SSS' + #13#10 + #13#10 +
    '欢迎反馈问题和优化意见';
end;
