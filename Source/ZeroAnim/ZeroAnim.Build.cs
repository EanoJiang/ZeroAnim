using UnrealBuildTool;
using System;
using System.IO;

public class ZeroAnim : ModuleRules
{
    public ZeroAnim(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore", "InputCore" });
        PrivateDependencyModuleNames.Add("EnhancedInput");
        if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Type != TargetType.Server)
        {
            string sdk = Environment.GetEnvironmentVariable("WWISESDK") ?? @"D:\Programs\Wwise2024.1.9.8920\SDK";
            if (!File.Exists(Path.Combine(sdk, "include", "AK", "AkWwiseSDKVersion.h")))
                throw new BuildException("Set WWISESDK to the Wwise 2024.1.9.8920 Windows SDK directory.");

            bool shipping = Target.Configuration == UnrealTargetConfiguration.Shipping;
            PrivateDefinitions.Add("WITH_POLE_WWISE=1");
            if (shipping)
                PrivateDefinitions.Add("AK_OPTIMIZED=1");
            PublicSystemIncludePaths.Add(Path.Combine(sdk, "include"));
            string libs = Path.Combine(sdk, "x64_vc170", shipping ? "Release" : "Profile", "lib");
            foreach (string lib in new[] { "AkSoundEngine", "AkStreamMgr", "AkMemoryMgr", "AkVorbisDecoder",
                "AkSilenceSource", "AkParametricEQFX", "AkDelayFX", "AkCompressorFX", "AkExpanderFX",
                "AkPeakLimiterFX", "AkRoomVerbFX", "AkFlangerFX", "AkTimeStretchFX", "AkGainFX", "AkMeterFX", "MasteringSuiteFX" })
                PublicAdditionalLibraries.Add(Path.Combine(libs, lib + ".lib"));
            if (!shipping)
            {
                PublicAdditionalLibraries.Add(Path.Combine(libs, "CommunicationCentral.lib"));
                PublicAdditionalLibraries.Add(Path.Combine(libs, "AkMusicEngine.lib"));
            }
            PublicSystemLibraries.AddRange(new[] { "dsound.lib", "dxguid.lib", "winmm.lib", "msacm32.lib", "avrt.lib", "ws2_32.lib" });

            RuntimeDependencies.Add("$(ProjectDir)/Content/PlumPole/Wwise/Windows/*.bnk", StagedFileType.UFS);
            RuntimeDependencies.Add("$(ProjectDir)/Content/PlumPole/Wwise/Windows/*.wem", StagedFileType.UFS);
        }
        else
        {
            PrivateDefinitions.Add("WITH_POLE_WWISE=0");
        }
    }
}
