; OSDS (Open Source Driving Simulator) Windows installer (Inno Setup 6).
; OSDS was named SimCruise until November 2026: the AppId, the program file
; (SimCruise.exe), the package folder and %LOCALAPPDATA%\SimCruise keep the old
; name so that an update installs over 2.0.x and keeps the player's settings.
;
; Build with scripts\installer.ps1, which packages nothing itself: it reads the
; Shipping package in Saved\Package\Windows (scripts\package.ps1 -Configuration
; Shipping) and passes the version and the folders to ISCC:
;
;   ISCC.exe /DAppVersion=2.0.0 /DSourceDir=<package>\Windows /DOutputDir=<out> packaging\windows\simcruise.iss
;
; The installer asks "for me only" (no administrator rights, under
; %LOCALAPPDATA%\Programs) or "for all users" (Program Files). It adds a Start
; menu entry and, if chosen, a desktop icon, and registers an uninstaller.
; Saved games, settings and logs are in %LOCALAPPDATA%\SimCruise\Saved and stay
; after an uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\Saved\Package\Windows"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\Saved\Package"
#endif

[Setup]
AppId={{6E2B8B53-4C0F-4D63-9A4B-5C1D2F0E7A91}
AppName=OSDS
AppVersion={#AppVersion}
AppVerName=OSDS {#AppVersion}
AppPublisher=OSDS contributors
AppPublisherURL=https://github.com/simcruisegame/simcruise
AppSupportURL=https://github.com/simcruisegame/simcruise/issues
AppUpdatesURL=https://github.com/simcruisegame/simcruise/releases
DefaultDirName={autopf}\OSDS
DefaultGroupName=OSDS
; An update from 2.0.x moves the Start menu entry from the old SimCruise folder to OSDS.
UsePreviousGroup=no
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=OSDS-{#AppVersion}-windows-x64-setup
SetupIconFile=osds.ico
UninstallDisplayIcon={app}\SimCruise.exe
UninstallDisplayName=OSDS {#AppVersion}
; Per-user install by default; the first page offers "all users" (needs administrator rights).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4
DiskSpanning=no
WizardStyle=modern
CloseApplications=yes
VersionInfoVersion={#NumericVersion}
VersionInfoProductName=OSDS

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An install over 2.0.0 must not keep its neural-network weights (research-only training data);
; since 2.0.1 the game ships none, so any .onnx left in the folder is old.
Type: files; Name: "{app}\SimCruise\data\ai\*.onnx"
; The 2.0.x shortcuts had the old name, SimCruise.
Type: filesandordirs; Name: "{autoprograms}\SimCruise"
Type: files; Name: "{autodesktop}\SimCruise.lnk"

[Files]
; The whole Shipping package except debug symbols, the build manifests and any local test run's Saved folder.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb,Manifest_*.txt,\SimCruise\Saved\*"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\OSDS"; Filename: "{app}\SimCruise.exe"; WorkingDir: "{app}"
Name: "{group}\OSDS controls"; Filename: "{app}\CONTROLS.md"
Name: "{group}\{cm:UninstallProgram,OSDS}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OSDS"; Filename: "{app}\SimCruise.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\SimCruise.exe"; Description: "{cm:LaunchProgram,OSDS}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Files the game writes next to itself (none in a normal Shipping run; a crash may leave a folder).
Type: filesandordirs; Name: "{app}\SimCruise\Saved"
