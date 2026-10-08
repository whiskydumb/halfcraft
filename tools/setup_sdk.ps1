# checks out the source sdk 2013 trees halfcraft builds against and applies halfcraft's edits to valve's
# code (source\sdk\halfcraft-<engine>.patch, written by tools/update_patches.py).
#
#   hl2dm  source-sdk-2013      hl2dm-sp: valve's 2025 sdk with the singleplayer campaigns building and
#                               running on half-life 2: deathmatch's 64-bit engine
#   hl2    source-sdk-2013-sp   valve's singleplayer sdk as of 2015 (the last one it published), for
#                               half-life 2's own 32-bit engine; its patch also makes it build with
#                               today's visual studio
#
#   tools/setup_sdk.ps1                 both
#   tools/setup_sdk.ps1 -Engine hl2dm   source-sdk-2013 only. -Engine hl2 sets up both all the same:
#                                       the hl2 build takes the world shaders and the campaign files
#                                       (chapters, strings) from source-sdk-2013 too
#
# afterwards: tools/build_hl2.ps1

param([ValidateSet("all", "hl2", "hl2dm")][string]$Engine = "all")

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$trees = @{
	hl2dm = @{ Dir = "source-sdk-2013"; Url = "https://github.com/hardlightbridge/hl2dm-sp.git"; Commit = "67f81f0f5a64a7f2bb3c0de02409ed63a0570ed5" }  # 2026-10-02
	# valve's repository dropped the singleplayer tree in 2025: its last commit with sp\ only
	hl2   = @{ Dir = "source-sdk-2013-sp"; Url = "https://github.com/ValveSoftware/source-sdk-2013.git"; Commit = "0d8dceea4310fde5706b3ce1c70609d72a38efdf"; Sparse = "sp" }  # 2015-09-09
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
	throw "git not found"
}

# whether the patch would apply (or, -Reverse, is applied already). a failed check prints to stderr,
# which windows powershell turns into a terminating error under "Stop": checked here with "Continue"
function Test-Patch([string]$patch, [switch]$Reverse) {
	$ErrorActionPreference = "Continue"
	if ($Reverse) { git apply --reverse --check $patch 2>$null } else { git apply --check $patch 2>$null }
	$LASTEXITCODE -eq 0
}

foreach ($e in $(if ($Engine -eq "hl2dm") { @("hl2dm") } else { @("hl2", "hl2dm") })) {
	$tree = $trees[$e]
	$sdk = Join-Path $repo $tree.Dir
	$patch = Join-Path $repo "source\sdk\halfcraft-$e.patch"

	# just the pinned commit, without history (a tree from before keeps its full clone)
	if (-not (Test-Path (Join-Path $sdk ".git"))) {
		git init --quiet $sdk
		if ($LASTEXITCODE -ne 0) { throw "git init in $sdk failed" }
		git -C $sdk config core.longpaths true
		git -C $sdk remote add origin $tree.Url
		if ($tree.Sparse) {
			git -C $sdk sparse-checkout set $tree.Sparse
			if ($LASTEXITCODE -ne 0) { throw "sparse checkout in $sdk failed" }
		}
		git -C $sdk fetch --quiet --depth 1 origin $tree.Commit
		if ($LASTEXITCODE -ne 0) { throw "fetching $($tree.Commit) from $($tree.Url) failed" }
	}

	Push-Location $sdk
	try {
		git checkout --quiet --detach $tree.Commit
		if ($LASTEXITCODE -ne 0) { throw "checkout of $($tree.Commit) failed (local changes? see git status in $sdk)" }

		if (Test-Patch $patch) {
			git apply --whitespace=nowarn $patch
			if ($LASTEXITCODE -ne 0) { throw "applying $patch failed" }
			Write-Host "$e sdk ready at $sdk"
		} elseif (Test-Patch $patch -Reverse) {
			Write-Host "$e sdk already patched at $sdk"
		} else {
			throw "$patch neither applies nor is already applied; see git status in $sdk"
		}
	} finally {
		Pop-Location
	}
}
