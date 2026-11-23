@echo off
rem ---------------------------------------------------------------------------
rem Host profile: no CUDA toolkit, no nvcc, no GPU. This is the configuration a
rem free CI runner builds and the one that proves the verifier needs no card.
rem cuda-timer is not fetched at all in this profile (its project() requires the
rem CUDA language); poolkit and graph-lint are, with their CUDA glue off.
rem
rem Usage:  build-host.bat [clean]
rem ---------------------------------------------------------------------------
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set "BUILD=%~dp0build-host"
if /i "%~1"=="clean" rmdir /s /q "%BUILD%" 2>nul

cmake -S "%~dp0." -B "%BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DQTOOLKIT_DEMO_WITH_CUDA=OFF
if errorlevel 1 exit /b 1

cmake --build "%BUILD%"
if errorlevel 1 exit /b 1

rem poolkit registers its own entries even with its tests off: excluded by name.
ctest --test-dir "%BUILD%" --output-on-failure -E "^poolkit_"
exit /b %errorlevel%
