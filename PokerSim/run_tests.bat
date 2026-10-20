@echo off
rem Lance les tests (apres build_windows.bat). Argument facultatif : racine du depot OpenHoldem pour les profils reels.
setlocal
set "BIN=%~dp0bin\Release"
set "ROOT=%~1"
if "%ROOT%"=="" set "ROOT=%~dp0.."
set FAIL=0
for %%t in (test_core test_ohlang test_prwin) do (
  echo === %%t
  "%BIN%\%%t.exe" || set FAIL=1
)
echo === test_gui_headless
"%BIN%\test_gui_headless.exe" "%ROOT%" || set FAIL=1
if %FAIL%==1 ( echo. & echo AU MOINS UN TEST A ECHOUE & exit /b 1 )
echo. & echo Tous les tests passent.
endlocal
