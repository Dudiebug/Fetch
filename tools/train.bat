@echo off
rem Trains a style clone: lists the recordings in Clones\, asks which one and how, and runs clonetrain.exe on it.
rem M merges some of them into one new clone first (clonetrain --merge), as the Fetch menu's Merge page does.
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
rem Nor before pre12 a --merge: then M is not offered.
set "HASMERGE="
"%EXE%" --help 2>&1 | findstr /c:"--merge" >nul && set "HASMERGE=1"
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
if defined HASMERGE if %n% GTR 1 echo   M. Merge some of them into one new clone
echo   Q. Quit
echo.
set "pick="
set /p "pick=  Which one? "
if not defined pick goto menu
if /i "%pick%"=="q" exit /b 0
if /i "%pick%"=="a" if %n% GTR 1 goto all
if /i "%pick%"=="m" if defined HASMERGE if %n% GTR 1 goto merge
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

:merge
echo.
echo   Type the numbers of the recordings to merge, in order, with spaces between: 1 3 4, say.
echo   The clone looks like the first one that has a look saved. Their files are not changed.
set "PICKS="
set /p "PICKS=  Which ones? "
if not defined PICKS goto menu
set "MARGS="
set /a m=0
set "BAD="
for %%i in (%PICKS%) do call :addpick %%i
if defined BAD (
    echo   %BAD% is not one of the numbers above.
    pause
    goto menu
)
if %m% LSS 2 (
    echo   Pick two recordings or more.
    pause
    goto menu
)
:mergename
set "MNAME="
set /p "MNAME=  Name of the new clone (letters, digits, spaces, - and _): "
if not defined MNAME goto menu
echo %MNAME%| findstr /r /c:"^[A-Za-z0-9_-][A-Za-z0-9 _-]*$" >nul || (echo   Letters, digits, spaces, - and _ only, not starting with a space. & goto mergename)
set "CHK=%MNAME:~32%"
if defined CHK (echo   32 letters at most. & goto mergename)
set "MSTEM=%MNAME: =_%"
if exist "%DIR%\%MSTEM%.hsmclone" (echo   There is a recording called %MSTEM% already. & goto mergename)
echo.
echo   ==== Merging into %MSTEM% ====
"%EXE%" --merge "%DIR%\%MSTEM%.hsmclone" "%MNAME%" %MARGS%
if errorlevel 1 (
    echo.
    echo   Merging failed; see above. No file was made.
    pause
    goto menu
)
echo.
set "T=y"
set /p "T=  Train it now? [Y/n] "
if /i "%T%"=="n" goto done
call :options
call :train "%DIR%\%MSTEM%.hsmclone"
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

:addpick
echo %~1| findstr /r "^[0-9][0-9]*$" >nul || (set "BAD=%~1" & exit /b 0)
if %~1 LSS 1 (set "BAD=%~1" & exit /b 0)
if %~1 GTR %n% (set "BAD=%~1" & exit /b 0)
call set "MARGS=%%MARGS%% "%%file%~1%%""
set /a m+=1
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
