#include "RealtimeTextureBaker.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FRealtimeTextureBakerModule"

void FRealtimeTextureBakerModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RealtimeTextureBaker"));
	if (Plugin.IsValid())
	{
		const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/RealtimeTextureBaker"), ShaderDirectory);
	}
}

void FRealtimeTextureBakerModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealtimeTextureBakerModule, RealtimeTextureBaker)
