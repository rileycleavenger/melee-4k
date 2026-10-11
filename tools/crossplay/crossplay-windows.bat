@echo off
rem Melee 4K cross-platform check (Windows). Put this file and the Mac's
rem *.mrc recordings in your Melee4K folder (next to Setup.exe), then
rem double-click it. It replays each recording and writes results.txt, then
rem records three matches of its own (win-*.mrc) for the Mac to replay.
setlocal enabledelayedexpansion
cd /d "%~dp0"
set GAME=Melee4K\melee.exe
set DISC=
for %%f in (PUT-YOUR-ROM-HERE\*.iso PUT-YOUR-ROM-HERE\*.ciso PUT-YOUR-ROM-HERE\*.gcm PUT-YOUR-ROM-HERE\*.rvz) do set DISC=%%f
if "%DISC%"=="" (echo Put your disc image in PUT-YOUR-ROM-HERE first. & pause & exit /b 1)
set MELEE_BOOT_SCENE=vs
set MELEE_DEBUG_VS=cpu4
set MELEE_NET_CHECKSUM_WIDE=1
set MELEE_FRAME_INTERP=0
echo Melee 4K cross-platform check > results.txt
for %%r in (mac-*.mrc) do (
  for /f "tokens=2,3 delims=-." %%a in ("%%~nr") do (set SEED=%%a& set STAGE=%%b)
  set MELEE_SEED=!SEED:seed=!
  set MELEE_DEBUG_VS_STAGE=!STAGE:stage=!
  set MELEE_NET_REPLAY=%%r
  set MELEE_EXIT_AFTER_FRAMES=2520
  set MELEE_LOG_FILE=%%~nr.log
  echo Replaying %%r ...
  "%GAME%" "%DISC%" >nul 2>&1
  findstr /c:"no divergence" "%%~nr.log" >nul && (echo PASS %%r>> results.txt) || (echo FAIL %%r>> results.txt & findstr /c:"DIVERGED" "%%~nr.log" >> results.txt)
)
set MELEE_NET_REPLAY=
set MELEE_EXIT_AFTER_FRAMES=2400
for %%s in (1 2 3) do for %%t in (0 3 24) do (
  set MELEE_SEED=%%s
  set MELEE_DEBUG_VS_STAGE=%%t
  set MELEE_NET_REPLAY=
  set MELEE_NET_RECORD=raw-seed%%s-stage%%t.mrc
  set MELEE_LOG_FILE=win-seed%%s-stage%%t-record.log
  echo Recording win-seed%%s-stage%%t ...
  "%GAME%" "%DISC%" >nul 2>&1
  rem Replay and re-record: the shared file is the canonical run, as in
  rem tools/interp_determinism.py, not this machine's input pacing.
  set MELEE_NET_REPLAY=raw-seed%%s-stage%%t.mrc
  set MELEE_NET_RECORD=win-seed%%s-stage%%t.mrc
  "%GAME%" "%DISC%" >nul 2>&1
  del raw-seed%%s-stage%%t.mrc
)
set MELEE_NET_REPLAY=
type results.txt
echo.
echo Done. Send results.txt and the win-*.mrc files back.
pause
