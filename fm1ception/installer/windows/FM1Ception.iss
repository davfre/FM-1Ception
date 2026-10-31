; FM-1Ception Windows installer — Xfer Records (Inno Setup 6)
;
; Compiled by installer\windows\build_installer.bat, which passes:
;   /DAppVersion=1.0.0  /DAppLabel=1.0.0b1  /DSourceVst3=<path\to\FM-1Ception.vst3>
;   /DSourceExe=<path\to\FM-1Ception.exe>  /DOutputDir=<Installers dir>
;   /DSignTool=fm1sign + /Sfm1sign=...  (optional Authenticode)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppLabel
  #define AppLabel AppVersion
#endif
#ifndef SourceVst3
  #define SourceVst3 "C:\fm1ception-build\x64\FM1Ception_artefacts\Release\VST3\FM-1Ception.vst3"
#endif
#ifndef SourceExe
  #define SourceExe "C:\fm1ception-build\x64\FM1Ception_artefacts\Release\Standalone\FM-1Ception.exe"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\Installers"
#endif

#define AppName      "FM-1Ception"
#define Publisher    "Xfer Records"
#define PublisherURL "https://github.com/stevexfer/FM-1Ception"

[Setup]
AppId={{6C0B6E8E-3F5A-4C1B-9B7E-2F1D4A8C9E31}
AppName={#AppName}
AppVersion={#AppLabel}
AppVerName={#AppName} {#AppLabel}
AppPublisher={#Publisher}
AppPublisherURL={#PublisherURL}
AppSupportURL={#PublisherURL}
AppUpdatesURL={#PublisherURL}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#Publisher}
VersionInfoDescription={#AppName} Setup
VersionInfoCopyright={#Publisher}

DefaultDirName={autopf}\Xfer Records\{#AppName}
DisableDirPage=yes
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\License.txt
OutputDir={#OutputDir}
OutputBaseFilename=Install_{#AppName}_{#AppLabel}_Win64
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppName}.exe

; x64 only: hosts on ARM64 Windows run x64 plug-ins under emulation.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; The VST3 folder lives under Program Files.
PrivilegesRequired=admin

#ifdef SignTool
SignTool={#SignTool}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
; The standard VST3 location every host scans.
Source: "{#SourceVst3}\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceExe}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\License.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: standalone

[UninstallDelete]
Type: dirifempty; Name: "{commoncf64}\VST3\{#AppName}.vst3"

[Messages]
FinishedLabelNoIcons=Setup has installed [name].%n%nThe VST3 is in C:\Program Files\Common Files\VST3: rescan plug-ins in your DAW to find it.
FinishedLabel=Setup has installed [name].%n%nThe VST3 is in C:\Program Files\Common Files\VST3: rescan plug-ins in your DAW to find it. The standalone app is in the Start menu.
