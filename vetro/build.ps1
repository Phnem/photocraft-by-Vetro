# PhotoCraft by Vetro -- build the engine bridge (cargo) and the C++ shell (CMake + Ninja, MSVC).
# Output: vetro/dist/PhotoCraftVetro.exe beside photocraft_bridge.dll.
# -Run starts the app (-Hidden: without a visible window, for automation); -Tests also builds the
# CTest executables next to it (ctest --test-dir vetro\build); -OutputDir assembles elsewhere.
param([switch]$Run, [switch]$Hidden, [switch]$Tests, [string]$Open = '', [string]$OutputDir = '')
# Native tools report progress on stderr; success is judged by their exit codes, not by that.
$ErrorActionPreference = 'Continue'
$repo = Split-Path $PSScriptRoot -Parent

# 1. The engine, as a C ABI DLL.
Push-Location $repo
try {
  cargo build --release -p photocraft-bridge
  if ($LASTEXITCODE) { throw 'cargo build of photocraft-bridge failed' }
} finally { Pop-Location }

# 2. The MSVC environment.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio with the C++ x64 tools was not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
  if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

# 3. CMake and Ninja: on PATH, bundled with Visual Studio, or from the Android SDK.
function Find-Tool($name, $candidates) {
  $onPath = Get-Command $name -ErrorAction SilentlyContinue
  if ($onPath) { return $onPath.Source }
  foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
  throw "$name was not found"
}
$sdkCmake = Get-ChildItem (Join-Path $env:LOCALAPPDATA 'Android\Sdk\cmake') -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
$cmake = Find-Tool 'cmake' @(
  (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
  $(if ($sdkCmake) { Join-Path $sdkCmake.FullName 'bin\cmake.exe' }))
$ninja = Find-Tool 'ninja' @(
  (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'),
  $(if ($sdkCmake) { Join-Path $sdkCmake.FullName 'bin\ninja.exe' }))

$build = Join-Path $PSScriptRoot 'build'
$output = if ($OutputDir) { [IO.Path]::GetFullPath($OutputDir) } else { Join-Path $PSScriptRoot 'dist' }
& $cmake -S (Join-Path $PSScriptRoot 'shell') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninja" "-DOUTPUT_DIR=$output" "-DBUILD_TESTING=$(if ($Tests) { 'ON' } else { 'OFF' })"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& $cmake --build $build
if ($LASTEXITCODE) { throw 'C++ build failed' }

$exe = Join-Path $output 'PhotoCraftVetro.exe'
Write-Host "Built $exe"
if ($Run) {
  $style = if ($Hidden) { 'Hidden' } else { 'Normal' }
  if ($Open) { Start-Process $exe -ArgumentList "`"$Open`"" -WindowStyle $style } else { Start-Process $exe -WindowStyle $style }
}
