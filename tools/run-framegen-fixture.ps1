param([Parameter(Mandatory=$true)][string]$Directory,[switch]$Recovery,[switch]$Capture,[switch]$ScaleChurn)
$ErrorActionPreference='Stop'
$nrRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nrFixture=[IO.Path]::GetFullPath((Join-Path $nrRoot $Directory))
if (-not $nrFixture.StartsWith((Join-Path $nrRoot 'build\framegen-fixture-'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture paths only.' }
if (-not (Test-Path -LiteralPath (Join-Path $nrFixture 'ReShade.ini')) -or (Test-Path -LiteralPath (Join-Path $nrFixture 'host.out'))) { throw 'Missing configuration or existing results.' }
$nrLines=@(& python (Join-Path $PSScriptRoot 'final_capture_test_identity.py'))
if ($LASTEXITCODE -ne 0 -or $nrLines.Count -ne 2) { throw 'Cannot resolve candidate.' }
$nrIdentity=$nrLines[0] | ConvertFrom-Json
$nrSymbols=$nrLines[1] | ConvertFrom-Json
Copy-Item -LiteralPath (Join-Path $nrRoot 'build/api-native-ui3-smoke/d3d11.dll') -Destination (Join-Path $nrFixture 'dxgi.dll')
foreach ($nrDll in @('nvngx_dlss.dll','nvngx_dlssnr.dll')) { Copy-Item -LiteralPath (Join-Path $nrRoot "build/api-native-ui3-smoke/$nrDll") -Destination (Join-Path $nrFixture $nrDll) }
Copy-Item -LiteralPath (Join-Path $nrRoot 'build/framegen_nr_host.exe') -Destination (Join-Path $nrFixture 'framegen_nr_host.exe')
$nrAddon=Join-Path $nrFixture 'renodx-dlss5-super-anus.addon64'
Copy-Item -LiteralPath (Join-Path $nrRoot 'build/dx11-integrated-game-test.addon64') -Destination $nrAddon
if ((Get-FileHash -LiteralPath $nrAddon).Hash -ne $nrIdentity.sha256) { throw 'Candidate changed.' }
$nrSaved=@{}
$nrVars=@{NR_PASS_TEST_RVA=$nrIdentity.preset_rva;NR_PASS_TEST_BYTES=$nrIdentity.preset_bytes}
$nrVars['NR_CAPTURE_TEST_RVA']=if ($Capture) { $nrIdentity.rva } else { $null }
$nrVars['NR_CAPTURE_TEST_BYTES']=if ($Capture) { $nrIdentity.bytes } else { $null }
foreach ($nrEntry in $nrSymbols.PSObject.Properties) { $nrVars[$nrEntry.Name]=$nrEntry.Value }
$nrVars['NR_TEST_SCALE_CHURN']=if ($ScaleChurn) { '1' } else { $null }
try {
    foreach ($nrName in $nrVars.Keys) { $nrSaved[$nrName]=[Environment]::GetEnvironmentVariable($nrName); [Environment]::SetEnvironmentVariable($nrName,$nrVars[$nrName]) }
    $nrArgument=if ($Recovery) { 'recovery' } else { 'framegen' }
    $nrProc=Start-Process -FilePath (Join-Path $nrFixture 'framegen_nr_host.exe') -ArgumentList $nrArgument -WorkingDirectory $nrFixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $nrFixture 'host.out') -RedirectStandardError (Join-Path $nrFixture 'host.err')
    if (-not $nrProc.WaitForExit(55000)) { Stop-Process -Id $nrProc.Id; throw 'Owned fixture timed out; results preserved.' }
    Get-Content -LiteralPath (Join-Path $nrFixture 'host.out')
    if ($nrProc.ExitCode -ne 0) { throw "FrameGen fixture failed: $($nrProc.ExitCode)" }
} finally { foreach ($nrName in $nrSaved.Keys) { [Environment]::SetEnvironmentVariable($nrName,$nrSaved[$nrName]) } }
