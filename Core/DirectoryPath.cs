namespace DLSS5ManAger.Core;

public static class DirectoryPath
{
    public static StringComparer Comparer { get; } = StringComparer.OrdinalIgnoreCase;

    public static string Normalize(string path)
    {
        var fullPath = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        return fullPath.Length > 1 && fullPath[1] == ':'
            ? char.ToUpperInvariant(fullPath[0]) + fullPath[1..]
            : fullPath;
    }

    public static List<string> NormalizeDistinct(IEnumerable<string> paths) => paths
        .Where(path => !string.IsNullOrWhiteSpace(path))
        .Select(TryNormalize)
        .OfType<string>()
        .Distinct(Comparer)
        .ToList();

    private static string? TryNormalize(string path)
    {
        try { return Normalize(path); }
        catch { return null; }
    }
}
