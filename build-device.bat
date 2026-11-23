@echo off
rem ---------------------------------------------------------------------------
rem Device profile: CUDA 12.6 (nvcc) + Visual Studio 2022 BuildTools (MSVC 14.44).
rem
rem Two toolchains on one machine and no PATH surgery: vcvars64 from the
rem BuildTools installation is called here, and CMAKE_CUDA_COMPILER is pinned to
rem the 12.6 toolkit so the build cannot silently pick up the 12.1 first on PATH.
rem
rem NVCC_PREPEND_FLAGS=--allow-unsupported-compiler: CUDA 12.6 does not know
rem MSVC 14.44 and refuses to compile without it. Safe here because the host code
rem is still gated by /W4 /WX through -Xcompiler (CMakeLists.txt).
rem
rem Usage:  build-device.bat [clean]
rem ---------------------------------------------------------------------------
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set "NVCC_PREPEND_FLAGS=--allow-unsupported-compiler"
set "BUILD=%~dp0build-dev"
if /i "%~1"=="clean" rmdir /s /q "%BUILD%" 2>nul

cmake -S "%~dp0." -B "%BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_CUDA_COMPILER=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/bin/nvcc.exe" ^
    -DCMAKE_CUDA_ARCHITECTURES=75 ^
    -DQTOOLKIT_DEMO_WITH_CUDA=ON
if errorlevel 1 exit /b 1

cmake --build "%BUILD%"
if errorlevel 1 exit /b 1

rem M1 run: one command, one artifact. Not a ctest entry (§3.12).
rem The run starts from the repo root and passes relative paths: an absolute
rem path would make a committed artifact depend on the machine that produced it.
pushd "%~dp0"
"%BUILD%\qtoolkit_workload.exe" artifacts
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%
