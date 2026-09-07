param([Parameter(Mandatory=$true)][string]$Directory, [switch]$NoCapture, [switch]$PassChurn)
$ErrorActionPreference = 'Stop'
$nrWorkspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nrFixture = [IO.Path]::GetFullPath((Join-Path $nrWorkspace $Directory))
if (-not $nrFixture.StartsWith((Join-Path $nrWorkspace 'build\api-native-capture-'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Capture fixtures only.' }
if (-not (Test-Path -LiteralPath (Join-Path $nrFixture 'ReShade.ini')) -or
    -not (Test-Path -LiteralPath (Join-Path $nrFixture 'scenario.txt'))) { throw 'Missing authored test configuration.' }
if (Test-Path -LiteralPath (Join-Path $nrFixture 'host.out')) { throw 'Preserving previous fixture logs; use a fresh directory.' }
$nrIdentity = (& python (Join-Path $PSScriptRoot 'capture_test_identity.py')) | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Unable to validate capture entry.' }
foreach ($nrName in @('d3d11.dll','nvngx_dlss.dll','nvngx_dlssnr.dll')) {
    Copy-Item -LiteralPath (Join-Path $nrWorkspace "build\api-native-ui3-smoke\$nrName") -Destination (Join-Path $nrFixture $nrName)
}
Copy-Item -LiteralPath (Join-Path $nrWorkspace 'build\ngxGym.exe') -Destination (Join-Path $nrFixture 'ngxGym.exe')
$nrAddon = Join-Path $nrFixture 'renodx-dlss5-super-anus.addon64'
Copy-Item -LiteralPath (Join-Path $nrWorkspace 'build\dx11-integrated-game-test.addon64') -Destination $nrAddon
if ((Get-FileHash -LiteralPath $nrAddon -Algorithm SHA256).Hash -ne $nrIdentity.sha256) { throw 'Candidate changed during staging.' }
$nrOldRva = $env:NR_CAPTURE_TEST_RVA
$nrOldBytes = $env:NR_CAPTURE_TEST_BYTES
$nrOldPassRva = $env:NR_PASS_TEST_RVA
$nrOldPassBytes = $env:NR_PASS_TEST_BYTES
try {
    $env:NR_CAPTURE_TEST_RVA = if ($NoCapture) { $null } else { $nrIdentity.rva }
    $env:NR_CAPTURE_TEST_BYTES = if ($NoCapture) { $null } else { $nrIdentity.bytes }
    $env:NR_PASS_TEST_RVA = if ($PassChurn) { $nrIdentity.preset_rva } else { $null }
    $env:NR_PASS_TEST_BYTES = if ($PassChurn) { $nrIdentity.preset_bytes } else { $null }
    & (Join-Path $PSScriptRoot 'run-native-fixture.ps1') -Directory $Directory -DelayMilliseconds 10 -TimeoutSeconds 90
} finally {
    $env:NR_CAPTURE_TEST_RVA = $nrOldRva
    $env:NR_CAPTURE_TEST_BYTES = $nrOldBytes
    $env:NR_PASS_TEST_RVA = $nrOldPassRva
    $env:NR_PASS_TEST_BYTES = $nrOldPassBytes
}
