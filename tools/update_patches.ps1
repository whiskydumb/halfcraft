# writes the sdk trees' edits to valve's code back into source\sdk\halfcraft-<engine>.patch. run it after
# changing anything inside source-sdk-2013\src or source-sdk-2013-sp\sp\src; tools/setup_sdk.ps1 applies them.
#
#   tools/update_patches.ps1                 both
#   tools/update_patches.ps1 -Engine hl2     source-sdk-2013-sp only

param([ValidateSet("all", "hl2", "hl2dm")][string]$Engine = "all")

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

# the libraries the 2015 tree's build writes over its prebuilt vs2013 copies: build output, not edits
$rebuiltLibs = @("tier1", "mathlib", "raytrace", "vgui_controls") | ForEach-Object { ":(exclude)sp/src/lib/public/$_.lib" }
$patches = @{
	hl2dm = @{ Tree = "source-sdk-2013"; Paths = @("src") }
	hl2   = @{ Tree = "source-sdk-2013-sp"; Paths = @("sp/src") + $rebuiltLibs }
}

# git diff into a file. --output keeps the bytes as git writes them (a powershell redirect would re-encode
# them); git's line-ending notes on stderr would stop this script under "Stop" (windows powershell turns
# them into errors), so they're switched off and the call runs with "Continue"
function Write-TreeDiff([string]$tree, [string]$patch, [string[]]$paths) {
	$ErrorActionPreference = "Continue"
	git -C $tree -c core.safecrlf=false diff --binary "--output=$patch" -- @($paths)
	$LASTEXITCODE -eq 0
}

foreach ($e in $(if ($Engine -eq "all") { @("hl2", "hl2dm") } else { @($Engine) })) {
	$tree = Join-Path $repo $patches[$e].Tree
	$patch = Join-Path $repo "source\sdk\halfcraft-$e.patch"
	if (-not (Write-TreeDiff $tree $patch $patches[$e].Paths)) { throw "git diff in $tree failed" }
	$files = (Select-String -Path $patch -Pattern '^diff --git' | Measure-Object).Count
	Write-Host "$patch ($files files)"
}
