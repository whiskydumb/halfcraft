# the two engines halfcraft runs on, and what they need. dot-sourced by the scripts in tools\.
#
#   hl2    half-life 2's own engine: 32-bit hl2.exe (app 220), dlls from source-sdk-2013-sp
#   hl2dm  half-life 2: deathmatch's: 64-bit hl2mp_win64.exe (app 320), dlls from source-sdk-2013
#
#   Copy-GameFolder -Engine hl2 -Destination <folder>   lays out a game folder (what -game points at):
#       build_hl2.ps1's dev ones in build\, package.ps1's for a release, so both look the same
#   Find-EngineExe -Engine hl2                          the engine's exe in whichever steam library has it
#
# a game folder holds half-life 2, episode one, episode two and lost coast as one game, as valve's
# hl2_complete does for the first three.
# its layers, later ones win: hl2dm-sp's files of the campaigns (commentary, hud scripts, fonts, ...;
# the later campaign's over the earlier's), the chapters of all of them numbered on (their cfg, picture and
# title) with the campaigns' strings merged, then source\mod\common, then source\mod\<engine>
# (gameinfo.txt), then the engine's dlls and the world shaders. files a running game writes (save\,
# cfg\config.cfg, screenshots\) are left alone.

$script:GameFolderRepo = Split-Path -Parent $PSScriptRoot

# engine id -> where its sdk tree publishes the episodic dlls (they run half-life 2 and both episodes, as
# valve's hl2_complete does), the subfolder its engine loads them from, and the
# steam app whose folder has the engine
$script:GameFolderEngines = @{
	hl2   = @{ Bin = "source-sdk-2013-sp\sp\game\mod_episodic\bin"; GameBin = "bin"; App = 220; SteamFolder = "Half-Life 2"; Exe = "hl2.exe" }
	hl2dm = @{ Bin = "source-sdk-2013\game\mod_ep1\bin\x64"; GameBin = "bin\x64"; App = 320; SteamFolder = "Half-Life 2 Deathmatch"; Exe = "hl2mp_win64.exe" }
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

# the campaigns in play order: hl2dm-sp's mod folder for each, the folder of its chapter pictures in
# valve's hl2_complete_misc.vpk (each campaign's own vgui\chapters would overwrite the others'), its
# search path id (core/hc_campaign.h), which prefixes its own value of a string the campaigns differ in,
# the name an episode's skill_episodic.cfg gets (server.dll's hc_campaign.cpp runs it on its maps), and
# which of its other files a campaign brings (lost coast's hud, particle and skill files are half-life 2's
# from before the episodes: only its commentary comes)
$script:GameFolderCampaigns = @(
	@{ Mod = "mod_hl2"; Pictures = "hl2"; PathId = "hc_hl2" },
	@{ Mod = "mod_ep1"; Pictures = "episodic"; PathId = "hc_ep1"; Skill = "skill_ep1.cfg" },
	@{ Mod = "mod_ep2"; Pictures = "ep2"; PathId = "hc_ep2"; Skill = "skill_ep2.cfg" },
	@{ Mod = "mod_lostcoast"; Pictures = "lostcoast"; PathId = "hc_lc"; Files = '^maps\\' }
)

# hl2dm-sp's files a game folder makes its own: the chapters and their titles (numbered on across the
# campaigns), the strings (merged) and the skill manifest (source\mod\common has it: half-life 2's
# skill.cfg on every map, and server.dll adds an episode's skill cfg on its maps). skill.cfg itself
# comes from hl2dm-sp: hl2:dm's engine only runs cfg files in the game folder
$script:GameFolderGenerated = '^(gameinfo\.txt|cfg\\chapter[0-9a-z]+\.cfg|cfg\\skill_manifest\.cfg|resource\\mod_[a-z0-9]+_english\.txt)$'

# a chapter's name ("9a") as a sortable key: its number, then its letter
function Get-ChapterOrder([string]$name) {
	$match = [regex]::Match($name, '^(\d+)(.*)$')
	[int]$match.Groups[1].Value * 100 + $(if ($match.Groups[2].Value) { [int][char]$match.Groups[2].Value[0] } else { 0 })
}

# the tokens of a "lang" file as (key, the whole line), in file order. one token per line, the way valve
# writes them
function Read-LangTokens([string]$path) {
	$tokens = [ordered]@{}
	foreach ($line in [IO.File]::ReadAllLines($path, [Text.Encoding]::Unicode)) {
		$match = [regex]::Match($line, '^\s*"([^"]+)"\s+"')
		if ($match.Success -and $match.Groups[1].Value -ne "Language" -and -not $tokens.Contains($match.Groups[1].Value)) {
			$tokens[$match.Groups[1].Value] = $line
		}
	}
	$tokens
}

# the chapters of the campaigns as one list (cfg, picture, title), and their strings merged into
# resource\<game folder>_english.txt: the engine reads a mod's strings from that file and the new game
# dialog's titles from "<game folder>_ChapterN_Title". where the campaigns' strings differ (episode two's
# game over lines), the earlier campaign's value is the string and a later one's is "<its path id>_<key>"
# too, which client.dll's titles.txt messages of that campaign look for first (hc_campaign_text.cpp)
function Write-Campaigns([string]$sdk, [string]$Destination, [string]$folder) {
	$offset = 0
	$titles = [System.Collections.Generic.List[string]]::new()
	$strings = [ordered]@{}
	$variants = [System.Collections.Generic.List[string]]::new()
	$value = { param($line) [regex]::Match($line, '^\s*"[^"]+"\s+"(.*)"').Groups[1].Value }
	foreach ($campaign in $script:GameFolderCampaigns) {
		$mod = Join-Path $sdk "game\$($campaign.Mod)"
		$tokens = Read-LangTokens (Join-Path $mod "resource\$($campaign.Mod)_english.txt")
		$chapters = Get-ChildItem (Join-Path $mod "cfg") -Filter "chapter*.cfg" | ForEach-Object { $_.BaseName.Substring("chapter".Length) } |
			Sort-Object { Get-ChapterOrder $_ }
		$last = 0
		foreach ($chapter in $chapters) {
			$number = [regex]::Match($chapter, '^\d+').Value
			$name = if ($offset -eq 0) { $chapter } else { "$([int]$number + $offset)$($chapter.Substring($number.Length))" }
			$last = [Math]::Max($last, [int]$number + $offset)
			Copy-Item (Join-Path $mod "cfg\chapter$chapter.cfg") (Join-Path $Destination "cfg\chapter$name.cfg") -Force
			$picture = Join-Path $Destination "materials\vgui\chapters\chapter$name.vmt"
			New-Item -ItemType Directory (Split-Path $picture) -Force | Out-Null
			Set-Content $picture -Encoding ascii @"
"UnlitGeneric"
{
	"`$baseTexture" "vgui/$($campaign.Pictures)/chapters/chapter$chapter"
	"`$vertexalpha" 1
	"`$gammaColorRead" "1"
	"`$linearWrite" "1"
}
"@
			$title = $tokens["$($campaign.Mod)_Chapter$($chapter)_Title"]
			if ($title) { $titles.Add([regex]::new('"[^"]+"').Replace($title, "`"$($folder)_Chapter$($name)_Title`"", 1)) }
		}
		$offset = $last
		foreach ($key in $tokens.Keys) {
			# the mod folders' own chapter titles are written above under this folder's name; the
			# campaigns' (HL2_, episodic_, ep2_) stay: their titles.txt messages show them
			if ($key -match '^mod_[a-z0-9]+_Chapter[0-9a-z]+_Title$') { continue }
			if (-not $strings.Contains($key)) {
				$strings[$key] = $tokens[$key]
			} elseif ((& $value $strings[$key]) -cne (& $value $tokens[$key])) {
				$variants.Add([regex]::new('"[^"]+"').Replace($tokens[$key], "`"$($campaign.PathId)_$key`"", 1))
			}
		}
	}
	$lines = @('"lang"', '{', '"Language" "English"', '"Tokens"', '{') + $titles + @($strings.Values) + $variants + @('}', '}')
	[IO.File]::WriteAllLines((Join-Path $Destination "resource\$($folder)_english.txt"), [string[]]$lines, [Text.Encoding]::Unicode)
}

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
	# laid out afresh in build\stage, then synced over: a file halfcraft shipped before and doesn't now
	# (halfcraft-files.txt lists the last layout's) goes, what the game wrote stays
	$stage = Join-Path $script:GameFolderRepo "build\stage\game-$Engine"
	if (Test-Path $stage) { [IO.Directory]::Delete($stage, $true) }
	New-Item -ItemType Directory $stage -Force | Out-Null
	Write-GameFolder $Engine $stage (Split-Path -Leaf $Destination) $WithSymbols.IsPresent

	$manifest = Join-Path $Destination "halfcraft-files.txt"
	$files = @(Get-ChildItem $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stage.Length + 1) })
	if (Test-Path $manifest) {
		foreach ($gone in (Get-Content $manifest | Where-Object { $_ -and $files -notcontains $_ })) {
			$path = Join-Path $Destination $gone
			if (Test-Path $path) { Remove-Item $path -Force }
		}
	}
	New-Item -ItemType Directory $Destination -Force | Out-Null
	Copy-Item -Recurse -Force (Join-Path $stage "*") $Destination
	Set-Content $manifest $files -Encoding utf8
}

