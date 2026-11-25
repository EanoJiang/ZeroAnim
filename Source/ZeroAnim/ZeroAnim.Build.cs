using UnrealBuildTool;

public class ZeroAnim : ModuleRules
{
    public ZeroAnim(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore", "InputCore" });
        PrivateDependencyModuleNames.Add("EnhancedInput");
    }
}
