# formats halfcraft's own c++ with clang-format (.clang-format says how), or checks that it is: make format,
# make lint. the files: source\src, source\launcher, protocol and tools\*.cpp, tracked or new; never the sdk
# trees, nor the java (clang-format breaks mixin annotations).
#
#   tools/format.ps1          rewrites the files that aren't formatted
#   tools/format.ps1 -Check   lists them and fails; writes nothing
#
# clang-format 22 (the style's options are its): $env:CLANG_FORMAT if set, else visual studio's (its c++
# clang tools), else the one on the path. ci gets it with pip install clang-format==22.1.3.

param([switch]$Check)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$major = 22

function Find-ClangFormat {
	$candidates = @()
	if ($env:CLANG_FORMAT) { $candidates += $env:CLANG_FORMAT }
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path $vswhere) {
		$vs = & $vswhere -latest -prerelease -products * -property installationPath
		if ($vs) { $candidates += Join-Path $vs "VC\Tools\Llvm\x64\bin\clang-format.exe" }
	}
	$onPath = Get-Command clang-format -ErrorAction SilentlyContinue
	if ($onPath) { $candidates += $onPath.Source }
	foreach ($exe in $candidates) {
		if (-not (Test-Path $exe)) { continue }
		$version = & $exe --version
		if ($version -match "clang-format version $major\.") { return $exe }
		Write-Warning "$exe is $version; the style is clang-format $major's"
	}
	throw "no clang-format $major found: set CLANG_FORMAT, add visual studio's c++ clang tools, or pip install clang-format==$major.*"
}

$clangFormat = Find-ClangFormat
$files = git -C $repo ls-files --cached --others --exclude-standard -- "source/src/*.cpp" "source/src/*.h" "source/launcher/*.cpp" "source/launcher/*.h" "protocol/*.h" "tools/*.cpp" |
	ForEach-Object { Join-Path $repo $_ } | Where-Object { Test-Path $_ }
if (-not $files) { throw "no c++ files found under $repo" }

if ($Check) {
	# --dry-run prints a warning on stderr for every line that would change: name each file once. windows
	# powershell turns a native program's stderr into errors, which "Stop" would throw on
	$ErrorActionPreference = "Continue"
	$report = & $clangFormat --style=file --dry-run $files 2>&1
	$ErrorActionPreference = "Stop"
	$unformatted = $report | ForEach-Object { "$_" } | Where-Object { $_ -match "^(.+?):\d+:\d+: (warning|error):" } |
		ForEach-Object { $Matches[1] } | Sort-Object -Unique
	if ($unformatted) {
		$unformatted | ForEach-Object { Write-Host "not formatted: $($_.Substring($repo.Length + 1))" }
		throw "$(@($unformatted).Count) of $(@($files).Count) files aren't formatted: run make format"
	}
	Write-Host "formatted: $(@($files).Count) files"
} else {
	& $clangFormat --style=file -i $files
	if ($LASTEXITCODE -ne 0) { throw "clang-format failed ($LASTEXITCODE)" }
	Write-Host "formatted $(@($files).Count) files with $clangFormat"
}
