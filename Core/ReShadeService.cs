using System.Diagnostics;
using System.Net.Http;
using System.Text.RegularExpressions;

namespace DLSS5ManAger.Core;

public sealed class ReShadeService
{
    private const long MaximumInstallerBytes = 128L * 1024 * 1024;
    private static readonly Uri HomePage = new("https://reshade.me/");
    private static readonly HttpClient Client = new() { Timeout = TimeSpan.FromMinutes(2) };
    private static readonly Regex AddonLink = new(
        "href\\s*=\\s*[\\\"'](?<url>/downloads/ReShade_Setup_(?<version>\\d+(?:\\.\\d+){2,3})_Addon\\.exe)[\\\"']",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    static ReShadeService() => Client.DefaultRequestHeaders.UserAgent.ParseAdd("DLAssAss5Tool/0.1");

    public async Task<ReShadeInstallResult> InstallLatestAsync(GameEntry game, string graphicsApi)
    {
        var executablePath = game.ExecutablePath;
        if (!File.Exists(executablePath) || !Path.GetExtension(executablePath).Equals(".exe", StringComparison.OrdinalIgnoreCase))
            return new(false, "The selected game does not have a valid executable.");
        var api = ResolveInstallerApi(graphicsApi);
        if (api is null)
            return new(false, "ReShade installation requires one unambiguous detected graphics API.");

        string? setupPath = null;
        var proxyBackups = new List<(string Original, string Backup)>();
        try
        {
            var release = ParseLatestAddonInstaller(await Client.GetStringAsync(HomePage));
            setupPath = Path.Combine(Path.GetTempPath(), $"ReShade_Setup_{release.Version}_Addon_{Guid.NewGuid():N}.exe");
            using (var response = await Client.GetAsync(release.DownloadUrl, HttpCompletionOption.ResponseHeadersRead))
            {
                response.EnsureSuccessStatusCode();
                var finalUrl = response.RequestMessage?.RequestUri;
                if (finalUrl?.Scheme != Uri.UriSchemeHttps ||
                    !finalUrl.Host.Equals("reshade.me", StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("The official download redirected outside reshade.me.");
                if (response.Content.Headers.ContentLength is > MaximumInstallerBytes)
                    throw new InvalidDataException("The official installer response was unexpectedly large.");
                await using var input = await response.Content.ReadAsStreamAsync();
                await using var output = File.Create(setupPath);
                await CopyToLimitedAsync(input, output);
            }
            ValidateInstaller(setupPath);

            var expectedProxy = ExpectedProxyName(graphicsApi);
            if (expectedProxy is not null)
            {
                foreach (var path in game.ReShadeModulePaths.Where(path =>
                             !Path.GetFileName(path).Equals(expectedProxy, StringComparison.OrdinalIgnoreCase)))
                {
                    if (!IsReShadeModule(path)) continue;
                    var backup = path + $".dlss5manager-{Guid.NewGuid():N}.bak";
                    File.Move(path, backup);
                    proxyBackups.Add((path, backup));
                }
            }

            var start = new ProcessStartInfo(setupPath) { UseShellExecute = true, Verb = "runas" };
            start.ArgumentList.Add(executablePath);
            start.ArgumentList.Add("--headless");
            start.ArgumentList.Add("--api");
            start.ArgumentList.Add(api);
            using var process = Process.Start(start) ?? throw new InvalidOperationException("ReShade Setup did not start.");
            await process.WaitForExitAsync();
            if (process.ExitCode != 0)
                throw new InvalidOperationException($"ReShade Setup {release.Version} exited with code {process.ExitCode}.");
            foreach (var (_, backup) in proxyBackups)
                try { File.Delete(backup); } catch { }
            proxyBackups.Clear();
            return new(true, $"ReShade {release.Version} with full add-on support was installed for {graphicsApi} without shaders.");
        }
        catch (Exception ex)
        {
            var restoreErrors = RestoreProxyBackups(proxyBackups);
            return new(false, "ReShade installation failed: " + ex.Message + restoreErrors);
        }
        finally
        {
            if (setupPath is not null)
                try { File.Delete(setupPath); } catch { }
        }
    }

    internal static string? ResolveInstallerApi(string graphicsApi)
    {
        var apis = graphicsApi.Split('/', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (apis.Length != 1) return null;
        return apis[0].ToUpperInvariant() switch
        {
            "DX9" => "d3d9",
            "DX10" or "DX11" or "DX12" => "dxgi",
            "OPENGL" => "opengl",
            "VULKAN" => "vulkan",
            _ => null
        };
    }

    internal static IReadOnlyList<string> SupportedGraphicsApis(string graphicsApi) => graphicsApi
        .Split('/', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
        .Where(api => ResolveInstallerApi(api) is not null)
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .ToArray();

    internal static string? ExpectedProxyName(string graphicsApi) => ResolveInstallerApi(graphicsApi) switch
    {
        "d3d9" => "d3d9.dll",
        "dxgi" => "dxgi.dll",
        "opengl" => "opengl32.dll",
        _ => null
    };

    internal static bool IsReShadeModule(string path)
    {
        if (!new[] { "d3d9.dll", "d3d10.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "opengl32.dll" }
            .Contains(Path.GetFileName(path), StringComparer.OrdinalIgnoreCase)) return false;
        try { return FileVersionInfo.GetVersionInfo(path).ProductName?.Equals("ReShade", StringComparison.OrdinalIgnoreCase) == true; }
        catch { return false; }
    }

    private static string RestoreProxyBackups(IEnumerable<(string Original, string Backup)> backups)
    {
        var errors = new List<string>();
        foreach (var (original, backup) in backups.Reverse())
        {
            try { if (File.Exists(backup) && !File.Exists(original)) File.Move(backup, original); }
            catch (Exception ex) { errors.Add(ex.Message); }
        }
        return errors.Count == 0 ? "" : " ReShade proxy restoration also failed: " + string.Join("; ", errors);
    }

    internal static ReShadeRelease ParseLatestAddonInstaller(string html)
    {
        var releases = AddonLink.Matches(html).Select(match =>
        {
            var version = Version.Parse(match.Groups["version"].Value);
            var url = new Uri(HomePage, match.Groups["url"].Value);
            return new ReShadeRelease(version, url);
        }).Where(release => release.DownloadUrl.Scheme == Uri.UriSchemeHttps &&
            release.DownloadUrl.Host.Equals("reshade.me", StringComparison.OrdinalIgnoreCase)).ToArray();
        return releases.OrderByDescending(release => release.Version).FirstOrDefault()
            ?? throw new InvalidDataException("The official full add-on installer link was not found.");
    }

    private static void ValidateInstaller(string path)
    {
        var info = new FileInfo(path);
        if (info.Length < 1024 * 1024 || info.Length > MaximumInstallerBytes)
            throw new InvalidDataException("The official installer response had an unexpected size.");
        using var stream = File.OpenRead(path);
        if (stream.ReadByte() != 'M' || stream.ReadByte() != 'Z')
            throw new InvalidDataException("The official installer response was not a Windows executable.");
    }

    private static async Task CopyToLimitedAsync(Stream input, Stream output)
    {
        var buffer = new byte[81920];
        long total = 0;
        int read;
        while ((read = await input.ReadAsync(buffer)) != 0)
        {
            total += read;
            if (total > MaximumInstallerBytes)
                throw new InvalidDataException("The official installer response exceeded the size limit.");
            await output.WriteAsync(buffer.AsMemory(0, read));
        }
    }
}

public sealed record ReShadeRelease(Version Version, Uri DownloadUrl);
public sealed record ReShadeInstallResult(bool Success, string Message);
