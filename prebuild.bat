@echo off

echo %Time%

if not exist "shared\external\vcpkg\vcpkg.exe" (
    cd shared\external\vcpkg
    call bootstrap-vcpkg.bat -disableMetrics
    cd %~dp0
)

cd shared\external\vcpkg
echo Installing Libraries
vcpkg install tinydir cppcodec tinyfiledialogs concurrentqueue portaudio stb clipp glm sdl3[vulkan] catch2 --triplet x64-windows-static-md --recurse
cd %~dp0
echo %Time%

