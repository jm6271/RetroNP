param([switch]$Release)
$ErrorActionPreference = 'Stop'
$configuration = if ($Release) { 'Release' } else { 'Debug' }

# Initialize MSVC even when VS Code was opened outside a developer shell.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Install Visual Studio with the Desktop development with C++ workload.' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
if (-not $vsPath) { throw 'Install the MSVC x64 tools and C++ CMake tools through Visual Studio Installer.' }
& (Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Could not initialize the MSVC compiler environment.' }

$cmakeTools = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake'
$cmake = Join-Path $cmakeTools 'CMake/bin/cmake.exe'
$ctest = Join-Path $cmakeTools 'CMake/bin/ctest.exe'
$ninja = Join-Path $cmakeTools 'Ninja/ninja.exe'
$buildDir = Join-Path $PSScriptRoot "build/msvc-$($configuration.ToLowerInvariant())"

& $cmake -S $PSScriptRoot -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_CXX_COMPILER=cl.exe' "-DCMAKE_BUILD_TYPE=$configuration"
if ($LASTEXITCODE -ne 0) { throw 'MSVC CMake configuration failed.' }
& $cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'MSVC build failed.' }
& $ctest --test-dir $buildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'MSVC tests failed.' }
Write-Host "Built $buildDir/RetroNP.exe"
