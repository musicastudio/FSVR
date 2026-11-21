@echo off
rem Build helper: MSVC x64 from the VS 2022 Professional install (the Community install on E: has a broken vcvarsall).
rem Everything that runs lands in bin\ (objects and the test binaries in build\).
rem   build.bat                      -> bin\fsvr_console.exe, the console (:console below)
rem   build.bat test                 -> builds and runs the self checks (engine, formant, presets)
rem   build.bat plugin               -> every plug-in format through CMake into bin\<format>: CLAP, VST3, VST2 and the
rem                                     standalone (64-bit, build\x64), then the 32-bit VST2 with the DXi in it (build\x86),
rem                                     then the plug-in's own checks (check_plugin, check_gui)
rem   build.bat bench                -> bin\dsp_bench.exe, the filter and effects' cost and bit-exact output (tools\dsp_bench.cpp)
rem   build.bat trace                -> bin\wavetrace.exe, a voice's operators sample by sample (tools\wavetrace.cpp)
rem   build.bat perf                 -> bin\perfbench.exe, the engine's cost on fixed scenarios, hashed (tools\perfbench.cpp)
rem   build.bat file.cpp [cl args]   -> compiles whatever you pass (paths relative to this folder)
rem   set FSVR_MATH=0..3           -> the sample loop's maths backend (src\math\fastmath.h): 0 raw libm, 1 LUT (unset), 2 CORDIC, 3 hybrid
rem CMakeLists.txt builds the same targets for anything that is not MSVC-on-Windows.
pushd "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build mkdir build
if not exist bin mkdir bin
set MATHDEF=
rem The two hosts and the filters and effects are bit-exact only without FMA contraction, so they are compiled apart.
rem VS 2022 does not contract under /fp:precise (tools\dsp_bench.cpp writes the same bytes as under /fp:strict).
set VOP3FLAGS=/nologo /O2 /EHsc /W3 /std:c++20 /fp:precise /I src
rem The engine too: its lane kernels replay the scalar arithmetic operation for operation (fm::selfcheck tests it).
set ENGINEFLAGS=/nologo /O2 /EHsc /W3 /std:c++17 /fp:precise
set VOP3OBJ=build\vop3host\*.obj build\vop3host\isa0\*.obj build\vop3host\isa1\*.obj build\vop3host\isa2\*.obj
set ENGINE=src\tonegen\*.cpp src\engine\render.cpp src\engine\controllers.cpp src\engine\fseq.cpp src\engine\midi.cpp src\engine\notes.cpp src\presets\patch.cpp src\presets\rom.cpp src\math\fastmath.cpp src\kernel\tonegen.cpp src\patchfiles\*.cpp
rem The tone generator's lane kernel, once per instruction set (src\kernel\README.md), linked with the engine.
set TGOBJ=build\kernel\isa0\*.obj build\kernel\isa1\*.obj build\kernel\isa2\*.obj
if defined FSVR_MATH set MATHDEF=/DFSVR_MATH=%FSVR_MATH%
if "%~1"=="" (
  call :console
) else if "%~1"=="test" (
  call :console || goto :done
  "%~dp0bin\fsvr_console.exe" -selftest || goto :done
  rem The same on the sse2 kernels, and the avx2 object defines nothing outside its namespace (src\kernel\README.md)
  set FSVR_SIMD=sse2
  "%~dp0bin\fsvr_console.exe" -selftest || goto :done
  set FSVR_SIMD=
  python "%~dp0tools\check_isa.py" avx2 "%~dp0build\vop3host\isa2\*.obj" "%~dp0build\kernel\isa2\*.obj" || goto :done
  python "%~dp0tools\check_formant.py" || goto :done
  python "%~dp0tools\check_presets.py"
) else if "%~1"=="plugin" (
  cmake -S . -B build\x64 -G "Visual Studio 17 2022" -A x64 -DFSVR_BUILD_PLUGIN=ON || goto :done
  cmake --build build\x64 --config Release -- -m || goto :done
  cmake -S . -B build\x86 -G "Visual Studio 17 2022" -A Win32 -DFSVR_BUILD_PLUGIN=ON || goto :done
  cmake --build build\x86 --config Release -- -m || goto :done
  build\x64\check_plugin.exe || goto :done
  build\x64\check_gui.exe
) else if "%~1"=="bench" (
  call :vop3host || goto :done
  cl %VOP3FLAGS% tools\dsp_bench.cpp %VOP3OBJ% /Fe:bin\dsp_bench.exe /Fobuild\
) else if "%~1"=="perf" (
  call :vop3host || goto :done
  cl %ENGINEFLAGS% %MATHDEF% /DFSVR_HAVE_SSE2=1 /DFSVR_HAVE_AVX2=1 /I src %ENGINE% src\device\device.cpp src\selftest\selftest.cpp src\selftest\patchfiles.cpp tools\perfbench.cpp %VOP3OBJ% %TGOBJ% /Fe:bin\perfbench.exe /Fobuild\
) else if "%~1"=="trace" (
  call :vop3host || goto :done
  cl %ENGINEFLAGS% %MATHDEF% /DFSVR_HAVE_SSE2=1 /DFSVR_HAVE_AVX2=1 /I src %ENGINE% tools\wavetrace.cpp %VOP3OBJ% %TGOBJ% /Fe:bin\wavetrace.exe /Fobuild\
) else (
  cl /nologo /O2 /EHsc /W3 /std:c++17 %* /Fobuild\
)
goto :done

