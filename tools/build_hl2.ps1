# builds halfcraft's half-life 2 side (client.dll + server.dll, two world shaders) and lays out the mod folder, then
# HalfCraft.exe, a release's launcher (into build\launcher).
#
#   tools/build_hl2.ps1              generate projects, build release, deploy
#   tools/build_hl2.ps1 -NoProjects  skip vpc (no .vpc file changed)
#
# expects the patched sdk at <repo>/source-sdk-2013 (tools/setup_sdk.ps1) and visual studio 2022+.

param(
	[switch]$NoProjects,
	[ValidateSet("Release", "Debug")][string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $repo "source-sdk-2013"
$src = Join-Path $sdk "src"
$modDir = Join-Path $sdk "game\mod_hl2"

if (-not (Test-Path (Join-Path $src "createallprojects.bat"))) {
	throw "no sdk at $sdk - run tools/setup_sdk.ps1 first"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vs) {
	throw "visual studio with msbuild not found"
}
$msbuild = Join-Path $vs "MSBuild\Current\Bin\MSBuild.exe"

# the sdk's projects ask for vs2022's toolset; build with whichever one this visual studio has
$toolsets = Get-ChildItem (Join-Path $vs "MSBuild\Microsoft\VC") -Recurse -Directory -Filter "v14*" |
	Where-Object { $_.Parent.Name -eq "PlatformToolsets" } | Select-Object -ExpandProperty Name -Unique | Sort-Object
$toolset = if ($toolsets -contains "v143") { "v143" } else { $toolsets | Select-Object -Last 1 }

Push-Location $src
try {
	if (-not $NoProjects) {
		& .\devtools\bin\vpc.exe /hl2 /define:SOURCESDK +game /mksln games_hl2.sln
		if ($LASTEXITCODE -ne 0) { throw "vpc failed ($LASTEXITCODE)" }
	}
	& $msbuild games_hl2.sln /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=win64 "/p:PlatformToolset=$toolset"
	if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
} finally {
	Pop-Location
}

# shaders: the stock world flashlight pair with halfcraft's edits (halfcraft-sdk.patch), compiled under
# their stock names into the mod's shaders\fxc, where the engine finds them before hl2's vpk. built in a
# copy: ShaderCompile2 also writes .inc files next to its input, which would land in the sdk's tree.
# they have to keep the stock combo layout, which stdshader_dx9.dll indexes them by
$stdshaders = Join-Path $src "materialsystem\stdshaders"
$stage = Join-Path $repo "build\shaders"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item (Join-Path $stdshaders "*.h") $stage
$fxcOut = Join-Path $modDir "shaders\fxc"
New-Item -ItemType Directory -Force $fxcOut | Out-Null
$shaders = @(
	@{ Source = "flashlight_ps2x.fxc"; Name = "flashlight_ps20b"; Combos = 1152; Dynamic = 4 },
	@{ Source = "lightmappedgeneric_flashlight_vs20.fxc"; Name = "lightmappedgeneric_flashlight_vs20"; Combos = 32; Dynamic = 2 }
)
foreach ($shader in $shaders) {
	Copy-Item (Join-Path $stdshaders $shader.Source) $stage
	& (Join-Path $src "devtools\bin\ShaderCompile2.exe") -ver 20b -shaderpath $stage $shader.Source | Out-Null
	$vcs = Join-Path $stage "shaders\fxc\$($shader.Name).vcs"
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path $vcs)) { throw "ShaderCompile2 failed on $($shader.Source) ($LASTEXITCODE)" }
	$header = [IO.File]::ReadAllBytes($vcs)  # version, combos, dynamic combos
	if ([BitConverter]::ToInt32($header, 0) -ne 6 -or [BitConverter]::ToInt32($header, 4) -ne $shader.Combos -or [BitConverter]::ToInt32($header, 8) -ne $shader.Dynamic) {
		throw "$($shader.Name).vcs doesn't have the stock combo layout"
	}
	Copy-Item $vcs $fxcOut
}

# the mod's own files (gameinfo, cfg) next to the freshly published dlls
Copy-Item -Recurse -Force (Join-Path $repo "source\mod\*") $modDir
Write-Host "built with $toolset; mod folder: $modDir"

# HalfCraft.exe, a release's launcher (source\launcher), versioned like the minecraft mod
$launcherSrc = Join-Path $repo "source\launcher"
$launcherOut = Join-Path $repo "build\launcher"
New-Item -ItemType Directory -Force $launcherOut | Out-Null
$version = (Select-String -Path (Join-Path $repo "minecraft\gradle.properties") -Pattern '^version=(\d+)\.(\d+)\.(\d+)').Matches[0]
$numbers = ($version.Groups[1..3] | ForEach-Object { $_.Value }) -join ","
Set-Content (Join-Path $launcherOut "version.h") "#define HC_VERSION $numbers,0`r`n#define HC_VERSION_TEXT `"$($numbers -replace ',', '.')`"" -Encoding ascii
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$script = Join-Path $launcherOut "build.cmd"
Set-Content $script -Encoding ascii @"
@echo off
call "$vcvars" >nul 2>&1 || exit /b 1
cd /d "$launcherSrc"
rc /nologo /I "$launcherOut" /fo "$launcherOut\halfcraft.res" halfcraft.rc || exit /b 1
cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /DUNICODE /D_UNICODE /I "$repo\source\src" /I "$repo\protocol" /Fo"$launcherOut\\" /Fe"$launcherOut\HalfCraft.exe" halfcraft_launcher.cpp "$repo\source\src\core\hc_prism.cpp" "$launcherOut\halfcraft.res" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib shlwapi.lib advapi32.lib || exit /b 1
"@
cmd /c "`"$script`""
if ($LASTEXITCODE -ne 0) { throw "launcher build failed ($LASTEXITCODE)" }
Write-Host "launcher: $launcherOut\HalfCraft.exe"
