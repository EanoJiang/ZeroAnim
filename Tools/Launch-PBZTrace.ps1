<#
.SYNOPSIS
  Launch the Phantom Blade Zero engine process directly (not via Steam -applaunch)
  with Unreal Trace flags, then report whether a .utrace was actually written.

.DESCRIPTION
  The previous attempts went through `steam.exe -applaunch`, which re-quotes the
  argument list and drops the -trace* switches. This script calls PBZ.exe itself
  so the trace switches reach FTraceAuxiliary::Start unmodified.

  Nothing is written outside -OutputDirectory. Read-only with respect to the game.
#>
[CmdletBinding()]
param(
    [string]$EngineExe = 'E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe',
    [string]$OutputDirectory = 'E:\PBZTraceWork',
    [string]$Channels = 'cpu,gpu,frame,bookmark,log,object,animation,counters,loadtime,file,memory',
    [int]$SmokeSeconds = 45,
    [switch]$KeepRunning,
    [switch]$NoTrace
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $EngineExe -PathType Leaf)) {
    throw "Engine exe not found: $EngineExe"
}
if (Get-Process -Name 'PBZ' -ErrorAction SilentlyContinue) {
    throw 'PBZ is already running. Close it through its normal Exit menu first.'
}

$sessionDir = Join-Path $OutputDirectory ("Session-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $sessionDir -Force | Out-Null
$tracePath = Join-Path $sessionDir 'PBZ-animation.utrace'
$logPath = Join-Path $sessionDir 'PBZ.log'

$launchArgs = @()
if (-not $NoTrace) {
    $launchArgs += "-trace=$Channels"
    $launchArgs += "-tracefile=$($tracePath.Replace('\','/'))"
}
$launchArgs += "-abslog=$($logPath.Replace('\','/'))"
$launchArgs += '-log'

$proc = Start-Process -FilePath $EngineExe -ArgumentList $launchArgs -PassThru
Write-Host "pid=$($proc.Id)"
Write-Host "args=$($launchArgs -join ' ')"

$timer = [Diagnostics.Stopwatch]::StartNew()
$deadline = $SmokeSeconds
while ($timer.Elapsed.TotalSeconds -lt $deadline) {
    Start-Sleep -Milliseconds 1000
    $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    $exists = Test-Path -LiteralPath $tracePath
    $len = if ($exists) { (Get-Item -LiteralPath $tracePath).Length } else { 0 }
    $logLen = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0 }
    Write-Host ("t={0,4:N0}s alive={1} utrace={2} bytes={3} log={4}" -f $timer.Elapsed.TotalSeconds, [bool]$p, $exists, $len, $logLen)
    if (-not $p) { break }
}

$report = [ordered]@{
    launched_at   = (Get-Date).ToString('o')
    engine_exe    = $EngineExe
    pid           = $proc.Id
    args          = $launchArgs
    channels      = $Channels
    trace_path    = $tracePath
    trace_created = (Test-Path -LiteralPath $tracePath)
    trace_bytes   = if (Test-Path -LiteralPath $tracePath) { (Get-Item -LiteralPath $tracePath).Length } else { 0 }
    log_path      = $logPath
    log_bytes     = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0 }
    observed_s    = [Math]::Round($timer.Elapsed.TotalSeconds, 2)
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $sessionDir 'session.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 4

if (-not $KeepRunning) {
    $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if ($p) { Write-Host 'Leaving game running; close it through the normal Exit menu so the trace flushes.' }
}
Write-Host "session=$sessionDir"
