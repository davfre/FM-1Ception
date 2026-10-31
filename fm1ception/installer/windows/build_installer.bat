@echo off
setlocal EnableExtensions
rem FM-1Ception Windows installer — Xfer Records
rem
rem Builds the x64 Release VST3 and Standalone with Visual Studio and wraps them in an Inno
rem Setup installer. Run inside the Windows VM (or any Windows box with Visual Studio 2026,
rem CMake, Rust with the x86_64-pc-windows-msvc target, and Inno Setup 6):
rem   installer\windows\build_installer.bat [--skip-build]
rem
rem JUCE is the shared checkout beside the repository (..\Libraries\Juce); the emulator is the repository itself.
rem
rem Environment overrides:
rem   FM1C_BUILD_DIR      local build dir (default C:\fm1ception-build\x64). Keep it off the
rem                       network share: MSBuild and cargo are far faster on a local disk.
rem   FM1C_VS_GENERATOR   CMake generator (default "Visual Studio 18 2026")
rem   FM1C_SIGN_PFX [+ FM1C_SIGN_PFX_PASSWORD]  or  FM1C_SIGN_SHA1
rem                       Authenticode signing of the plug-in, app, installer and uninstaller.
rem                       Signing is skipped (with a notice) when neither is set.

set "SKIP_BUILD=0"
if /i "%~1"=="--skip-build" set "SKIP_BUILD=1"

rem --- Resolve the repo. %~dp0 may be a UNC path (\\Mac\Home\...), which cmd can't cd into, so the
rem --- parent of the repo is mapped to a drive letter with pushd and everything uses that drive.
for %%I in ("%~dp0..\..") do set "REPO_FULL=%%~fI"
for %%I in ("%REPO_FULL%") do set "REPO_NAME=%%~nxI"
for %%I in ("%REPO_FULL%\..") do set "PARENT_FULL=%%~fI"
pushd "%PARENT_FULL%" || (echo ERROR: cannot access %PARENT_FULL% & exit /b 1)
set "PARENT=%CD%"
if not "%PARENT:~-1%"=="\" set "PARENT=%PARENT%\"
set "SRC=%PARENT%%REPO_NAME%"

if not defined FM1C_BUILD_DIR set "FM1C_BUILD_DIR=C:\fm1ception-build\x64"
if not defined FM1C_VS_GENERATOR set "FM1C_VS_GENERATOR=Visual Studio 18 2026"
set "BUILD=%FM1C_BUILD_DIR%"
set "OUT=%SRC%\Installers"
set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
rem Inno Setup installs per-machine or per-user depending on how it was installed.
set "ISCC="
for %%p in ("%ProgramFiles(x86)%\Inno Setup 6" "%ProgramFiles%\Inno Setup 6" "%LOCALAPPDATA%\Programs\Inno Setup 6") do if not defined ISCC if exist "%%~p\ISCC.exe" set "ISCC=%%~p\ISCC.exe"
if not defined ISCC for /f "delims=" %%p in ('where ISCC.exe 2^>nul') do if not defined ISCC set "ISCC=%%p"

for /f "tokens=3" %%v in ('findstr /r /c:"^project(FM1Ception VERSION" "%SRC%\CMakeLists.txt"') do set "VERSION=%%v"
if not defined VERSION (echo ERROR: could not read the version from CMakeLists.txt & goto :fail)
set /p LABEL=<"%SRC%\installer\label.txt"
if not defined LABEL set "LABEL=%VERSION%"

echo =========================================
echo Building FM-1Ception %LABEL% Windows installer
echo =========================================
echo Source:    %SRC%
echo Build dir: %BUILD%
echo.

if not defined ISCC (echo ERROR: Inno Setup 6 not found ^(ISCC.exe^) & goto :fail)
echo Inno Setup: %ISCC%
where cmake >nul 2>nul || (echo ERROR: cmake is not on PATH & goto :fail)
where cargo >nul 2>nul || (echo ERROR: cargo is not on PATH ^(install Rust with rustup^) & goto :fail)
rustup target list --installed | findstr /c:"x86_64-pc-windows-msvc" >nul || rustup target add x86_64-pc-windows-msvc || goto :fail

