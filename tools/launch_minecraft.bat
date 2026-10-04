@echo off
rem Starts HalfCraft's Minecraft dev client (offline name "Gordon"; HALFCRAFT_USERNAME changes it).
rem It waits on the title screen until Half-Life 2 (tools/run_hl2.ps1) is running, then hides its
rem window and loads the mirror world by itself. Needs JAVA_HOME on JDK 25.
cd /d "%~dp0..\minecraft"
call gradlew.bat runClient --no-configuration-cache
