@echo off
rem Lance OpenReplayShooter. Double-clique ce fichier.
rem Au premier lancement il cree l'environnement Python et installe ce qu'il faut.
setlocal
cd /d "%~dp0"

if exist ".venv\Scripts\openreplayshooter-gui.exe" goto :run

echo Premiere installation, cela peut prendre une minute...
where py >nul 2>&1
if errorlevel 1 goto :nopython
py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)" >nul 2>&1
if errorlevel 1 goto :nopython

py -3 -m venv .venv
if errorlevel 1 goto :fail
".venv\Scripts\python.exe" -m pip install -e ".[gui]"
if errorlevel 1 goto :fail

:run
start "" ".venv\Scripts\openreplayshooter-gui.exe" %*
exit /b 0

:nopython
echo.
echo Python 3.11 ou plus recent est introuvable.
echo Installe-le depuis https://www.python.org/downloads/ puis relance ce fichier.
echo Garde coche l'option "py launcher" pendant l'installation.
pause
exit /b 1

:fail
echo.
echo L'installation a echoue. Lis le message ci-dessus.
echo Pour recommencer a zero, supprime le dossier .venv puis relance ce fichier.
pause
exit /b 1
