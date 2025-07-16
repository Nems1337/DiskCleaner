@echo off
g++ -std=c++17 -O2 -DUNICODE -D_UNICODE -mwindows DiskCleaner.cpp -o DiskCleaner.exe -lcomctl32 -lshell32 -lole32
pause