# checks out the source sdk 2013 halfcraft is built against and applies halfcraft's hooks into
# valve's game code (source/sdk/halfcraft-sdk.patch).
#
# the sdk is hl2dm-sp (valve's sdk with the singleplayer campaigns building and running on
# half-life 2: deathmatch's 64-bit engine), pinned to a known commit.
#
#   tools/setup_sdk.ps1
#
# afterwards: tools/build_hl2.ps1

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $repo "source-sdk-2013"
$patch = Join-Path $repo "source\sdk\halfcraft-sdk.patch"
$url = "https://github.com/hardlightbridge/hl2dm-sp.git"
$commit = "67f81f0f"  # 2026-10-02

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
	throw "git not found"
}

if (-not (Test-Path (Join-Path $sdk ".git"))) {
	git clone $url $sdk
	if ($LASTEXITCODE -ne 0) { throw "clone failed" }
}

Push-Location $sdk
try {
	git checkout --detach $commit
	if ($LASTEXITCODE -ne 0) { throw "checkout of $commit failed (local changes? see git status in $sdk)" }

	git apply --check $patch 2>$null
	if ($LASTEXITCODE -eq 0) {
		git apply $patch
		if ($LASTEXITCODE -ne 0) { throw "applying the patch failed" }
		Write-Host "sdk ready at $sdk"
	} else {
		git apply --reverse --check $patch 2>$null
		if ($LASTEXITCODE -ne 0) { throw "the patch neither applies nor is already applied; see git status in $sdk" }
		Write-Host "sdk already patched at $sdk"
	}
} finally {
	Pop-Location
}
