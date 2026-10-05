# the two engines halfcraft runs on, and what they need. dot-sourced by the scripts in tools\.
#
#   hl2    half-life 2's own engine: 32-bit hl2.exe (app 220), dlls from source-sdk-2013-sp
#   hl2dm  half-life 2: deathmatch's: 64-bit hl2mp_win64.exe (app 320), dlls from source-sdk-2013
#
#   Copy-GameFolder -Engine hl2 -Destination <folder>   lays out a game folder (what -game points at):
#       build_hl2.ps1's dev ones in build\, package.ps1's for a release, so both look the same
#   Find-EngineExe -Engine hl2                          the engine's exe in whichever steam library has it
#
# a game folder's layers, later ones win: hl2dm-sp's campaign files (cfg\chapterN.cfg, commentary,
# strings, ...), then source\mod\common, then source\mod\<engine> (gameinfo.txt), then the engine's dlls
# and the world shaders. files a running game writes (save\, cfg\config.cfg, screenshots\) are left alone.

$script:GameFolderRepo = Split-Path -Parent $PSScriptRoot

# engine id -> where its sdk tree publishes the dlls, the subfolder its engine loads them from, and the
# steam app whose folder has the engine
$script:GameFolderEngines = @{
	hl2   = @{ Bin = "source-sdk-2013-sp\sp\game\mod_hl2\bin"; GameBin = "bin"; App = 220; SteamFolder = "Half-Life 2"; Exe = "hl2.exe" }
	hl2dm = @{ Bin = "source-sdk-2013\game\mod_hl2\bin\x64"; GameBin = "bin\x64"; App = 320; SteamFolder = "Half-Life 2 Deathmatch"; Exe = "hl2mp_win64.exe" }
}

# hl2dm-sp's files that only fit half-life 2: deathmatch's engine: its steam.inf (app 320), its
# gameui option layouts (they'd replace half-life 2's own) and copies of strings half-life 2 has itself.
# the particle manifest is the 2025 tree's; half-life 2 brings its own
$script:GameFolderHl2dmOnly = @(
	"steam.inf",
	"resource\OptionsSubMouse.res",
	"resource\OptionsSubVideo.res",
	"resource\OptionsSubVideoAdvancedDlg.res",
	"resource\gameui_english.txt",
	"resource\valve_english.txt",
	"particles\particles_manifest.txt"
)

function Get-EngineDlls([string]$Engine) {
	$bin = Join-Path $script:GameFolderRepo $script:GameFolderEngines[$Engine].Bin
	@("client", "server") | ForEach-Object { Join-Path $bin "$_.dll" }
}

function Copy-GameFolder {
	param(
		[Parameter(Mandatory)][ValidateSet("hl2", "hl2dm")][string]$Engine,
		[Parameter(Mandatory)][string]$Destination,
		[switch]$WithSymbols  # the .pdb next to each dll (dev folders: tools/read_dump.py finds them there)
	)
	$sdk = Join-Path $script:GameFolderRepo "source-sdk-2013"
	New-Item -ItemType Directory $Destination -Force | Out-Null

	# hl2dm-sp's campaign files: what its git tracks, not what running it left behind
	foreach ($file in (git -C $sdk ls-files "game/mod_hl2")) {
		$relative = $file.Substring("game/mod_hl2/".Length) -replace '/', '\'
		if ($relative -eq "gameinfo.txt" -or ($Engine -ne "hl2dm" -and $script:GameFolderHl2dmOnly -contains $relative)) { continue }
		$target = Join-Path $Destination $relative
		New-Item -ItemType Directory (Split-Path $target) -Force | Out-Null
		Copy-Item (Join-Path $sdk ($file -replace '/', '\')) $target -Force
	}

	# the engine reads the mod's strings from resource\<game folder>_<language>.txt and the new game
	# dialog's chapter titles from "<game folder>_ChapterN_Title"
	$folder = Split-Path -Leaf $Destination
	$strings = Join-Path $Destination "resource\mod_hl2_english.txt"
	$text = [IO.File]::ReadAllText($strings, [Text.Encoding]::Unicode)
	[IO.File]::WriteAllText((Join-Path $Destination "resource\$($folder)_english.txt"), $text.Replace('"mod_hl2_Chapter', "`"$($folder)_Chapter"), [Text.Encoding]::Unicode)
	Remove-Item $strings

	foreach ($layer in @("common", $Engine)) {
		Copy-Item -Recurse -Force (Join-Path $script:GameFolderRepo "source\mod\$layer\*") $Destination
	}

	$gameBin = Join-Path $Destination $script:GameFolderEngines[$Engine].GameBin
	New-Item -ItemType Directory $gameBin -Force | Out-Null
	foreach ($dll in (Get-EngineDlls $Engine)) {
		if (-not (Test-Path $dll)) { throw "missing $dll (build the $Engine engine's dlls first)" }
		Copy-Item $dll $gameBin -Force
		if ($WithSymbols) { Copy-Item ([IO.Path]::ChangeExtension($dll, "pdb")) $gameBin -Force }
	}

	# the two world flashlight shaders with halfcraft's edits; both engines' stdshader_dx9 take the same
	# files (the stock ones are byte for byte the same in half-life 2 and hl2:dm)
	$fxc = Join-Path $Destination "shaders\fxc"
	New-Item -ItemType Directory $fxc -Force | Out-Null
	foreach ($shader in @("flashlight_ps20b", "lightmappedgeneric_flashlight_vs20")) {
		$vcs = Join-Path $script:GameFolderRepo "build\shaders\out\$shader.vcs"
		if (-not (Test-Path $vcs)) { throw "missing $vcs (build the shaders first)" }
		Copy-Item $vcs $fxc -Force
	}
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
