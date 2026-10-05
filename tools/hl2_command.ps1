# sends console commands to the running game (tools/run_hl2.ps1, either engine), one after another:
#
#   tools/hl2_command.ps1 "save test" "hc_look 75 0" "hc_click 3" "load test"
#
# each command goes through a cfg file and +exec over -hijack (-hijack alone drops some
# arguments). output lands in the game's console.log. useful where typing into the console
# isn't (synthetic keys, keyboard layouts).

param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Commands)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "engines.ps1")

$game = Get-RunningEngine
if (-not $game) { throw "the game isn't running (tools/run_hl2.ps1)" }
$engine = if ($game.ProcessName -eq "hl2") { "hl2" } else { "hl2dm" }
$exe = Find-EngineExe $engine  # not $game.Path: a 32-bit powershell can't read a 64-bit process's
$mod = Join-Path $repo "build\game-$engine"

foreach ($command in $Commands) {
	Set-Content -Path (Join-Path $mod "cfg\hc_command.cfg") -Value $command -Encoding ascii
	$p = Start-Process -FilePath $exe -ArgumentList @("-game", "`"$mod`"", "-hijack", "+exec", "hc_command") -WorkingDirectory (Split-Path -Parent $exe) -PassThru
	if (-not $p.WaitForExit(15000)) {
		Stop-Process -Id $p.Id -Force
		throw "the command '$command' didn't reach the game"
	}
	Start-Sleep -Milliseconds 1500
	# a hand-over sometimes leaves a second instance behind with "only one instance" on screen
	Get-Process $game.ProcessName -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $game.Id } | Stop-Process -Force
	Write-Host "sent: $command"
}
