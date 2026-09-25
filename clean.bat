@echo off
REM Deletes generated Unreal folders: Binaries, Intermediate, DerivedDataCache, Saved
cd /d "%~dp0"
for %%D in (Binaries Intermediate DerivedDataCache Saved) do (
    if exist "%%D" (
        echo Removing %%D ...
        rmdir /s /q "%%D"
    )
)
echo Clean complete. Run build.bat to rebuild.
pause
