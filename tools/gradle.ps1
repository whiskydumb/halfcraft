# runs the minecraft mod's gradle on a JDK 25, which the mod needs, whatever JAVA_HOME points at
# (JAVA_HOME itself if it's 25+, else the newest one installed in program files).
#
#   tools/gradle.ps1 build        the mod's jar (minecraft\build\libs)
#   tools/gradle.ps1 runClient    the dev client (tools/launch_minecraft.bat)

param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Tasks = @("build"))

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

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

$javaHome = $env:JAVA_HOME
try {
	$env:JAVA_HOME = Find-Jdk25
	Push-Location (Join-Path $repo "minecraft")
	try {
		.\gradlew.bat @Tasks --no-configuration-cache
		if ($LASTEXITCODE) { throw "gradle $($Tasks -join ' ') failed ($LASTEXITCODE)" }
	} finally { Pop-Location }
} finally { $env:JAVA_HOME = $javaHome }
