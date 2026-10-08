# builds halfcraft's half-life 2 side for one or both engines (client.dll + server.dll, two world shaders),
# lays out the dev game folders build\game-<engine>, then HalfCraft.exe, a release's launcher (build\launcher).
#
#   tools/build_hl2.ps1                  both engines: generate projects, build release, lay out
#   tools/build_hl2.ps1 -Engine hl2      half-life 2's own 32-bit engine only (source-sdk-2013-sp)
#   tools/build_hl2.ps1 -Engine hl2dm    half-life 2: deathmatch's 64-bit engine only (source-sdk-2013)
#   tools/build_hl2.ps1 -NoProjects      skip vpc (no .vpc file changed)
#   tools/build_hl2.ps1 -NoLauncher      skip HalfCraft.exe
#
# expects both patched sdk trees (tools/setup_sdk.ps1: the shaders always come from source-sdk-2013) and
# visual studio 2022+ with its x64 and x86 compilers. the games have to be closed: their dlls get replaced.
# halfcraft's own code (source\) builds without warnings: one there fails the build (the sdk's are valve's).

param(
	[ValidateSet("all", "hl2", "hl2dm")][string]$Engine = "all",
	[switch]$NoProjects,
	[switch]$NoLauncher,
	[ValidateSet("Release", "Debug")][string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "engines.ps1")
$engines = if ($Engine -eq "all") { @("hl2", "hl2dm") } else { @($Engine) }

$running = Get-Process hl2, hl2mp_win64 -ErrorAction SilentlyContinue
if ($running) {
	throw "close the game first ($(($running | ForEach-Object { "$($_.ProcessName).exe" }) -join ', ') runs): its dlls get replaced"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vs) {
	throw "visual studio with msbuild not found"
}
$msbuild = Join-Path $vs "MSBuild\Current\Bin\MSBuild.exe"

# the sdks' projects ask for vs2013's and vs2022's toolsets; build with whichever one this visual studio has
$toolsets = Get-ChildItem (Join-Path $vs "MSBuild\Microsoft\VC") -Recurse -Directory -Filter "v14*" |
	Where-Object { $_.Parent.Name -eq "PlatformToolsets" } | Select-Object -ExpandProperty Name -Unique | Sort-Object
$toolset = if ($toolsets -contains "v143") { "v143" } else { $toolsets | Select-Object -Last 1 }

# the 2015 tree's vs2013 projects name no windows sdk; give them the newest one installed
$windowsSdk = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include") -Directory |
	Where-Object { Test-Path (Join-Path $_.FullName "um\windows.h") } | Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty Name
if (-not $windowsSdk) {
	throw "no windows 10/11 sdk found"
}

$logs = Join-Path $repo "build\logs"
New-Item -ItemType Directory -Force $logs | Out-Null

function Invoke-MSBuild([string]$project, [string]$platform, [string]$target = "Build") {
	$warnings = Join-Path $logs "$([IO.Path]::GetFileNameWithoutExtension($project))-$platform-warnings.log"
	& $msbuild $project "/t:$target" /m /nologo /v:minimal "/p:Configuration=$Configuration" "/p:Platform=$platform" "/p:PlatformToolset=$toolset" "/p:WindowsTargetPlatformVersion=$windowsSdk" "/flp:LogFile=$warnings;WarningsOnly"
	if ($LASTEXITCODE -ne 0) { throw "build of $project failed ($LASTEXITCODE)" }
	# what the compiler says about halfcraft's own files (the sdk compiles them too, with each of its projects);
	# msbuild /m puts its node's number in front of each line
	$ours = Get-Content $warnings | ForEach-Object { $_ -replace "^\s*\d+>", "" } |
		Where-Object { $_ -match "warning" -and $_.StartsWith("$repo\source\", [StringComparison]::OrdinalIgnoreCase) } |
		ForEach-Object { $_ -replace " \[[^\]]+\]$", "" } | Sort-Object -Unique
	if ($ours) {
		$ours | ForEach-Object { Write-Host $_ }
		throw "halfcraft's code has $(@($ours).Count) warnings (above): fix them"
	}
}

function Get-SdkSource([string]$tree, [string]$engine) {
	$src = Join-Path $repo $tree
	if (-not (Test-Path (Join-Path $src "createallprojects.bat"))) {
		throw "no sdk at $src - run tools/setup_sdk.ps1 -Engine $engine first"
	}
	$src
}

# the shaders and the game folders' campaign files come from source-sdk-2013 whichever engine is built:
# missing, it should stop the build before anything compiles, not after
Get-SdkSource "source-sdk-2013\src" "hl2dm" | Out-Null

# half-life 2: deathmatch's 64-bit engine: hl2dm-sp, valve's 2025 tree
function Build-Hl2dm {
	Push-Location (Get-SdkSource "source-sdk-2013\src" "hl2dm")
	try {
		if (-not $NoProjects) {
			& .\devtools\bin\vpc.exe /episodic /define:SOURCESDK +game /mksln games_episodic.sln
			if ($LASTEXITCODE -ne 0) { throw "vpc failed ($LASTEXITCODE)" }
		}
		Invoke-MSBuild games_episodic.sln win64
	} finally {
		Pop-Location
	}
}

# half-life 2's own 32-bit engine: valve's singleplayer tree of 2015. its vpc (2014) writes the projects
# but no .sln without vs2013's registry key, so they're built one by one: the libraries the game links
# first, because the prebuilt vs2013 copies of those don't link with today's crt
function Build-Hl2 {
	$tree = Join-Path $repo "source-sdk-2013-sp"
	Push-Location (Get-SdkSource "source-sdk-2013-sp\sp\src" "hl2")
	try {
		if (-not $NoProjects) {
			& .\devtools\bin\vpc.exe /episodic +game /f
			if ($LASTEXITCODE -ne 0) { throw "vpc failed ($LASTEXITCODE)" }
		}
		# every configuration of these writes the same tracked file, sp\src\lib\public\<name>.lib, and
		# msbuild calls it up to date while it's newer than the objects. so they're rebuilt when it's still
		# valve's vs2013 copy (setup, a git restore) or the last build was another configuration
		$libs = @("tier1\tier1.vcxproj", "mathlib\mathlib.vcxproj", "raytrace\raytrace.vcxproj", "vgui2\vgui_controls\vgui_controls.vcxproj")
		$stamp = Join-Path $repo "build\hl2-libs-configuration.txt"
		$rebuild = -not (Test-Path $stamp) -or (Get-Content $stamp) -ne $Configuration
		foreach ($lib in $libs) {
			git -C $tree diff --quiet -- "sp/src/lib/public/$([IO.Path]::GetFileNameWithoutExtension($lib)).lib"
			if ($LASTEXITCODE -eq 0) { $rebuild = $true }
		}
		foreach ($project in $libs) {
			Invoke-MSBuild $project Win32 $(if ($rebuild) { "Rebuild" } else { "Build" })
		}
		New-Item -ItemType Directory (Split-Path $stamp) -Force | Out-Null
		Set-Content $stamp $Configuration -Encoding ascii
		foreach ($project in @("game\client\client_episodic.vcxproj", "game\server\server_episodic.vcxproj")) {
			Invoke-MSBuild $project Win32
		}
	} finally {
		Pop-Location
	}
}

foreach ($e in $engines) {
	if ($e -eq "hl2") { Build-Hl2 } else { Build-Hl2dm }
}

# shaders: the stock world flashlight pair with halfcraft's edits (halfcraft-hl2dm.patch), compiled under
# their stock names into build\shaders\out; the game folders get them in shaders\fxc, where the engine
# finds them before half-life 2's vpk. both engines take the same files. built in a copy: ShaderCompile2
# also writes .inc files next to its input, which would land in the sdk's tree. they have to keep the stock
# combo layout, which stdshader_dx9.dll indexes them by. the 2015 tree has neither the compiler nor
# flashlight_ps2x.fxc, so they always come from source-sdk-2013
$src = Get-SdkSource "source-sdk-2013\src" "hl2dm"
$stdshaders = Join-Path $src "materialsystem\stdshaders"
$stage = Join-Path $repo "build\shaders"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item (Join-Path $stdshaders "*.h") $stage
$shaderOut = Join-Path $stage "out"
New-Item -ItemType Directory $shaderOut | Out-Null
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
	Copy-Item $vcs $shaderOut
}

# the dev game folders: laid out like a release's, plus the symbols
foreach ($e in $engines) {
	$game = Join-Path $repo "build\game-$e"
	Copy-GameFolder -Engine $e -Destination $game -WithSymbols
	Write-Host "built $e with $toolset; game folder: $game"
}

if ($NoLauncher) { return }

# HalfCraft.exe, a release's launcher (source\launcher), with halfcraft's version (tools/version.ps1)
$launcherSrc = Join-Path $repo "source\launcher"
$launcherOut = Join-Path $repo "build\launcher"
New-Item -ItemType Directory -Force $launcherOut | Out-Null
$numbers = & (Join-Path $PSScriptRoot "version.ps1") -Numbers
$version = & (Join-Path $PSScriptRoot "version.ps1")
Set-Content (Join-Path $launcherOut "version.h") "#define HC_VERSION $numbers`r`n#define HC_VERSION_TEXT `"$version`"" -Encoding ascii
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$script = Join-Path $launcherOut "build.cmd"
Set-Content $script -Encoding ascii @"
@echo off
call "$vcvars" >nul 2>&1 || exit /b 1
cd /d "$launcherSrc"
rc /nologo /I "$launcherOut" /fo "$launcherOut\halfcraft.res" halfcraft.rc || exit /b 1
cl /nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /I "$repo\source\src" /I "$repo\protocol" /Fo"$launcherOut\\" /Fe"$launcherOut\HalfCraft.exe" halfcraft_launcher.cpp "$repo\source\src\core\hc_prism.cpp" "$launcherOut\halfcraft.res" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib shlwapi.lib advapi32.lib comctl32.lib || exit /b 1
"@
cmd /c "`"$script`""
if ($LASTEXITCODE -ne 0) { throw "launcher build failed ($LASTEXITCODE)" }
Write-Host "launcher: $launcherOut\HalfCraft.exe"