rem The console.
:console
call :vop3host || exit /b 1
cl %ENGINEFLAGS% %MATHDEF% /DFSVR_HAVE_SSE2=1 /DFSVR_HAVE_AVX2=1 /I src %ENGINE% src\device\device.cpp src\selftest\selftest.cpp src\selftest\patchfiles.cpp %VOP3OBJ% %TGOBJ% src\console\main.cpp winmm.lib /Fe:bin\fsvr_console.exe /Fobuild\
exit /b %ERRORLEVEL%

:vop3host
if exist build\vop3host rmdir /s /q build\vop3host
mkdir build\vop3host build\vop3host\isa0 build\vop3host\isa1 build\vop3host\isa2
cl /c /MP %VOP3FLAGS% /DFSVR_HAVE_SSE2=1 /DFSVR_HAVE_AVX2=1 src\effects\*.cpp src\effects\host\*.cpp src\filters\*.cpp src\modulators\*.cpp src\fs1r\rom_tables.cpp src\kernel\dispatch.cpp /Fobuild\vop3host\ || exit /b 1
rem The lane kernels, once per instruction set (src\kernel\README.md): scalar, sse2, and avx2 under /arch:AVX2. The
rem DSP's (the filter bank) with the DSP, the tone generator's (group_kernel.cpp) with the engine, which it reaches into.
cl /c /MP %VOP3FLAGS% /DFSVR_ISA=0 src\kernel\isa\bank_kernel.cpp src\kernel\isa\table.cpp /Fobuild\vop3host\isa0\ || exit /b 1
cl /c /MP %VOP3FLAGS% /DFSVR_ISA=1 src\kernel\isa\bank_kernel.cpp src\kernel\isa\table.cpp /Fobuild\vop3host\isa1\ || exit /b 1
cl /c /MP %VOP3FLAGS% /DFSVR_ISA=2 /arch:AVX2 src\kernel\isa\bank_kernel.cpp src\kernel\isa\table.cpp /Fobuild\vop3host\isa2\ || exit /b 1
if exist build\kernel rmdir /s /q build\kernel
mkdir build\kernel build\kernel\isa0 build\kernel\isa1 build\kernel\isa2
cl /c %ENGINEFLAGS% %MATHDEF% /I src /DFSVR_ISA=0 src\kernel\isa\group_kernel.cpp /Fobuild\kernel\isa0\ || exit /b 1
cl /c %ENGINEFLAGS% %MATHDEF% /I src /DFSVR_ISA=1 src\kernel\isa\group_kernel.cpp /Fobuild\kernel\isa1\ || exit /b 1
cl /c %ENGINEFLAGS% %MATHDEF% /I src /DFSVR_ISA=2 /arch:AVX2 src\kernel\isa\group_kernel.cpp /Fobuild\kernel\isa2\ || exit /b 1
exit /b %ERRORLEVEL%

:done
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%
