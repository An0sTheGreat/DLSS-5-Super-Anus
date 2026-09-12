using Microsoft.Win32;
using System.Text.RegularExpressions;

namespace DLSS5ManAger.Core;

public sealed class GameScanner
{
    private static readonly Regex SteamPath = new("\"path\"\\s+\"(?<path>[^\"]+)\"",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);
    private static readonly HashSet<string> ExcludedDirectories = new(StringComparer.OrdinalIgnoreCase)
    {
        "$Recycle.Bin", "$Windows.~BT", ".git", "node_modules", "ProgramData", "System Volume Information", "Windows", "WindowsApps"
    };
    private static readonly HashSet<string> GameMarkers = new(StringComparer.OrdinalIgnoreCase)
    {
        "d3d9.dll", "d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "GameAssembly.dll",
        "nvngx_dlss.dll", "nvngx_dlssg.dll", "nvngx_dlssnr.dll", "opengl32.dll", "ReShade.ini",
        "steam_api.dll", "steam_api64.dll", "UnityPlayer.dll", "vulkan-1.dll"
    };
    private static readonly HashSet<string> GameDataDirectories = new(StringComparer.OrdinalIgnoreCase)
    {
        "Binaries", "Content", "Data", "Game", "Paks", "StreamingAssets"
    };

    public IReadOnlyList<string> ScanSteamLibraries()
    {
        var libraries = new HashSet<string>(DirectoryPath.Comparer);
        var steam = FindSteamDirectory();
        if (steam is null) return [];

        libraries.Add(DirectoryPath.Normalize(steam));
        var libraryFile = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
        try
        {
            if (File.Exists(libraryFile))
                foreach (Match match in SteamPath.Matches(File.ReadAllText(libraryFile)))
                    libraries.Add(DirectoryPath.Normalize(match.Groups["path"].Value.Replace("\\\\", "\\")));
        }
        catch { }

        return libraries
            .Select(path => Path.Combine(path, "steamapps", "common"))
            .Where(Directory.Exists)
            .SelectMany(SafeDirectories)
            .Select(DirectoryPath.Normalize)
            .Distinct(DirectoryPath.Comparer)
            .OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public IReadOnlyList<string> ScanDirectory(string searchDirectory)
    {
        var root = DirectoryPath.Normalize(searchDirectory);
        var results = new HashSet<string>(DirectoryPath.Comparer);
        var pending = new Stack<string>();
        pending.Push(root);

        while (pending.TryPop(out var directory))
        {
            var children = SafeDirectories(directory).Where(child => !ShouldSkip(child)).ToArray();
            if (Path.GetFileName(directory).Equals("common", StringComparison.OrdinalIgnoreCase) &&
                Directory.GetParent(directory)?.Name.Equals("steamapps", StringComparison.OrdinalIgnoreCase) == true)
            {
                foreach (var child in children) results.Add(DirectoryPath.Normalize(child));
                continue;
            }

            var files = SafeFiles(directory);
            if (IsGameDirectory(files, children))
            {
                results.Add(DirectoryPath.Normalize(directory));
                continue;
            }

            foreach (var child in children) pending.Push(child);
        }

        return results.OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    private static bool IsGameDirectory(IReadOnlyList<string> files, IReadOnlyList<string> children)
    {
        var executables = files.Where(path => path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
            .Where(path => !Path.GetFileName(path).Contains("unins", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (executables.Length == 0) return false;
        var names = files.Select(Path.GetFileName).OfType<string>().ToHashSet(StringComparer.OrdinalIgnoreCase);
        if (names.Overlaps(GameMarkers)) return true;
        if (children.Any(path => GameDataDirectories.Contains(Path.GetFileName(path))) && executables.Any(path => SafeLength(path) >= 4 * 1024 * 1024))
            return true;
        return executables.Any(path => SafeLength(path) >= 20 * 1024 * 1024);
    }

    private static string? FindSteamDirectory()
    {
        foreach (var key in new[]
        {
            @"HKEY_CURRENT_USER\Software\Valve\Steam",
            @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam"
        })
        {
            var value = Registry.GetValue(key, "SteamPath", null) as string
                ?? Registry.GetValue(key, "InstallPath", null) as string;
            if (!string.IsNullOrWhiteSpace(value) && Directory.Exists(value)) return value;
        }
        return null;
    }

    private static bool ShouldSkip(string path)
    {
        if (ExcludedDirectories.Contains(Path.GetFileName(path))) return true;
        try { return File.GetAttributes(path).HasFlag(FileAttributes.ReparsePoint); }
        catch { return true; }
    }

    private static string[] SafeDirectories(string directory)
    {
        try { return Directory.GetDirectories(directory); }
        catch { return []; }
    }

    private static string[] SafeFiles(string directory)
    {
        try { return Directory.GetFiles(directory); }
        catch { return []; }
    }

    private static long SafeLength(string path)
    {
        try { return new FileInfo(path).Length; }
        catch { return 0; }
    }
}
