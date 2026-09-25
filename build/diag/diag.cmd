@echo off
(
  echo CWD: %CD%
  echo --- dir ---
  dir /b
  echo --- env APP ---
  echo.
) > "%TEMP%\iexpress-diag.txt" 2>&1
exit /b 0
