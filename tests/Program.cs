using DLSS5ManAger.Core;

var root = Path.Combine(Path.GetTempPath(), "DLAssAss5Tool-" + Guid.NewGuid().ToString("N"));
var game = Path.Combine(root, "steamapps", "common", "Game");
var installDirectory = Path.Combine(game, "Bin", "Win64MasterMasterSteamPGO");
var executable = Path.Combine(installDirectory, "game.exe");
var payload = Path.Combine(root, "Payload");
var dlss = Path.Combine(root, "DLSS Files");
var backups = Path.Combine(root, "Backups");

try
{
    Directory.CreateDirectory(installDirectory);
    Directory.CreateDirectory(payload);
    Directory.CreateDirectory(dlss);
    File.WriteAllText(Path.Combine(root, "steamapps", "appmanifest_123.acf"),
        "\"AppState\" { \"appid\" \"123\" \"installdir\" \"Game\" }");
    File.WriteAllText(Path.Combine(installDirectory, "ReShade.ini"), "[GENERAL]");
    File.Copy(Environment.ProcessPath!, executable);
    File.AppendAllText(executable, "d3d12.dll");
    File.WriteAllText(Path.Combine(installDirectory, "nvngx_dlssnr.dll"), "original");
    File.WriteAllText(Path.Combine(game, InstallerService.AddonName), "misplaced");
    File.WriteAllText(Path.Combine(game, "nvngx_dlssg.dll"), "misplaced");
    File.WriteAllText(Path.Combine(payload, InstallerService.AddonName), "addon");
    File.WriteAllText(Path.Combine(dlss, "nvngx_dlssnr.dll"), "replacement");

    var analyzer = new GameAnalyzer();
    var scanner = new GameScanner();
    var service = new InstallerService(backups, payload);
    var analysis = analyzer.Analyze(game);
    Require(analysis.HasReShade, "ReShade detection failed.");
    Require(!analysis.HasAddon && !analysis.HasDlssG, "Files away from the selected executable were treated as installed.");
    Require(analysis.IconSource is not null, "Executable icon extraction failed.");
    Require(analysis.GraphicsApi.Contains("DX12"), "Executable API detection failed.");
    Require(analysis.SteamAppId == "123", "Steam AppID detection failed.");
    Require(scanner.ScanDirectory(root).Contains(DirectoryPath.Normalize(game), DirectoryPath.Comparer),
        "Recursive game discovery failed.");
    Require(DirectoryPath.NormalizeDistinct([game, game.ToLowerInvariant().Replace('\\', '/')]).Count == 1,
        "Equivalent Windows paths were not deduplicated.");
    Require(InstallerService.RequiredDlssFiles.Count == 3, "Required DLSS file set changed unexpectedly.");
    Require(CoverArtService.NormalizeTitle("Ghost of Tsushima DIRECTOR'S CUT") ==
        CoverArtService.NormalizeTitle("Ghost of Tsushima: Director’s Cut"), "Cover title normalization failed.");
    var reshade = ReShadeService.ParseLatestAddonInstaller("""
        <a href="/downloads/ReShade_Setup_6.7.0_Addon.exe">old</a>
        <a href="/downloads/ReShade_Setup_6.8.0_Addon.exe">latest</a>
        """);
    Require(reshade.Version == new Version(6, 8, 0) && reshade.DownloadUrl.Host == "reshade.me",
        "Official ReShade add-on release parsing failed.");
    Require(ReShadeService.ResolveInstallerApi("DX12") == "dxgi", "DX12 must use the DXGI ReShade proxy.");
    Require(ReShadeService.ResolveInstallerApi("DX11") == "dxgi", "DX11 must use the DXGI ReShade proxy.");
    Require(ReShadeService.ResolveInstallerApi("DX9") == "d3d9", "DX9 ReShade mapping failed.");
    Require(ReShadeService.ResolveInstallerApi("DX12 / DX11") is null &&
        ReShadeService.SupportedGraphicsApis("DX12 / DX11").SequenceEqual(["DX12", "DX11"]),
        "Multi-API games must require an explicit supported API selection.");

    var store = new AppStore(Path.Combine(root, "Store"));
    store.Save(new ManagerSettings
    {
        GameDirectories = [game, game.ToLowerInvariant().Replace('\\', '/')],
        HiddenGameDirectories = [game, game.ToLowerInvariant().Replace('\\', '/')],
        CustomGameNames = new(DirectoryPath.Comparer) { [game.ToLowerInvariant().Replace('\\', '/')] = "Renamed Game" },
        IsLibraryView = false
    });
    var loaded = store.Load();
    Require(loaded.GameDirectories.Count == 1, "Persisted game paths were not deduplicated.");
    Require(loaded.HiddenGameDirectories.Count == 1, "Hidden game paths were not deduplicated.");
    Require(loaded.CustomGameNames.TryGetValue(game, out var customName) && customName == "Renamed Game",
        "Custom game name was not normalized and persisted.");
    Require(!loaded.IsLibraryView, "View preference was not persisted.");
    Require(service.Install(executable, dlss, true).Success, "Install failed.");
    Require(File.ReadAllText(Path.Combine(installDirectory, "nvngx_dlssnr.dll")) == "replacement", "DLSS replacement failed.");
    Require(File.Exists(Path.Combine(installDirectory, InstallerService.AddonName)), "Add-on was not installed beside the executable.");
    Require(File.ReadAllText(Path.Combine(game, InstallerService.AddonName)) == "misplaced", "Parent-folder files were modified.");
    Require(service.RestoreLatest(executable).Success, "Restore failed.");
    Require(File.ReadAllText(Path.Combine(installDirectory, "nvngx_dlssnr.dll")) == "original", "Original DLSS file was not restored.");
    Require(!File.Exists(Path.Combine(installDirectory, InstallerService.AddonName)), "New add-on was not removed by restore.");
    File.Delete(Path.Combine(installDirectory, "ReShade.ini"));
    Require(!service.Install(executable, dlss, true).Success, "Install proceeded without ReShade beside the executable.");
    Console.WriteLine("PASS: paths, discovery, API/AppID analysis, covers, ReShade API mapping, preferences, install, backup, and restore");
}
finally
{
    if (Directory.Exists(root)) Directory.Delete(root, true);
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}
