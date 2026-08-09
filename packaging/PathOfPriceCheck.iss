; Windows installer for PathOfPriceCheck. Built by .github/workflows/release.yml; see
; ../docs/updater.md for why this exists at all and how the updater leans on it.
;
; Per-user by design. %LOCALAPPDATA%\Programs needs no elevation to install to and none to
; write to afterwards, which is the whole point: an install the user cannot write is an install
; the updater can only ever *offer* to update. The portable .zip keeps that degraded path, and
; is the honest answer for someone who wants a folder they can move.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\build\Release\PathOfPriceCheck.exe"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
#ifndef OutName
  #define OutName "PathOfPriceCheck-setup"
#endif

#define AppExe "PathOfPriceCheck.exe"
#define AppDisplayName "Path of Price Check"

[Setup]
; Fixed for the life of the product: this is what makes a new release replace the old install
; rather than stack up beside it in Add/Remove Programs.
AppId={{0F10A7BB-E969-4DD7-87C0-D45D22164EEC}
AppName={#AppDisplayName}
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
AppPublisher=JIRPOS
AppPublisherURL=https://github.com/JIRPOS/PathOfPriceCheck
AppSupportURL=https://github.com/JIRPOS/PathOfPriceCheck/issues
AppUpdatesURL=https://github.com/JIRPOS/PathOfPriceCheck/releases

; No vendor directory above the application, here or anywhere else this project names a path.
DefaultDirName={localappdata}\Programs\PathOfPriceCheck
; ...and no start-menu folder either: one shortcut sits directly in the programs list.
DisableProgramGroupPage=yes

PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Restart Manager closes a running copy so the .exe can be replaced. Silent runs close it
; without asking, which is what the in-app updater depends on. Bringing the app back is the
; [Run] entry below rather than this, so that the relaunch carries --updated.
CloseApplications=yes
RestartApplications=no

SetupIconFile=..\assets\popc_icon.ico
UninstallDisplayIcon={app}\{#AppExe}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutDir}
OutputBaseFilename={#OutName}
LicenseFile=..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "{#AppExe}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppDisplayName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppDisplayName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; How the application tells an installed copy from a portable one: it compares this against its
; own directory. Nothing else reads it, and it is the only key this project writes.
Root: HKCU; Subkey: "Software\PathOfPriceCheck"; ValueType: string; ValueName: "InstallDir"; \
    ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty

[UninstallDelete]
; The updater renames the running .exe aside before swapping; a copy uninstalled between that
; rename and the next launch would otherwise leave it behind.
Type: files; Name: "{app}\{#AppExe}.old"

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppDisplayName}"; \
    Flags: nowait postinstall skipifsilent
; A silent run is the in-app updater. It asks for /LAUNCH=1 only when the user pressed Restart
; now; an update applied as the application closes deliberately does not, because putting the
; app back up would be it deciding to run. --updated makes the new copy wait for the
; single-instance lock the old one is still dropping.
Filename: "{app}\{#AppExe}"; Parameters: "--updated"; Flags: nowait; Check: RelaunchRequested

[Code]
// Read through the {param:} constant, which is the only documented way to get at the command
// line here — Inno has no built-in for testing a bare switch, which is why /LAUNCH carries a
// value it does not otherwise need.
function RelaunchRequested: Boolean;
begin
  Result := ExpandConstant('{param:LAUNCH|0}') = '1';
end;
