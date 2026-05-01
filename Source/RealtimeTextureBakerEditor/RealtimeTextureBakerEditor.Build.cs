using UnrealBuildTool;

public class RealtimeTextureBakerEditor : ModuleRules
{
	public RealtimeTextureBakerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RealtimeTextureBaker"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"AssetTools",
				"AssetRegistry",
				"MeshDescription",
				"StaticMeshDescription",
				"UnrealEd"
			}
		);
	}
}
