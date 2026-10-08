# keeps test sessions away from the player's own games: half-life's saves and settings, and minecraft's
# world.
#
#   tools/test_session.ps1 start [-Engine hl2dm]   puts build\game-<engine>\save and cfg\config.cfg aside
#                                                  (build\test-session\<engine>): the test starts without saves
#   tools/test_session.ps1 stop [-Engine hl2dm]    puts them back; the test's own saves and settings go to
#                                                  build\test-session\<engine>-last
#   tools/test_session.ps1 status                  what's set aside, and which games run
#   tools/test_session.ps1 minecraft               the minecraft test client (make mc-test): its own run
#                                                  folder (minecraft\run\test) and world (HalfCraftTest)
#   tools/test_session.ps1 dev-client              the player's minecraft dev client (make mc-run)
#
# half-life's saves carry checkpoints of the player's minecraft world, and every changelevel autosaves
# over them, so a test never plays with them. the game folder itself stays: its strings file is named
# after it (resource\<folder>_english.txt). only one minecraft can hold the link, so neither client
# starts next to another one. the test client only starts while every engine has a test session on,
# the player's only while none has, and a session neither starts nor stops while a minecraft runs:
# otherwise one world plays against the other's saves (it follows their loads and rolls back, and
# their autosaves land on the wrong saves). -Engine all (the default) does both engines.

param(
	[Parameter(Mandatory = $true, Position = 0)][ValidateSet("start", "stop", "status", "minecraft", "dev-client")][string]$Action,
	[ValidateSet("hl2", "hl2dm", "all")][string]$Engine = "all"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "engines.ps1")

$engines = if ($Engine -eq "all") { @("hl2", "hl2dm") } else { @($Engine) }
$sessions = Join-Path $repo "build\test-session"
# what minecraft (HostLink.announceRunning) holds while it runs: the dev client, the test client or a release's
$minecraftMutex = "Local\HalfCraft_v1_minecraft"
$playerLog = Join-Path $repo "minecraft\run\logs\latest.log"
$testLog = Join-Path $repo "minecraft\run\test\logs\latest.log"

function Test-MinecraftRunning {
	$mutex = $null
	if ([Threading.Mutex]::TryOpenExisting($minecraftMutex, [ref]$mutex)) {
		$mutex.Dispose()
		return $true
	}
	$false
}

# a running minecraft keeps its latest.log open for writing: that tells which run folder it plays from
function Test-FileInUse([string]$path) {
	if (-not (Test-Path $path)) { return $false }
	try {
		$stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
		$stream.Close()
		$false
	} catch [IO.IOException] {
		$true
	}
}

# "none", "test" (make mc-test's), "player" (the dev client in minecraft\run) or "other" (a release's)
function Get-MinecraftClient {
	if (-not (Test-MinecraftRunning)) { return "none" }
	if (Test-FileInUse $testLog) { return "test" }
	if (Test-FileInUse $playerLog) { return "player" }
	"other"
}

function Assert-GameStopped {
	$game = Get-RunningEngine
	if ($game) { throw "$($game.ProcessName).exe runs: quit it first (it writes save\ and cfg\config.cfg)" }
}

function Get-Paths([string]$name) {
	$mod = Join-Path $repo "build\game-$name"
	@{
		Mod    = $mod
		Save   = Join-Path $mod "save"
		Config = Join-Path $mod "cfg\config.cfg"
		Keep   = Join-Path $sessions $name
		Last   = Join-Path $sessions "$name-last"
	}
}

# the engines (with a game folder) that have a test session on ($true) or haven't ($false)
function Get-Engines([bool]$inSession) {
	foreach ($name in $engines) {
		$p = Get-Paths $name
		if ((Test-Path $p.Mod) -and (Test-Path $p.Keep) -eq $inSession) { $name }
	}
}

function Assert-MinecraftStopped([string]$why) {
	$client = Get-MinecraftClient
	if ($client -ne "none") { throw "the $client minecraft runs: $why" }
}

