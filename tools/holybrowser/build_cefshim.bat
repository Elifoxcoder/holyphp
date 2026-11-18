@echo off
rem ---------------------------------------------------------------------------
rem HolyBrowser: build cefshim.dll (MSVC) and stage the CEF runtime.
rem
rem Driven through a batch file on purpose: MSYS mangles /flags into paths and
rem vcvars64.bat hangs under this shell.
rem
rem   COMPILE_EXIT / STAGE_EXIT must both be 0.
rem
rem Everything lives in holybrowser/ beside this script, so the folder can be
rem moved anywhere: the repo root is derived from %~dp0, not hard-coded.
rem ---------------------------------------------------------------------------
setlocal
set "VCROOT=C:\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDKROOT=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "PATH=%VCROOT%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%VCROOT%\include;%SDKROOT%\Include\%SDKVER%\ucrt;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\shared"
set "LIB=%VCROOT%\lib\x64;%SDKROOT%\Lib\%SDKVER%\ucrt\x64;%SDKROOT%\Lib\%SDKVER%\um\x64"

rem %~dp0 is ...\HolyPHP\holybrowser\  -> the repo root is its parent.
for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "HERE=%REPO%\holybrowser"
set "CEF=%REPO%\build\cef\cef_binary_154.0.33+ga03e714+chromium-154.0.8037.94_windows64_minimal"
set "WRAP=%REPO%\build\cef_wrap_build\libcef_dll_wrapper\Release\libcef_dll_wrapper.lib"
set "OUT=%REPO%\build\cef_runtime"

if not exist "%OUT%" mkdir "%OUT%"

echo === COMPILE cefshim.dll ===
cd /d "%REPO%"
rem No "more" in the pipeline: ERRORLEVEL after a pipe is the last command's
rem (more's, i.e. 0), which hid a failed compile for a whole session.
cl /nologo /W3 /O2 /EHsc /MT /std:c++20 /D_CRT_SECURE_NO_WARNINGS ^
   /Fo:"%OUT%\cefshim.obj" ^
   /I"%CEF%" ^
   "%HERE%\native\cefshim.cpp" ^
   "%WRAP%" "%CEF%\Release\libcef.lib" ^
   /Fe:"%OUT%\cefshim.dll" ^
   /link /DLL /SUBSYSTEM:WINDOWS ^
   user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib wininet.lib
echo COMPILE_EXIT=%ERRORLEVEL%
if errorlevel 1 goto :nodll

echo === STAGE runtime ===
rem bootstrap.exe/bootstrapc.exe are samples, not runtime; skip them.
for %%F in ("%CEF%\Release\*") do (
  echo %%~nxF | findstr /b /c:"bootstrap" >nul || copy /y "%%F" "%OUT%\" >nul
)
rem CEF looks for icudtl.dat, the .pak files and locales/ NEXT TO libcef.dll,
rem not inside a Resources subfolder: the Windows layout is flat.
copy /y "%CEF%\Resources\*.pak" "%OUT%\" >nul
copy /y "%CEF%\Resources\icudtl.dat" "%OUT%\" >nul
xcopy /e /i /y /q "%CEF%\Resources\locales" "%OUT%\locales" >nul
echo STAGE_EXIT=%ERRORLEVEL%
goto :done

:nodll
echo NO_DLL
:done
endlocal