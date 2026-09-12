using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Reflection.PortableExecutable;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DLSS5ManAger.Core;

public sealed class GameAnalyzer
{
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr ExtractAssociatedIcon(IntPtr instance, StringBuilder iconPath, ref ushort iconIndex);

    [DllImport("user32.dll")]
    private static extern bool DestroyIcon(IntPtr icon);

    private static readonly HashSet<string> IgnoredExecutables = new(StringComparer.OrdinalIgnoreCase)
    {
        "crashreporter.exe", "crashpad_handler.exe", "unins000.exe", "setup.exe", "launcher.exe"
    };
    private static readonly string[] IgnoredExecutableFragments =
        ["benchmark", "config", "crash", "creationkit", "ffmpeg", "installer", "redist", "setup", "unins", "updater"];
    private static readonly HashSet<string> SkippedDirectories = new(StringComparer.OrdinalIgnoreCase)
    {
        "_DLSS5_Backups", "__Installer", "assets", "audio", "backup", "backups", "cache", "content", "data", "dlc", "libs", "localization", "mods", "movies", "packs", "redist", "screenshots", "tools"
    };
    private static readonly (string Api, string[] Tokens)[] ApiTokens =
    [
        ("Vulkan", ["vulkan-1.dll", "vulkan"]),
        ("DX12", ["d3d12.dll", "direct3d 12", "d3d12"]),
        ("DX11", ["d3d11.dll", "direct3d 11", "d3d11"]),
        ("DX10", ["d3d10.dll", "direct3d 10", "d3d10"]),
        ("DX9", ["d3d9.dll", "direct3d 9", "d3d9"]),
        ("OpenGL", ["opengl32.dll", "opengl"])
    ];
    private static readonly Regex AppIdPattern = new("\"appid\"\\s+\"(?<value>\\d+)\"", RegexOptions.IgnoreCase);
    private static readonly Regex InstallDirPattern = new("\"installdir\"\\s+\"(?<value>[^\"]+)\"", RegexOptions.IgnoreCase);

