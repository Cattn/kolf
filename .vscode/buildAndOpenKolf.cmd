@echo off

PowerShell -ExecutionPolicy Bypass -File "C:\CraftRoot\craft\craftenv.ps1"
craft --compile --install --qmerge packagename
kolf
