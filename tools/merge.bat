@echo off
rem Merges the recordings dropped on this file into one new clone, with clonetrain.exe (clonetrain --merge). The new clone goes
rem beside the first one dropped. Put this file next to clonetrain.exe and dsound.dll (HalfswordUE5\Binaries\Win64).
setlocal EnableExtensions DisableDelayedExpansion
title Fetch: merge clones

set "HERE=%~dp0"
set "EXE="
if exist "%HERE%clonetrain.exe" set "EXE=%HERE%clonetrain.exe"
if not defined EXE if exist "%HERE%..\clonetrain.exe" set "EXE=%HERE%..\clonetrain.exe"
if not defined EXE (
    echo clonetrain.exe is not next to this file.
    echo Put merge.bat and clonetrain.exe in the game's Win64 folder, next to dsound.dll.
    pause
    exit /b 1
)
"%EXE%" --help 2>&1 | findstr /c:"--merge" >nul || (
    echo This clonetrain.exe cannot merge: it is from before Fetch pre12.
    pause
    exit /b 1
)
if "%~2"=="" (
    echo Drop two .hsmclone recordings or more on merge.bat, all at once.
    echo The clone looks like the first one that has a look saved. Their files are not changed.
    pause
    exit /b 1
)
set "DIR=%~dp1"
echo.
echo   Merging, in this order:
for %%F in (%*) do echo     %%~nF
echo.
:name
set "MNAME="
set /p "MNAME=  Name of the new clone (letters, digits, spaces, - and _): "
if not defined MNAME exit /b 0
echo %MNAME%| findstr /r /c:"^[A-Za-z0-9_-][A-Za-z0-9 _-]*$" >nul || (echo   Letters, digits, spaces, - and _ only, not starting with a space. & goto name)
set "CHK=%MNAME:~32%"
if defined CHK (echo   32 letters at most. & goto name)
set "MSTEM=%MNAME: =_%"
if exist "%DIR%%MSTEM%.hsmclone" (echo   There is a recording called %MSTEM% already. & goto name)
"%EXE%" --merge "%DIR%%MSTEM%.hsmclone" "%MNAME%" %*
if errorlevel 1 (
    echo.
    echo   Merging failed; see above. No file was made.
) else (
    echo.
    echo   Made %DIR%%MSTEM%.hsmclone. Train it with train.bat, or fight it as it is: open the Fetch menu in an offline
    echo   fight (the ; key^) and pick it under "Clone to fight".
)
echo.
pause
