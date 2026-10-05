# waits for a line in a game's log, for test scripts: half-life's console.log (run with -condebug, as
# tools/run_hl2.ps1 does) or minecraft's latest.log.
#
#   $mark = tools/wait_log.ps1 -Log mc-test -Mark            where the log ends now (a byte offset)
#   tools/hl2_command.ps1 "hc_mc kill @s"
#   tools/wait_log.ps1 -Log hl2dm -After $mark -Pattern "so does gordon" -Timeout 20
#   tools/wait_log.ps1 -Log mc-test -After $mark -Pattern "teleported" -Absent -Timeout 5
#
# -Log: hl2 or hl2dm (build\game-<engine>\console.log), mc (minecraft\run, the player's dev client),
# mc-test (minecraft\run\test, make mc-test's) or a file's path. -Pattern is a regex. Lines before
# -After don't count; without it only lines written after this starts do, which can miss a line the
# command before already caused: take a -Mark first. Prints the line it found and exits 0; exits 1
# when the timeout passes without one. -Absent turns it round: 0 when no line matched in time.

param(
	[Parameter(Mandatory = $true)][string]$Log,
	[string]$Pattern = "",
	[double]$Timeout = 30,
	[long]$After = -1,
	[switch]$Mark,
	[switch]$Absent
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$path = switch ($Log) {
	"hl2" { Join-Path $repo "build\game-hl2\console.log" }
	"hl2dm" { Join-Path $repo "build\game-hl2dm\console.log" }
	"mc" { Join-Path $repo "minecraft\run\logs\latest.log" }
	"mc-test" { Join-Path $repo "minecraft\run\test\logs\latest.log" }
	default { $Log }
}

function Get-LogLength {
	if (Test-Path $path) { (Get-Item $path).Length } else { 0 }
}

if ($Mark) {
	Get-LogLength
	exit 0
}
if (-not $Pattern) { throw "-Pattern is needed (or -Mark)" }

$regex = [regex]::new($Pattern)
$offset = if ($After -ge 0) { $After } else { Get-LogLength }
$partial = ""
$deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
while ($true) {
	if (Test-Path $path) {
		# the game writes on: share everything so neither side gets in the other's way
		$stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
		try {
			# a shorter file is a new one (a restarted minecraft): read it from the start
			if ($stream.Length -lt $offset) { $offset = 0; $partial = "" }
			if ($stream.Length -gt $offset) {
				$stream.Position = $offset
				$bytes = New-Object byte[] ($stream.Length - $offset)
				$read = $stream.Read($bytes, 0, $bytes.Length)
				$offset += $read
				$lines = ($partial + [Text.Encoding]::UTF8.GetString($bytes, 0, $read)) -split "`r?`n"
				$partial = $lines[-1]  # not finished yet
				$finished = if ($lines.Length -gt 1) { $lines[0..($lines.Length - 2)] } else { @() }
				foreach ($line in $finished) {
					if ($regex.IsMatch($line)) {
						if ($Absent) {
							Write-Host "unexpected in ${Log}: $line"
							exit 1
						}
						Write-Host "found in ${Log}: $line"
						exit 0
					}
				}
			}
		} finally {
			$stream.Close()
		}
	}
	if ([DateTime]::UtcNow -ge $deadline) { break }
	Start-Sleep -Milliseconds 250
}
if ($Absent) {
	Write-Host "not in ${Log} within $Timeout s, as expected: $Pattern"
	exit 0
}
Write-Host "not in ${Log} within $Timeout s: $Pattern"
exit 1
