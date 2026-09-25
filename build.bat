@echo off
REM ============================================================
REM  geomapr - Windows build (MapViewerUnrealEditor, Win64)
REM
REM  Close the Unreal Editor before running.
REM  Override the engine location with:  set UE_ROOT=D:\UE_5.8
REM ============================================================

if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0MapViewerUnreal.uproject"

if not exist "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" (
    echo ERROR: Unreal Engine not found at "%UE_ROOT%"
    echo Set UE_ROOT to your UE 5.8 install folder and re-run.
    pause
    exit /b 1
)

echo Building MapViewerUnrealEditor Win64 Development ...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" MapViewerUnrealEditor Win64 Development "%PROJECT%" -waitmutex

if %ERRORLEVEL% EQU 0 (
    echo Build SUCCEEDED - open MapViewerUnreal.uproject
) else (
    echo Build FAILED - check the output above
)
pause
