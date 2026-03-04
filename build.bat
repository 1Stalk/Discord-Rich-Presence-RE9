@echo off
setlocal EnableDelayedExpansion
title Building DiscordPresenceRE9...

echo ================================================
echo  Building DiscordPresenceRE9.dll
echo  REFramework Plugin for Resident Evil Requiem
echo ================================================
echo.

:: -----------------------------------------------
:: Find Visual Studio vcvars64.bat
:: -----------------------------------------------
set "VCVARS="
for %%Y in (2022 2019 2017) do (
    for %%E in (BuildTools Community Professional Enterprise) do (
        if exist "C:\Program Files (x86)\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
            echo Found: Visual Studio %%Y %%E
            goto :found_vs
        )
        if exist "C:\Program Files\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=C:\Program Files\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
            echo Found: Visual Studio %%Y %%E
            goto :found_vs
        )
    )
)
echo ERROR: Visual Studio not found!
pause
exit /b 1

:found_vs
echo Setting up 64-bit compiler environment...
call "%VCVARS%" x64 > nul 2>&1

:: -----------------------------------------------
:: Build with cl.exe directly (no CMake needed!)
:: -----------------------------------------------
set "SRC=%~dp0src\main.cpp"
set "OUT=%~dp0DiscordPresenceRE9.dll"
set "PLUGINS_DIR=%~dp0reframework\plugins"

echo Compiling...
echo.

cl.exe ^
    /LD ^
    /O2 ^
    /GL ^
    /GS- ^
    /std:c++17 ^
    /EHsc ^
    /DWIN32_LEAN_AND_MEAN ^
    /DNOMINMAX ^
    /W3 ^
    "%SRC%" ^
    /Fe:"%OUT%" ^
    /link ^
    /DLL ^
    /LTCG ^
    /OPT:REF ^
    /OPT:ICF ^
    kernel32.lib

if errorlevel 1 (
    echo.
    echo ERROR: Compilation failed!
    pause
    exit /b 1
)

if not exist "%PLUGINS_DIR%" mkdir "%PLUGINS_DIR%"
move /Y "%OUT%" "%PLUGINS_DIR%\DiscordPresenceRE9.dll" > nul

echo.
echo ================================================
echo  SUCCESS!
echo  DLL: %PLUGINS_DIR%\DiscordPresenceRE9.dll
echo ================================================
echo.
pause
