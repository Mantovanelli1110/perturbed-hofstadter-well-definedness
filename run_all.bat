@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ========================================
REM Reproducibility pipeline with logging
REM ========================================

if not exist output (
mkdir output
)

set LOGFILE=output\run_all.log

echo ======================================== > "%LOGFILE%"
echo Reproducibility Pipeline >> "%LOGFILE%"
echo Started: %date% %time% >> "%LOGFILE%"
echo ======================================== >> "%LOGFILE%"

echo.
echo ========================================
echo Reproducibility Pipeline
echo Log file: %LOGFILE%
echo ========================================
echo.

call :run_step 1 7 "Construct anchor pairs" ^
"gcc -O2 -std=c11 -o anchors src/pipeline/make_exact_anchor_pairs_win64.c" ^
"anchors"

call :run_step 2 7 "Generate macro profiles" ^
"gcc -O2 -std=c11 -o gen src/data_generation/q_D_macro_profiles_stream_occ_win64.c" ^
"gen"

call :run_step 3 7 "Compress observed transition structure" ^
"gcc -O2 -std=c11 -o compress src/pipeline/compress_edges_to_9state.c" ^
"compress"

call :run_step 4 7 "Construct and propagate finite local model" ^
"gcc -O2 -std=c11 -o propagate src/pipeline/construct_and_propagate.c" ^
"propagate"

call :run_step 5 7 "Construct two-mode automaton" ^
"gcc -O2 -std=c11 -o two_mode src/automaton/two_mode_automaton.c" ^
"two_mode"

call :run_step 6 7 "Run independent branch comparison" ^
"gcc -O2 -std=c11 -o branch src/automaton/branch_comparison_solver.c" ^
"branch"

call :run_step 7 7 "Run final critical-core verification" ^
"g++ -O2 -std=c++17 -o checkall src/verification/checkall.cpp" ^
"checkall"

echo. >> "%LOGFILE%"
echo ======================================== >> "%LOGFILE%"
echo Pipeline completed successfully. >> "%LOGFILE%"
echo Finished: %date% %time% >> "%LOGFILE%"
echo ======================================== >> "%LOGFILE%"

echo.
echo ========================================
echo Pipeline completed successfully.
echo See log file: %LOGFILE%
echo ========================================
goto :eof

:run_step
set STEP=%~1
set TOTAL=%~2
set TITLE=%~3
set COMPILE_CMD=%~4
set RUN_CMD=%~5

echo.
echo [%STEP%/%TOTAL%] %TITLE%
echo [%STEP%/%TOTAL%] %TITLE% >> "%LOGFILE%"
echo Command (compile): %COMPILE_CMD% >> "%LOGFILE%"

cmd /c "%COMPILE_CMD%" >> "%LOGFILE%" 2>&1
if errorlevel 1 (
echo ERROR during compilation in step %STEP%: %TITLE%
echo ERROR during compilation in step %STEP%: %TITLE% >> "%LOGFILE%"
echo Aborting pipeline. >> "%LOGFILE%"
exit /b 1
)

echo Command (run): %RUN_CMD% >> "%LOGFILE%"
cmd /c "%RUN_CMD%" >> "%LOGFILE%" 2>&1
if errorlevel 1 (
echo ERROR during execution in step %STEP%: %TITLE%
echo ERROR during execution in step %STEP%: %TITLE% >> "%LOGFILE%"
echo Aborting pipeline. >> "%LOGFILE%"
exit /b 1
)

echo Step %STEP% completed successfully.
echo Step %STEP% completed successfully. >> "%LOGFILE%"
exit /b 0
