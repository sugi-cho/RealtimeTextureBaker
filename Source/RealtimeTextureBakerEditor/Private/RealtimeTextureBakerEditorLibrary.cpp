#include "RealtimeTextureBakerEditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"

UTexture2D* URealtimeTextureBakerEditorLibrary::SaveRenderTargetToTextureAsset(UTextureRenderTarget2D* RenderTarget, const FString& PackagePath, const FString& AssetName, bool bSavePackage)
{
	if (!RenderTarget || PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return nullptr;
	}

	const FString SanitizedAssetName = ObjectTools::SanitizeObjectName(AssetName);
	const FString NormalizedPackagePath = PackagePath.StartsWith(TEXT("/")) ? PackagePath : FString::Printf(TEXT("/Game/%s"), *PackagePath);
	const FString PackageName = FString::Printf(TEXT("%s/%s"), *NormalizedPackagePath, *SanitizedAssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return nullptr;
	}

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return nullptr;
	}

	TArray<FColor> Pixels;
	if (!Resource->ReadPixels(Pixels) || Pixels.Num() == 0)
	{
		return nullptr;
	}

	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;
	UTexture2D* Texture = NewObject<UTexture2D>(Package, *SanitizedAssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
	Texture->SRGB = !RenderTarget->bForceLinearGamma;
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
	Texture->UpdateResource();

	FAssetRegistryModule::AssetCreated(Texture);
	Package->MarkPackageDirty();

	if (bSavePackage)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		UPackage::SavePackage(Package, Texture, *Filename, SaveArgs);
	}

	return Texture;
}
