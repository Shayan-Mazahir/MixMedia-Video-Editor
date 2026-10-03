; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.
;
; The Windows installer (Inno Setup). Everything the app needs is packed inside,
; so it installs fine without internet. CI builds it like this:
;   iscc /DAppVersion=0.3.0 /DSourceDir=C:\path\to\MixMedia packaging\windows\mixmedia.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\MixMedia"
#endif

[Setup]
; This ID is how Windows recognises MixMedia across updates. Never change it.
AppId={{FED47C29-85C9-4F05-8070-0A488072C72D}
AppName=MixMedia Video Editor
AppVersion={#AppVersion}
AppVerName=MixMedia Video Editor {#AppVersion}
AppPublisher=Shayan Mazahir
AppPublisherURL=https://github.com/Shayan-Mazahir/MixMedia-Video-Editor
AppSupportURL=https://github.com/Shayan-Mazahir/MixMedia-Video-Editor/issues
AppUpdatesURL=https://github.com/Shayan-Mazahir/MixMedia-Video-Editor/releases
DefaultDirName={autopf}\MixMedia
DefaultGroupName=MixMedia
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
SetupIconFile=..\mixmedia.ico
UninstallDisplayIcon={app}\mixmedia.exe
UninstallDisplayName=MixMedia Video Editor
OutputDir=..\..
OutputBaseFilename=MixMedia-Setup-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Lets people pick "just for me" (no admin needed) or "everyone on this PC"
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Put a shortcut on the desktop"; GroupDescription: "Shortcuts:"
Name: "projects"; Description: "Open .mixmedia project files with MixMedia"; GroupDescription: "File types:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\MixMedia Video Editor"; Filename: "{app}\mixmedia.exe"
Name: "{group}\Uninstall MixMedia"; Filename: "{uninstallexe}"
Name: "{autodesktop}\MixMedia Video Editor"; Filename: "{app}\mixmedia.exe"; Tasks: desktopicon

[Registry]
; Double-clicking a .mixmedia project opens it in MixMedia
Root: HKA; Subkey: "Software\Classes\.mixmedia"; ValueType: string; ValueName: ""; ValueData: "MixMedia.Project"; Flags: uninsdeletevalue; Tasks: projects
Root: HKA; Subkey: "Software\Classes\MixMedia.Project"; ValueType: string; ValueName: ""; ValueData: "MixMedia project"; Flags: uninsdeletekey; Tasks: projects
Root: HKA; Subkey: "Software\Classes\MixMedia.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\mixmedia.exe,0"; Tasks: projects
Root: HKA; Subkey: "Software\Classes\MixMedia.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\mixmedia.exe"" ""%1"""; Tasks: projects

[Run]
Filename: "{app}\mixmedia.exe"; Description: "Open MixMedia now"; Flags: nowait postinstall skipifsilent
