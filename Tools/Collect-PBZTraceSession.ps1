[CmdletBinding()]
param(
    [string]$SteamExecutable,

    # Keep large session artifacts off D: (it was 100% full during the last attempt).
    [string]$OutputDirectory = 'E:\PBZTraceWork',

    [ValidateRange(10, 180)]
    [int]$StartupTimeoutSeconds = 45,

    # Requested UE trace channels. The shipping build may refuse to write a trace;
    # the log below is the primary evidence source.
    [string]$TraceChannels = 'Object,Animation,Frame,Bookmark',

    # Category verbosity overrides applied at boot. Custom PBZ categories default to
    # Log; this raises them explicitly and keeps the engine animation categories on.
    [string]$LogCategories = @(
        'LogPBZ Log',
        'LogPBZMontageNotify Log',
        'LogPBZFallRise Log',
        'LogPBZFallSlide Log',
        'LogPBZSwitchMotion Log',
        'LogPBZFloorAdjust Log',
        'LogPBZSoftCollision Log',
        'LogPBZTeleportCover Log',
        'LogPBZAnimationLibrary Log',
        'LogPBZRevealPose Log',
        'LogPBZNBInput Log',
        'LogAnimation Log',
        'LogAnimMontage Log',
        'LogAnimNotify Log',
        'LogRootMotion Log',
        'LogMotionWarping Log',
        'LogCharacterMovement Log',
        'LogMontage Log'
    ) -join ',',

    # Optional boot-time console commands (retail allowlist may drop these).
    [string]$ExecCmds = 'PBZ.NBInput.Debug 1,PBZ.NBInput.Debug.LogInterval 1',

    [switch]$Trace
)

$ErrorActionPreference = 'Stop'

if (Get-Process -Name 'PBZ' -ErrorAction SilentlyContinue) {
    throw 'PBZ is running. Close it through its normal Exit menu first, then re-run this script.'
}

if (-not $SteamExecutable) {
    $SteamExecutable = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamExe).SteamExe
}
if (-not (Test-Path -LiteralPath $SteamExecutable -PathType Leaf)) {
    throw 'Steam was not found. Specify the path to steam.exe with -SteamExecutable.'
}

$output = New-Item -ItemType Directory -Path $OutputDirectory -Force
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$sessionDir = New-Item -ItemType Directory -Path (Join-Path $output.FullName "Session-$stamp") -Force
$tracePath = Join-Path $sessionDir.FullName 'PBZ-animation.utrace'
$logPath = Join-Path $sessionDir.FullName 'PBZ.log'
$sessionPath = Join-Path $sessionDir.FullName 'session.json'

$argumentList = @('-applaunch', '4115450', '-log', "-LogCmds=`"$LogCategories`"", "-ABSLOG=`"$($logPath.Replace('\', '/'))`"")
if ($ExecCmds) {
    $argumentList += "-ExecCmds=`"$ExecCmds`""
}
if ($Trace) {
    $argumentList += "-trace=$TraceChannels"
    $argumentList += "-tracefile=`"$($tracePath.Replace('\', '/'))`""
}

$candidateLogRoots = @(
    $sessionDir.FullName
    'E:\SteamLibrary\steamapps\common\PBZ\ProjectPBZ\Saved\Logs'
    'E:\SteamLibrary\steamapps\common\PBZ\platform\steam\Saved\Logs'
    'E:\SteamLibrary\steamapps\common\PBZ\Engine\Saved\Logs'
    (Join-Path $env:LOCALAPPDATA 'PBZ\Saved\Logs')
)
$before = @{}
foreach ($root in $candidateLogRoots) {
    if (Test-Path -LiteralPath $root) {
        Get-ChildItem -LiteralPath $root -Filter '*.log' -ErrorAction SilentlyContinue | ForEach-Object {
            $before[$_.FullName] = $_.LastWriteTimeUtc
        }
    }
}

Start-Process -FilePath $SteamExecutable -ArgumentList ($argumentList -join ' ') -WindowStyle Hidden
Write-Host "Launched Phantom Blade Zero with -log. Session directory: $($sessionDir.FullName)"

$timer = [Diagnostics.Stopwatch]::StartNew()
$sawGame = $false
$logFile = $null
do {
    $sawGame = $sawGame -or [bool](Get-Process -Name 'PBZ' -ErrorAction SilentlyContinue)
    foreach ($root in $candidateLogRoots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $candidate = Get-ChildItem -LiteralPath $root -Filter '*.log' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if ($candidate -and (-not $before.ContainsKey($candidate.FullName) -or $candidate.LastWriteTimeUtc -gt $before[$candidate.FullName])) {
            $logFile = $candidate
            break
        }
    }
    if ($logFile -and (Get-Item -LiteralPath $logFile.FullName).Length -gt 0) { break }
    Start-Sleep -Milliseconds 500
} while ($timer.Elapsed.TotalSeconds -lt $StartupTimeoutSeconds)

$traceCreated = Test-Path -LiteralPath $tracePath -PathType Leaf
$report = [ordered]@{
    app_id                       = 4115450
    engine_build                 = 'CL-302256_JKS-17280_Shipping'
    launched_at                  = (Get-Date).ToString('o')
    requested_channels           = $TraceChannels.Split(',')
    log_categories               = $LogCategories
    exec_cmds                    = $ExecCmds
    trace_requested              = [bool]$Trace
    game_started                 = $sawGame
    trace_created                = $traceCreated
    trace_path                   = $tracePath
    log_path                     = if ($logFile) { $logFile.FullName } else { $null }
    animation_events_verified    = $null
    observed_seconds             = [Math]::Round($timer.Elapsed.TotalSeconds, 2)
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $sessionPath -Encoding UTF8

if ($logFile) {
    Write-Host "Log file: $($logFile.FullName)"
} else {
    Write-Host 'No log file appeared. Shipping logging may be compiled out; see session.json.'
}

Write-Host ''
Write-Host 'Play the pole route now, then exit through the normal menu so the log flushes.'
Write-Host 'After that, run: python Tools\parse_pbz_log.py "<log file>" -OutCsv "<session dir>\events.csv" -OutJsonl "<session dir>\events.jsonl"'
Get-Item -LiteralPath $sessionPath
