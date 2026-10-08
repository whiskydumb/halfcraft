# the two engines halfcraft runs on, for the powershell scripts in tools\ that dot-source this
# (tools/halfcraft/engines.py has them for the python ones).
#
#   Find-EngineExe -Engine hl2   the engine's exe in whichever steam library has it
#   Get-RunningEngine            the running source game, or nothing

# engine id -> the subfolder of a game folder its engine loads the dlls from, and the steam app whose folder
# has the engine
$script:GameFolderEngines = @{
	hl2   = @{ GameBin = "bin"; App = 220; SteamFolder = "Half-Life 2"; Exe = "hl2.exe" }
	hl2dm = @{ GameBin = "bin\x64"; App = 320; SteamFolder = "Half-Life 2 Deathmatch"; Exe = "hl2mp_win64.exe" }
}

# the engine's exe from any steam library (steamapps\libraryfolders.vdf lists them)
function Find-EngineExe([string]$Engine) {
	$info = $script:GameFolderEngines[$Engine]
	$steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
	if (-not $steam) { throw "steam not found" }
	$libraries = @($steam)
	$vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
	if (Test-Path $vdf) {
		$libraries += Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' }
	}
	$exe = $libraries | ForEach-Object { Join-Path $_ "steamapps\common\$($info.SteamFolder)\$($info.Exe)" } | Where-Object { Test-Path $_ } | Select-Object -First 1
	if (-not $exe) { throw "$($info.SteamFolder) isn't installed (steam app $($info.App))" }
	$exe
}

# the running source game (halfcraft's or not: only one can run at a time), or nothing
function Get-RunningEngine {
	Get-Process hl2, hl2mp_win64 -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1
}
