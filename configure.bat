@echo off
REM Configure build and generate compilation database using MSYS2/MinGW

set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

if exist build rmdir /s /q build
mkdir build
cd build

cmake .. -G "Ninja" ^
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe ^
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe ^
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if exist compile_commands.json (
  echo Compilation database generated successfully
  cd ..
  if exist compile_commands.json del compile_commands.json
  mklink compile_commands.json build\compile_commands.json
) else (
  echo Failed to generate compilation database
)

cd ..