    public GameEntry Analyze(string directory)
    {
        var fullPath = DirectoryPath.Normalize(directory);
        var files = EnumerateRelevantFiles(fullPath).ToArray();
        var names = files.Select(Path.GetFileName).OfType<string>()
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var executables = files.Where(path => path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
            .Where(path => !IsIgnoredExecutable(path))
            .OrderByDescending(SafeLength).ToArray();

        var apis = DetectApis(names, executables, files);
        var graphicsApi = apis.Count == 0 ? "Unknown" : string.Join(" / ", apis);
        var executableDirectory = Path.GetDirectoryName(executables.FirstOrDefault());
        var installFiles = executableDirectory is null ? [] : files
            .Where(path => DirectoryPath.Comparer.Equals(Path.GetDirectoryName(path), executableDirectory))
            .ToArray();
        var installNames = installFiles.Select(Path.GetFileName).OfType<string>()
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var hasAddon = installNames.Any(name => name.EndsWith(".addon64", StringComparison.OrdinalIgnoreCase) &&
            name.Contains("renodx-dlss", StringComparison.OrdinalIgnoreCase));
        var reshadeModules = installFiles.Where(ReShadeService.IsReShadeModule).ToArray();
        var hasReShade = installNames.Contains("ReShade.ini") || installNames.Contains("ReShade.log") ||
            executableDirectory is not null && Directory.Exists(Path.Combine(executableDirectory, "reshade-shaders")) ||
            reshadeModules.Length > 0;
        var expectedProxy = ReShadeService.ExpectedProxyName(graphicsApi);

        var game = new GameEntry
        {
            Name = Path.GetFileName(fullPath.TrimEnd(Path.DirectorySeparatorChar)) is { Length: > 0 } name ? name : fullPath,
            GameDirectory = fullPath,
            ExecutablePath = executables.FirstOrDefault(),
            HasReShade = hasReShade,
            HasReShadeProxyMismatch = expectedProxy is not null && reshadeModules.Length > 0 &&
                !reshadeModules.Any(path => Path.GetFileName(path).Equals(expectedProxy, StringComparison.OrdinalIgnoreCase)),
            ReShadeModulePaths = reshadeModules,
            HasAddon = hasAddon,
            HasDlss = installNames.Contains("nvngx_dlss.dll"),
            HasDlssG = installNames.Contains("nvngx_dlssg.dll"),
            HasDlssNr = installNames.Contains("nvngx_dlssnr.dll"),
            SteamAppId = FindSteamAppId(fullPath),
            GraphicsApi = graphicsApi
        };
        game.IconSource = LoadIcon(game.ExecutablePath);

        game.Status = hasAddon ? "Installed" : hasReShade ? "Ready" : "ReShade not detected";
        game.Details = string.Join("  •  ", new[]
        {
            game.ExecutablePath is null ? "No game executable found" : Path.GetFileName(game.ExecutablePath),
            hasReShade ? "ReShade detected" : "ReShade not detected",
            game.HasDlss ? "DLSS SR" : "No DLSS SR",
            game.HasDlssG ? "DLSS FG" : "No DLSS FG",
            game.HasDlssNr ? "DLSS NR" : "No DLSS NR"
        });
        return game;
    }

    private static List<string> DetectApis(HashSet<string> names, IEnumerable<string> executables, IEnumerable<string> files)
    {
        var executableArray = executables.Take(6).ToArray();
        var active = DetectActiveApis(files);
        if (active.Count > 0) return Ordered(active);

        if (names.Any(name => name.Equals("dxil.dll", StringComparison.OrdinalIgnoreCase) ||
                              name.Equals("D3D12Core.dll", StringComparison.OrdinalIgnoreCase))) return ["DX12"];
        if (names.Contains("UnityPlayer.dll")) return ["DX11"];

        var imported = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var engineModules = files.Where(path => new[] { "UnityPlayer.dll", "GameAssembly.dll", "engine.dll", "renderer.dll" }
            .Contains(Path.GetFileName(path), StringComparer.OrdinalIgnoreCase));
        foreach (var path in executableArray.Take(1).Concat(engineModules).Take(5))
            AddApiMatches(string.Join('\n', ReadImportedDlls(path)), imported);
        if (imported.Count > 0) return Ordered(imported);

        var named = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var localFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var executableNames = string.Join('\n', executableArray.Select(Path.GetFileName));
        if (executableNames.Contains("vulkan", StringComparison.OrdinalIgnoreCase) ||
            executableNames.Contains("x64vk", StringComparison.OrdinalIgnoreCase)) named.Add("Vulkan");
        foreach (var (api, clues) in new[]
        {
            ("DX12", new[] { "dx12", "d3d12" }), ("DX11", new[] { "dx11", "d3d11" }),
            ("DX10", new[] { "dx10", "d3d10" }), ("DX9", new[] { "dx9", "d3d9" }),
            ("OpenGL", new[] { "opengl" })
        })
            if (clues.Any(clue => executableNames.Contains(clue, StringComparison.OrdinalIgnoreCase)))
                named.Add(api);
        if (named.Count > 0) return Ordered(named);

        AddApiMatches(string.Join('\n', names), localFiles);
        if (names.Contains("UnityPlayer.dll") && !localFiles.Contains("Vulkan")) localFiles.Add("DX11");
        if (localFiles.Count > 0) return Ordered(localFiles);

        var binary = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (executableArray.FirstOrDefault() is { } executable) ScanApiEvidence(executable, binary);
        return Ordered(binary);
    }

    private static HashSet<string> DetectActiveApis(IEnumerable<string> files)
    {
        foreach (var log in files.Where(path => Path.GetFileName(path).Equals("ReShade.log", StringComparison.OrdinalIgnoreCase))
                     .OrderByDescending(SafeLastWrite))
        {
            var evidence = ReadEvidence(log);
            var explicitApi = Regex.Match(evidence, @"api:\s*(d3d12|d3d11|d3d10|d3d9|vulkan|opengl)\b", RegexOptions.IgnoreCase);
            if (explicitApi.Success) return [MapApi(explicitApi.Groups[1].Value)];
            var redirectedApi = Regex.Match(evidence,
                @"Redirecting (D3D12CreateDevice|D3D11CreateDevice|D3D10CreateDevice|Direct3DCreate9|vkCreate|wglCreateContext)",
                RegexOptions.IgnoreCase);
            if (redirectedApi.Success) return [MapApi(redirectedApi.Groups[1].Value)];
        }
        return new(StringComparer.OrdinalIgnoreCase);
    }

    private static string MapApi(string evidence) => evidence.ToLowerInvariant() switch
    {
        var value when value.Contains("d3d12") => "DX12",
        var value when value.Contains("d3d11") => "DX11",
        var value when value.Contains("d3d10") => "DX10",
        var value when value.Contains("d3d9") || value.Contains("direct3dcreate9") => "DX9",
        var value when value.Contains("vulkan") || value.Contains("vkcreate") => "Vulkan",
        _ => "OpenGL"
    };

    private static void ScanApiEvidence(string path, HashSet<string> result)
    {
        try
        {
            using var stream = File.OpenRead(path);
            var buffer = new byte[1024 * 1024];
            var tail = "";
            long remaining = Math.Min(stream.Length, 128L * 1024 * 1024);
            while (remaining > 0)
            {
                var read = stream.Read(buffer, 0, (int)Math.Min(buffer.Length, remaining));
                if (read == 0) break;
                var evidence = tail + Encoding.ASCII.GetString(buffer, 0, read);
                AddApiMatches(evidence, result);
                tail = evidence[^Math.Min(64, evidence.Length)..];
                remaining -= read;
            }
        }
        catch { }
    }

    private static string ReadEvidence(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            const int sampleSize = 4 * 1024 * 1024;
            var first = new byte[Math.Min(sampleSize, checked((int)Math.Min(stream.Length, int.MaxValue)))];
            stream.ReadExactly(first);
            if (stream.Length <= sampleSize) return Encoding.ASCII.GetString(first);
            stream.Position = Math.Max(sampleSize, stream.Length - sampleSize);
            var last = new byte[checked((int)(stream.Length - stream.Position))];
            stream.ReadExactly(last);
            return Encoding.ASCII.GetString(first) + Encoding.ASCII.GetString(last);
        }
        catch { return ""; }
    }

