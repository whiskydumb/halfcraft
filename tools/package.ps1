# builds both halves of halfcraft and packs a release into dist\:
#   HalfCraft-<version>.zip       unpack anywhere, start HalfCraft.exe (package\README.txt says the rest)
#   HalfCraft-<version>-pdb.zip   client.dll's and server.dll's debug symbols, for crash dumps
#
#   tools/package.ps1 [-NoBuild]
#
# the zip's HalfCraft folder: HalfCraft.exe, game\ (the mod folder: hl2dm-sp's files, source\mod,
# the dlls), minecraft\ (portable Prism Launcher with the HalfCraft instance and its mods, from
# package\minecraft), README.txt, LICENSE.txt, THIRD-PARTY-NOTICES.md.
#
# building needs what tools/build_hl2.ps1 needs plus JDK 25 (JAVA_HOME, or one installed in program
# files). Prism Launcher and Fabric API are downloaded once into .cache\package and checked against
# the hashes pinned below.

param([switch]$NoBuild)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repo = Split-Path -Parent $PSScriptRoot
$properties = Get-Content (Join-Path $repo "minecraft\gradle.properties") -Raw
function Get-Property([string]$name) {
	$match = [regex]::Match($properties, "(?m)^$([regex]::Escape($name))=(.+)$")
	if (-not $match.Success) { throw "no $name in minecraft\gradle.properties" }
	$match.Groups[1].Value.Trim()
}
$version = Get-Property "version"

# pinned downloads
$prismVersion = "11.1.1"
$prismZip = "PrismLauncher-Windows-MSVC-Portable-$prismVersion.zip"
$prismUrl = "https://github.com/PrismLauncher/PrismLauncher/releases/download/$prismVersion/$prismZip"
$prismSha256 = "ab35a770fb06d89d2ccc098079db5db329fb4e68f42b72babd8b095efde3d2d7"
$prismLicenseUrl = "https://raw.githubusercontent.com/PrismLauncher/PrismLauncher/$prismVersion/LICENSE"
$prismLicenseSha256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"
$fabricApiVersion = "0.161.0+26.3"
$fabricApiUrl = "https://cdn.modrinth.com/data/P7dR8mSH/versions/bNnaTiuM/fabric-api-0.161.0%2B26.3.jar"
$fabricApiSha512 = "ed6b2586d6fde11fde8472f5a527c51e99b67026e46f94d4bfd85e7e28ce5ee299173ee16ad576ceb51f39f98d30a811086a6deb1a86a524859cc16e12da109d"
if ((Get-Property "fabric_api_version") -ne $fabricApiVersion) {
	throw "the mod builds against Fabric API $(Get-Property 'fabric_api_version') but $fabricApiVersion is pinned here: pin the new one's download and hash"
}

function Get-Pinned([string]$url, [string]$path, [string]$algorithm, [string]$hash) {
	if (-not (Test-Path $path)) {
		New-Item -ItemType Directory (Split-Path $path) -Force | Out-Null
		Invoke-WebRequest -Uri $url -OutFile $path -UseBasicParsing
	}
	$actual = (Get-FileHash $path -Algorithm $algorithm).Hash
	if ($actual -ne $hash.ToUpper()) {
		Remove-Item $path
		throw "$url doesn't match its pinned $algorithm hash (got $actual)"
	}
}

function Find-Jdk25 {
	$candidates = @()
	if ($env:JAVA_HOME) { $candidates += $env:JAVA_HOME }
	foreach ($vendor in @("Eclipse Adoptium", "Java", "Microsoft", "Zulu", "Amazon Corretto")) {
		$root = Join-Path $env:ProgramFiles $vendor
		if (Test-Path $root) { $candidates += Get-ChildItem $root -Directory | Sort-Object Name -Descending | ForEach-Object FullName }
	}
	foreach ($jdk in $candidates) {
		$release = Join-Path $jdk "release"
		if ((Test-Path (Join-Path $jdk "bin\javac.exe")) -and (Test-Path $release) -and
			((Get-Content $release -Raw) -match 'JAVA_VERSION="(\d+)') -and [int]$Matches[1] -ge 25) {
			return $jdk
		}
	}
	throw "no JDK 25 or newer found: set JAVA_HOME to one"
}

