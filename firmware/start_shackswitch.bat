@echo off
title G0JKN ShackSwitch Launcher
color 0A

echo.
echo  ============================================
echo   G0JKN ShackSwitch Launcher
echo  ============================================
echo.

:: ── Configuration ────────────────────────────────────────────
:: Edit these if your IPs change
set ARDUINO_IP=10.0.0.85
set FLEX_IP=10.0.0.250
set FILES_DIR=%~dp0

echo  Arduino IP  : %ARDUINO_IP%
echo  Flex IP     : %FLEX_IP%
echo  Files folder: %FILES_DIR%
echo.
echo  Starting services...
echo.

:: ── Start TCP/WebSocket bridge ────────────────────────────────
echo  [1/3] Starting TCP bridge (port 9008 ^> WS 9009)...
start "ShackSwitch Bridge" cmd /k "title ShackSwitch Bridge && color 0B && node "%FILES_DIR%bridge.js" %ARDUINO_IP%"
timeout /t 2 /nobreak >nul

:: ── Start Flex bridge ─────────────────────────────────────────
echo  [2/3] Starting Flex-6700 band tracker...
start "Flex Band Tracker" cmd /k "title Flex Band Tracker && color 0E && node "%FILES_DIR%flexbridge.js" %FLEX_IP% %ARDUINO_IP%"
timeout /t 2 /nobreak >nul

:: ── Start local web server for dashboard ─────────────────────
echo  [3/3] Starting dashboard web server...
start "ShackSwitch Dashboard" cmd /k "title Dashboard Server && color 0D && npx serve "%FILES_DIR%" --listen 3000" --no-clipboard
timeout /t 3 /nobreak >nul

:: ── Open browser directly ─────────────────────────────────────
echo  [3/3] Opening dashboard...
start "" "%FILES_DIR%dashboard.html"


echo.
echo  ============================================
echo   All services running!
echo   Dashboard : http://localhost:3000/dashboard
echo   Arduino   : http://%ARDUINO_IP%
echo  ============================================
echo.
echo  Close this window to shut everything down,
echo  or close individual windows to stop a service.
echo.
pause
