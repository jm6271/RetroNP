param([switch]$Run, [switch]$Debug)
$ErrorActionPreference = 'Stop'
$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'Install a C++17 MinGW-w64 toolchain with g++ on PATH, or build with CMake and Visual Studio.' }
New-Item -ItemType Directory -Path "$PSScriptRoot\build" -Force | Out-Null
$buildFlags = if ($Debug) { @('-g', '-O0') } else { @('-O2') }
& $compiler.Source -std=c++17 @buildFlags -Wall -Wextra -Wpedantic -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN "$PSScriptRoot\src\main.cpp" -o "$PSScriptRoot\build\RetroNP.exe" -municode -mwindows -static -static-libgcc -static-libstdc++ -lgdi32 -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { throw 'RetroNP build failed.' }
& $compiler.Source -std=c++17 @buildFlags -Wall -Wextra -Wpedantic "$PSScriptRoot\tests\editor_tests.cpp" -o "$PSScriptRoot\build\editor_tests.exe" -static -static-libgcc -static-libstdc++
if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
& "$PSScriptRoot\build\editor_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Editor tests failed.' }
& $compiler.Source -std=c++17 @buildFlags -Wall -Wextra -Wpedantic -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN "$PSScriptRoot\tests\io_tests.cpp" -o "$PSScriptRoot\build\io_tests.exe" -static -static-libgcc -static-libstdc++ -lgdi32 -luser32 -lshell32
if ($LASTEXITCODE -ne 0) { throw 'File test build failed.' }
Push-Location $PSScriptRoot
try {
    & "$PSScriptRoot\build\io_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'File tests failed.' }
} finally { Pop-Location }
Write-Host "Built $PSScriptRoot\build\RetroNP.exe"
if ($Run) { Start-Process -FilePath "$PSScriptRoot\build\RetroNP.exe" }
