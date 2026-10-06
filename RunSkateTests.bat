@echo off
rem Runs the skating core automation tests headless and writes the log to Saved\Logs.
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.4"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0IceFootball.uproject" -ExecCmds="Automation RunTests IceFootball.Skate; Quit" -unattended -nullrhi -nosplash -log
