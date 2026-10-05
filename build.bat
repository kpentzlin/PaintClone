@echo off
rem Erzeugt PaintClone.exe mit Visual Studio (2019/2022) und CMake.
rem Aufruf aus einer normalen Eingabeaufforderung im Projektordner.
setlocal
cmake -S . -B build\msvc -A x64 || goto :error
cmake --build build\msvc --config Release || goto :error
if not exist bin mkdir bin
copy /Y build\msvc\Release\PaintClone.exe bin\PaintClone.exe >nul || goto :error
build\msvc\Release\test_algo.exe || goto :error
echo.
echo Fertig: bin\PaintClone.exe
exit /b 0
:error
echo.
echo Build fehlgeschlagen.
exit /b 1
