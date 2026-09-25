using UnrealBuildTool;

public class MapViewerUnrealTarget : TargetRules
{
    public MapViewerUnrealTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("MapViewerUnreal");
    }
}
