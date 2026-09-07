param(
    [Parameter(Mandatory=$true)][string]$Directory,
    [switch]$SrControl,
    [switch]$DirectNr,
    [switch]$ZeroIntensity,
    [ValidateRange(5,60)][int]$TimeoutSeconds = 45
)
$ErrorActionPreference = 'Stop'
if ($SrControl -and $DirectNr) { throw 'Choose SR control or direct NR, not both.' }
if ($ZeroIntensity -and -not $DirectNr) { throw 'ZeroIntensity requires DirectNr.' }
$nrRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nrDirectory = [IO.Path]::GetFullPath((Join-Path $nrRoot $Directory))
if (-not $nrDirectory.StartsWith((Join-Path $nrRoot 'build\api-vulkan-'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only workspace build/api-vulkan-* fixtures may run.'
}
$nrExe = Join-Path $nrDirectory 'vulkan-ngx-probe.exe'
if (-not (Test-Path -LiteralPath $nrExe)) { throw 'Missing fixture executable.' }
if (Test-Path -LiteralPath (Join-Path $nrDirectory 'probe.out')) { throw 'Preserving existing results; use a fresh fixture.' }
$nrStart = @{
    FilePath = $nrExe; WorkingDirectory = $nrDirectory; WindowStyle = 'Hidden'; PassThru = $true
    RedirectStandardOutput = (Join-Path $nrDirectory 'probe.out')
    RedirectStandardError = (Join-Path $nrDirectory 'probe.err')
}
if ($SrControl) { $nrStart.ArgumentList = '--sr-control' }
if ($DirectNr) { $nrStart.ArgumentList = '--direct-nr' }
if ($ZeroIntensity) { $nrStart.ArgumentList = '--direct-nr-zero' }
$nrProcess = $null
try {
    $nrProcess = Start-Process @nrStart
    if (-not $nrProcess.WaitForExit($TimeoutSeconds * 1000)) { throw 'Vulkan probe timed out (not a pass).' }
    Get-Content -LiteralPath (Join-Path $nrDirectory 'probe.out')
    Get-Content -LiteralPath (Join-Path $nrDirectory 'probe.err')
    Write-Output "Vulkan probe exit: $($nrProcess.ExitCode)"
    if ($nrProcess.ExitCode -ne 0) { throw 'Vulkan probe failed; inspect preserved diagnostics. This is not a compatibility pass.' }
} finally {
    if ($nrProcess -and -not $nrProcess.HasExited) {
        $nrOwned = Get-Process -Id $nrProcess.Id -ErrorAction SilentlyContinue
        if ($nrOwned -and $nrOwned.Path -eq $nrExe) { Stop-Process -Id $nrOwned.Id }
    }
}
