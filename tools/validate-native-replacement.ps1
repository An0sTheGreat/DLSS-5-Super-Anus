param(
    [Parameter(Mandatory=$true)][string]$Control,
    [Parameter(Mandatory=$true)][string]$Replacement,
    [switch]$RequireSourceShutdown,
    [switch]$RequirePrivateRecreation
)
# Acceptance check for the controlled 50%, two-pass, 60/60/90-frame probe.
# This is not a game benchmark or a native/private-core clean-shutdown test.
$ErrorActionPreference = 'Stop'
if ($RequirePrivateRecreation) { $RequireSourceShutdown = $true }
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
$nrRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nrControl = [IO.Path]::GetFullPath((Join-Path $nrRoot $Control))
$nrReplacement = [IO.Path]::GetFullPath((Join-Path $nrRoot $Replacement))
foreach ($nrDirectory in @($nrControl, $nrReplacement)) {
    Require ($nrDirectory.StartsWith((Join-Path $nrRoot 'build\api-native-'), [StringComparison]::OrdinalIgnoreCase)) 'Only workspace native fixtures are accepted.'
}
$nrControlLog = Get-Content -LiteralPath (Join-Path $nrControl 'ReShade.log') -Raw
$nrProbeLog = Get-Content -LiteralPath (Join-Path $nrReplacement 'ReShade.log') -Raw
$nrHostLog = Get-Content -LiteralPath (Join-Path $nrReplacement 'host.out') -Raw
$nrControlHost = Get-Content -LiteralPath (Join-Path $nrControl 'host.out') -Raw
$nrControlExeHash = (Get-FileHash -LiteralPath (Join-Path $nrControl 'ngxGym.exe')).Hash
$nrProbeExeHash = (Get-FileHash -LiteralPath (Join-Path $nrReplacement 'ngxGym.exe')).Hash
Require ($nrControlExeHash -eq $nrProbeExeHash) 'Control and replacement must use the same host build.'
foreach ($nrHost in @($nrControlHost, $nrHostLog)) {
    Require ($nrHost -match 'frames 210, evaluates 210, succeeded 210') 'Missing complete native frame coverage.'
    Require ($nrHost -match 'captured frame 180 native output:') 'Missing frame-180 GPU capture.'
}
Require ([regex]::Matches($nrHostLog, 'TEST ONLY: replacement [12], .*distinct=1;').Count -eq 2) 'Two distinct source-device replacements were not proven.'
Require ([regex]::Matches($nrHostLog, 'Init_with_ProjectID -> 0x00000001').Count -eq 3) 'Expected three successful native device initializations.'
if ($RequirePrivateRecreation) {
    Require ([regex]::Matches($nrProbeLog, 'private NGX init \(0x00000001\)').Count -eq 3) 'Expected three private core initializations.'
    Require ([regex]::Matches($nrProbeLog, 'TEST ONLY: fresh private core initialization permitted').Count -eq 2) 'Expected two one-shot fresh-core permissions.'
    Require ([regex]::Matches($nrProbeLog, 'TEST ONLY: private NGX core shutdown result \(0x00000001\)').Count -eq 3) 'Expected three successful private core shutdowns.'
    Require ([regex]::Matches($nrProbeLog, 'TEST ONLY: private graphics/cache ownership released').Count -eq 3) 'Expected three private graphics/cache releases.'
    Require ($nrProbeLog -notmatch 'shared-session teardown incomplete') 'Private teardown remained incomplete.'
    # A retained-core replacement must not accidentally pass this mode. Verify
    # actual lifecycle order, not just cumulative marker counts.
    $nrStages = [regex]::Matches($nrProbeLog, 'private NGX init \(0x00000001\)|private NGX core shutdown result \(0x00000001\)|private graphics/cache ownership released')
    for ($nrIndex = 0; $nrIndex -lt 9; $nrIndex += 3) {
        Require ($nrStages[$nrIndex].Value.StartsWith('private NGX init') -and
            $nrStages[$nrIndex + 1].Value.StartsWith('private NGX core shutdown result') -and
            $nrStages[$nrIndex + 2].Value.StartsWith('private graphics/cache ownership released')) 'Invalid private lifecycle order.'
    }
} else {
    Require ([regex]::Matches($nrProbeLog, 'private NGX init \(0x00000001\)').Count -eq 1) 'Expected one retained private core, not a private-core recreation test.'
    Require ([regex]::Matches($nrProbeLog, 'TEST ONLY: NR session moved to replacement DX11 device').Count -eq 2) 'Missing NR reattachments.'
}
Require ([regex]::Matches($nrProbeLog, 'shared NR binding closed \(0x00000000\)').Count -eq 3) 'Missing successful NR binding closes.'
Require ([regex]::Matches($nrProbeLog, 'tracked consumer resources drained \(0x00000000\)').Count -eq 3) 'Missing proven resource retirement at all boundaries.'
Require ($nrProbeLog -notmatch 'transport skipped') 'Private NR skipped frames; this is not full-coverage acceptance.'
if ($RequireSourceShutdown) {
    Require ([regex]::Matches($nrHostLog, 'native session Shutdown1 result -> 0x00000001').Count -eq 3) 'Expected successful shutdown of all three native sessions.'
    Require ([regex]::Matches($nrHostLog, 'native parameter destroy -> 0x00000001').Count -eq 6) 'Expected both parameter maps destroyed for all native sessions.'
    Require ([regex]::Matches($nrHostLog, 'TEST ONLY: replaced host graphics references released.').Count -eq 2) 'Old host-owned graphics references were not released.'
    Require ($nrHostLog -match 'TEST ONLY: 0 retired source hosts retained;') 'Old native hosts are still retained.'
    Require ($nrHostLog -match '(?m)^ok\r?$') 'Replacement host did not finish its cleanup sequence.'
}
$nrCounterPattern = 'session submitted=(\d+) gate=(\d+) evaluations=(\d+) scaled=(\d+)'
$nrControlCounters = [regex]::Matches($nrControlLog, $nrCounterPattern)
$nrProbeCounters = [regex]::Matches($nrProbeLog, $nrCounterPattern)
Require ($nrControlCounters.Count -eq 1 -and $nrControlCounters[0].Value -eq 'session submitted=210 gate=210 evaluations=419 scaled=419') 'Control lacks full two-pass NR coverage.'
$nrExpected = @(
    'session submitted=60 gate=60 evaluations=119 scaled=119',
    'session submitted=120 gate=120 evaluations=239 scaled=239',
    'session submitted=210 gate=210 evaluations=419 scaled=419'
)
Require ($nrProbeCounters.Count -eq 3) 'Missing replacement boundary counters.'
for ($nrIndex = 0; $nrIndex -lt 3; ++$nrIndex) {
    Require ($nrProbeCounters[$nrIndex].Value -eq $nrExpected[$nrIndex]) "Incomplete private NR coverage at boundary $nrIndex."
}
& python (Join-Path $PSScriptRoot 'compare_native_output.py') (Join-Path $nrControl 'gym-output.bin') (Join-Path $nrReplacement 'gym-output.bin') --expect equal
Require ($LASTEXITCODE -eq 0) 'GPU output comparison failed.'
Write-Output 'PASS: controlled replacement-device NR delivery, full 210-frame/two-pass coverage and exact frame-180 RGB.'
if ($RequirePrivateRecreation) { Write-Output 'Three native/private core sessions closed, private graphics/cache ownership released and NR resumed after both recreations. Controlled lifecycle checkpoint only; NOT game/long-session acceptance.' }
elseif ($RequireSourceShutdown) { Write-Output 'All three source native sessions shut down; old host-owned graphics references released. Private DX12 core remains retained; NOT full lifecycle/game acceptance.' }
else { Write-Output 'Old native sessions/private core may be retained; NOT clean-shutdown or game acceptance.' }