    private static IEnumerable<string> ReadImportedDlls(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            using var reader = new PEReader(stream, PEStreamOptions.LeaveOpen);
            var headers = reader.PEHeaders;
            var directory = headers.PEHeader?.ImportTableDirectory;
            if (directory is null || directory.Value.RelativeVirtualAddress == 0) return [];
            var descriptorOffset = RvaToOffset(headers, directory.Value.RelativeVirtualAddress);
            if (descriptorOffset < 0) return [];

            var names = new List<string>();
            using var binary = new BinaryReader(stream, Encoding.ASCII, true);
            stream.Position = descriptorOffset;
            while (stream.Position + 20 <= stream.Length)
            {
                var originalThunk = binary.ReadUInt32();
                var timestamp = binary.ReadUInt32();
                var forwarder = binary.ReadUInt32();
                var nameRva = binary.ReadUInt32();
                var firstThunk = binary.ReadUInt32();
                if ((originalThunk | timestamp | forwarder | nameRva | firstThunk) == 0) break;
                var nextDescriptor = stream.Position;
                var nameOffset = RvaToOffset(headers, checked((int)nameRva));
                if (nameOffset >= 0)
                {
                    stream.Position = nameOffset;
                    var bytes = new List<byte>(32);
                    while (stream.Position < stream.Length && bytes.Count < 260)
                    {
                        var value = binary.ReadByte();
                        if (value == 0) break;
                        bytes.Add(value);
                    }
                    if (bytes.Count > 0) names.Add(Encoding.ASCII.GetString(bytes.ToArray()));
                }
                stream.Position = nextDescriptor;
            }
            return names;
        }
        catch { return []; }
    }

    private static int RvaToOffset(PEHeaders headers, int rva)
    {
        foreach (var section in headers.SectionHeaders)
        {
            var size = Math.Max(section.VirtualSize, section.SizeOfRawData);
            if (rva >= section.VirtualAddress && rva < section.VirtualAddress + size)
                return rva - section.VirtualAddress + section.PointerToRawData;
        }
        return -1;
    }

    private static void AddApiMatches(string evidence, HashSet<string> result)
    {
        foreach (var (api, tokens) in ApiTokens)
            if (tokens.Any(token => evidence.Contains(token, StringComparison.OrdinalIgnoreCase))) result.Add(api);
        if (evidence.Contains("dxil.dll", StringComparison.OrdinalIgnoreCase)) result.Add("DX12");
    }

    private static List<string> Ordered(HashSet<string> result) =>
        ApiTokens.Select(item => item.Api).Where(result.Contains).ToList();

    private static IEnumerable<string> EnumerateRelevantFiles(string root)
    {
        var pending = new Queue<(string Path, int Depth)>();
        pending.Enqueue((root, 0));
        while (pending.TryDequeue(out var current))
        {
            foreach (var file in SafeFiles(current.Path)) yield return file;
            if (current.Depth >= 5) continue;
            foreach (var child in SafeDirectories(current.Path))
            {
                if (SkippedDirectories.Contains(Path.GetFileName(child)) || IsReparsePoint(child)) continue;
                pending.Enqueue((child, current.Depth + 1));
            }
        }
    }

    private static IEnumerable<string> SafeFiles(string directory)
    {
        try { return Directory.EnumerateFiles(directory).ToArray(); }
        catch { return []; }
    }

    private static IEnumerable<string> SafeDirectories(string directory)
    {
        try { return Directory.EnumerateDirectories(directory).ToArray(); }
        catch { return []; }
    }

    private static long SafeLength(string path)
    {
        try { return new FileInfo(path).Length; }
        catch { return 0; }
    }

    private static bool IsIgnoredExecutable(string path)
    {
        var name = Path.GetFileName(path);
        return IgnoredExecutables.Contains(name) ||
               IgnoredExecutableFragments.Any(fragment => name.Contains(fragment, StringComparison.OrdinalIgnoreCase));
    }

    private static DateTime SafeLastWrite(string path)
    {
        try { return File.GetLastWriteTimeUtc(path); }
        catch { return DateTime.MinValue; }
    }

    private static bool IsReparsePoint(string path)
    {
        try { return File.GetAttributes(path).HasFlag(FileAttributes.ReparsePoint); }
        catch { return true; }
    }

    private static string? FindSteamAppId(string path)
    {
        try
        {
            for (var gameRoot = new DirectoryInfo(path); gameRoot.Parent?.Parent is not null; gameRoot = gameRoot.Parent)
            {
                if (!gameRoot.Parent.Name.Equals("common", StringComparison.OrdinalIgnoreCase) ||
                    !gameRoot.Parent.Parent!.Name.Equals("steamapps", StringComparison.OrdinalIgnoreCase)) continue;
                foreach (var manifest in Directory.EnumerateFiles(gameRoot.Parent.Parent.FullName, "appmanifest_*.acf"))
                {
                    var text = File.ReadAllText(manifest);
                    if (!string.Equals(InstallDirPattern.Match(text).Groups["value"].Value, gameRoot.Name,
                        StringComparison.OrdinalIgnoreCase)) continue;
                    var appId = AppIdPattern.Match(text).Groups["value"].Value;
                    return appId.Length == 0 ? null : appId;
                }
                return null;
            }
        }
        catch { }
        return null;
    }

    private static ImageSource? LoadIcon(string? executable)
    {
        if (executable is null) return null;
        IntPtr icon = IntPtr.Zero;
        try
        {
            var iconPath = new StringBuilder(executable, Math.Max(260, executable.Length + 1));
            ushort iconIndex = 0;
            icon = ExtractAssociatedIcon(IntPtr.Zero, iconPath, ref iconIndex);
            if (icon == IntPtr.Zero) return null;
            var image = Imaging.CreateBitmapSourceFromHIcon(icon, Int32Rect.Empty,
                BitmapSizeOptions.FromWidthAndHeight(32, 32));
            image.Freeze();
            return image;
        }
        catch { return null; }
        finally { if (icon != IntPtr.Zero) DestroyIcon(icon); }
    }
}