# lays out a game folder for one engine into an empty $Destination; $Folder is the name it will have
function Write-GameFolder([string]$Engine, [string]$Destination, [string]$Folder, [bool]$WithSymbols) {
	$sdk = Join-Path $script:GameFolderRepo "source-sdk-2013"

	# hl2dm-sp's campaign files (what its git tracks, not what running it left behind), the later
	# campaign's over the earlier's: the episodes' hud scripts, fonts and particles cover half-life 2's
	foreach ($campaign in $script:GameFolderCampaigns) {
		$prefix = "game/$($campaign.Mod)/"
		foreach ($file in (git -C $sdk ls-files $prefix)) {
			$relative = $file.Substring($prefix.Length) -replace '/', '\'
			if ($relative -match $script:GameFolderGenerated -or ($Engine -ne "hl2dm" -and $script:GameFolderHl2dmOnly -contains $relative)) { continue }
			if ($campaign.Files -and $relative -notmatch $campaign.Files) { continue }
			if ($relative -eq "cfg\skill_episodic.cfg") { $relative = "cfg\$($campaign.Skill)" }
			$target = Join-Path $Destination $relative
			New-Item -ItemType Directory (Split-Path $target) -Force | Out-Null
			$source = Join-Path $sdk ($file -replace '/', '\')
			if ($relative -like "cfg\*.cfg") {
				# the engine drops the line before a block of '#' comments (skill_episodic.cfg's
				# sk_dmg_take_scale3): the same comments as '//' cost nothing
				[IO.File]::WriteAllLines($target, [string[]]([IO.File]::ReadAllLines($source) -replace '^#', '//'))
			} else {
				Copy-Item $source $target -Force
			}
		}
	}
	Write-Campaigns $sdk $Destination $Folder

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
