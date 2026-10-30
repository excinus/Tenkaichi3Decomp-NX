@echo off
rem Tenkaichi3Decomp for the Nintendo Switch: copies your PC installation's game data onto this SD card
rem (bt3-install\install_gamedata.ps1). Double-click it on the SD card.
title Tenkaichi3Decomp - game data
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0bt3-install\install_gamedata.ps1"
if errorlevel 1 pause
