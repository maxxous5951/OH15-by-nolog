@echo off
rem Compile PokerSim (Release x64) avec Visual Studio 2022 et lance l'interface graphique.
rem Prerequis : Visual Studio 2022 avec la charge de travail "Developpement Desktop en C++".
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio 2022 est introuvable. Installez-le avec la charge de travail "Developpement Desktop en C++".
  exit /b 1
)
set "MSBUILD="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
  echo MSBuild est introuvable. Installez la charge de travail "Developpement Desktop en C++".
  exit /b 1
)
"%MSBUILD%" "%~dp0PokerSim.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if errorlevel 1 (
  echo.
  echo La compilation a echoue.
  exit /b 1
)
echo.
echo Termine : %~dp0bin\Release\pokersim_gui.exe  (interface)   et   pokersim.exe  (ligne de commande)
if /i not "%~1"=="nolaunch" start "" "%~dp0bin\Release\pokersim_gui.exe"
endlocal
