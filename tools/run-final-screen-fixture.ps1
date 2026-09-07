param([Parameter(Mandatory=$true)][string]$Directory,[switch]$HDR,[switch]$TimeoutCase)
$ErrorActionPreference='Stop'
$nrRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nrFixture = [IO.Path]::GetFullPath((Join-Path $nrRoot $Directory))
if (-not $nrFixture.StartsWith((Join-Path $nrRoot 'build\final-screen-fixture-'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture path only.' }
if (-not (Test-Path -LiteralPath (Join-Path $nrFixture 'ReShade.ini')) -or (Test-Path -LiteralPath (Join-Path $nrFixture 'host.out'))) { throw 'Missing configuration or existing results.' }
$nrLines = @(& python (Join-Path $PSScriptRoot 'final_capture_test_identity.py'))
if ($LASTEXITCODE -ne 0 -or $nrLines.Count -ne 2) { throw 'Cannot resolve tested candidate.' }
$nrIdentity = $nrLines[0] | ConvertFrom-Json
$nrSymbols = $nrLines[1] | ConvertFrom-Json
Copy-Item -LiteralPath (Join-Path $nrRoot 'build/api-native-ui3-smoke/d3d11.dll') -Destination (Join-Path $nrFixture 'dxgi.dll')
Copy-Item -LiteralPath (Join-Path $nrRoot 'build/final_screen_host.exe') -Destination (Join-Path $nrFixture 'final_screen_host.exe')
$nrAddon = Join-Path $nrFixture 'renodx-dlss5-super-anus.addon64'
Copy-Item -LiteralPath (Join-Path $nrRoot 'build/dx11-integrated-game-test.addon64') -Destination $nrAddon
if ((Get-FileHash -LiteralPath $nrAddon).Hash -ne $nrIdentity.sha256) { throw 'Candidate changed.' }
$nrSaved = @{}
$nrVars = @{ NR_CAPTURE_TEST_RVA=$nrIdentity.rva }
foreach ($nrEntry in $nrSymbols.PSObject.Properties) { $nrVars[$nrEntry.Name]=$nrEntry.Value }
try {
    foreach ($nrName in $nrVars.Keys) { $nrSaved[$nrName]=[Environment]::GetEnvironmentVariable($nrName); [Environment]::SetEnvironmentVariable($nrName,$nrVars[$nrName]) }
    $nrArg = if ($TimeoutCase) { 'timeout' } elseif ($HDR) { 'hdr' } else { 'sdr' }
    $nrProc = Start-Process -FilePath (Join-Path $nrFixture 'final_screen_host.exe') -ArgumentList $nrArg -WorkingDirectory $nrFixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $nrFixture 'host.out') -RedirectStandardError (Join-Path $nrFixture 'host.err')
    if (-not $nrProc.WaitForExit(45000)) { Stop-Process -Id $nrProc.Id; throw 'Owned test host timed out; results preserved.' }
    Get-Content -LiteralPath (Join-Path $nrFixture 'host.out')
    if ($nrProc.ExitCode -ne 0) { throw "Host failed: $($nrProc.ExitCode)" }
} finally { foreach ($nrName in $nrSaved.Keys) { [Environment]::SetEnvironmentVariable($nrName,$nrSaved[$nrName]) } }
