@echo off
rem Builds the plugin. Needs git, xmake 3 and the Visual Studio C++ tools.
cd /d "%~dp0"
if not exist "lib\commonlibsse-ng" git clone https://github.com/alandtse/CommonLibVR "lib\commonlibsse-ng" || exit /b 1
git -C "lib\commonlibsse-ng" checkout d13d10a0ccb4945870eb841bf1ad8a6cf5ed84dd || exit /b 1
rem a VR build (GLOW_VR=1) also needs the OpenVR headers: a CommonLib submodule a plain clone does not fetch
if "%GLOW_VR%"=="1" (git -C "lib\commonlibsse-ng" submodule update --init --depth 1 extern/openvr || exit /b 1)
xmake f -y -m releasedbg || exit /b 1
xmake -y || exit /b 1
dir /s /b build\*.dll
