# checks a map's collision voxels offline: wherever half-life 2's player can stand, minecraft's mobs and
# items must find a voxel to stand on too. builds tools\collision_check.cpp (the streamer's own voxelizer,
# source\src\core\hc_collision_shapes.h) into build\collision_check and runs it on the map's .bsp from half-life 2's
# install. no game has to run.
#
#   tools/collision_check.ps1 -Map d1_canals_01 -GridZ 16                    the whole map
#   tools/collision_check.ps1 -Map d1_canals_01 -GridZ 16 -At "217,-968"     512 units square around a spot (x, y)
#   tools/collision_check.ps1 -Bsp C:\maps\x.bsp -GridZ 0                    any .bsp
#
# -GridZ: the map's grid height, as server.dll logs it on a load ("grid offset for d1_canals_01: 16 units").
# exits 1 when it finds holes, and lists the worst of them (source coordinates). it also prints what the
# streamer's worker thread spends on a region (triangulate + voxelize), on average and at worst.

param(
	[string]$Map = "",
	[string]$Bsp = "",
	[Parameter(Mandatory)][int]$GridZ,
	[string]$At = "",
	[double]$Radius = 256
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "engines.ps1")

if (-not $Bsp) {
	if (-not $Map) { throw "give -Map or -Bsp" }
	$install = Split-Path -Parent (Find-EngineExe "hl2")
	$Bsp = "hl2", "episodic", "ep2" | ForEach-Object { Join-Path $install "$_\maps\$Map.bsp" } | Where-Object { Test-Path $_ } | Select-Object -First 1
	if (-not $Bsp) { throw "$Map.bsp isn't in half-life 2's install ($install)" }
}

$out = Join-Path $repo "build\collision_check"
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $out "collision_check.exe"
$sources = @((Join-Path $PSScriptRoot "collision_check.cpp")) + (Get-ChildItem (Join-Path $repo "source\src\core") -Filter "hc_*.h" | ForEach-Object { $_.FullName })
$stale = -not (Test-Path $exe) -or ($sources | Where-Object { (Get-Item $_).LastWriteTime -gt (Get-Item $exe).LastWriteTime })
if ($stale) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	$vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (-not $vs) { throw "visual studio with the c++ compiler not found" }
	$script = Join-Path $out "build.cmd"
	Set-Content $script -Encoding ascii @"
@echo off
call "$(Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat")" >nul 2>&1 || exit /b 1
cl /nologo /O2 /W4 /EHsc /std:c++17 /I "$repo\source\src" /Fo"$out\\" /Fe"$exe" "$PSScriptRoot\collision_check.cpp" || exit /b 1
"@
	cmd /c "`"$script`""
	if ($LASTEXITCODE -ne 0) { throw "collision_check build failed ($LASTEXITCODE)" }
}

$arguments = @($Bsp, "$GridZ")
if ($At) {
	$point = @($At -split "," | ForEach-Object { [double]$_.Trim() })
	if ($point.Count -ne 2) { throw "-At takes a spot as x,y (source units)" }
	$arguments += @("$($point[0])", "$($point[1])", "$Radius")
}
& $exe @arguments
exit $LASTEXITCODE
