# Builds Kagura.dll, and optionally installs it into the game or packages a release zip.
#
#   .\build.ps1                             build + install into the game
#   .\build.ps1 -NoInstall                  build only
#   .\build.ps1 -Package -Version v1.0.0    build + produce dist\kagura_v1.0.0.zip
#
# The Storm Framework is x64, so this must be an x64 Release build.

param(
    [string]$GamePath = "D:\SteamLibrary\steamapps\common\NARUTO SHIPPUDEN Ultimate Ninja STORM 4",
    [switch]$NoInstall,
    [switch]$Package,
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcDir  = Join-Path $root "src"
$outDir  = Join-Path $root "build"
$libDir  = Join-Path $root "lib"
$dllName = "Kagura.dll"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# Locate vcvars64.bat through vswhere so this works on any edition - Build Tools
# locally, Enterprise on the GitHub runners - rather than a hardcoded path.
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

$vcvars = Find-VcVars
Write-Host "Toolchain: $vcvars"

# cl must run inside the vcvars environment, so shell out through cmd.
$sources = (Get-ChildItem $srcDir -Filter *.cpp | ForEach-Object { '"' + $_.FullName + '"' }) -join " "

$clArgs = @(
    "/nologo"
    "/LD"           # build a DLL
    "/EHsc"
    "/O2"
    "/MD"           # dynamic CRT, matching the framework
    "/std:c++17"
    "/I`"$srcDir`""
    $sources
    "/link"
    "/OUT:`"$outDir\$dllName`""
) -join " "

$cmd = "call `"$vcvars`" >nul 2>&1 && cd /d `"$outDir`" && cl $clArgs"

Write-Host "Building $dllName ..."
$output = & cmd.exe /c $cmd 2>&1
$exit = $LASTEXITCODE

$output | ForEach-Object { Write-Host $_ }

if ($exit -ne 0) { throw "Build FAILED (exit $exit)" }
if (-not (Test-Path "$outDir\$dllName")) { throw "Build reported success but $dllName is missing" }

Write-Host "Build OK -> $outDir\$dllName"

if (-not (Test-Path "$libDir\prism.dll")) {
    throw "lib\prism.dll is missing. Run scripts\build-prism.ps1 first."
}

# info.txt is REQUIRED or the framework skips the mod folder entirely.
# Line 1 name, line 2 description, line 3 author.
$infoText = "Kagura`r`nScreen reader accessibility for NARUTO SHIPPUDEN: Ultimate Ninja STORM 4`r`ntunmi13productions"

# Assembles the exact folder the user drops into moddingapi\mods\.
# prism.dll goes in a SUBfolder: the framework scans the mod folder
# non-recursively for *.dll and would otherwise try to load it as a plugin.
function New-ModTree($target) {
    New-Item -ItemType Directory -Force -Path "$target\lib" | Out-Null
    Copy-Item "$outDir\$dllName" $target -Force
    Copy-Item "$root\names.txt" $target -Force
    Copy-Item "$libDir\prism.dll" "$target\lib" -Force
    $infoText | Set-Content -Path "$target\info.txt" -Encoding ascii -NoNewline
}

if ($Package) {
    $distDir = Join-Path $root "dist"
    $stage   = Join-Path $outDir "package"

    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $distDir, $stage | Out-Null

    New-ModTree "$stage\Kagura"
    foreach ($doc in @("README.md", "LICENSE", "NOTICE")) {
        if (Test-Path "$root\$doc") { Copy-Item "$root\$doc" $stage -Force }
    }

    $zipName = if ($Version) { "kagura_$Version.zip" } else { "kagura.zip" }
    $zipPath = Join-Path $distDir $zipName

    Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path "$stage\*" -DestinationPath $zipPath

    Write-Host "Packaged -> $zipPath"
    return
}

if ($NoInstall) { return }

if (-not (Test-Path $GamePath)) { throw "Game not found at $GamePath. Pass -GamePath." }

$modDir = Join-Path $GamePath "moddingapi\mods\Kagura"
New-ModTree $modDir

Write-Host "Installed -> $modDir"
