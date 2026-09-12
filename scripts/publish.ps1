param(
    [string]$Configuration = "Release",
    [string]$Runtime = "win-x64"
)

$ErrorActionPreference = "Stop"
$managerRoot = Split-Path -Parent $PSScriptRoot
$sourceAddon = Join-Path $managerRoot "Payload\renodx-dlss5-super-anus.addon64"
$expectedAddonHash = "9CA45FC9A84D3603C13FD9A72516F3166E0B2F4A811CD13AFE0DAF7F5550C212"
$publishDirectory = Join-Path $managerRoot "artifacts\publish"
$archive = Join-Path $managerRoot "artifacts\DLAssAss-5-Tool-$Runtime.zip"
$fullManagerRoot = [IO.Path]::GetFullPath($managerRoot) + [IO.Path]::DirectorySeparatorChar
if (-not ([IO.Path]::GetFullPath($publishDirectory).StartsWith($fullManagerRoot, [StringComparison]::OrdinalIgnoreCase))) {
    throw "Unsafe publish path: $publishDirectory"
}
$localDotnet = Join-Path $managerRoot ".tools\dotnet\dotnet.exe"
$dotnet = if (Test-Path -LiteralPath $localDotnet) { $localDotnet } else { "dotnet" }
$sdkVersion = & $dotnet --version
if ($LASTEXITCODE -ne 0 -or [int]($sdkVersion.Split('.')[0]) -lt 8) { throw ".NET 8 SDK or newer is required." }

$bundledDlls = Get-ChildItem -LiteralPath (Join-Path $managerRoot "DLSS Files") -Filter "*.dll" -File
if ($bundledDlls) { throw "Remove NVIDIA DLLs from 'DLSS Files' before publishing. They must be user supplied." }
if (-not (Test-Path -LiteralPath $sourceAddon)) { throw "Add-on payload not found: $sourceAddon" }
$addonHash = (Get-FileHash -LiteralPath $sourceAddon -Algorithm SHA256).Hash
if ($addonHash -ne $expectedAddonHash) { throw "Unexpected add-on payload hash: $addonHash" }

if (Test-Path -LiteralPath $publishDirectory) { Remove-Item -LiteralPath $publishDirectory -Recurse -Force }
$project = Join-Path $managerRoot "DLAssAss5Tool.csproj"
& $dotnet restore $project -r $Runtime --configfile (Join-Path $managerRoot "NuGet.Config")
if ($LASTEXITCODE -ne 0) { throw "dotnet restore failed." }
& $dotnet publish $project -c $Configuration -r $Runtime --self-contained true --no-restore -p:PublishSingleFile=true -p:DebugType=None -p:DebugSymbols=false -o $publishDirectory
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed." }

$payloadDirectory = Join-Path $publishDirectory "Payload"
$dlssDirectory = Join-Path $publishDirectory "DLSS Files"
New-Item -ItemType Directory -Path $payloadDirectory, $dlssDirectory -Force | Out-Null
Copy-Item -LiteralPath $sourceAddon -Destination (Join-Path $payloadDirectory ([IO.Path]::GetFileName($sourceAddon))) -Force
Copy-Item -LiteralPath (Join-Path $managerRoot "DLSS Files\README.md") -Destination $dlssDirectory -Force
Copy-Item -LiteralPath (Join-Path $managerRoot "README.md") -Destination $publishDirectory -Force
if (Get-ChildItem -LiteralPath $dlssDirectory -Filter "*.dll" -File) { throw "Publish contains an NVIDIA DLL." }

$checksums = @(
    Get-FileHash -LiteralPath (Join-Path $publishDirectory "DLAssAss 5 Tool.exe") -Algorithm SHA256
    Get-FileHash -LiteralPath (Join-Path $payloadDirectory ([IO.Path]::GetFileName($sourceAddon))) -Algorithm SHA256
) | ForEach-Object { "$($_.Hash)  $([IO.Path]::GetFileName($_.Path))" }
$checksums | Set-Content -LiteralPath (Join-Path $publishDirectory "SHA256SUMS.txt") -Encoding utf8

if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
& tar.exe -a -c -f $archive -C $publishDirectory .
if ($LASTEXITCODE -ne 0) { throw "archive creation failed." }
Write-Host "Published: $publishDirectory"
Write-Host "Archive:   $archive"
