[CmdletBinding()]
param(
    [string]$SteamExecutable,

    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\Saved\PBZAnimationTraces'),

    [ValidateRange(5, 180)]
    [int]$StartupTimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'

if (Get-Process -Name 'PBZ' -ErrorAction SilentlyContinue) {
    throw 'Close Phantom Blade Zero through its normal Exit menu before starting a traced session.'
}

if (-not $SteamExecutable) {
    $SteamExecutable = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamExe).SteamExe
}
if (-not (Test-Path -LiteralPath $SteamExecutable -PathType Leaf)) {
    throw 'Steam was not found. Specify the path to steam.exe with -SteamExecutable.'
}

$output = New-Item -ItemType Directory -Path $OutputDirectory -Force
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$tracePath = Join-Path $output.FullName "PBZ-animation-$stamp.utrace"
$reportPath = Join-Path $output.FullName "PBZ-animation-$stamp.json"
$traceArgument = $tracePath.Replace('\', '/')
$channels = 'Object,Animation,Frame,Bookmark'

# Use Epic's trace flags through the normal Steam launcher. Only the running game can emit events.
Start-Process -FilePath $SteamExecutable -ArgumentList "-applaunch 4115450 -trace=$channels -tracefile=`"$traceArgument`"" -WindowStyle Hidden
Write-Host "Waiting for Unreal Trace output at $tracePath"

$timer = [Diagnostics.Stopwatch]::StartNew()
$sawGame = $false
$trace = $null
do {
    $sawGame = $sawGame -or [bool](Get-Process -Name 'PBZ' -ErrorAction SilentlyContinue)
    $trace = Get-Item -LiteralPath $tracePath -ErrorAction SilentlyContinue
    if ($trace -and $trace.Length -gt 0) {
        break
    }
    Start-Sleep -Milliseconds 500
} while ($timer.Elapsed.TotalSeconds -lt $StartupTimeoutSeconds)

$traceCreated = $null -ne $trace -and $trace.Length -gt 0
$report = [ordered]@{
    app_id = 4115450
    requested_channels = $channels.Split(',')
    game_started = $sawGame
    trace_created = $traceCreated
    trace_path = $tracePath
    animation_events_verified = $null
    observed_seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 2)
}
$report | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding UTF8

if (-not $traceCreated) {
    if (-not $sawGame) {
        throw "Steam did not start PBZ during the check. See $reportPath"
    }
    throw "PBZ started but did not create a trace during the check. No animation samples were collected. See $reportPath"
}

Write-Host 'Play the route to record, then exit the game normally to flush the trace.'
Write-Host 'Open the trace in Unreal Insights and check the character animation tracks before using the data.'
Get-Item -LiteralPath $tracePath
