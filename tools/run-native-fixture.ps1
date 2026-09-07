param(
    [Parameter(Mandatory=$true)][string]$Directory,
    [int]$TimeoutSeconds = 150,
    [ValidateRange(0,20)][int]$DelayMilliseconds = 0,
    [switch]$ReplaceDevice,
    [switch]$ShutdownReplacedDevice,
    [switch]$RequireCleanExit
)
$ErrorActionPreference = 'Stop'
if ($ShutdownReplacedDevice -and -not $ReplaceDevice) { throw 'ShutdownReplacedDevice requires ReplaceDevice.' }
$nrWorkspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nrRun = [IO.Path]::GetFullPath((Join-Path $nrWorkspace $Directory))
$nrAllowed = Join-Path $nrWorkspace 'build\api-native-'
if (-not $nrRun.StartsWith($nrAllowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only workspace build/api-native-* test fixtures may be launched.'
}
$nrExe = Join-Path $nrRun 'ngxGym.exe'
$nrOut = Join-Path $nrRun 'host.out'
$nrErr = Join-Path $nrRun 'host.err'
if (-not (Test-Path -LiteralPath $nrExe)) { throw 'Missing fixture host.' }
if (Get-Process -Name ngxGym -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $nrExe }) {
    throw 'That fixture is already running; preserving its logs.'
}
$nrOldBackground = $env:NGXGYM_BACKGROUND
$nrOldDelay = $env:NGXGYM_FRAME_DELAY_MS
$nrOldReplacement = $env:NGXGYM_REPLACE_DEVICE
$nrOldShutdown = $env:NGXGYM_SHUTDOWN_REPLACED
$nrProcess = $null
try {
    $env:NGXGYM_BACKGROUND = '1'
    $env:NGXGYM_FRAME_DELAY_MS = "$DelayMilliseconds"
    $env:NGXGYM_REPLACE_DEVICE = if ($ReplaceDevice) { '1' } else { '0' }
    $env:NGXGYM_SHUTDOWN_REPLACED = if ($ShutdownReplacedDevice) { '1' } else { '0' }
    $nrProcess = Start-Process -FilePath $nrExe -ArgumentList 'scenario.txt' -WorkingDirectory $nrRun `
        -WindowStyle Hidden -RedirectStandardOutput $nrOut -RedirectStandardError $nrErr -PassThru
    Write-Output "Test-owned PID $($nrProcess.Id): $nrRun"
    $nrDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $nrCaptured = $false
    while (-not $nrProcess.HasExited -and [DateTime]::UtcNow -lt $nrDeadline) {
        $nrLines = Get-Content -LiteralPath $nrOut -ErrorAction SilentlyContinue
        if (-not $RequireCleanExit -and $nrLines -match '^cleanup: native NGX Shutdown1') {
            $nrCaptured = [bool]($nrLines -match '^captured frame 180 native output:')
            $nrProcess.WaitForExit(2000) | Out-Null
            break
        }
        Start-Sleep -Milliseconds 250
        $nrProcess.Refresh()
    }
    $nrForced = -not $nrProcess.HasExited
    if ($nrForced) {
        $nrOwned = Get-Process -Id $nrProcess.Id -ErrorAction SilentlyContinue
        if ($nrOwned -and $nrOwned.Path -eq $nrExe) { Stop-Process -Id $nrOwned.Id }
        Write-Output 'Test host terminated at timeout/shutdown; this is NOT a clean-exit PASS.'
    }
    $nrLines = Get-Content -LiteralPath $nrOut
    $nrCaptured = [bool]($nrLines -match '^captured frame 180 native output:') -and
        [bool]($nrLines -match '^cleanup: native NGX Shutdown1')
    $nrLines
    if ($RequireCleanExit) {
        if ($nrForced) { throw 'Required clean exit timed out; owned test process terminated.' }
        $nrProcess.WaitForExit()
        if ($nrProcess.ExitCode -ne 0 -or -not ($nrLines -match '^ok$')) { throw 'Required clean exit failed.' }
        Write-Output 'Host exited successfully without forced termination. Inspect per-session cleanup separately.'
    }
    if (-not $nrCaptured) { throw 'Fixture did not reach the capture and shutdown checkpoint.' }
    Write-Output 'Capture checkpoint reached. Compare GPU output separately; lifecycle remains unverified.'
} finally {
    if ($nrProcess -and -not $nrProcess.HasExited) {
        $nrOwned = Get-Process -Id $nrProcess.Id -ErrorAction SilentlyContinue
        if ($nrOwned -and $nrOwned.Path -eq $nrExe) { Stop-Process -Id $nrOwned.Id -ErrorAction SilentlyContinue }
    }
    $env:NGXGYM_BACKGROUND = $nrOldBackground
    $env:NGXGYM_FRAME_DELAY_MS = $nrOldDelay
    $env:NGXGYM_REPLACE_DEVICE = $nrOldReplacement
    $env:NGXGYM_SHUTDOWN_REPLACED = $nrOldShutdown
}
