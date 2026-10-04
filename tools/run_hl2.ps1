# starts halfcraft's half-life 2 on half-life 2: deathmatch's engine (it must be installed).
#
#   tools/run_hl2.ps1                         windowed 1280x720, main menu
#   tools/run_hl2.ps1 -Map d1_trainstation_02 straight into a map
#   tools/run_hl2.ps1 -Fullscreen
#
# minecraft: tools/launch_minecraft.bat (dev client); it links up by itself once the game runs.

param(
	[string]$Map = "",
	[switch]$Fullscreen,
	[string[]]$Extra = @()  # more engine arguments, e.g. -Extra "+hc_debug_blocks","1"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$mod = Join-Path $repo "source-sdk-2013\game\mod_hl2"
if (-not (Test-Path (Join-Path $mod "bin\x64\client.dll"))) {
	throw "no build in $mod - run tools/build_hl2.ps1 first"
}

# half-life 2: deathmatch in any steam library
$steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
if (-not $steam) { throw "steam not found" }
$libraries = @($steam)
$vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
if (Test-Path $vdf) {
	$libraries += Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' }
}
$exe = $libraries | ForEach-Object { Join-Path $_ "steamapps\common\Half-Life 2 Deathmatch\hl2mp_win64.exe" } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "half-life 2: deathmatch isn't installed (steam app 320)" }

$arguments = @("-game", "`"$mod`"", "-novid", "-condebug", "+con_enable", "1", "+developer", "1")
if (-not $Fullscreen) { $arguments += @("-windowed", "-w", "1280", "-h", "720") }
$arguments += $Extra
if ($Map) { $arguments += @("+map", $Map) }
Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exe) | Out-Null
Write-Host "started $exe; console log: $mod\console.log"
