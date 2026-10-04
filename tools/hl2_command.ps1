# sends console commands to the running game (tools/run_hl2.ps1), one after another:
#
#   tools/hl2_command.ps1 "save test" "hc_look 75 0" "hc_click 3" "load test"
#
# each command goes through a cfg file and +exec over -hijack (-hijack alone drops some
# arguments). output lands in the game's console.log. useful where typing into the console
# isn't (synthetic keys, keyboard layouts).

param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Commands)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$mod = Join-Path $repo "source-sdk-2013\game\mod_hl2"
$game = Get-Process hl2mp_win64 -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1
if (-not $game) { throw "the game isn't running (tools/run_hl2.ps1)" }
$exe = $game.Path

foreach ($command in $Commands) {
    Set-Content -Path (Join-Path $mod "cfg\hc_command.cfg") -Value $command -Encoding ascii
    $p = Start-Process -FilePath $exe -ArgumentList @("-game", "`"$mod`"", "-hijack", "+exec", "hc_command") -WorkingDirectory (Split-Path -Parent $exe) -PassThru
    if (-not $p.WaitForExit(15000)) {
        Stop-Process -Id $p.Id -Force
        throw "the command '$command' didn't reach the game"
    }
    Start-Sleep -Milliseconds 1500
    # a hand-over sometimes leaves a second instance behind with "only one instance" on screen
    Get-Process hl2mp_win64 -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $game.Id } | Stop-Process -Force
    Write-Host "sent: $command"
}