switch ($Action) {
	"start" {
		Assert-GameStopped
		Assert-MinecraftStopped "its world would follow the test's loads. quit it, start the session, then the test client (make mc-test)"
		$todo = @()
		foreach ($name in $engines) {
			$p = Get-Paths $name
			if (-not (Test-Path $p.Mod)) { Write-Host "${name}: no game folder ($($p.Mod)); skipped"; continue }
			if (Test-Path $p.Keep) { throw "${name}: a test session is already on (since $(Get-Content (Join-Path $p.Keep 'session.txt') -First 1)); stop it first" }
			$todo += $name
		}
		foreach ($name in $todo) {
			$p = Get-Paths $name
			New-Item -ItemType Directory -Path $p.Keep | Out-Null
			Set-Content -Path (Join-Path $p.Keep "session.txt") -Encoding ascii -Value (Get-Date -Format s)
			$hadSave = Test-Path $p.Save
			$hadConfig = Test-Path $p.Config
			if ($hadSave) { Move-Item $p.Save (Join-Path $p.Keep "save") }
			if ($hadConfig) { Copy-Item $p.Config (Join-Path $p.Keep "config.cfg") }
			Add-Content -Path (Join-Path $p.Keep "session.txt") -Encoding ascii -Value @("save=$hadSave", "config=$hadConfig")
			Write-Host "${name}: test session on; the player's saves and settings are in $($p.Keep)"
		}
	}
	"stop" {
		Assert-GameStopped
		Assert-MinecraftStopped "quit it first: with the player's saves back, its world would play against them"
		foreach ($name in $engines) {
			$p = Get-Paths $name
			if (-not (Test-Path $p.Keep)) { Write-Host "${name}: no test session"; continue }
			$sessionFile = Join-Path $p.Keep "session.txt"
			$keptSave = Join-Path $p.Keep "save"
			$keptConfig = Join-Path $p.Keep "config.cfg"
			# the test's own saves and settings go aside once. "parked" says they went, so a stop run again
			# after one that broke off never takes the player's (already back) for the test's
			if (-not ((Test-Path $sessionFile) -and (Get-Content $sessionFile) -contains "parked")) {
				if (Test-Path $p.Last) { Remove-Item $p.Last -Recurse -Force }
				New-Item -ItemType Directory -Path $p.Last | Out-Null
				if (Test-Path $p.Save) { Move-Item $p.Save (Join-Path $p.Last "save") }
				if (Test-Path $p.Config) { Move-Item $p.Config (Join-Path $p.Last "config.cfg") }
				Add-Content -Path $sessionFile -Encoding ascii -Value "parked"
			}
			# whatever is still in the session folder is the player's: it goes back, never over anything
			foreach ($item in @(@($keptSave, $p.Save), @($keptConfig, $p.Config))) {
				if (-not (Test-Path $item[0])) { continue }
				if (Test-Path $item[1]) { throw "${name}: $($item[1]) came back since the last stop; move it away and stop again (the player's is in $($item[0]))" }
				Move-Item $item[0] $item[1]
			}
			Remove-Item $sessionFile
			Remove-Item $p.Keep
			Write-Host "${name}: test session off; the player's saves and settings are back, the test's are in $($p.Last)"
		}
	}
	"status" {
		foreach ($name in $engines) {
			$p = Get-Paths $name
			if (Test-Path $p.Keep) {
				Write-Host "${name}: test session on since $(Get-Content (Join-Path $p.Keep 'session.txt') -First 1)"
			} else {
				Write-Host "${name}: no test session"
			}
		}
		$game = Get-RunningEngine
		Write-Host "half-life: $(if ($game) { "$($game.ProcessName).exe runs" } else { 'not running' })"
		Write-Host "minecraft: $(Get-MinecraftClient)"
	}
	"minecraft" {
		Assert-MinecraftStopped "two would fight over the one link"
		$missing = @(Get-Engines $false)
		if ($missing.Count -gt 0) {
			throw "no test session for $($missing -join ', '): the test world would autosave over the player's saves. make test-start first"
		}
		python (Join-Path $PSScriptRoot "gradle.py") runTestClient
		if ($LASTEXITCODE -ne 0) { throw "the test client's gradle failed ($LASTEXITCODE)" }
	}
	"dev-client" {
		Assert-MinecraftStopped "two would fight over the one link"
		$on = @(Get-Engines $true)
		if ($on.Count -gt 0) {
			throw "a test session is on for $($on -join ', '): the player's world would play against the test's saves. make test-stop first"
		}
		python (Join-Path $PSScriptRoot "gradle.py") runClient
		if ($LASTEXITCODE -ne 0) { throw "the dev client's gradle failed ($LASTEXITCODE)" }
	}
}
