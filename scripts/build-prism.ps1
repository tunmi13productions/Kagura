# Builds Prism (https://github.com/ethindp/prism) and drops prism.dll into lib\.
#
# Kagura loads Prism at runtime rather than linking against it, so all that is
# needed from this build is the DLL itself.
#
#   .\scripts\build-prism.ps1
#   .\scripts\build-prism.ps1 -Ref main      build a different revision
#
# Requires: git, cmake, and the Visual Studio 2022 C++ build tools INCLUDING the
# "C++ ATL" component (Microsoft.VisualStudio.Component.VC.ATL). Without ATL the
# JAWS, SAPI, ZoomText, Window-Eyes and Sense Reader backends fail to compile.

param(
    # Pinned so a Kagura release is reproducible. Bump deliberately.
    [string]$Ref = "83742ee415fc895567493c19c1a639ad3c092760",
    [string]$WorkDir = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$libDir = Join-Path $root "lib"
if (-not $WorkDir) { $WorkDir = Join-Path $env:TEMP "kagura-prism" }

foreach ($tool in @("git", "cmake")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH" }
}

function Find-VcVars {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($path) {
            $candidate = Join-Path $path "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw "vcvars64.bat not found. Install the Visual Studio 2022 C++ build tools."
}

if (-not (Test-Path "$WorkDir\.git")) {
    Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Cloning Prism into $WorkDir ..."
    git -c core.longpaths=true clone https://github.com/ethindp/prism.git $WorkDir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
}

git -C $WorkDir fetch --all --tags
git -C $WorkDir checkout --force $Ref
if ($LASTEXITCODE -ne 0) { throw "git checkout $Ref failed" }

# The Godot GDExtension submodule is not needed and is large; the rest are vendored.
git -C $WorkDir submodule update --init --recursive --depth 1 2>&1 | Out-Null

$vcvars = Find-VcVars
$buildDir = Join-Path $WorkDir "build"

# CMake must run inside vcvars: without lib.exe on PATH it fails with
# "Neither 'lib' nor 'llvm-lib' found; cannot build import libraries."
$cfgArgs = @(
    "-S `"$WorkDir`""
    "-B `"$buildDir`""
    '-G "Visual Studio 17 2022"'
    "-A x64"
    "-DBUILD_SHARED_LIBS=ON"
    "-DPRISM_ENABLE_TESTS=OFF"
    "-DPRISM_ENABLE_DEMOS=OFF"
    "-DPRISM_ENABLE_GDEXTENSION=OFF"
    "-DPRISM_ENABLE_SHIMS=OFF"
    "-DPRISM_ENABLE_LEGACY_BACKENDS=ON"
) -join " "

Write-Host "Configuring Prism ..."
& cmd.exe /c "call `"$vcvars`" >nul 2>&1 && cmake $cfgArgs"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "Building Prism ..."
& cmd.exe /c "call `"$vcvars`" >nul 2>&1 && cmake --build `"$buildDir`" --config Release --parallel"
if ($LASTEXITCODE -ne 0) { throw "Prism build failed" }

$dll = Join-Path $buildDir "Release\prism.dll"
if (-not (Test-Path $dll)) { throw "Prism built but prism.dll is missing at $dll" }

New-Item -ItemType Directory -Force -Path $libDir | Out-Null
Copy-Item $dll $libDir -Force

Write-Host ("prism.dll -> {0} ({1:N0} KB)" -f $libDir, ((Get-Item $dll).Length / 1KB))
