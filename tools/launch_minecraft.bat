@echo off
rem Starts HalfCraft's Minecraft dev client (offline name "Gordon"; HALFCRAFT_USERNAME changes it).
rem It waits on the title screen until Half-Life 2 (tools/run_hl2.ps1) is running, then hides its
rem window and loads the mirror world by itself. tools/gradle.ps1 finds the JDK 25 it needs.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0gradle.ps1" runClient
