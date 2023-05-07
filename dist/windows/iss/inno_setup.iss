﻿
#include "inno_setup_config.isi"

[Setup]
#if APP_IS_64_BIT
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
#endif

; Windows 7. The program should also work on Vista (6.0), but this was
; not tested.
MinVersion=6.1

AppName={#APP_NAME}
AppVersion={#APP_VERSION}
AppPublisher={#APP_AUTHOR}
AppPublisherURL={#APP_URL}
AppCopyright=Copyright (c) {#APP_COPYRIGHT_YEAR} {#APP_AUTHOR}
AppSupportURL={#APP_URL}
LicenseFile={#APP_SOURCE_DIR}\LICENSE.txt

PrivilegesRequiredOverridesAllowed=dialog
; We know that we should not make user-specific changes in admin
; install mode. Our entry in [Files] that uses {localappdata} has
; "Check:" that will skip it in admin mode, so disable the warning.
UsedUserAreasWarning=no

OutputDir=.

#if APP_IS_64_BIT
#define OUTPUT_SUFFIX 64
#else
#define OUTPUT_SUFFIX 32
#endif

; We use APP_NAME instead of APP_FILE_NAME to match CPack output.
OutputBaseFilename={#APP_NAME}-{#APP_VERSION}-win{#OUTPUT_SUFFIX}

DefaultDirName={autopf}\{#APP_NAME}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#APP_FILE_NAME}.exe
SetupIconFile={#APP_SOURCE_DIR}\data\icons\{#APP_FILE_NAME}.ico

ShowLanguageDialog=auto

#define RES_DIR APP_SOURCE_DIR + "\dist\windows\iss"
WizardImageFile={#RES_DIR}\wizard.bmp
WizardSmallImageFile={#RES_DIR}\wizard_small.bmp

Compression=lzma2
SolidCompression=yes

[Files]
Source: "{#APP_FILE_NAME}.exe"; \
  DestDir: "{app}"; \
  Flags: ignoreversion
Source: "*.dll"; \
  DestDir: "{app}"; \
  Flags: ignoreversion
Source: "doc\*"; \
  DestDir: "{app}\doc"; \
  Flags: ignoreversion recursesubdirs
Source: "icons\*"; \
  DestDir: "{app}\icons"; \
  Flags: ignoreversion recursesubdirs
#if APP_USES_NLS
Source: "locale\*"; \
  DestDir: "{app}\locale"; \
  Flags: ignoreversion recursesubdirs
#endif

#define TESSDATA_DIR "tessdata"
#define TESSERACT_DATA_DIR \
  "tesseract_" + APP_TESSERACT_VERSION_MAJOR + "_data"
#define ENG_TRAINEDDATA "eng.traineddata"

; Here we try to migrate any custom user data from the previously
; installed version created by "CPack -G NSIS". At this moment, the
; old version is already removed together with the eng data, so the
; path returned by NsisGetTessdataDirPath will only exist if it
; actually has custom data. Note that there's no "uninsneveruninstall"
; flag, and we don't remove files from NsisGetTessdataDirPath. See
; comments in the code section for the details.
Source: "{code:NsisGetTessdataDirPath}\*"; \
  DestDir: "{app}\{#TESSERACT_DATA_DIR}"; \
  Check: NsisCanMigrateTessdata(); \
  Flags: ignoreversion recursesubdirs external

; Our application copies TESSERACT_DATA_DIR (if any) to the current
; user's local app data directory on first start; this is the only
; purpose of TESSERACT_DATA_DIR in the installation directory.
; However, when we install for the current user, we can do the same
; copying from the installer and avoid installing TESSERACT_DATA_DIR
; to save disk space. Therefore, there are two source lines below: for
; admin and non-admin install modes, respectively.
Source: "{#TESSERACT_DATA_DIR}\{#ENG_TRAINEDDATA}"; \
  DestDir: "{app}\{#TESSERACT_DATA_DIR}"; \
  Check: ShouldInstallEngTraineddata(); \
  Flags: ignoreversion
Source: "{#TESSERACT_DATA_DIR}\{#ENG_TRAINEDDATA}"; \
  DestDir: "{localappdata}\{#APP_FILE_NAME}\{#TESSERACT_DATA_DIR}"; \
  Check: ShouldCopyEngTraineddataForCurrentUser(); \
  Flags: ignoreversion uninsneveruninstall

#if APP_UI == "qt"
Source: "platforms\*"; \
  DestDir: "{app}\platforms"; \
  Flags: ignoreversion recursesubdirs
Source: "styles\*"; \
  DestDir: "{app}\styles"; \
  Flags: ignoreversion recursesubdirs
#if APP_USES_NLS
Source: "translations\*"; \
  DestDir: "{app}\translations"; \
  Flags: ignoreversion recursesubdirs
#endif
#endif

[Icons]
Name: "{autoprograms}\{#APP_NAME}"; \
  Filename: "{app}\{#APP_FILE_NAME}.exe"

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#if APP_USES_NLS
#include "inno_setup_languages.isi"
#endif

[Code]

procedure OurLog(const Scope, Msg: String);
begin
  Log('[{#APP_FILE_NAME}] ' + Scope + ': ' + Msg);
end;

// In previous app versions that use the installer generated by
// "cpack -G NSIS", users have to put custom Tesseract data in the
// "tessdata" directory located in the installation path.
//
// Since version 1.1.0 (which uses Inno Setup), "tessdata" was renamed
// to "tesseract_N_data", where N is the major Tesseract version.
// Instead of using data directly from "tesseract_N_data", the
// application now just copies this directory to the user's local app
// data path on the first start.
//
// When updating from an old NSIS-based version, we need to migrate
// all custom user data from "tessdata" of the old installation to
// "tesseract_N_data" of the new one. The old version used Tesseract
// 4, so we only do migration if our current Tesseract version is
// either 4 or 5 (both use the same data format).
//
// Although it's possible to just copy the data before uninstalling
// the old version, we instead take a different approach based on a
// [Files] entry with an "external" flag. The main advantage is that
// the uninstaller will remove copied data from "tesseract_N_data".
// This is more robust than using [UninstallDelete], which will wipe
// not only files that were copied from "tessdata", but also those
// that were in "tesseract_N_data" before installation. We also don't
// have to write a routine to copy a directory (Inno Setup does not
// provide one as of version 6.2).
//
// Note that we don't remove files in the "tessdata" directory after
// copying them. We can't be sure the user no longer needs them.
//
// The [Files] entry is processed after the old version is removed, so
// we have to cache some information before invoking the uninstaller.
// First is NsisTessdataDirPath that is used in "Source:". Second is
// NsisWasEngTraineddata, which tells whether the eng data existed;
// if not, this means it was intentionally removed by the user and we
// don't need to install it again.
//
// Keep in mind that our old NSIS-based installer only works in admin
// mode, i.e. it only installs for all users. If we are in non-admin
// mode, we don't touch anything NSIS-related at all.
var
  NsisUninstallerPath: String;
  // Empty if the "tessdata" of the old NSIS-based version was not
  // detected, or if we are in the non-admin install mode.
  NsisTessdataDirPath: String;
  // Only relevant if NsisTessdataDirPath is not empty and exists.
  NsisWasEngTraineddata: Boolean;

// For {code:...}
function NsisGetTessdataDirPath(Param: String): String;
begin
  if NsisTessdataDirPath <> '' then
    Result := NsisTessdataDirPath
  else
    // As of version 6.2.1, the "Source:" path of [Files] entries with
    // the external flag is processed before the "Check:" function is
    // called. We thus must make sure that the expanded source path
    // is nether an empty string nor "\*", because with the
    // recursesubdirs flag this will lead to scanning either the
    // directory of the installer or the whole drive, respectively.
    //
    // To ensure that the returned name will never exist, we use
    // some characters that Windows doesn't allow to be in file names
    // (excluding ? and * that have special meaning in the
    // FindFirstFile() function).
    Result := '<>:|';
end;

function NsisCanMigrateTessdata(): Boolean;
begin
  Result :=
  #if APP_TESSERACT_VERSION_MAJOR == "4" \
    || APP_TESSERACT_VERSION_MAJOR == "5"
    DirExists(NsisTessdataDirPath);
  #else
    False;
  #endif
end;

// The two following Should*() checks are used in "Check:"; they are
// mutually exclusive and intended for admin and non-admin install
// modes, respectively. See the [Files] section for the details.

function ShouldInstallEngTraineddata(): Boolean;
begin
  Result := IsAdminInstallMode()
    and (not NsisCanMigrateTessdata() or NsisWasEngTraineddata);
end;

function ShouldCopyEngTraineddataForCurrentUser(): Boolean;
begin
  Result := not IsAdminInstallMode()
    // To handle the case when the user updates from an old version
    // that doesn't use data from {localappdata}, we must check the
    // existence of TESSERACT_DATA_DIR rather that just the app data
    // directory.
    and not DirExists(
      ExpandConstant('{localappdata}')
      + '\{#APP_FILE_NAME}\{#TESSERACT_DATA_DIR}');
end;

function LocateRootRegKey(
  const RootKey32, RootKey64: Integer;
  const SubKeyName: String;
  var RootKey: Integer): Boolean;
begin
  Result := True;

  if RegKeyExists(RootKey32, SubKeyName) then
    RootKey := RootKey32
  else if IsWin64 and RegKeyExists(RootKey64, SubKeyName) then
    RootKey := RootKey64
  else
    Result := False;
end;

const
  UninstallStringKey = 'UninstallString';

// Return path to uninstaller created by our old installer made by
// "CPack -G NSIS", or an empty string if it's not installed or we
// are in non-admin install mode.
function NsisGetUninstallerPath(): String;
var
  LogScope: String;
  RootRegKey: Integer;
  UninstallRegPath: String;
  UninstallString: String;
  UninstallerPath: String;
  CPackInstallDir: String;
  UninstallerDir: String;
begin
  LogScope := 'NsisGetUninstallerPath';

  Result := '';

  // Our old NSIS-based installer only works in admin mode, i.e. it
  // installs for all users.
  if not IsAdminInstallMode() then
    exit;

  UninstallRegPath :=
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#APP_NAME}';

  if not LocateRootRegKey(
      HKLM32, HKLM64, UninstallRegPath, RootRegKey) then
    exit;

  if not RegQueryStringValue(
      RootRegKey, UninstallRegPath,
      UninstallStringKey, UninstallString) then
    exit;

  // Our old installer was actually built with an older CPack version
  // that doesn't add quotes around UninstallString.
  UninstallerPath := RemoveQuotes(UninstallString);
  if not FileExists(UninstallerPath) then
  begin
    OurLog(LogScope, format(
      '"%s" doesn''t point to a file or the file ' +
      'doesn''t exist', [UninstallerPath]));
    exit;
  end;

  // Old "CPack -G NSIS" doesn't write InstallLocation to the
  // Uninstall section. Instead, it writes the path to
  // "Software\(App author)\(App name)". Use this to check if this
  // is actually our old installer. Otherwise, there is no guarantee
  // that this is really an NSIS uninstaller, and we should not try to
  // execute it with NSIS-specific arguments.
  if not RegQueryStringValue(
    RootRegKey, 'Software\{#APP_AUTHOR}\{#APP_NAME}',
    '', CPackInstallDir) then
  begin
    OurLog(LogScope, 'No "Software\{#APP_AUTHOR}\{#APP_NAME}"');
    exit;
  end;

  UninstallerDir := ExtractFileDir(UninstallerPath);
  if CPackInstallDir <> UninstallerDir then
  begin
    OurLog(LogScope, format(
      'String from "Software\{#APP_AUTHOR}\{#APP_NAME}" (%s) is ' +
      'not the same as path to uninstaller directory (%s)'
      , [CPackInstallDir, UninstallerDir]));
    exit;
  end;

  Result := UninstallerPath;
end;

// Set up NsisTessdataDirPath and NsisWasEngTraineddata.
procedure NsisSetupTessdataVars(const UninstallerPath: String);
var
  LogScope: String;
  DirPath: String;
begin
  LogScope := 'NsisSetupTessdataVars';

  NsisTessdataDirPath := '';
  NsisWasEngTraineddata := False;

  if not IsAdminInstallMode() then
    exit;

  DirPath := ExtractFileDir(UninstallerPath);
  if DirPath <> '' then
  begin
    // DirExists() doesn't make sense here since the current version
    // is not uninstalled yet.
    NsisTessdataDirPath := DirPath + '\{#TESSDATA_DIR}';
    NsisWasEngTraineddata := FileExists(
      NsisTessdataDirPath + '\{#ENG_TRAINEDDATA}');

    OurLog(LogScope, format(
      'Set "{#TESSDATA_DIR}" parent to the directory of '
      + 'the uninstaller (\"%s\")'
      , [DirPath]));
    exit;
  end;

  // In case the old version is already uninstalled (either by the
  // user or by previous run of our installer), try to adopt the
  // "tessdata" dir from '{commonpf32}\{#APP_NAME}'. The NSIS-based
  // version was 32-bit only, so don't bother with {commonpf64}.
  //
  // Ideally, we should first check the target installation directory,
  // but the {app} variable is not initialized yet when this function
  // is called from InitializeSetup(). Probably the only alternative
  // is not to rely on a [Files] entry with "recursesubdirs" and
  // "external" flags, but instead to write a custom function to copy
  // a directory recursively.
  DirPath := ExpandConstant(
    '{commonpf32}\{#APP_NAME}\{#TESSDATA_DIR}');
  if DirExists(DirPath) then
  begin
    NsisTessdataDirPath := DirPath;
    NsisWasEngTraineddata := True;

    OurLog(LogScope, format('Detected "%s"' , [NsisTessdataDirPath]));
  end;
end;

// Remove an installation created by our old installer made by
// "CPack -G NSIS".
procedure NsisUninstallExisting(const UninstallerPath: String);
var
  LogScope: String;
  UninstallerDir: String;
  ExecResultCode: Integer;
begin
  LogScope := 'NsisUninstallExisting';

  if UninstallerPath = '' then
    exit;

  UninstallerDir := ExtractFileDir(UninstallerPath);

  if not Exec(
    UninstallerPath, '/S _?=' + UninstallerDir, '',
    SW_HIDE, ewWaitUntilTerminated, ExecResultCode) then
  begin
    OurLog(LogScope, format(
      'Can''t execute "%s /S _?=%s": %s (code %d)'
      , [UninstallerPath, UninstallerDir,
      SysErrorMessage(ExecResultCode), ExecResultCode]));
    exit;
  end;

  if not DeleteFile(UninstallerPath) then
  begin
    OurLog(LogScope, format(
      'Can''t delete \"%s\"', [UninstallerPath]));
    exit;
  end;

  if not RemoveDir(UninstallerDir) then
    OurLog(LogScope, format(
      'Can''t delete "%s"; the directory is probably not ' +
      'empty', [UninstallerDir]));

  OurLog(LogScope, format(
    'Uninstalled existing via "%s"', [UninstallerPath]));
end;

const
  UninstallerMutexName = 'iss_{#APP_FILE_NAME}_uninstaller_mutex';

// Returns false if mutex was not released within the given time.
function WaitForMutex(
  const MutexName: String;
  const MaxWaitTimeMilliseconds: Integer): Boolean;
var
  CheckInterval: Integer;
  RemainingWaitTime: Integer;
begin
  CheckInterval := 200;
  RemainingWaitTime := MaxWaitTimeMilliseconds;

  while True do
  begin
    if not CheckForMutexes(MutexName) then
    begin
      Result := True;
      exit;
    end;

    if RemainingWaitTime <= 0 then
      break;

    if CheckInterval > RemainingWaitTime then
      CheckInterval := RemainingWaitTime;

    Sleep(CheckInterval);

    RemainingWaitTime := RemainingWaitTime - CheckInterval;
  end;

  Result := False;
end;

// Removing the previous version is not actually necessary with Inno
// Setup: it always appends to the uninstall log, so the uninstaller
// will never leave any files, even if the user has installed several
// versions of the app with different file hierarchies. However, this
// also means that until the user uninstalls the app, unnecessary
// files from previous versions will still waste disk space, so let's
// try to uninstall the existing version automatically.
//
// Alternatively, we could use the [InstallDelete] section, but it's
// rather cumbersome to keep a list of files to be removed for all
// previous versions, and there will be no full cleanup if the user
// rolls back to a previous version.
procedure UninstallExisting();
var
  LogScope: String;
  RootRegKey: Integer;
  UninstallRegPath: String;
  UninstallString: String;
  ExecResultCode: Integer;
begin
  LogScope := 'UninstallExisting';
  UninstallRegPath :=
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\' +
    '{#APP_NAME}_is1';

  if not LocateRootRegKey(
      HKA32, HKA64, UninstallRegPath, RootRegKey) then
    exit;

  if not RegQueryStringValue(
      RootRegKey, UninstallRegPath,
      UninstallStringKey, UninstallString) then
    exit;

  if not Exec(
    '>',
    UninstallString + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART',
    '',
    SW_HIDE, ewWaitUntilTerminated, ExecResultCode) then
  begin
    OurLog(LogScope, format(
      'Can''t execute "%s": %s (code %d)'
      , [UninstallString,
      SysErrorMessage(ExecResultCode), ExecResultCode]));
    exit;
  end;

  if not WaitForMutex(UninstallerMutexName, 10000) then
    OurLog(LogScope, 'Uninstaller didn''t release the mutex');

  OurLog(LogScope, format(
    'Uninstalled existing via "%s"', [UninstallString]));
end;

function InitializeSetup(): Boolean;
begin
  // [File] entries may be processed even before the install step
  // (ssInstall in CurStepChanged()), so we cache variables as early
  // as possible so that {code:...} always gives reasonable values.
  NsisUninstallerPath := NsisGetUninstallerPath();
  NsisSetupTessdataVars(NsisUninstallerPath);

  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    NsisUninstallExisting(NsisUninstallerPath);
    UninstallExisting();
  end;
end;

function InitializeUninstall(): Boolean;
begin
  CreateMutex(UninstallerMutexName);
  Result := True;
end;
