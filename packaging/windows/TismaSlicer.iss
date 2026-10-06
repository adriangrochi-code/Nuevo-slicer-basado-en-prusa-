; Tisma Slicer: Windows installer (Inno Setup 6).
;
; Built by .github/workflows/build_windows.yml:
;   ISCC.exe /DSourceDir=<package folder> /DAppVersion=2.9.6 /DBuildNumber=<run> /DOutputDir=<folder> TismaSlicer.iss
;
; Inno Setup is free software (Inno Setup License, permissive, commercial use allowed); it is only used to build the
; installer and is not distributed with Tisma.
;
; PrusaSlicer is released under the terms of the AGPLv3 or higher

#ifndef SourceDir
  #define SourceDir "..\..\TismaSlicer"
#endif
#ifndef AppVersion
  #define AppVersion "2.9.6"
#endif
#ifndef BuildNumber
  #define BuildNumber "0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName      "Tisma Slicer"
#define AppExe       "prusa-slicer.exe"
#define ViewerExe    "prusa-gcodeviewer.exe"
#define AppPublisher "Tisma"
#define AppURL       "https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-"

[Setup]
; Fixed identifier of the application: an update replaces the previous installation. Never change it.
AppId={{EF9BF87F-A56B-4EBB-B89B-4C957B7E3899}
AppName={#AppName}
AppVersion={#AppVersion} (build {#BuildNumber})
AppVerName={#AppName} {#AppVersion}
VersionInfoVersion={#AppVersion}.{#BuildNumber}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; For all users (administrator) or only for the current user, chosen in the installer.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\resources\icons\PrusaSlicer.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=TismaSlicer-{#AppVersion}-build{#BuildNumber}-setup
; Closes a running Tisma before updating its files.
CloseApplications=yes
ChangesAssociations=yes

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.AssocModels=Abrir archivos .3mf, .stl, .obj y .step con Tisma Slicer
en.AssocModels=Open .3mf, .stl, .obj and .step files with Tisma Slicer
es.AssocGcode=Abrir archivos .gcode y .bgcode con el visor de G-code de Tisma
en.AssocGcode=Open .gcode and .bgcode files with the Tisma G-code viewer
es.Associations=Asociaciones de archivos:
en.Associations=File associations:
es.GcodeViewer=Visor de G-code de Tisma
en.GcodeViewer=Tisma G-code viewer
es.ModelFile=Modelo 3D (Tisma Slicer)
en.ModelFile=3D model (Tisma Slicer)
es.GcodeFile=G-code (Tisma Slicer)
en.GcodeFile=G-code (Tisma Slicer)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "assocmodels"; Description: "{cm:AssocModels}"; GroupDescription: "{cm:Associations}"
Name: "assocgcode"; Description: "{cm:AssocGcode}"; GroupDescription: "{cm:Associations}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; An update installs a complete new set of resources: the old ones (removed or renamed files) are deleted first.
Type: filesandordirs; Name: "{app}\resources"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autoprograms}\{cm:GcodeViewer}"; Filename: "{app}\{#ViewerExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; File associations (current user or all users, as the installation). They are removed by the uninstaller.
Root: HKA; Subkey: "Software\Classes\TismaSlicer.Model"; ValueType: string; ValueName: ""; ValueData: "{cm:ModelFile}"; Flags: uninsdeletekey; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\TismaSlicer.Model\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\TismaSlicer.Model\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\.3mf\OpenWithProgids"; ValueType: string; ValueName: "TismaSlicer.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\.stl\OpenWithProgids"; ValueType: string; ValueName: "TismaSlicer.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\.obj\OpenWithProgids"; ValueType: string; ValueName: "TismaSlicer.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\.step\OpenWithProgids"; ValueType: string; ValueName: "TismaSlicer.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\.stp\OpenWithProgids"; ValueType: string; ValueName: "TismaSlicer.Model"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmodels
Root: HKA; Subkey: "Software\Classes\TismaSlicer.Gcode"; ValueType: string; ValueName: ""; ValueData: "{cm:GcodeFile}"; Flags: uninsdeletekey; Tasks: assocgcode
Root: HKA; Subkey: "Software\Classes\TismaSlicer.Gcode\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#ViewerExe},0"; Tasks: assocgcode
Root: HKA; Subkey: "Software\Classes\TismaSlicer.Gcode\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#ViewerExe}"" ""%1"""; Tasks: assocgcode
Root: HKA; Subkey: "Software\Classes\.gcode\OpenWithProgids"; ValueType: string; ValueName: "TismaSlicer.Gcode"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocgcode
Root: HKA; Subkey: "Software\Classes\.bgcode\OpenWithProgids"; ValueType: string; ValueName: "TismaSlicer.Gcode"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocgcode

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; The settings, profiles and projects of the user (%APPDATA%\PrusaSlicer, see SLIC3R_APP_KEY in version.inc) are kept on
; uninstall.
