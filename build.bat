@echo off
setlocal enabledelayedexpansion
:: Remove existing executable if it exists
if exist "DiskCleaner.exe" (
    del "DiskCleaner.exe"
)
:: Start compilation in background and get process ID
start /b cmd /c "g++ -std=c++17 -O2 -DUNICODE -D_UNICODE -mwindows DiskCleaner.cpp -o DiskCleaner.exe -lcomctl32 -lshell32 -lole32 && echo SUCCESS > build_status.log || echo FAILED > build_status.log"

:: Animation characters for spinner
set /a "count=0"

:: Show animated building text while compilation runs
:animate
if exist "build_status.log" goto :done

set /a "index=count %% 4"

if !index!==0 set "char=|"
if !index!==1 set "char=/"
if !index!==2 set "char=-"
if !index!==3 set "char=\"

<nul set /p="Building !char!"
ping 127.0.0.1 -n 1 -w 250 >nul
cls

set /a "count+=1"
ping 127.0.0.1 -n 1 -w 500 >nul
goto :animate

:done
cls

:: Check if compilation was successful
for /f "delims=" %%a in ('type build_status.log 2^>nul') do set "result=%%a"

echo Done.

:: Cleanup temporary files
if exist "build_status.log" del "build_status.log"

timeout /t 1 /nobreak >nul