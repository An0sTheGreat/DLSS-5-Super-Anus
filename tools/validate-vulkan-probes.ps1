param(
    [Parameter(Mandatory=$true)][string]$NrFixture,
    [Parameter(Mandatory=$true)][string]$SrFixture
)
# Regression for the OBSERVED SDK limitation, not a Vulkan NR acceptance test.
$ErrorActionPreference = 'Stop'
$nrRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$nrLogs = @()
$nrHashes = @()
foreach ($nrFixture in @($NrFixture, $SrFixture)) {
    $nrDirectory = [IO.Path]::GetFullPath((Join-Path $nrRoot $nrFixture))
    Require ($nrDirectory.StartsWith((Join-Path $nrRoot 'build\api-vulkan-'), [StringComparison]::OrdinalIgnoreCase)) 'Only workspace Vulkan fixtures accepted.'
    $nrLogs += Get-Content -LiteralPath (Join-Path $nrDirectory 'probe.out') -Raw
    $nrHashes += (Get-FileHash -LiteralPath (Join-Path $nrDirectory 'vulkan-ngx-probe.exe')).Hash
}
Require ($nrHashes[0] -eq $nrHashes[1]) 'Control and NR probe builds differ.'
foreach ($nrLog in $nrLogs) {
    Require ($nrLog -match 'device: NVIDIA GeForce RTX 5080') 'This evidence gate covers the observed RTX 5080 only.'
    Require ($nrLog -match 'Vulkan NGX init: NGX 0x00000001') 'Missing successful Vulkan initialization.'
    Require ($nrLog -match 'destroy parameters: NGX 0x00000001') 'Missing parameter cleanup.'
    Require ($nrLog -match 'Vulkan NGX shutdown: NGX 0x00000001') 'Missing successful shutdown.'
}
Require ($nrLogs[0] -match 'Requested feature: prerelease NR \(18\)' -and
    $nrLogs[0] -match 'feature requirements: NGX 0xBAD00012' -and
    $nrLogs[0] -match 'NR snippet module loaded: no' -and
    $nrLogs[0] -match 'Vulkan feature create: NGX 0xBAD0000B' -and
    $nrLogs[0] -notmatch 'Feature creation/submission completed') 'NR limitation was not reproduced; inspect changed behavior.'
Require ($nrLogs[1] -match 'Requested feature: SR control \(1\)' -and
    $nrLogs[1] -match 'Vulkan feature create: NGX 0x00000001' -and
    $nrLogs[1] -match 'Feature creation/submission completed' -and
    $nrLogs[1] -match 'release feature: NGX 0x00000001') 'SR control did not complete creation/submission/cleanup.'
Write-Output 'Observed limitation reproduced: SR creation/submission passes; SDK NR creation fails before loading the NR snippet. NO Vulkan NR pixels or compatibility proven.'
