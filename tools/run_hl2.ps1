# starts halfcraft's half-life 2 from its dev game folder (build\game-<engine>, tools/build_hl2.ps1) on one
# of the two engines (tools/engines.ps1); that engine's steam app must be installed.
#
#   tools/run_hl2.ps1                         hl2:dm's 64-bit engine, windowed 1280x720, main menu
#   tools/run_hl2.ps1 -Engine hl2             half-life 2's own 32-bit engine
#   tools/run_hl2.ps1 -Map d1_trainstation_02 straight into a map
#   tools/run_hl2.ps1 -Fullscreen
#   tools/run_hl2.ps1 -Width 2560 -Height 1080   another window size
#
# minecraft: tools/launch_minecraft.bat (dev client); it links up by itself once the game runs.

param(
	[ValidateSet("hl2", "hl2dm")][string]$Engine = "hl2dm",
	[string]$Map = "",
	[switch]$Fullscreen,
	[int]$Width = 1280,
	[int]$Height = 720,
	[string[]]$Extra = @()  # more engine arguments, e.g. -Extra "+hc_debug_blocks","1"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "engines.ps1")

$game = Join-Path $repo "build\game-$Engine"
if (-not (Test-Path (Join-Path $game "$($script:GameFolderEngines[$Engine].GameBin)\client.dll"))) {
	throw "no build in $game - run tools/build_hl2.ps1 -Engine $Engine first"
}
$running = Get-RunningEngine
if ($running) {
	throw "$($running.ProcessName).exe already runs: source runs one game at a time"
}
$exe = Find-EngineExe $Engine

$arguments = @("-game", "`"$game`"", "-novid", "-condebug", "+con_enable", "1", "+developer", "1")
if (-not $Fullscreen) { $arguments += @("-windowed", "-w", "$Width", "-h", "$Height") }
$arguments += $Extra
if ($Map) { $arguments += @("+map", $Map) }
Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exe) | Out-Null
Write-Host "started $exe; console log: $game\console.log"
