param([switch]$Run, [switch]$Debug)
$ErrorActionPreference = 'Stop'
$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'Install a C++17 MinGW-w64 toolchain with g++ on PATH, or build with CMake and Visual Studio.' }
New-Item -ItemType Directory -Path "$PSScriptRoot\build" -Force | Out-Null
$buildFlags = if ($Debug) { @('-g', '-O0') } else { @('-O2') }
# Keep the explicit source list aligned with retronp_app in CMakeLists.txt.
$editorSource = Join-Path $PSScriptRoot 'src/editor.cpp'
$appSources = @('app', 'dialogs', 'dialog_rendering', 'document_commands', 'file_io', 'graphics', 'keyboard', 'mouse', 'presenter', 'rendering', 'window') |
    ForEach-Object { Join-Path $PSScriptRoot "src/$_.cpp" }
$commonFlags = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic') + $buildFlags
$windowsFlags = @('-DUNICODE', '-D_UNICODE', '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN')
$staticFlags = @('-static', '-static-libgcc', '-static-libstdc++')
$windowsLibraries = @('-lgdi32', '-luser32', '-lshell32')
& $compiler.Source @commonFlags @windowsFlags "$PSScriptRoot\src\main.cpp" $editorSource @appSources -o "$PSScriptRoot\build\RetroNP.exe" '-municode' '-mwindows' @staticFlags @windowsLibraries
if ($LASTEXITCODE -ne 0) { throw 'RetroNP build failed.' }
& $compiler.Source @commonFlags "$PSScriptRoot\tests\editor_tests.cpp" $editorSource -o "$PSScriptRoot\build\editor_tests.exe" @staticFlags
if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
& "$PSScriptRoot\build\editor_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Editor tests failed.' }
& $compiler.Source @commonFlags @windowsFlags "$PSScriptRoot\tests\io_tests.cpp" $editorSource @appSources -o "$PSScriptRoot\build\io_tests.exe" @staticFlags @windowsLibraries
if ($LASTEXITCODE -ne 0) { throw 'File test build failed.' }
Push-Location $PSScriptRoot
try {
    & "$PSScriptRoot\build\io_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'File tests failed.' }
} finally { Pop-Location }
Write-Host "Built $PSScriptRoot\build\RetroNP.exe"
if ($Run) { Start-Process -FilePath "$PSScriptRoot\build\RetroNP.exe" }
