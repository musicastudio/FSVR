@echo off
rem Build helper: MSVC x64 from the VS 2022 Professional install (the Community install on E: has a broken vcvarsall).
rem Everything that runs lands in bin\ (objects and the test binaries in build\).
rem   build.bat                      -> bin\fsvr_console.exe, the console, from src\fs1r\chips\ymp706.cpp src\fs1r\firmware\controllers.cpp src\fs1r\firmware\fseq.cpp src\fs1r\firmware\midi.cpp src\fs1r\firmware\notes.cpp src\fs1r\firmware\patch.cpp src\fs1r\firmware\rom.cpp src\fsvr\device.cpp src\fsvr\fastmath.cpp src\fsvr\selftest.cpp + src\console\main.cpp
rem   build.bat test                 -> builds and runs the self checks (effects, engine, formant, presets)
rem   build.bat plugin               -> every plug-in format through CMake into bin\<format>: CLAP, VST3, VST2 and the
rem                                     standalone (64-bit, build\x64), then the 32-bit VST2 with the DXi in it (build\x86),
rem                                     then the plug-in's own checks (check_plugin, check_gui)
rem   build.bat file.cpp [cl args]   -> compiles whatever you pass (paths relative to this folder)
rem   set FSVR_MATH=0..3           -> the sample loop's maths backend (src\fsvr\fastmath.h): 0 raw libm, 1 LUT (unset), 2 CORDIC, 3 hybrid
rem CMakeLists.txt builds the same targets for anything that is not MSVC-on-Windows.
pushd "%~dp0"
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul 2>&1
if not exist build mkdir build
if not exist bin mkdir bin
set MATHDEF=
if defined FSVR_MATH set MATHDEF=/DFSVR_MATH=%FSVR_MATH%
if "%~1"=="" (
  cl /nologo /O2 /EHsc /W3 /std:c++17 %MATHDEF% /I src src\fs1r\chips\ymp706.cpp src\fs1r\firmware\controllers.cpp src\fs1r\firmware\fseq.cpp src\fs1r\firmware\midi.cpp src\fs1r\firmware\notes.cpp src\fs1r\firmware\patch.cpp src\fs1r\firmware\rom.cpp src\fsvr\device.cpp src\fsvr\fastmath.cpp src\fsvr\selftest.cpp src\console\main.cpp winmm.lib /Fe:bin\fsvr_console.exe /Fobuild\
) else if "%~1"=="test" (
  cl /nologo /O2 /EHsc /W3 /std:c++17 %MATHDEF% /I src src\fs1r\chips\ymp706.cpp src\fs1r\firmware\controllers.cpp src\fs1r\firmware\fseq.cpp src\fs1r\firmware\midi.cpp src\fs1r\firmware\notes.cpp src\fs1r\firmware\patch.cpp src\fs1r\firmware\rom.cpp src\fsvr\device.cpp src\fsvr\fastmath.cpp src\fsvr\selftest.cpp src\console\main.cpp winmm.lib /Fe:bin\fsvr_console.exe /Fobuild\ || goto :done
  cl /nologo /O2 /EHsc /W3 /std:c++17 %MATHDEF% /I src tools\test_effects.cpp src\fsvr\fastmath.cpp /Fe:build\test_effects.exe /Fobuild\ || goto :done
  build\test_effects.exe || goto :done
  "%~dp0bin\fsvr_console.exe" -selftest || goto :done
  python "%~dp0tools\check_formant.py" || goto :done
  python "%~dp0tools\check_presets.py"
) else if "%~1"=="plugin" (
  cmake -S . -B build\x64 -G "Visual Studio 17 2022" -A x64 -DFSVR_BUILD_PLUGIN=ON || goto :done
  cmake --build build\x64 --config Release -- -m || goto :done
  cmake -S . -B build\x86 -G "Visual Studio 17 2022" -A Win32 -DFSVR_BUILD_PLUGIN=ON || goto :done
  cmake --build build\x86 --config Release -- -m || goto :done
  build\x64\check_plugin.exe || goto :done
  build\x64\check_gui.exe
) else (
  cl /nologo /O2 /EHsc /W3 /std:c++17 %* /Fobuild\
)
:done
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%
