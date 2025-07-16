@echo off
setlocal enabledelayedexpansion

if exist "DiskCleaner.exe" (
    del "DiskCleaner.exe"
)

start /b cmd /c "g++ -std=c++17 -O2 -DUNICODE -D_UNICODE -mwindows DiskCleaner.cpp -o DiskCleaner.exe -lcomctl32 -lshell32 -lole32 && echo SUCCESS > build_status.log || echo FAILED > build_status.log"

set /a "count=0"

:: some unnecesary fancy shit i made for fun :P
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

for /f "delims=" %%a in ('type build_status.log 2^>nul') do set "result=%%a"

echo Done.

if exist "build_status.log" del "build_status.log"

timeout /t 1 /nobreak >nul

exit /b
