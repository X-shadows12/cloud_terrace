@echo off
setlocal

set "FROMELF=%~1"
set "AXF=%~2"
set "OUTBIN=%~3"
set "OUTDIR=%OUTBIN%.dir"

if "%FROMELF%"=="" exit /b 2
if "%AXF%"=="" exit /b 2
if "%OUTBIN%"=="" exit /b 2

if exist "%OUTDIR%\" rmdir /s /q "%OUTDIR%"
if exist "%OUTDIR%" del /f /q "%OUTDIR%"

"%FROMELF%" --bin "%AXF%" --output="%OUTDIR%"
if errorlevel 1 exit /b 1

if exist "%OUTDIR%\ER_IROM1" goto copy_irom
if exist "%OUTDIR%" goto copy_single_bin
exit /b 1

:copy_irom
copy /Y "%OUTDIR%\ER_IROM1" "%OUTBIN%" >nul
set "COPYERR=%errorlevel%"
if "%COPYERR%"=="0" rmdir /s /q "%OUTDIR%"
exit /b %COPYERR%

:copy_single_bin
copy /Y "%OUTDIR%" "%OUTBIN%" >nul
set "COPYERR=%errorlevel%"
if "%COPYERR%"=="0" del /f /q "%OUTDIR%"
exit /b %COPYERR%
