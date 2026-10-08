# halfcraft's version, from git's tags. a commit tagged vX.Y.Z is release X.Y.Z; N commits past the newest
# such tag are X.(Y+1).0-dev.N+<commit>: the release they lead to, and how far along. before the first tag
# they lead to 0.1.0. $env:HALFCRAFT_VERSION wins when set (ci's jobs share one; a source tree without git).
# the minecraft mod (tools/gradle.ps1), HalfCraft.exe (tools/build_hl2.ps1) and the release zip
# (tools/package.ps1) all take it from here.
#
#   tools/version.ps1            prints it: 0.2.0-dev.12+1a2b3c4d5
#   tools/version.ps1 -Numbers   prints the four numbers a windows version resource takes: 0,2,0,12

param([switch]$Numbers)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

function Get-HalfCraftVersion {
	if ($env:HALFCRAFT_VERSION) { return $env:HALFCRAFT_VERSION }
	# git describe fails without a tag (and says so on stderr): "Continue" keeps windows powershell from throwing
	$ErrorActionPreference = "Continue"
	$described = git -C $repo describe --tags --long --abbrev=9 --match "v[0-9]*.[0-9]*.[0-9]*" 2>$null
	$ErrorActionPreference = "Stop"
	if ($LASTEXITCODE -eq 0 -and "$described" -match '^v(\d+)\.(\d+)\.(\d+)-(\d+)-g([0-9a-f]+)$') {
		if ($Matches[4] -eq "0") { return "$($Matches[1]).$($Matches[2]).$($Matches[3])" }
		return "$($Matches[1]).$([int]$Matches[2] + 1).0-dev.$($Matches[4])+$($Matches[5])"
	}
	$count = git -C $repo rev-list --count HEAD
	$commit = git -C $repo rev-parse --short=9 HEAD
	if ($LASTEXITCODE -ne 0 -or -not $count) { throw "no version: not a git checkout, and HALFCRAFT_VERSION isn't set" }
	"0.1.0-dev.$count+$commit"
}

$version = Get-HalfCraftVersion
if ($Numbers) {
	if ($version -notmatch '^(\d+)\.(\d+)\.(\d+)(?:-dev\.(\d+))?') { throw "$version isn't a halfcraft version" }
	$build = if ($Matches[4]) { $Matches[4] } else { "0" }
	"$($Matches[1]),$($Matches[2]),$($Matches[3]),$build"
} else {
	$version
}
