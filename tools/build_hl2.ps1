# builds halfcraft's half-life 2 side (client.dll + server.dll) and lays out the mod folder.
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

# the mod's own files (gameinfo, cfg) next to the freshly published dlls
Copy-Item -Recurse -Force (Join-Path $repo "source\mod\*") $modDir
Write-Host "built with $toolset; mod folder: $modDir"
