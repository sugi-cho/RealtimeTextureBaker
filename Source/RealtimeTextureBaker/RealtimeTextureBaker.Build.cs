using UnrealBuildTool;

public class RealtimeTextureBaker : ModuleRules
{
	public RealtimeTextureBaker(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Projects",
				"Renderer",
				"RenderCore",
				"RHI"
			}
		);
	}
}
