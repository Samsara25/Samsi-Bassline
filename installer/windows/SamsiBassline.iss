; Windows-Installer (Inno Setup 6) – wird im Cloud-Build automatisch erzeugt.
; Installiert das VST3 in den Standardordner, den alle DAWs durchsuchen.
#ifndef AppVersion
  #define AppVersion "0.5.0"
#endif
#ifndef SourceVst3
  #define SourceVst3 "..\..\build\SamsiBassline_artefacts\Release\VST3\Samsi Bassline.vst3"
#endif

[Setup]
AppId={{6E0C7E1A-58B2-4F4B-9C2B-5A11B5A55B01}
AppName=Samsi Bassline
AppVersion={#AppVersion}
AppPublisher=Samsi Audio
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename=SamsiBassline-{#AppVersion}-Windows-x64-Setup
OutputDir=..\..\dist
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Samsi Bassline (VST3)
WizardStyle=modern

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceVst3}\*"; DestDir: "{commoncf64}\VST3\Samsi Bassline.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Samsi Bassline.vst3"

[Messages]
de.FinishedLabel=Samsi Bassline wurde nach C:\Program Files\Common Files\VST3 installiert.%n%nIn Ableton Live: Einstellungen > Plug-Ins > "Use VST3 Plug-in System Folders" an > Rescan.
