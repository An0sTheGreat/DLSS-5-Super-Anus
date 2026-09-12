using System.Security.Cryptography;
using System.Text.Json;

namespace DLSS5ManAger.Core;

public sealed class InstallResult
{
    public bool Success { get; init; }
    public required string Message { get; init; }
}

public sealed class InstallManifest
{
    public DateTimeOffset CreatedUtc { get; set; }
    public string GameDirectory { get; set; } = "";
    public List<InstalledFile> Files { get; set; } = [];
}

public sealed class InstalledFile
{
    public string TargetName { get; set; } = "";
    public string? BackupName { get; set; }
    public string InstalledHash { get; set; } = "";
}

public sealed class InstallerService
{
    public const string AddonName = "renodx-dlss5-super-anus.addon64";
    public static IReadOnlyList<string> RequiredDlssFiles { get; } =
        ["nvngx_dlss.dll", "nvngx_dlssg.dll", "nvngx_dlssnr.dll"];
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private readonly string _backupRoot;
    private readonly string _payloadDirectory;

    public InstallerService(string backupRoot, string? payloadDirectory = null)
    {
        _backupRoot = backupRoot;
        _payloadDirectory = payloadDirectory ?? Path.Combine(AppContext.BaseDirectory, "Payload");
    }

    public InstallResult Install(string gameDirectory, string dlssDirectory, bool includeDlssFiles)
    {
        var addon = Path.Combine(_payloadDirectory, AddonName);
        if (!File.Exists(addon))
            return Fail($"Manager payload is missing: {addon}");
        if (!Directory.Exists(gameDirectory)) return Fail("The selected game directory does not exist.");

        var sources = new List<string> { addon };
        if (includeDlssFiles)
            sources.AddRange(RequiredDlssFiles.Select(name => Path.Combine(dlssDirectory, name)).Where(File.Exists));

        var backupDirectory = Path.Combine(_backupRoot, SafeName(gameDirectory),
            DateTimeOffset.UtcNow.ToString("yyyyMMdd-HHmmss-fff"));
        var manifest = new InstallManifest { CreatedUtc = DateTimeOffset.UtcNow, GameDirectory = gameDirectory };
        Directory.CreateDirectory(backupDirectory);

        try
        {
            foreach (var source in sources)
            {
                var targetName = Path.GetFileName(source);
                var target = Path.Combine(gameDirectory, targetName);
                string? backupName = null;
                if (File.Exists(target))
                {
                    backupName = targetName + ".original";
                    File.Copy(target, Path.Combine(backupDirectory, backupName), true);
                }

                var installedFile = new InstalledFile { TargetName = targetName, BackupName = backupName };
                manifest.Files.Add(installedFile);
                ReplaceFile(source, target);
                installedFile.InstalledHash = Hash(target);
            }

            File.WriteAllText(Path.Combine(backupDirectory, "manifest.json"),
                JsonSerializer.Serialize(manifest, JsonOptions));
            return new InstallResult
            {
                Success = true,
                Message = $"Installed {manifest.Files.Count} file(s). Backup: {backupDirectory}"
            };
        }
        catch (Exception exception)
        {
            RollBackPartial(manifest, backupDirectory);
            return Fail($"Installation failed and was rolled back: {exception.Message}");
        }
    }

    public InstallResult RestoreLatest(string gameDirectory)
    {
        var gameBackupRoot = Path.Combine(_backupRoot, SafeName(gameDirectory));
        var backupDirectory = Directory.Exists(gameBackupRoot)
            ? Directory.EnumerateDirectories(gameBackupRoot).OrderByDescending(path => path).FirstOrDefault()
            : null;
        if (backupDirectory is null) return Fail("No backup exists for this game.");

        try
        {
            var manifestPath = Path.Combine(backupDirectory, "manifest.json");
            var manifest = JsonSerializer.Deserialize<InstallManifest>(File.ReadAllText(manifestPath))
                ?? throw new InvalidDataException("Backup manifest is invalid.");
            var kept = new List<string>();

            foreach (var file in manifest.Files.AsEnumerable().Reverse())
            {
                var target = Path.Combine(gameDirectory, file.TargetName);
                if (file.BackupName is not null)
                    ReplaceFile(Path.Combine(backupDirectory, file.BackupName), target);
                else if (File.Exists(target) && Hash(target).Equals(file.InstalledHash, StringComparison.OrdinalIgnoreCase))
                    File.Delete(target);
                else if (File.Exists(target))
                    kept.Add(file.TargetName);
            }

            var suffix = kept.Count == 0 ? "" : $" Modified files kept: {string.Join(", ", kept)}.";
            return new InstallResult { Success = true, Message = "Latest installation restored." + suffix };
        }
        catch (Exception exception)
        {
            return Fail($"Restore failed: {exception.Message}");
        }
    }

    private static void RollBackPartial(InstallManifest manifest, string backupDirectory)
    {
        foreach (var file in manifest.Files.AsEnumerable().Reverse())
        {
            try
            {
                var target = Path.Combine(manifest.GameDirectory, file.TargetName);
                if (file.BackupName is not null)
                    ReplaceFile(Path.Combine(backupDirectory, file.BackupName), target);
                else if (File.Exists(target)) File.Delete(target);
            }
            catch { }
        }
    }

    private static void ReplaceFile(string source, string target)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        var temporary = target + $".dlss5manager-{Guid.NewGuid():N}.tmp";
        try
        {
            File.Copy(source, temporary, true);
            File.Move(temporary, target, true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    private static string SafeName(string path)
    {
        var invalid = Path.GetInvalidFileNameChars().ToHashSet();
        return new string(Path.GetFullPath(path).Select(character => invalid.Contains(character) ? '_' : character).ToArray());
    }

    private static InstallResult Fail(string message) => new() { Success = false, Message = message };
}
