using UnrealBuildTool;

public class MapViewerUnreal : ModuleRules
{
    public MapViewerUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "SQLiteCore",
            "ProceduralMeshComponent"
        });
    }
}
