#!/bin/bash
# Version PC (SDL2) d'AKA Turbo 3D Grand Prix - compilation avec MSYS2 / MinGW64.
#   host/build_pc.sh   -> host/dist/AKATurbo3D.exe (autonome, SDL2 lie en statique)
set -e
cd "$(dirname "$0")/.."
export PATH=/c/msys64/mingw64/bin:$PATH
mkdir -p host/dist
# online play: OpenSSL (pacman -S mingw-w64-x86_64-openssl mingw-w64-x86_64-pkgconf)
g++ -O2 -std=c++17 -DHOST_BUILD -DHAVE_OPENSSL -I main -I . host/host_main.cpp host/net_pc.cpp main/game/*.cpp \
    -o host/dist/AKATurbo3D.exe -static $(sdl2-config --static-libs) $(pkg-config --static --libs openssl) -lws2_32 -lcrypt32
echo "OK : host/dist/AKATurbo3D.exe"
