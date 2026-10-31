@echo off
cd /d "%~dp0"
title Upload ESP32 TX ke GitHub
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0UPLOAD_SEMUA.ps1"
