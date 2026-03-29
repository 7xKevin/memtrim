#define MyAppName "MemTrim Lite"
#define MyAppVersion "1.2.0"
#define MyAppPublisher "MemTrim Lite"
#define MyAppURL "https://github.com/7xKevin/memtrim"
#define MyAppExeName "MemTrimLite.exe"
#define MyAppId "{{C4030854-228A-4D9A-94AA-4A6B9F8EF201}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\MemTrim Lite
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=license.txt
OutputDir=..\dist
OutputBaseFilename=MemTrimLite-Setup
SetupIconFile=..\mem_trim.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=no
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoDescription=MemTrim Lite Installer

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "..\dist\app\MemTrimLite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\app\mem_trim.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\MemTrim Lite"; Filename: "{app}\MemTrimLite.exe"; IconFilename: "{app}\mem_trim.ico"
Name: "{autodesktop}\MemTrim Lite"; Filename: "{app}\MemTrimLite.exe"; IconFilename: "{app}\mem_trim.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\MemTrimLite.exe"; Description: "Launch MemTrim Lite"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