# zip entries named with forward slashes, as the zip format expects (windows powershell's own
# zipping writes backslashes)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
function New-Zip([string]$path, [System.Collections.IDictionary]$entries) {
	$zip = [System.IO.Compression.ZipFile]::Open($path, [System.IO.Compression.ZipArchiveMode]::Create)
	try {
		foreach ($name in $entries.Keys) {
			[System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $entries[$name], $name, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
		}
	} finally { $zip.Dispose() }
}
function New-ZipFromFolder([string]$path, [string]$folder, [string]$prefix) {
	$entries = [ordered]@{}
	$base = (Resolve-Path $folder).Path.TrimEnd('\') + '\'
	Get-ChildItem $folder -Recurse -File | Sort-Object FullName | ForEach-Object {
		$entries[$prefix + $_.FullName.Substring($base.Length).Replace('\', '/')] = $_.FullName
	}
	New-Zip $path $entries
}

# text the player opens in notepad: crlf, placeholders filled in
function Copy-Text([string]$from, [string]$to, [hashtable]$values = @{}) {
	$text = [IO.File]::ReadAllText($from) -replace "`r`n", "`n"
	foreach ($key in $values.Keys) { $text = $text.Replace("{$key}", $values[$key]) }
	[IO.File]::WriteAllText($to, ($text -replace "`n", "`r`n"))
}

if (-not $NoBuild) {
	$javaHome = $env:JAVA_HOME
	try {
		$env:JAVA_HOME = Find-Jdk25
		Push-Location (Join-Path $repo "minecraft")
		try {
			.\gradlew.bat build --no-configuration-cache
			if ($LASTEXITCODE) { throw "the minecraft mod didn't build" }
		} finally { Pop-Location }
	} finally { $env:JAVA_HOME = $javaHome }
	& (Join-Path $PSScriptRoot "build_hl2.ps1")
}

$sdk = Join-Path $repo "source-sdk-2013"
$modDir = Join-Path $sdk "game\mod_hl2"
$jar = Join-Path $repo "minecraft\build\libs\halfcraft-$version.jar"
$launcher = Join-Path $repo "build\launcher\HalfCraft.exe"
$dlls = @("client", "server") | ForEach-Object { Join-Path $modDir "bin\x64\$_.dll" }
foreach ($file in @($jar, $launcher) + $dlls) {
	if (-not (Test-Path $file)) { throw "missing $file (build first, or drop -NoBuild)" }
}

$cache = Join-Path $repo ".cache\package"
Get-Pinned $prismUrl "$cache\$prismZip" SHA256 $prismSha256
Get-Pinned $prismLicenseUrl "$cache\PrismLauncher-$prismVersion-LICENSE.txt" SHA256 $prismLicenseSha256
$fabricApiJar = "fabric-api-$fabricApiVersion.jar"
Get-Pinned $fabricApiUrl "$cache\$fabricApiJar" SHA512 $fabricApiSha512

$dist = Join-Path $repo "dist"
$stage = Join-Path $dist "HalfCraft"
New-Item -ItemType Directory $dist -Force | Out-Null
Get-ChildItem $dist -Filter "HalfCraft-*.zip" | Remove-Item -Force
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null

# game\: the mod folder. hl2dm-sp's own files (what its git tracks, not what running it left behind),
# halfcraft's over them, the dlls
$game = Join-Path $stage "game"
foreach ($file in (git -C $sdk ls-files "game/mod_hl2")) {
	$target = Join-Path $game ($file.Substring("game/mod_hl2/".Length) -replace '/', '\')
	New-Item -ItemType Directory (Split-Path $target) -Force | Out-Null
	Copy-Item (Join-Path $sdk ($file -replace '/', '\')) $target
}
Copy-Item -Recurse -Force (Join-Path $repo "source\mod\*") $game
# the engine reads the mod's strings (chapter titles) from resource\<mod folder>_<language>.txt
Move-Item (Join-Path $game "resource\mod_hl2_english.txt") (Join-Path $game "resource\game_english.txt")
New-Item -ItemType Directory (Join-Path $game "bin\x64") -Force | Out-Null
Copy-Item $dlls (Join-Path $game "bin\x64")

# minecraft\: portable prism with the halfcraft instance and its mods (names without versions, so
# unpacking a new release over an old one replaces them)
$minecraft = Join-Path $stage "minecraft"
Copy-Item -Recurse (Join-Path $repo "package\minecraft") $minecraft
Expand-Archive "$cache\$prismZip" (Join-Path $minecraft "Prism") -Force
if (-not (Test-Path (Join-Path $minecraft "Prism\prismlauncher.exe"))) { throw "$prismZip isn't laid out the way it used to be: no prismlauncher.exe at its top" }
Copy-Item "$cache\PrismLauncher-$prismVersion-LICENSE.txt" (Join-Path $minecraft "Prism\LICENSE-PrismLauncher.txt")
Copy-Text (Join-Path $repo "package\minecraft\Prism\THIRD-PARTY.txt") (Join-Path $minecraft "Prism\THIRD-PARTY.txt") @{ PRISM_VERSION = $prismVersion; FABRIC_API_VERSION = $fabricApiVersion }
$mods = Join-Path $minecraft "Prism\instances\HalfCraft\.minecraft\mods"
New-Item -ItemType Directory $mods -Force | Out-Null
Copy-Item $jar (Join-Path $mods "halfcraft.jar")
Copy-Item "$cache\$fabricApiJar" (Join-Path $mods "fabric-api.jar")

Copy-Item $launcher $stage
Copy-Text (Join-Path $repo "package\README.txt") (Join-Path $stage "README.txt") @{ VERSION = $version }
Copy-Text (Join-Path $repo "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Text (Join-Path $repo "THIRD-PARTY-NOTICES.md") (Join-Path $stage "THIRD-PARTY-NOTICES.md")

New-ZipFromFolder (Join-Path $dist "HalfCraft-$version.zip") $stage "HalfCraft/"
New-Zip (Join-Path $dist "HalfCraft-$version-pdb.zip") ([ordered]@{
	"client.pdb" = Join-Path $modDir "bin\x64\client.pdb"
	"server.pdb" = Join-Path $modDir "bin\x64\server.pdb"
})
Remove-Item -Recurse -Force $stage

Get-ChildItem $dist -Filter "HalfCraft-*.zip" | ForEach-Object { "{0,-32} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB) }
