@echo off
rem Trains a style clone: lists the recordings in Clones\, asks which one and how, and runs clonetrain.exe on it.
rem Put this file next to clonetrain.exe and dsound.dll (HalfswordUE5\Binaries\Win64), or inside Clones\ itself.
setlocal EnableExtensions DisableDelayedExpansion
title Fetch: train a clone

set "HERE=%~dp0"
set "EXE="
if exist "%HERE%clonetrain.exe" set "EXE=%HERE%clonetrain.exe"
if not defined EXE if exist "%HERE%..\clonetrain.exe" set "EXE=%HERE%..\clonetrain.exe"
if not defined EXE (
    echo clonetrain.exe is not next to this file.
    echo Put train.bat and clonetrain.exe in the game's Win64 folder, next to dsound.dll.
    pause
    exit /b 1
)
rem A clonetrain older than pre8 has no --outcome: then the question is not asked.
set "HASOUTCOME="
"%EXE%" --help 2>&1 | findstr /c:"--outcome" >nul && set "HASOUTCOME=1"
set "DIR=%HERE%Clones"
if not exist "%DIR%\" set "DIR=%HERE:~0,-1%"

:menu
cls
echo.
echo   Fetch: train a clone
echo   Recordings in %DIR%
echo.
set /a n=0
for %%F in ("%DIR%\*.hsmclone") do call :list "%%~fF" "%%~nF" "%%~zF"
if %n%==0 (
    echo   No .hsmclone files here. Record one first: set record=1 in Fetch.ini, or turn on
    echo   "Record my fighting" in the Fetch menu, and fight.
    echo.
    pause
    exit /b 1
)
echo.
if %n% GTR 1 echo   A. Train them all, one after another
echo   Q. Quit
echo.
set "pick="
set /p "pick=  Which one? "
if not defined pick goto menu
if /i "%pick%"=="q" exit /b 0
if /i "%pick%"=="a" if %n% GTR 1 goto all
echo %pick%| findstr /r "^[0-9][0-9]*$" >nul || goto menu
if %pick% LSS 1 goto menu
if %pick% GTR %n% goto menu
call set "FILE=%%file%pick%%%"
call :options
call :train "%FILE%"
goto done

:all
call :options
for /l %%i in (1,1,%n%) do call :trainnum %%i
goto done

:done
echo.
echo   To fight it: open the Fetch menu in an offline fight (the ; key) and pick it under "Clone to fight",
echo   or set clone=NAME in Fetch.ini.
echo.
pause
goto menu

rem ---------------------------------------------------------------------------------------------------------------------------

:list
set /a n+=1
set "file%n%=%~1"
set /a kb=%~3/1024
set "size=%kb% KB"
if %kb% GEQ 1024 set /a mb=%kb%/1024
if %kb% GEQ 1024 call set "size=%%mb%% MB"
set "has=no model yet"
if exist "%DIR%\%~2.hsmmodel" set "has=has a model"
if exist "%DIR%\%~2.hsmmodel" for %%M in ("%DIR%\%~2.hsmmodel") do set "has=has a model, from %%~tM"
echo   %n%. %~2  (%size%, %has%)
exit /b 0

:options
echo.
echo   Press Enter for the usual setting.
set "MIN=4.5"
set /p "MIN=  Time limit in minutes [4.5]: "
echo %MIN%| findstr /r "^[0-9][0-9]*\.*[0-9]*$" >nul || (echo   Not a number: 4.5 it is. & set "MIN=4.5")
set "OUTCOME=1"
if defined HASOUTCOME call :askoutcome
set "THREADS="
set /p "THREADS=  CPU threads [Enter: half of them, at most 8]: "
if defined THREADS echo %THREADS%| findstr /r "^[0-9][0-9]*$" >nul || set "THREADS="
set "ARGS=--minutes %MIN%"
if defined HASOUTCOME set "ARGS=%ARGS% --outcome %OUTCOME%"
if defined THREADS set "ARGS=%ARGS% --threads %THREADS%"
exit /b 0

:askoutcome
echo   How much a move's result counts: hits you landed soon after it count it up, hits you took count it down.
echo   0 copies every move the same, 1 is usual, up to 4 leans hard to what worked.
set /p "OUTCOME=  Weight of results, 0 to 4 [1]: "
echo %OUTCOME%| findstr /r "^[0-9][0-9]*\.*[0-9]*$" >nul || (echo   Not a number: 1 it is. & set "OUTCOME=1")
exit /b 0

:trainnum
call set "FILE=%%file%1%%"
call :train "%FILE%"
exit /b 0

:train
set "MODEL=%~dpn1.hsmmodel"
echo.
echo   ==== Training %~n1 (up to %MIN% minutes) ====
if exist "%MODEL%" (
    copy /y "%MODEL%" "%MODEL%.bak" >nul && echo   The old model is kept as %~n1.hsmmodel.bak
)
"%EXE%" "%~1" "%MODEL%" %ARGS%
if errorlevel 1 (
    echo.
    echo   Training %~n1 failed; see above. The old model, if any, is unchanged.
    exit /b 1
)
echo.
echo   Done: %MODEL%
exit /b 0
