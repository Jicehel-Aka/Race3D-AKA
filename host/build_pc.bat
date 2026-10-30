@echo off
rem Version PC (SDL2) d'AKA Turbo 3D Grand Prix : necessite MSYS2 (C:\msys64) avec mingw-w64-x86_64-gcc et mingw-w64-x86_64-SDL2.
C:\msys64\usr\bin\bash.exe -lc "cd \"$(cygpath '%~dp0')\" && ./build_pc.sh"
pause
