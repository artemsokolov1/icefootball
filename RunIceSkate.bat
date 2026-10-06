@echo off
rem Launches the skating test rink in a standalone window - no map asset needed.
rem The empty engine map /Engine/Maps/Entry is loaded with the "skate" game mode alias,
rem which builds the rink, ball and lighting at runtime.
rem Set UE_ROOT if the engine is not in the default location.
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.4"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0IceFootball.uproject" /Engine/Maps/Entry?game=skate -game -windowed -ResX=1600 -ResY=900 -log
