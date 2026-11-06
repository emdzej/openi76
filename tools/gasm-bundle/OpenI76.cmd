@echo off
rem OpenI76 @VERSION@ on gasm-run @GASM_VERSION@. Double-click to play; OpenI76.cmd --help for the options.
rem The work is done by openi76.ps1 (game data picker, saved location, gasm-run command line).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0openi76.ps1" %*
set rc=%errorlevel%
rem Keep the window open on errors when started by double-click (OPENI76_NO_PAUSE=1 skips this).
if not "%rc%"=="0" if not "%OPENI76_NO_PAUSE%"=="1" pause
exit /b %rc%