rem --- 1. Build ---------------------------------------------------------------------------
rem pushd hands out whichever drive letter is free, so the cached source path can go stale;
rem CMake then refuses to configure. Start the build dir afresh when it changed.
set "SRC_FWD=%SRC:\=/%"
if "%SKIP_BUILD%"=="0" if exist "%BUILD%\CMakeCache.txt" findstr /i /c:"CMAKE_HOME_DIRECTORY:INTERNAL=%SRC_FWD%" "%BUILD%\CMakeCache.txt" >nul || (echo Source path changed; clearing %BUILD% & rmdir /s /q "%BUILD%")
if "%SKIP_BUILD%"=="0" (
    cmake -S "%SRC%" -B "%BUILD%" -G "%FM1C_VS_GENERATOR%" -A x64 -DFM1C_BUILD_TESTS=OFF || goto :fail
    cmake --build "%BUILD%" --config Release --target FM1Ception_VST3 FM1Ception_Standalone --parallel || goto :fail
)
set "VST3=%BUILD%\FM1Ception_artefacts\Release\VST3\FM-1Ception.vst3"
set "DLL=%VST3%\Contents\x86_64-win\FM-1Ception.vst3"
set "EXE=%BUILD%\FM1Ception_artefacts\Release\Standalone\FM-1Ception.exe"
if not exist "%DLL%" (echo ERROR: %DLL% not found & goto :fail)
if not exist "%EXE%" (echo ERROR: %EXE% not found & goto :fail)
echo Built %VST3%
echo Built %EXE%

rem --- 2. Optional Authenticode signing ---------------------------------------------------
set "SIGNTOOL="
for /d %%k in ("%ProgramFiles(x86)%\Windows Kits\10\bin\10.*") do if exist "%%~k\x64\signtool.exe" set "SIGNTOOL=%%~k\x64\signtool.exe"
set "SIGNARGS="
rem No parenthesised blocks below: cmd expands %SIGNTOOL% ("...Program Files (x86)...") while
rem parsing a block, and the ")" in the path would end the block early.
if defined FM1C_SIGN_PFX set "SIGNARGS=/f "%FM1C_SIGN_PFX%""
if defined FM1C_SIGN_PFX if defined FM1C_SIGN_PFX_PASSWORD set "SIGNARGS=/f "%FM1C_SIGN_PFX%" /p "%FM1C_SIGN_PFX_PASSWORD%""
if not defined FM1C_SIGN_PFX if defined FM1C_SIGN_SHA1 set "SIGNARGS=/sha1 %FM1C_SIGN_SHA1%"
set "ISCC_SIGN="
if not defined SIGNARGS goto :nosign
if not defined SIGNTOOL echo ERROR: signing requested but signtool.exe was not found in the Windows SDK
if not defined SIGNTOOL goto :fail
echo Signing %DLL% and %EXE% ...
"%SIGNTOOL%" sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 %SIGNARGS% "%DLL%" "%EXE%" || goto :fail
set "SIGNARGS_Q=%SIGNARGS:"=$q%"
set "ISCC_SIGN=/DSignTool=fm1sign "/Sfm1sign=$q%SIGNTOOL%$q sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 %SIGNARGS_Q% $f""
goto :signed
:nosign
echo NOTE: no FM1C_SIGN_PFX / FM1C_SIGN_SHA1 set - the installer will be unsigned.
:signed

rem --- 3. Installer -----------------------------------------------------------------------
if not exist "%OUT%" mkdir "%OUT%"
echo.
echo Compiling installer...
"%ISCC%" /Qp "/DAppVersion=%VERSION%" "/DAppLabel=%LABEL%" "/DSourceVst3=%VST3%" "/DSourceExe=%EXE%" "/DOutputDir=%OUT%" %ISCC_SIGN% "%SRC%\installer\windows\FM1Ception.iss" || goto :fail

echo.
echo =========================================
echo Windows installer complete
echo =========================================
echo Output: %OUT%\Install_FM-1Ception_%LABEL%_Win64.exe
popd
exit /b 0

:fail
echo.
echo *** Windows installer build FAILED ***
popd
exit /b 1
