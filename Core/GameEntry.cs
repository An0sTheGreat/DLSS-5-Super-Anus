using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Media;

namespace DLSS5ManAger.Core;

public sealed class GameEntry : INotifyPropertyChanged
{
    private string _name = "";
    private string _status = "Not analyzed";
    private string _graphicsApi = "Unknown";
    private string _details = "";
    private ImageSource? _coverSource;
    private bool _isHidden;
    private bool _isCoverLoading;

    public required string Name { get => _name; set => Set(ref _name, value); }
    public required string GameDirectory { get; init; }
    public string? ExecutablePath { get; set; }
    public bool HasReShade { get; set; }
    public bool HasReShadeProxyMismatch { get; set; }
    public IReadOnlyList<string> ReShadeModulePaths { get; set; } = [];
    public bool HasAddon { get; set; }
    public bool HasDlss { get; set; }
    public bool HasDlssG { get; set; }
    public bool HasDlssNr { get; set; }
    public string? SteamAppId { get; set; }
    public bool IsHidden
    {
        get => _isHidden;
        set
        {
            if (!Set(ref _isHidden, value)) return;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(HideActionLabel)));
        }
    }

    public string Status { get => _status; set => Set(ref _status, value); }
    public string GraphicsApi { get => _graphicsApi; set => Set(ref _graphicsApi, value); }
    public string Details { get => _details; set => Set(ref _details, value); }
    public bool IsCoverLoading { get => _isCoverLoading; set => Set(ref _isCoverLoading, value); }
    public ImageSource? IconSource { get; set; }
    public ImageSource? CoverSource
    {
        get => _coverSource;
        set
        {
            if (!Set(ref _coverSource, value)) return;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(DisplayArt)));
        }
    }
    public ImageSource? DisplayArt => CoverSource ?? IconSource;
    public string HideActionLabel => IsHidden ? "Unhide game" : "Hide game";
    public string ReShadeLabel => HasReShade ? "Detected" : "Not found";
    public bool CanInstallReShade => ReShadeService.SupportedGraphicsApis(GraphicsApi).Count is > 1 ||
        ReShadeService.ResolveInstallerApi(GraphicsApi) is not null && (!HasReShade || HasReShadeProxyMismatch);
    public string ReShadeActionLabel => ReShadeService.SupportedGraphicsApis(GraphicsApi).Count > 1 && HasReShade
        ? "CHANGE RESHADE API"
        : HasReShadeProxyMismatch ? "REPAIR RESHADE"
        : HasReShade ? "RESHADE INSTALLED!"
        : ReShadeService.SupportedGraphicsApis(GraphicsApi).Count == 0 ? "GRAPHICS API REQUIRED"
        : "INSTALL RESHADE";
    public string AddonLabel => HasAddon ? "Installed" : "Not installed";
    public string DlssLabel => string.Join(" / ", new[]
    {
        HasDlss ? "SR" : null,
        HasDlssG ? "FG" : null,
        HasDlssNr ? "NR" : null
    }.OfType<string>()) is { Length: > 0 } value ? value : "None";
    public IReadOnlyList<DetailStatus> DetailStatuses =>
    [
        new("Executable", ExecutablePath is not null),
        new(GraphicsApi == "Unknown" ? "Graphics API" : GraphicsApi, GraphicsApi != "Unknown"),
        new("ReShade", HasReShade),
        new("Add-on", HasAddon),
        new("DLSS SR", HasDlss),
        new("DLSS FG", HasDlssG),
        new("DLSS NR", HasDlssNr)
    ];

    public event PropertyChangedEventHandler? PropertyChanged;

    private bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        return true;
    }
}

public sealed record DetailStatus(string Label, bool Exists)
{
    public string Symbol => Exists ? "✓" : "✕";
    public string Color => Exists ? "#76B900" : "#FF5F63";
}
