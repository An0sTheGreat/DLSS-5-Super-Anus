using System.Diagnostics;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DLSS5ManAger.Core;

public sealed class CoverArtService
{
    private static readonly HttpClient Client = new() { Timeout = TimeSpan.FromSeconds(12) };
    private static readonly SemaphoreSlim Requests = new(4);
    private readonly string _cacheDirectory;

    static CoverArtService() => Client.DefaultRequestHeaders.UserAgent.ParseAdd("DLAssAss5Tool/0.1");

    public CoverArtService(string cacheDirectory)
    {
        _cacheDirectory = cacheDirectory;
        Directory.CreateDirectory(cacheDirectory);
    }

    public async Task<ImageSource?> LoadAsync(GameEntry game)
    {
        await Requests.WaitAsync();
        try
        {
            if (IsAppId(game.SteamAppId))
                try
                {
                    if (await LoadSteamAsync(game.SteamAppId!) is { } knownCover) return knownCover;
                }
                catch { }

            var candidates = GetTitleCandidates(game).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            foreach (var candidate in candidates)
                try
                {
                    if (await FindSteamAppIdAsync(candidate) is { } appId && await LoadSteamAsync(appId) is { } steamCover)
                        return steamCover;
                }
                catch { }

            foreach (var candidate in candidates)
                try
                {
                    if (await FindGogCoverAsync(candidate) is { } cover)
                        return Decode(await LoadCachedAsync(CacheKey(cover), cover));
                }
                catch { }
        }
        catch { }
        finally { Requests.Release(); }
        return null;
    }

    internal static string NormalizeTitle(string title) =>
        new(title.Where(char.IsLetterOrDigit).Select(char.ToLowerInvariant).ToArray());

    private async Task<ImageSource?> LoadSteamAsync(string appId)
    {
        var cachePath = Path.Combine(_cacheDirectory, $"steam-{appId}.img");
        if (File.Exists(cachePath)) return Decode(await File.ReadAllBytesAsync(cachePath));

        foreach (var suffix in new[] { "library_600x900_2x.jpg", "library_600x900.jpg" })
        {
            var url = new Uri($"https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/{appId}/{suffix}");
            try { return Decode(await LoadCachedAsync($"steam-{appId}", url)); }
            catch (HttpRequestException) { }
        }
        return null;
    }

    private static async Task<string?> FindSteamAppIdAsync(string title)
    {
        var uri = new Uri("https://store.steampowered.com/api/storesearch/?term=" +
            Uri.EscapeDataString(title) + "&l=english&cc=US");
        using var document = JsonDocument.Parse(await Client.GetStringAsync(uri));
        if (!document.RootElement.TryGetProperty("items", out var items)) return null;
        var expected = NormalizeTitle(title);
        foreach (var item in items.EnumerateArray())
        {
            if (!item.TryGetProperty("name", out var name) || NormalizeTitle(name.GetString() ?? "") != expected) continue;
            if (item.TryGetProperty("id", out var id) && id.TryGetInt32(out var appId)) return appId.ToString();
        }
        return null;
    }

    private static async Task<Uri?> FindGogCoverAsync(string title)
    {
        var uri = new Uri("https://catalog.gog.com/v1/catalog?query=like:" + Uri.EscapeDataString(title) +
            "&limit=10&countryCode=US&locale=en-US&currencyCode=USD");
        using var document = JsonDocument.Parse(await Client.GetStringAsync(uri));
        if (!document.RootElement.TryGetProperty("products", out var products)) return null;
        var expected = NormalizeTitle(title);
        foreach (var product in products.EnumerateArray())
        {
            if (!product.TryGetProperty("title", out var name) || NormalizeTitle(name.GetString() ?? "") != expected) continue;
            if (product.TryGetProperty("coverVertical", out var cover) &&
                Uri.TryCreate(cover.GetString(), UriKind.Absolute, out var result) && result.Scheme == Uri.UriSchemeHttps)
                return result;
        }
        return null;
    }

    private async Task<byte[]> LoadCachedAsync(string key, Uri url)
    {
        var cachePath = Path.Combine(_cacheDirectory, key + ".img");
        if (File.Exists(cachePath)) return await File.ReadAllBytesAsync(cachePath);
        var bytes = await Client.GetByteArrayAsync(url);
        if (bytes.Length < 1024) throw new InvalidDataException("Cover response was invalid.");
        await File.WriteAllBytesAsync(cachePath, bytes);
        return bytes;
    }

    private static IEnumerable<string> GetTitleCandidates(GameEntry game)
    {
        yield return game.Name;
        if (game.ExecutablePath is not { } executable) yield break;
        FileVersionInfo? info = null;
        try { info = FileVersionInfo.GetVersionInfo(executable); } catch { }
        if (!string.IsNullOrWhiteSpace(info?.ProductName)) yield return info.ProductName.Trim();
        if (!string.IsNullOrWhiteSpace(info?.FileDescription)) yield return info.FileDescription.Trim();
    }

    private static bool IsAppId(string? value) => !string.IsNullOrWhiteSpace(value) && value.All(char.IsDigit);

    private static string CacheKey(Uri url) => "gog-" + Convert.ToHexString(
        SHA256.HashData(Encoding.UTF8.GetBytes(url.AbsoluteUri)))[..16];

    private static ImageSource Decode(byte[] bytes)
    {
        using var stream = new MemoryStream(bytes);
        var image = new BitmapImage();
        image.BeginInit();
        image.CacheOption = BitmapCacheOption.OnLoad;
        image.DecodePixelWidth = 300;
        image.StreamSource = stream;
        image.EndInit();
        image.Freeze();
        return image;
    }
}
